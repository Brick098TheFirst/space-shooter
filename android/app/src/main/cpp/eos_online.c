#include "eos_online.h"

#include <android/log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_lobby.h"
#include "eos_p2p.h"
#include "Android/eos_Android.h"

#define LOG_TAG "SpaceUnlimitedEOS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Versioned with the coop protocol (coop.c COOP_PROTOCOL_VERSION): builds
 * that speak different packet layouts never matchmake into each other's
 * lobbies, so an old APK can never silently misbehave against a new one. */
#define COOP_BUCKET "SPACE_UNLIMITED_COOP_V3"
#define COOP_SOCKET "SPACECOOP1"
#define EOS_TEXT_CAP 192
#define LOBBY_ID_CAP 128

static EOS_HPlatform s_platform;
static EOS_HConnect s_connect;
static EOS_HLobby s_lobby;
static EOS_HP2P s_p2p;
static EOS_ProductUserId s_local_user;
static EOS_ProductUserId s_remote_user;
static EOS_HLobbySearch s_search;
static EOS_HLobbyDetails s_join_details;
static EOS_NotificationId s_lobby_member_notify = EOS_INVALID_NOTIFICATIONID;
static EOS_NotificationId s_p2p_request_notify = EOS_INVALID_NOTIFICATIONID;
static EOS_P2P_SocketId s_socket;

static int s_sdk_initialized;
static int s_status = EOS_ONLINE_CONFIG_REQUIRED;
static int s_is_host;
static int s_member_count;
static int s_wait_ticks;
static int s_restart_after_leave;
static int s_cancel_pending;
static int s_status_ticks;          /* ticks in current status (watchdog) */
static int s_login_retries;         /* transient network blips self-heal */
static char s_status_text[EOS_TEXT_CAP] = "Online co-op needs EOS credentials";
static char s_lobby_id[LOBBY_ID_CAP];
static char s_display_name[EOS_CONNECT_USERLOGININFO_DISPLAYNAME_MAX_LENGTH + 1] = "Space Pilot";

static void start_device_login(void);
static void start_lobby_search(void);
static void create_public_lobby(void);
static void refresh_lobby_members(void);
static void prepare_p2p(void);

/* Every long-running lobby op (search/create/join) tags its callback with a
 * generation token so a stale completion from an abandoned op can never
 * corrupt a fresh matchmaking pass (this was a source of "stuck forever"
 * states - a late search callback would grab a lobby mid-cancel). */
static uint32_t s_op_gen;
static int s_login_retry_at;            /* eos tick index, 0 = none pending */
static int s_eos_tick;
static char s_p2p_peer_text[EOS_PRODUCTUSERID_MAX_LENGTH + 1];

static int is_transient(EOS_EResult r) {
    return r == EOS_NoConnection || r == EOS_ServiceFailure ||
           r == EOS_TooManyRequests || r == EOS_OperationWillRetry;
}

static void set_status(int status, const char* text) {
    if (s_status != status) s_status_ticks = 0;
    s_status = status;
    snprintf(s_status_text, sizeof(s_status_text), "%s", text ? text : "");
    LOGI("%s", s_status_text);
}

static void set_result_error(const char* operation, EOS_EResult result) {
    const char* result_text = EOS_EResult_ToString(result);
    s_status = EOS_ONLINE_ERROR;
    snprintf(s_status_text, sizeof(s_status_text), "%s: %s", operation,
             result_text ? result_text : "unknown EOS error");
    LOGE("%s", s_status_text);
}

static int valid_text(const char* value) {
    return value && value[0] != '\0';
}

static int same_product_user(EOS_ProductUserId a, EOS_ProductUserId b) {
    if (!a || !b) return 0;
    char a_text[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {0};
    char b_text[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {0};
    int32_t a_len = (int32_t)sizeof(a_text);
    int32_t b_len = (int32_t)sizeof(b_text);
    if (EOS_ProductUserId_ToString(a, a_text, &a_len) != EOS_Success) return 0;
    if (EOS_ProductUserId_ToString(b, b_text, &b_len) != EOS_Success) return 0;
    return strcmp(a_text, b_text) == 0;
}

static uint32_t local_user_hash(void) {
    char text[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {0};
    int32_t length = (int32_t)sizeof(text);
    if (!s_local_user || EOS_ProductUserId_ToString(s_local_user, text, &length) != EOS_Success) {
        return 1;
    }
    uint32_t hash = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static void clear_search_handles(void) {
    if (s_search) {
        EOS_LobbySearch_Release(s_search);
        s_search = NULL;
    }
    if (s_join_details) {
        EOS_LobbyDetails_Release(s_join_details);
        s_join_details = NULL;
    }
}

static void clear_match_state(void) {
    clear_search_handles();
    s_lobby_id[0] = '\0';
    s_remote_user = NULL;
    s_is_host = 0;
    s_member_count = 0;
    s_wait_ticks = 0;
    s_cancel_pending = 0;
    s_p2p_peer_text[0] = '\0';
    s_op_gen++; /* invalidate any in-flight op callbacks */

    if (s_p2p_request_notify != EOS_INVALID_NOTIFICATIONID && s_p2p) {
        EOS_P2P_RemoveNotifyPeerConnectionRequest(s_p2p, s_p2p_request_notify);
        s_p2p_request_notify = EOS_INVALID_NOTIFICATIONID;
    }
}

static void EOS_CALL on_create_user(const EOS_Connect_CreateUserCallbackInfo* data) {
    if (data->ResultCode != EOS_Success) {
        if (is_transient(data->ResultCode) && s_login_retries < 3) {
            s_login_retries++;
            s_login_retry_at = s_eos_tick + 180; /* ~2 s */
            set_status(EOS_ONLINE_SIGNING_IN, "Connection hiccup - retrying sign in...");
            return;
        }
        set_result_error("Create EOS player", data->ResultCode);
        return;
    }
    s_login_retries = 0;
    s_local_user = data->LocalUserId;
    set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
}

static void EOS_CALL on_connect_login(const EOS_Connect_LoginCallbackInfo* data) {
    if (data->ResultCode == EOS_Success) {
        s_login_retries = 0;
        s_local_user = data->LocalUserId;
        set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
        return;
    }

    if (data->ResultCode == EOS_InvalidUser && data->ContinuanceToken) {
        EOS_Connect_CreateUserOptions options;
        memset(&options, 0, sizeof(options));
        options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
        options.ContinuanceToken = data->ContinuanceToken;
        set_status(EOS_ONLINE_SIGNING_IN, "Creating EOS player...");
        EOS_Connect_CreateUser(s_connect, &options, NULL, on_create_user);
        return;
    }

    if (is_transient(data->ResultCode) && s_login_retries < 3) {
        s_login_retries++;
        s_login_retry_at = s_eos_tick + 180;
        set_status(EOS_ONLINE_SIGNING_IN, "Connection hiccup - retrying sign in...");
        return;
    }
    set_result_error("EOS Device ID login", data->ResultCode);
}

static void login_with_device_id(void) {
    EOS_Connect_Credentials credentials;
    memset(&credentials, 0, sizeof(credentials));
    credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    credentials.Token = NULL;
    credentials.Type = EOS_ECT_DEVICEID_ACCESS_TOKEN;

    EOS_Connect_UserLoginInfo user_info;
    memset(&user_info, 0, sizeof(user_info));
    user_info.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    user_info.DisplayName = s_display_name;

    EOS_Connect_LoginOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    options.Credentials = &credentials;
    options.UserLoginInfo = &user_info;

    set_status(EOS_ONLINE_SIGNING_IN, "Signing in to Epic Online Services...");
    EOS_Connect_Login(s_connect, &options, NULL, on_connect_login);
}

static void EOS_CALL on_create_device_id(const EOS_Connect_CreateDeviceIdCallbackInfo* data) {
    if (data->ResultCode == EOS_Success || data->ResultCode == EOS_DuplicateNotAllowed) {
        login_with_device_id();
        return;
    }
    if (is_transient(data->ResultCode) && s_login_retries < 3) {
        s_login_retries++;
        s_login_retry_at = s_eos_tick + 180;
        set_status(EOS_ONLINE_SIGNING_IN, "Connection hiccup - retrying sign in...");
        return;
    }
    set_result_error("Create EOS Device ID", data->ResultCode);
}

static void start_device_login(void) {
    EOS_Connect_CreateDeviceIdOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    options.DeviceModel = "Android";
    set_status(EOS_ONLINE_SIGNING_IN, "Preparing EOS Device ID...");
    EOS_Connect_CreateDeviceId(s_connect, &options, NULL, on_create_device_id);
}

static void EOS_CALL on_p2p_connection_request(const EOS_P2P_OnIncomingConnectionRequestInfo* data) {
    if (!s_remote_user || !same_product_user(data->RemoteUserId, s_remote_user)) return;
    if (!data->SocketId || strcmp(data->SocketId->SocketName, COOP_SOCKET) != 0) return;

    EOS_P2P_AcceptConnectionOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    options.LocalUserId = s_local_user;
    options.RemoteUserId = s_remote_user;
    options.SocketId = &s_socket;
    EOS_EResult result = EOS_P2P_AcceptConnection(s_p2p, &options);
    if (result != EOS_Success) set_result_error("Accept co-op connection", result);
}

static void prepare_p2p(void) {
    if (!s_p2p || !s_local_user || !s_remote_user) return;

    /* prepare_p2p runs on every lobby member refresh; make it idempotent so
     * we don't spam AcceptConnection/hello for the same peer. */
    {
        char peer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {0};
        int32_t peer_len = (int32_t)sizeof(peer);
        if (EOS_ProductUserId_ToString(s_remote_user, peer, &peer_len) == EOS_Success &&
            peer[0] && strcmp(peer, s_p2p_peer_text) == 0) {
            return; /* already prepared for this peer */
        }
    }

    if (s_p2p_request_notify == EOS_INVALID_NOTIFICATIONID) {
        EOS_P2P_AddNotifyPeerConnectionRequestOptions notify;
        memset(&notify, 0, sizeof(notify));
        notify.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
        notify.LocalUserId = s_local_user;
        notify.SocketId = &s_socket;
        s_p2p_request_notify = EOS_P2P_AddNotifyPeerConnectionRequest(
            s_p2p, &notify, NULL, on_p2p_connection_request);
    }

    /* Accept proactively so either peer may send the first gameplay packet. */
    EOS_P2P_AcceptConnectionOptions accept;
    memset(&accept, 0, sizeof(accept));
    accept.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId = s_local_user;
    accept.RemoteUserId = s_remote_user;
    accept.SocketId = &s_socket;
    EOS_EResult result = EOS_P2P_AcceptConnection(s_p2p, &accept);
    if (result != EOS_Success) {
        /* Not fatal: the member-status notification will re-arm this, and
         * the guest's stale-state watchdog resends requests regardless. */
        LOGE("Accept co-op connection: %s", EOS_EResult_ToString(result));
        return;
    }

    {
        int32_t peer_len = (int32_t)sizeof(s_p2p_peer_text);
        if (EOS_ProductUserId_ToString(s_remote_user, s_p2p_peer_text, &peer_len) != EOS_Success) {
            s_p2p_peer_text[0] = '\0';
        }
    }

    static const char hello[] = "SUCOOP1";
    eos_online_send_packet(hello, (uint32_t)sizeof(hello), 0, 1);
}

static void refresh_lobby_members(void) {
    if (!s_lobby || !s_local_user || !s_lobby_id[0]) return;

    EOS_Lobby_CopyLobbyDetailsHandleOptions copy_options;
    memset(&copy_options, 0, sizeof(copy_options));
    copy_options.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    copy_options.LobbyId = s_lobby_id;
    copy_options.LocalUserId = s_local_user;

    EOS_HLobbyDetails details = NULL;
    EOS_EResult copy_result = EOS_Lobby_CopyLobbyDetailsHandle(s_lobby, &copy_options, &details);
    if (copy_result != EOS_Success || !details) return;

    EOS_LobbyDetails_GetMemberCountOptions count_options;
    memset(&count_options, 0, sizeof(count_options));
    count_options.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    s_member_count = (int)EOS_LobbyDetails_GetMemberCount(details, &count_options);

    EOS_LobbyDetails_GetLobbyOwnerOptions owner_options;
    memset(&owner_options, 0, sizeof(owner_options));
    owner_options.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
    EOS_ProductUserId owner = EOS_LobbyDetails_GetLobbyOwner(details, &owner_options);
    s_is_host = same_product_user(owner, s_local_user);

    s_remote_user = NULL;
    for (uint32_t index = 0; index < (uint32_t)s_member_count; ++index) {
        EOS_LobbyDetails_GetMemberByIndexOptions member_options;
        memset(&member_options, 0, sizeof(member_options));
        member_options.ApiVersion = EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;
        member_options.MemberIndex = index;
        EOS_ProductUserId member = EOS_LobbyDetails_GetMemberByIndex(details, &member_options);
        if (member && !same_product_user(member, s_local_user)) {
            s_remote_user = member;
            break;
        }
    }
    EOS_LobbyDetails_Release(details);

    if (s_member_count >= 2 && s_remote_user) {
        set_status(EOS_ONLINE_MATCHED, s_is_host
            ? "Co-op player found - you are host"
            : "Co-op player found - connected to host");
        prepare_p2p();
    } else if (s_status == EOS_ONLINE_MATCHED && !s_is_host) {
        /* Guest saw the lobby shrink (host closed it): drop back to a clean
         * READY state instead of staying MATCHED to a ghost lobby. */
        clear_match_state();
        set_status(EOS_ONLINE_READY, "Lobby ended - ready to re-match");
    } else if (s_status != EOS_ONLINE_MATCHMAKING) {
        set_status(EOS_ONLINE_WAITING_FOR_PLAYER, "Public lobby open - waiting for player...");
    }
}

static void EOS_CALL on_lobby_member_status(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* data) {
    if (!data->LobbyId || !s_lobby_id[0] || strcmp(data->LobbyId, s_lobby_id) != 0) return;
    if (data->CurrentStatus == EOS_LMS_CLOSED) {
        /* The lobby is gone (host destroyed it / service evicted it). Clean
         * up locally so the UI can offer an instant re-match instead of
         * sitting on a stale MATCHED/WAITING forever. */
        clear_match_state();
        if (!s_restart_after_leave) {
            set_status(EOS_ONLINE_READY, "Lobby closed - ready to re-match");
        }
        return;
    }
    refresh_lobby_members();
}

static void ensure_lobby_notifications(void) {
    if (s_lobby_member_notify != EOS_INVALID_NOTIFICATIONID || !s_lobby) return;
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
    s_lobby_member_notify = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(
        s_lobby, &options, NULL, on_lobby_member_status);
}

static void EOS_CALL on_create_lobby(const EOS_Lobby_CreateLobbyCallbackInfo* data) {
    if ((uintptr_t)data->ClientData != (uintptr_t)s_op_gen) {
        LOGI("Ignoring stale create-lobby completion (gen %lu != %lu)",
             (unsigned long)(uintptr_t)data->ClientData, (unsigned long)s_op_gen);
        return; /* abandoned op: a newer matchmaking pass owns the state */
    }
    if (s_cancel_pending) {
        s_cancel_pending = 0;
        if (data->ResultCode == EOS_Success && data->LobbyId) {
            snprintf(s_lobby_id, sizeof(s_lobby_id), "%s", data->LobbyId);
            s_is_host = 1;
            eos_online_cancel_match();
        } else {
            clear_match_state();
            set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
        }
        return;
    }
    if (data->ResultCode != EOS_Success || !data->LobbyId) {
        set_result_error("Create public co-op lobby", data->ResultCode);
        return;
    }
    snprintf(s_lobby_id, sizeof(s_lobby_id), "%s", data->LobbyId);
    s_is_host = 1;
    s_member_count = 1;
    /* Different users get different retry times, avoiding two simultaneous
     * empty hosts remaining split forever after a search/create race. */
    s_wait_ticks = 540 + (int)(local_user_hash() % 360u);
    set_status(EOS_ONLINE_WAITING_FOR_PLAYER, "Public lobby open - waiting for player...");
    ensure_lobby_notifications();
    refresh_lobby_members();
}

static void create_public_lobby(void) {
    EOS_Lobby_CreateLobbyOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    options.LocalUserId = s_local_user;
    options.MaxLobbyMembers = 2;
    options.PermissionLevel = EOS_LPL_PUBLICADVERTISED;
    options.bPresenceEnabled = EOS_FALSE;
    options.bAllowInvites = EOS_FALSE;
    options.BucketId = COOP_BUCKET;
    options.bDisableHostMigration = EOS_TRUE;
    options.bEnableRTCRoom = EOS_FALSE;
    options.bEnableJoinById = EOS_FALSE;
    options.bRejoinAfterKickRequiresInvite = EOS_FALSE;
    options.bCrossplayOptOut = EOS_FALSE;
    set_status(EOS_ONLINE_MATCHMAKING, "No lobby found - creating one...");
    s_op_gen++;
    EOS_Lobby_CreateLobby(s_lobby, &options, (void*)(uintptr_t)s_op_gen, on_create_lobby);
}

static void EOS_CALL on_join_lobby(const EOS_Lobby_JoinLobbyCallbackInfo* data) {
    if ((uintptr_t)data->ClientData != (uintptr_t)s_op_gen) {
        if (s_join_details) {
            EOS_LobbyDetails_Release(s_join_details);
            s_join_details = NULL;
        }
        LOGI("Ignoring stale join-lobby completion");
        return; /* abandoned op */
    }
    if (s_join_details) {
        EOS_LobbyDetails_Release(s_join_details);
        s_join_details = NULL;
    }
    if (s_cancel_pending) {
        s_cancel_pending = 0;
        if (data->ResultCode == EOS_Success && data->LobbyId) {
            snprintf(s_lobby_id, sizeof(s_lobby_id), "%s", data->LobbyId);
            s_is_host = 0;
            eos_online_cancel_match();
        } else {
            clear_match_state();
            set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
        }
        return;
    }
    if (data->ResultCode != EOS_Success || !data->LobbyId) {
        /* A result can become full between search and join. Search again. */
        if (data->ResultCode == EOS_Lobby_TooManyPlayers ||
            data->ResultCode == EOS_Lobby_HostAtCapacity ||
            data->ResultCode == EOS_NotFound) {
            start_lobby_search();
        } else {
            set_result_error("Join public co-op lobby", data->ResultCode);
        }
        return;
    }
    snprintf(s_lobby_id, sizeof(s_lobby_id), "%s", data->LobbyId);
    set_status(EOS_ONLINE_WAITING_FOR_PLAYER, "Joined lobby - connecting player...");
    ensure_lobby_notifications();
    refresh_lobby_members();
}

static void EOS_CALL on_lobby_search(const EOS_LobbySearch_FindCallbackInfo* data) {
    if ((uintptr_t)data->ClientData != (uintptr_t)s_op_gen) {
        LOGI("Ignoring stale lobby-search completion");
        return; /* abandoned op */
    }
    if (s_cancel_pending) {
        s_cancel_pending = 0;
        clear_match_state();
        set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
        return;
    }
    if (data->ResultCode != EOS_Success) {
        clear_search_handles();
        set_result_error("Search public co-op lobbies", data->ResultCode);
        return;
    }

    EOS_LobbySearch_GetSearchResultCountOptions count_options;
    memset(&count_options, 0, sizeof(count_options));
    count_options.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
    uint32_t count = EOS_LobbySearch_GetSearchResultCount(s_search, &count_options);
    if (count == 0) {
        clear_search_handles();
        create_public_lobby();
        return;
    }

    EOS_LobbySearch_CopySearchResultByIndexOptions copy_options;
    memset(&copy_options, 0, sizeof(copy_options));
    copy_options.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    copy_options.LobbyIndex = 0;
    EOS_EResult copy_result = EOS_LobbySearch_CopySearchResultByIndex(
        s_search, &copy_options, &s_join_details);
    EOS_LobbySearch_Release(s_search);
    s_search = NULL;
    if (copy_result != EOS_Success || !s_join_details) {
        set_result_error("Read co-op lobby result", copy_result);
        return;
    }

    EOS_Lobby_JoinLobbyOptions join;
    memset(&join, 0, sizeof(join));
    join.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
    join.LobbyDetailsHandle = s_join_details;
    join.LocalUserId = s_local_user;
    join.bPresenceEnabled = EOS_FALSE;
    join.bCrossplayOptOut = EOS_FALSE;
    set_status(EOS_ONLINE_MATCHMAKING, "Joining public co-op lobby...");
    s_op_gen++;
    EOS_Lobby_JoinLobby(s_lobby, &join, (void*)(uintptr_t)s_op_gen, on_join_lobby);
}

static void start_lobby_search(void) {
    clear_search_handles();

    EOS_Lobby_CreateLobbySearchOptions create;
    memset(&create, 0, sizeof(create));
    create.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    create.MaxResults = 10;
    EOS_EResult result = EOS_Lobby_CreateLobbySearch(s_lobby, &create, &s_search);
    if (result != EOS_Success || !s_search) {
        set_result_error("Create co-op lobby search", result);
        return;
    }

    EOS_Lobby_AttributeData bucket;
    memset(&bucket, 0, sizeof(bucket));
    bucket.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    bucket.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
    bucket.Value.AsUtf8 = COOP_BUCKET;
    bucket.ValueType = EOS_AT_STRING;

    EOS_LobbySearch_SetParameterOptions parameter;
    memset(&parameter, 0, sizeof(parameter));
    parameter.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
    parameter.Parameter = &bucket;
    parameter.ComparisonOp = EOS_CO_EQUAL;
    result = EOS_LobbySearch_SetParameter(s_search, &parameter);
    if (result != EOS_Success) {
        clear_search_handles();
        set_result_error("Set co-op search filter", result);
        return;
    }

    EOS_LobbySearch_FindOptions find;
    memset(&find, 0, sizeof(find));
    find.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
    find.LocalUserId = s_local_user;
    set_status(EOS_ONLINE_MATCHMAKING, "Searching for a public co-op game...");
    s_op_gen++;
    EOS_LobbySearch_Find(s_search, &find, (void*)(uintptr_t)s_op_gen, on_lobby_search);
}

static void EOS_CALL on_match_left(const EOS_Lobby_LeaveLobbyCallbackInfo* data) {
    int restart = s_restart_after_leave;
    s_restart_after_leave = 0;
    clear_match_state();
    if (data->ResultCode != EOS_Success) {
        set_result_error("Leave co-op lobby", data->ResultCode);
    } else if (restart) {
        start_lobby_search();
    } else {
        set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
    }
}

static void EOS_CALL on_match_destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo* data) {
    int restart = s_restart_after_leave;
    s_restart_after_leave = 0;
    clear_match_state();
    if (data->ResultCode != EOS_Success) {
        set_result_error("Close co-op lobby", data->ResultCode);
    } else if (restart) {
        start_lobby_search();
    } else {
        set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
    }
}

int eos_online_initialize(const EosOnlineConfig* config) {
    if (!config || !valid_text(config->product_id) || !valid_text(config->sandbox_id) ||
        !valid_text(config->deployment_id) || !valid_text(config->client_id) ||
        !valid_text(config->client_secret)) {
        set_status(EOS_ONLINE_CONFIG_REQUIRED,
                   "Online co-op needs android/eos.properties");
        return 0;
    }
    if (s_platform) return 1;

    snprintf(s_display_name, sizeof(s_display_name), "%s",
             valid_text(config->display_name) ? config->display_name : "Space Pilot");

    EOS_Android_InitializeOptions android_options;
    memset(&android_options, 0, sizeof(android_options));
    android_options.ApiVersion = EOS_ANDROID_INITIALIZEOPTIONS_API_LATEST;
    android_options.OptionalInternalDirectory = valid_text(config->internal_dir) ? config->internal_dir : NULL;
    android_options.OptionalExternalDirectory = valid_text(config->external_dir) ? config->external_dir : NULL;

    EOS_InitializeOptions init;
    memset(&init, 0, sizeof(init));
    init.ApiVersion = EOS_INITIALIZE_API_LATEST;
    init.ProductName = "Space Unlimited Recharged";
    init.ProductVersion = "1.0.0";
    init.SystemInitializeOptions = &android_options;

    set_status(EOS_ONLINE_INITIALIZING, "Starting Epic Online Services...");
    EOS_EResult result = EOS_Initialize(&init);
    if (result != EOS_Success && result != EOS_AlreadyConfigured) {
        set_result_error("Initialize EOS SDK", result);
        return 0;
    }
    s_sdk_initialized = 1;

    EOS_Platform_Options platform;
    memset(&platform, 0, sizeof(platform));
    platform.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    platform.ProductId = config->product_id;
    platform.SandboxId = config->sandbox_id;
    platform.DeploymentId = config->deployment_id;
    platform.ClientCredentials.ClientId = config->client_id;
    platform.ClientCredentials.ClientSecret = config->client_secret;
    platform.bIsServer = EOS_FALSE;
    platform.EncryptionKey = NULL;
    platform.CacheDirectory = valid_text(config->internal_dir) ? config->internal_dir : NULL;
    platform.Flags = EOS_PF_DISABLE_OVERLAY | EOS_PF_DISABLE_SOCIAL_OVERLAY;
    platform.TickBudgetInMilliseconds = 2;

    s_platform = EOS_Platform_Create(&platform);
    if (!s_platform) {
        if (s_sdk_initialized) {
            EOS_Shutdown();
            s_sdk_initialized = 0;
        }
        set_status(EOS_ONLINE_ERROR, "EOS platform creation failed - check IDs");
        return 0;
    }

    s_connect = EOS_Platform_GetConnectInterface(s_platform);
    s_lobby = EOS_Platform_GetLobbyInterface(s_platform);
    s_p2p = EOS_Platform_GetP2PInterface(s_platform);
    if (!s_connect || !s_lobby || !s_p2p) {
        EOS_Platform_Release(s_platform);
        s_platform = NULL;
        if (s_sdk_initialized) {
            EOS_Shutdown();
            s_sdk_initialized = 0;
        }
        s_connect = NULL;
        s_lobby = NULL;
        s_p2p = NULL;
        set_status(EOS_ONLINE_ERROR, "EOS interfaces unavailable");
        return 0;
    }

    memset(&s_socket, 0, sizeof(s_socket));
    s_socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    snprintf(s_socket.SocketName, sizeof(s_socket.SocketName), "%s", COOP_SOCKET);

    /* The EOS P2P default packet queues are tiny (a few dozen packets per
     * channel).  The host streams up to ~90 reliable packets per second of
     * snapshot fragments, so any hiccup — Android UI jank, slow NAT/relay
     * warm-up, a dropped frame — overflows the queue and EOS starts
     * dropping packets.  One dropped fragment stalls the guest's state
     * stream (it never "loads in").  Give the queues room to breathe. */
    EOS_P2P_SetPacketQueueSizeOptions queue_options;
    memset(&queue_options, 0, sizeof(queue_options));
    queue_options.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
    queue_options.IncomingPacketQueueMaxSizeBytes = 8ull * 1024 * 1024;
    queue_options.OutgoingPacketQueueMaxSizeBytes = 8ull * 1024 * 1024;
    EOS_EResult queue_result = EOS_P2P_SetPacketQueueSize(s_p2p, &queue_options);
    if (queue_result != EOS_Success) {
        LOGE("EOS_P2P_SetPacketQueueSize failed: %s", EOS_EResult_ToString(queue_result));
    }

    start_device_login();
    return 1;
}

void eos_online_tick(void) {
    if (!s_platform) return;
    EOS_Platform_Tick(s_platform);
    s_eos_tick++;
    s_status_ticks++;

    /* Scheduled sign-in retry after a transient network failure. */
    if (s_login_retry_at && s_eos_tick >= s_login_retry_at) {
        s_login_retry_at = 0;
        if (s_status == EOS_ONLINE_SIGNING_IN) start_device_login();
    }

    /* Resolve the rare race where both Quick Match users searched before
     * either public lobby existed, then each created an empty lobby. */
    if (s_status == EOS_ONLINE_WAITING_FOR_PLAYER && s_is_host && s_member_count < 2 && s_wait_ticks > 0) {
        --s_wait_ticks;
        if (s_wait_ticks == 0) {
            s_restart_after_leave = 1;
            eos_online_cancel_match();
        }
    }

    /* Matchmaking watchdog: a search/create/join/cancel that never completes
     * (killed activity, flaky network, orphaned callback) used to leave the
     * spinner running forever. Requeue automatically. ~20s then ~40s. */
    if (s_status == EOS_ONLINE_MATCHMAKING && s_status_ticks > 1800) {
        if (!s_cancel_pending) {
            LOGI("matchmaking watchdog: stuck in MATCHMAKING, cancelling + requeueing");
            s_restart_after_leave = 1;
            eos_online_cancel_match();
        } else if (s_status_ticks > 3600) {
            /* Even the cancel is stuck: hard reset and search fresh. The op
             * generation token keeps a late completion from corrupting the
             * new pass. */
            LOGI("matchmaking watchdog: cancel never completed, hard reset");
            clear_match_state();
            s_restart_after_leave = 0;
            set_status(EOS_ONLINE_READY, "Reconnecting Quick Match...");
            if (s_local_user) {
                ensure_lobby_notifications();
                start_lobby_search();
            }
        }
    }
}

void eos_online_set_foreground(int foreground) {
    if (!s_platform) return;
    EOS_Platform_SetApplicationStatus(s_platform,
        foreground ? EOS_AS_Foreground : EOS_AS_BackgroundSuspended);
}

void eos_online_shutdown(void) {
    clear_match_state();
    if (s_lobby_member_notify != EOS_INVALID_NOTIFICATIONID && s_lobby) {
        EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(s_lobby, s_lobby_member_notify);
        s_lobby_member_notify = EOS_INVALID_NOTIFICATIONID;
    }
    s_local_user = NULL;
    s_connect = NULL;
    s_lobby = NULL;
    s_p2p = NULL;
    if (s_platform) {
        EOS_Platform_Release(s_platform);
        s_platform = NULL;
    }
    if (s_sdk_initialized) {
        EOS_Shutdown();
        s_sdk_initialized = 0;
    }
    set_status(EOS_ONLINE_CONFIG_REQUIRED, "Online co-op stopped");
}

int eos_online_status(void) {
    return s_status;
}

const char* eos_online_status_text(void) {
    return s_status_text;
}

int eos_online_is_host(void) {
    return s_is_host;
}

int eos_online_member_count(void) {
    return s_member_count;
}

int eos_online_quick_match(void) {
    if (!s_platform || !s_local_user || s_status != EOS_ONLINE_READY) return 0;
    clear_match_state();
    ensure_lobby_notifications();
    start_lobby_search();
    return 1;
}

void eos_online_cancel_match(void) {
    /* Lobby search/create/join have no cancel API. Keep their handles alive and
     * let the completion callback perform the cleanup safely. */
    if (!s_lobby_id[0] && s_status == EOS_ONLINE_MATCHMAKING) {
        s_cancel_pending = 1;
        set_status(EOS_ONLINE_MATCHMAKING, "Cancelling Quick Match...");
        return;
    }

    clear_search_handles();
    if (!s_lobby_id[0] || !s_lobby || !s_local_user) {
        int restart = s_restart_after_leave;
        s_restart_after_leave = 0;
        clear_match_state();
        if (restart) start_lobby_search();
        else set_status(EOS_ONLINE_READY, "Online - ready for Quick Match");
        return;
    }

    if (s_is_host) {
        EOS_Lobby_DestroyLobbyOptions options;
        memset(&options, 0, sizeof(options));
        options.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
        options.LocalUserId = s_local_user;
        options.LobbyId = s_lobby_id;
        set_status(EOS_ONLINE_MATCHMAKING, "Closing co-op lobby...");
        EOS_Lobby_DestroyLobby(s_lobby, &options, NULL, on_match_destroyed);
    } else {
        EOS_Lobby_LeaveLobbyOptions options;
        memset(&options, 0, sizeof(options));
        options.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
        options.LocalUserId = s_local_user;
        options.LobbyId = s_lobby_id;
        set_status(EOS_ONLINE_MATCHMAKING, "Leaving co-op lobby...");
        EOS_Lobby_LeaveLobby(s_lobby, &options, NULL, on_match_left);
    }
}

int eos_online_send_packet(const void* data, uint32_t size, uint8_t channel, int reliable) {
    if (!s_p2p || !s_local_user || !s_remote_user || !data || size == 0 || size > EOS_P2P_MAX_PACKET_SIZE) {
        return 0;
    }
    EOS_P2P_SendPacketOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    options.LocalUserId = s_local_user;
    options.RemoteUserId = s_remote_user;
    options.SocketId = &s_socket;
    options.Channel = channel;
    options.DataLengthBytes = size;
    options.Data = data;
    options.bAllowDelayedDelivery = EOS_TRUE;
    options.Reliability = reliable ? EOS_PR_ReliableOrdered : EOS_PR_UnreliableUnordered;
    options.bDisableAutoAcceptConnection = EOS_FALSE;
    return EOS_P2P_SendPacket(s_p2p, &options) == EOS_Success;
}

int eos_online_receive_packet(void* data, uint32_t capacity, uint8_t* channel) {
    if (!s_p2p || !s_local_user || !s_remote_user || !data || capacity == 0) return 0;

    EOS_P2P_ReceivePacketOptions options;
    memset(&options, 0, sizeof(options));
    options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    options.LocalUserId = s_local_user;
    options.MaxDataSizeBytes = capacity;

    EOS_ProductUserId peer = NULL;
    EOS_P2P_SocketId socket;
    uint8_t packet_channel = 0;
    uint32_t written = 0;
    EOS_EResult result = EOS_P2P_ReceivePacket(
        s_p2p, &options, &peer, &socket, &packet_channel, data, &written);
    if (result != EOS_Success || !same_product_user(peer, s_remote_user) ||
        strcmp(socket.SocketName, COOP_SOCKET) != 0) {
        return 0;
    }
    if (channel) *channel = packet_channel;
    return (int)written;
}
