using System.Numerics;
using System.Runtime.InteropServices;

namespace SpaceUnlimited;

[Flags]
internal enum PadButton : ushort
{
    None = 0,
    DPadUp = 0x0001,
    DPadDown = 0x0002,
    DPadLeft = 0x0004,
    DPadRight = 0x0008,
    Start = 0x0010,
    Back = 0x0020,
    LeftThumb = 0x0040,
    RightThumb = 0x0080,
    LeftShoulder = 0x0100,
    RightShoulder = 0x0200,
    A = 0x1000,
    B = 0x2000,
    X = 0x4000,
    Y = 0x8000
}

internal readonly record struct GamepadSnapshot(
    bool Connected,
    PadButton Buttons,
    Vector2 Move,
    float LeftTrigger,
    float RightTrigger)
{
    public bool IsDown(PadButton button) => (Buttons & button) != 0;
}

internal static class XInput
{
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeGamepad
    {
        public ushort Buttons;
        public byte LeftTrigger;
        public byte RightTrigger;
        public short ThumbLX;
        public short ThumbLY;
        public short ThumbRX;
        public short ThumbRY;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeState
    {
        public uint PacketNumber;
        public NativeGamepad Gamepad;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeVibration
    {
        public ushort LeftMotorSpeed;
        public ushort RightMotorSpeed;
    }

    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
    private static extern uint GetState14(uint userIndex, out NativeState state);

    [DllImport("xinput9_1_0.dll", EntryPoint = "XInputGetState")]
    private static extern uint GetState91(uint userIndex, out NativeState state);

    [DllImport("xinput1_4.dll", EntryPoint = "XInputSetState")]
    private static extern uint SetState14(uint userIndex, ref NativeVibration vibration);

    [DllImport("xinput9_1_0.dll", EntryPoint = "XInputSetState")]
    private static extern uint SetState91(uint userIndex, ref NativeVibration vibration);

    public static GamepadSnapshot Read()
    {
        NativeState native;
        uint result;

        try
        {
            result = GetState14(0, out native);
        }
        catch (DllNotFoundException)
        {
            try
            {
                result = GetState91(0, out native);
            }
            catch
            {
                return default;
            }
        }
        catch
        {
            return default;
        }

        if (result != 0)
        {
            return default;
        }

        var raw = new Vector2(NormalizeAxis(native.Gamepad.ThumbLX), -NormalizeAxis(native.Gamepad.ThumbLY));
        var length = raw.Length();
        const float deadZone = 0.18f;
        Vector2 move = length <= deadZone
            ? Vector2.Zero
            : Vector2.Normalize(raw) * MathF.Min(1f, (length - deadZone) / (1f - deadZone));

        return new GamepadSnapshot(
            true,
            (PadButton)native.Gamepad.Buttons,
            move,
            native.Gamepad.LeftTrigger / 255f,
            native.Gamepad.RightTrigger / 255f);
    }

    public static void Vibrate(float left, float right)
    {
        var vibration = new NativeVibration
        {
            LeftMotorSpeed = (ushort)(Math.Clamp(left, 0f, 1f) * ushort.MaxValue),
            RightMotorSpeed = (ushort)(Math.Clamp(right, 0f, 1f) * ushort.MaxValue)
        };

        try
        {
            SetState14(0, ref vibration);
        }
        catch (DllNotFoundException)
        {
            try { SetState91(0, ref vibration); } catch { }
        }
        catch { }
    }

    private static float NormalizeAxis(short value) => value < 0 ? value / 32768f : value / 32767f;
}

internal sealed class InputState
{
    private readonly HashSet<Keys> _down = [];
    private readonly HashSet<Keys> _pressed = [];
    private readonly HashSet<Keys> _released = [];

    public GamepadSnapshot Pad { get; private set; }
    public GamepadSnapshot PreviousPad { get; private set; }
    public PointF MousePosition { get; set; }
    public bool MouseDown { get; set; }
    public bool MousePressed { get; set; }
    public bool MouseMoved { get; set; }

    public void KeyDown(Keys key)
    {
        if (_down.Add(key))
        {
            _pressed.Add(key);
        }
    }

    public void KeyUp(Keys key)
    {
        if (_down.Remove(key))
        {
            _released.Add(key);
        }
    }

    public bool Down(params Keys[] keys) => keys.Any(_down.Contains);
    public bool Pressed(params Keys[] keys) => keys.Any(_pressed.Contains);
    public bool Released(params Keys[] keys) => keys.Any(_released.Contains);

    public bool PadDown(PadButton button) => Pad.IsDown(button);
    public bool PadPressed(PadButton button) => Pad.IsDown(button) && !PreviousPad.IsDown(button);

    public void PollController()
    {
        PreviousPad = Pad;
        Pad = XInput.Read();
    }

    public void EndFrame()
    {
        _pressed.Clear();
        _released.Clear();
        MousePressed = false;
        MouseMoved = false;
    }
}
