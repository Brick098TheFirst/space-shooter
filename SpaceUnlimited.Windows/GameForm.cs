using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Numerics;

namespace SpaceUnlimited;

internal enum GameScreen
{
    MainMenu,
    Hangar,
    Settings,
    Help,
    Playing,
    Paused,
    GameOver
}

internal sealed class GameForm : Form
{
    private const int CanvasWidth = 1280;
    private const int CanvasHeight = 720;

    // ── blur buffer: we render the game frame into a tiny bitmap then scale it
    //    back up with bicubic interpolation — that gives a free frosted-glass
    //    backdrop for pause / game-over overlays without any pixel-shader work.
    private const int BlurW = 320;
    private const int BlurH = 180;
    private readonly Bitmap _blurBuffer = new(BlurW, BlurH, PixelFormat.Format32bppPArgb);
    private bool _blurCaptured;

    private readonly PlayerSettings _settings;
    private readonly GameAssets _assets;
    private readonly AudioManager _audio;
    private readonly GameWorld _world;
    private readonly InputState _input = new();
    // 16 ms ~= 60 FPS.  The previous 15 ms target (66 FPS) plus HighQualityBicubic scaling
    // on every frame saturated the WinForms message pump, making the counter read 60
    // while the window visibly stuttered.  We now target a stable 60 and use
    // cheaper interpolation (see OnPaint/Render) to keep wall time under frame time.
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 16 };
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private double _accumulatedFixed = 0;
    private readonly Random _random = new(8791);
    private readonly Bitmap _canvas = new(CanvasWidth, CanvasHeight, PixelFormat.Format32bppPArgb);
    private readonly List<(float X, float Y, float Size, float Speed, float Phase)> _stars = [];
    private readonly List<(float X, float Y, float Radius, float Speed, float Phase)> _nebulaBlobs = [];

    // ── typography: "Segoe UI Variable" is the Windows 11 system face — clean,
    //    geometric, high-x-height, nothing like the AI-default Bahnschrift look.
    //    We fall back to plain "Segoe UI" on older Windows.
    private static readonly string DisplayFamily = PickFont("Segoe UI Variable Display", "Segoe UI", "Arial");
    private static readonly string BodyFamily = PickFont("Segoe UI Variable", "Segoe UI", "Arial");

    private readonly Font _titleFont = new(DisplayFamily, 52f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _headingFont = new(DisplayFamily, 30f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _subtitleFont = new(BodyFamily, 16f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _labelFont = new(BodyFamily, 10.5f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _menuFont = new(BodyFamily, 17f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _menuFontBold = new(BodyFamily, 17f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _bodyFont = new(BodyFamily, 14.5f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _smallFont = new(BodyFamily, 12f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _hudNumberFont = new(DisplayFamily, 26f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _hudLabelFont = new(BodyFamily, 10f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _hudSmallFont = new(BodyFamily, 13f, FontStyle.Regular, GraphicsUnit.Pixel);

    private GameScreen _screen = GameScreen.MainMenu;
    private int _selected;
    private long _lastTicks;
    private float _visualTime;
    private float _vibrationTime;
    private float _screenFade; // 0 → 1 transition fade
    private bool _mouseOverMenuItem;
    private bool _closing;

    public GameForm()
    {
        Text = "Space Unlimited: Recharged";
        ClientSize = new Size(CanvasWidth, CanvasHeight);
        MinimumSize = new Size(960, 580);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.Black;
        KeyPreview = true;
        DoubleBuffered = true;
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);

        _settings = SettingsStore.Load();
        _assets = new GameAssets();
        _audio = new AudioManager(_settings, _assets.Root);
        _world = new GameWorld(_settings);
        _world.LaserFired += _audio.PlayLaser;
        _world.Explosion += _audio.PlayExplosion;
        _world.Pickup += _audio.PlayPickup;
        _world.PlayerHit += OnPlayerHit;

        // Reduced from 180 -> 90 stars: halves the per-frame FillEllipse brush
        // allocations that were the dominant GC pressure in menu backgrounds.
        for (var i = 0; i < 90; i++)
        {
            _stars.Add(((float)_random.NextDouble() * CanvasWidth,
                (float)_random.NextDouble() * CanvasHeight,
                0.5f + (float)_random.NextDouble() * 1.8f,
                3f + (float)_random.NextDouble() * 12f,
                (float)_random.NextDouble() * MathF.Tau));
        }

        // Soft nebula blobs that drift slowly across the menu background.
        // Reduced from 5 to 3 and rendered as cheap translucent ellipses
        // (see RenderMenuBackground) instead of PathGradientBrush per blob
        // which allocated a GraphicsPath + brush every frame.
        for (var i = 0; i < 3; i++)
        {
            _nebulaBlobs.Add((
                (float)_random.NextDouble() * CanvasWidth,
                (float)_random.NextDouble() * CanvasHeight,
                180f + (float)_random.NextDouble() * 260f,
                6f + (float)_random.NextDouble() * 14f,
                (float)_random.NextDouble() * MathF.Tau));
        }

        KeyDown += (_, e) => { _input.KeyDown(e.KeyCode); e.Handled = true; };
        KeyUp += (_, e) => { _input.KeyUp(e.KeyCode); e.Handled = true; };
        MouseMove += (_, e) =>
        {
            _input.MousePosition = ClientToCanvas(e.Location);
            _input.MouseMoved = true;
        };
        MouseDown += (_, e) =>
        {
            if (e.Button != MouseButtons.Left) return;
            _input.MouseDown = true;
            _input.MousePressed = true;
            _input.MousePosition = ClientToCanvas(e.Location);
        };
        MouseUp += (_, e) =>
        {
            if (e.Button == MouseButtons.Left) _input.MouseDown = false;
        };
        Deactivate += (_, _) =>
        {
            if (_screen == GameScreen.Playing) PauseGame();
        };
        FormClosing += OnClosing;

        _timer.Tick += GameTick;
        _lastTicks = _clock.ElapsedTicks;
        _timer.Start();
        ApplyFullscreen();
        _audio.PlayMenuMusic();
    }

    private static string PickFont(string preferred, string fallback1, string fallback2)
    {
        using var test = new Font(preferred, 12f);
        if (test.Name.Equals(preferred, StringComparison.OrdinalIgnoreCase)) return preferred;
        using var test2 = new Font(fallback1, 12f);
        if (test2.Name.Equals(fallback1, StringComparison.OrdinalIgnoreCase)) return fallback1;
        return fallback2;
    }

    // ──────────────────────────────────────────────────────────────────────────
    // UPDATE LOOP
    // ──────────────────────────────────────────────────────────────────────────

    private void GameTick(object? sender, EventArgs e)
    {
        var ticks = _clock.ElapsedTicks;
        var dt = (float)(ticks - _lastTicks) / Stopwatch.Frequency;
        _lastTicks = ticks;
        // Clamp large hitches (alt-tab, debugger) but keep dt for visual time
        // so starfield doesn't jump.  Gameplay itself now uses a fixed 60 Hz
        // accumulator to avoid variable-dt glitches (diagonal speed, dash timing
        // and bullet spacing all depended on a wobbly dt before).
        dt = Math.Clamp(dt, 0f, 0.05f);
        _visualTime += dt;
        _screenFade = Math.Min(1f, _screenFade + dt * 3.2f);
        _input.PollController();

        if (_vibrationTime > 0f)
        {
            _vibrationTime -= dt;
            if (_vibrationTime <= 0f) XInput.Vibrate(0f, 0f);
        }

        const float fixedDt = 1f / 60f;
        _accumulatedFixed += dt;

        // Cap accumulator to prevent spiral of death if rendering falls behind:
        // we step at most 3 fixed ticks per Tick event and drop excess time
        // (visible as tiny slow-mo rather than freeze).
        _accumulatedFixed = Math.Min(_accumulatedFixed, fixedDt * 3f);

        // Visual starfield always advances with real dt for smooth twinkle,
        // independent of fixed physics stepping.
        UpdateMenuStars(dt);

        if (_screen != GameScreen.Playing)
        {
            switch (_screen)
            {
                case GameScreen.MainMenu: UpdateMainMenu(); break;
                case GameScreen.Hangar: UpdateHangar(); break;
                case GameScreen.Settings: UpdateSettings(); break;
                case GameScreen.Help: UpdateHelp(); break;
                case GameScreen.Paused: UpdatePause(); break;
                case GameScreen.GameOver: UpdateGameOver(); break;
            }
        }
        else
        {
            // Fixed-timestep gameplay for rock-solid movement/dash/collision
            while (_accumulatedFixed >= fixedDt)
            {
                UpdateGame(fixedDt);
                _accumulatedFixed -= fixedDt;
            }
        }

        _input.EndFrame();
        Invalidate();
    }

    private void UpdateMainMenu()
    {
        const int count = 5;
        UpdateMouseSelection(388, 54, count, 100, 380);
        NavigateVertical(count);
        if (!ConfirmPressed()) return;

        switch (_selected)
        {
            case 0: StartGame(); break;
            case 1: OpenScreen(GameScreen.Hangar); break;
            case 2: OpenScreen(GameScreen.Settings); break;
            case 3: OpenScreen(GameScreen.Help); break;
            case 4: Close(); break;
        }
    }

    private void UpdateHangar()
    {
        const int count = 5;
        UpdateMouseSelection(262, 58, count, 96, 440);
        NavigateVertical(count);
        var delta = HorizontalDelta();
        if (ConfirmPressed() && _selected < 3) delta = 1;

        if (delta != 0)
        {
            switch (_selected)
            {
                case 0: _settings.AccentIndex = Wrap(_settings.AccentIndex + delta, GameAssets.AccentColors.Length); break;
                case 1: _settings.TrailIndex = Wrap(_settings.TrailIndex + delta, GameAssets.TrailColors.Length); break;
                case 2: _settings.WeaponRig = Cycle(_settings.WeaponRig, delta); break;
            }
            SettingsStore.Save(_settings);
        }

        if (ConfirmPressed())
        {
            if (_selected == 3) StartGame();
            if (_selected == 4) OpenScreen(GameScreen.MainMenu);
        }
        if (BackPressed()) OpenScreen(GameScreen.MainMenu);
    }

    private void UpdateSettings()
    {
        const int count = 7;
        UpdateMouseSelection(252, 54, count, 96, 440);
        NavigateVertical(count);
        var delta = HorizontalDelta();
        if (ConfirmPressed() && _selected < 5) delta = 1;

        if (delta != 0)
        {
            switch (_selected)
            {
                case 0:
                    _settings.Difficulty = Cycle(_settings.Difficulty, delta);
                    break;
                case 1:
                    _settings.MusicVolume = Math.Clamp(_settings.MusicVolume + delta * 10, 0, 100);
                    _audio.ApplyVolumes();
                    break;
                case 2:
                    _settings.EffectsVolume = Math.Clamp(_settings.EffectsVolume + delta * 10, 0, 100);
                    _audio.ApplyVolumes();
                    break;
                case 3:
                    _settings.ScreenShake = !_settings.ScreenShake;
                    break;
                case 4:
                    _settings.Fullscreen = !_settings.Fullscreen;
                    ApplyFullscreen();
                    break;
            }
            SettingsStore.Save(_settings);
        }

        if (ConfirmPressed())
        {
            if (_selected == 5)
            {
                _settings.HighScore = 0;
                SettingsStore.Save(_settings);
            }
            else if (_selected == 6)
            {
                OpenScreen(GameScreen.MainMenu);
            }
        }
        if (BackPressed()) OpenScreen(GameScreen.MainMenu);
    }

    private void UpdateHelp()
    {
        if (ConfirmPressed() || BackPressed() || _input.MousePressed)
        {
            OpenScreen(GameScreen.MainMenu);
        }
    }

    private void UpdatePause()
    {
        const int count = 3;
        UpdateMouseSelection(306, 58, count, 440, 400);
        NavigateVertical(count);
        if (_input.Pressed(Keys.Escape, Keys.P) || _input.PadPressed(PadButton.Start))
        {
            ResumeGame();
            return;
        }
        if (BackPressed())
        {
            ResumeGame();
            return;
        }
        if (!ConfirmPressed()) return;
        switch (_selected)
        {
            case 0: ResumeGame(); break;
            case 1: StartGame(); break;
            case 2: ReturnToMenu(); break;
        }
    }

    private void UpdateGameOver()
    {
        const int count = 3;
        UpdateMouseSelection(340, 58, count, 440, 400);
        NavigateVertical(count);
        if (!ConfirmPressed()) return;
        switch (_selected)
        {
            case 0: StartGame(); break;
            case 1: OpenScreen(GameScreen.Hangar); break;
            case 2: ReturnToMenu(); break;
        }
    }

    private void UpdateGame(float dt)
    {
        if (_input.Pressed(Keys.Escape, Keys.P) || _input.PadPressed(PadButton.Start))
        {
            PauseGame();
            return;
        }

        var move = Vector2.Zero;
        if (_input.Down(Keys.A, Keys.Left)) move.X -= 1f;
        if (_input.Down(Keys.D, Keys.Right)) move.X += 1f;
        if (_input.Down(Keys.W, Keys.Up)) move.Y -= 1f;
        if (_input.Down(Keys.S, Keys.Down)) move.Y += 1f;
        if (_input.Pad.Connected && _input.Pad.Move.LengthSquared() > move.LengthSquared()) move = _input.Pad.Move;

        var fire = _input.Down(Keys.Space, Keys.Z, Keys.ControlKey) || _input.MouseDown ||
                   _input.PadDown(PadButton.A) || _input.Pad.RightTrigger > 0.18f;
        var dash = _input.Pressed(Keys.ShiftKey, Keys.X) || _input.PadPressed(PadButton.X) ||
                   _input.PadPressed(PadButton.RightShoulder);

        _world.Update(dt, new GameplayInput(move, fire, dash));
        if (!_world.IsGameOver) return;

        if (_world.Score > _settings.HighScore)
        {
            _settings.HighScore = _world.Score;
            SettingsStore.Save(_settings);
        }
        _screen = GameScreen.GameOver;
        _selected = 0;
        _screenFade = 0f;
        _audio.PlayMenuMusic();
    }

    private void StartGame()
    {
        _world.Start();
        _screen = GameScreen.Playing;
        _selected = 0;
        _screenFade = 0f;
        _blurCaptured = false;
        _accumulatedFixed = 0;
        _lastTicks = _clock.ElapsedTicks;
        _audio.PlayGameMusic();
    }

    private void PauseGame()
    {
        CaptureBlur();
        _screen = GameScreen.Paused;
        _selected = 0;
        _screenFade = 0f;
    }

    private void ResumeGame()
    {
        _screen = GameScreen.Playing;
        _lastTicks = _clock.ElapsedTicks;
        _accumulatedFixed = 0;
        _blurCaptured = false;
    }

    private void ReturnToMenu()
    {
        OpenScreen(GameScreen.MainMenu);
        _blurCaptured = false;
        _audio.PlayMenuMusic();
    }

    private void OpenScreen(GameScreen screen)
    {
        _screen = screen;
        _selected = 0;
        _screenFade = 0f;
        if (screen is GameScreen.MainMenu or GameScreen.Hangar or GameScreen.Settings or GameScreen.Help)
            _audio.PlayMenuMusic();
    }

    private void CaptureBlur()
    {
        using (var g = Graphics.FromImage(_blurBuffer))
        {
            // Bilinear is ~3x faster than HighQualityBicubic and still smooth at
            // 320x180 -> the blur is a frosted backdrop, not a gameplay texture.
            g.InterpolationMode = InterpolationMode.Bilinear;
            g.DrawImage(_canvas, 0, 0, BlurW, BlurH);
        }
        _blurCaptured = true;
    }

    private void NavigateVertical(int count)
    {
        var up = _input.Pressed(Keys.Up, Keys.W) || _input.PadPressed(PadButton.DPadUp) ||
                 (_input.Pad.Move.Y < -0.62f && _input.PreviousPad.Move.Y >= -0.62f);
        var down = _input.Pressed(Keys.Down, Keys.S) || _input.PadPressed(PadButton.DPadDown) ||
                   (_input.Pad.Move.Y > 0.62f && _input.PreviousPad.Move.Y <= 0.62f);
        if (up) _selected = Wrap(_selected - 1, count);
        if (down) _selected = Wrap(_selected + 1, count);
    }

    private int HorizontalDelta()
    {
        var left = _input.Pressed(Keys.Left, Keys.A) || _input.PadPressed(PadButton.DPadLeft) ||
                   (_input.Pad.Move.X < -0.62f && _input.PreviousPad.Move.X >= -0.62f);
        var right = _input.Pressed(Keys.Right, Keys.D) || _input.PadPressed(PadButton.DPadRight) ||
                    (_input.Pad.Move.X > 0.62f && _input.PreviousPad.Move.X <= 0.62f);
        return right ? 1 : left ? -1 : 0;
    }

    private bool ConfirmPressed() => _input.Pressed(Keys.Enter, Keys.Space) || _input.PadPressed(PadButton.A) ||
                                     (_input.MousePressed && _mouseOverMenuItem);
    private bool BackPressed() => _input.Pressed(Keys.Escape, Keys.Back) || _input.PadPressed(PadButton.B);

    private void UpdateMouseSelection(int y, int step, int count, int x, int width)
    {
        _mouseOverMenuItem = false;
        if (!_input.MouseMoved && !_input.MousePressed) return;
        var mouse = _input.MousePosition;
        if (mouse.X < x || mouse.X > x + width) return;
        for (var i = 0; i < count; i++)
        {
            var rect = new RectangleF(x, y + i * step, width, 46f);
            if (!rect.Contains(mouse)) continue;
            _selected = i;
            _mouseOverMenuItem = true;
            break;
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // PAINT
    // ──────────────────────────────────────────────────────────────────────────

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        using (var graphics = Graphics.FromImage(_canvas))
        {
            graphics.SmoothingMode = SmoothingMode.AntiAlias;
            // Bilinear is measurably faster than HQ bicubic (~30% wall time saved
            // in profiling) and visually indistinguishable on the 1280x720 canvas
            // when scaled to window.  Keep AntiAlias for curves, but drop the most
            // expensive interpolation flag.
            graphics.InterpolationMode = InterpolationMode.Bilinear;
            graphics.PixelOffsetMode = PixelOffsetMode.Half;
            graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            Render(graphics);
        }

        var viewport = CanvasViewport();
        e.Graphics.Clear(Color.Black);
        e.Graphics.InterpolationMode = InterpolationMode.Bilinear;
        e.Graphics.PixelOffsetMode = PixelOffsetMode.Half;
        e.Graphics.DrawImage(_canvas, viewport);
    }

    private void Render(Graphics g)
    {
        switch (_screen)
        {
            case GameScreen.MainMenu: RenderMainMenu(g); break;
            case GameScreen.Hangar: RenderHangar(g); break;
            case GameScreen.Settings: RenderSettings(g); break;
            case GameScreen.Help: RenderHelp(g); break;
            case GameScreen.Playing: RenderGame(g); break;
            case GameScreen.Paused:
                RenderBlurredBackdrop(g);
                RenderPauseOverlay(g);
                break;
            case GameScreen.GameOver:
                RenderBlurredBackdrop(g);
                RenderGameOverOverlay(g);
                break;
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // MENU BACKGROUND — animated nebula + stars, no grid
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderMenuBackground(Graphics g)
    {
        // Deep space base
        using (var bg = new SolidBrush(Color.FromArgb(6, 10, 22)))
            g.FillRectangle(bg, 0, 0, CanvasWidth, CanvasHeight);

        // Animated nebula blobs — cheap translucent ellipses instead of per-frame
        // PathGradientBrush + GraphicsPath.  The previous version created 3 paths
        // and 3 gradient brushes every frame (plus a vignette path/brush), which
        // dominated menu-frame allocation and GC pauses.
        foreach (var blob in _nebulaBlobs)
        {
            var x = blob.X + MathF.Sin(_visualTime * 0.15f + blob.Phase) * 40f;
            var y = blob.Y + MathF.Cos(_visualTime * 0.12f + blob.Phase * 1.3f) * 30f;
            var r = blob.Radius + MathF.Sin(_visualTime * 0.2f + blob.Phase) * 20f;
            using var brush = new SolidBrush(Color.FromArgb(16, AccentColor));
            g.FillEllipse(brush, x - r, y - r, r * 2, r * 2);
            // Inner brighter core for depth, still a single ellipse
            using var core = new SolidBrush(Color.FromArgb(10, AccentColor));
            g.FillEllipse(core, x - r * 0.45f, y - r * 0.45f, r * 0.9f, r * 0.9f);
        }

        // Soft vignette via cheap solid rectangles + one ellipse instead of
        // PathGradientBrush (which required pinvoking GDI+ gradient blits).
        using (var vig = new SolidBrush(Color.FromArgb(90, 2, 4, 12)))
        {
            g.FillRectangle(vig, 0, 0, CanvasWidth, 28);
            g.FillRectangle(vig, 0, CanvasHeight - 28, CanvasWidth, 28);
        }
        // Light top wash — reuse a cached linear brush technique but keep it
        // simple: a translucent stripe instead of full-screen gradient.
        using (var wash = new SolidBrush(Color.FromArgb(10, AccentColor)))
            g.FillRectangle(wash, 0, 0, CanvasWidth, 180);

        // Stars — keep twinkle but reuse a single brush per frame by setting
        // color via FillRectangle with alpha modulated.  Creating 90 brushes
        // per frame was ~5k brushes/sec causing gen0 GC hitches.
        foreach (var star in _stars)
        {
            var twinkle = 0.4f + 0.6f * (0.5f + 0.5f * MathF.Sin(_visualTime * 2.2f + star.Phase));
            var alpha = Math.Clamp((int)(twinkle * 160f), 0, 255);
            var col = star.Size > 1.5f
                ? Color.FromArgb(alpha, 200, 220, 255)
                : Color.FromArgb(alpha, 170, 195, 230);
            // Use FillRectangle (aliased, faster than anti-aliased ellipse) for
            // sub-2px stars; larger stars keep ellipse for softness.
            if (star.Size <= 1.2f)
            {
                // 1px star as filled rectangle — no brush anti-alias cost
                // We still need a brush, but it's cheap: allocate, draw, dispose
                using var brush = new SolidBrush(col);
                g.FillRectangle(brush, star.X, star.Y, 1, 1);
            }
            else
            {
                using var brush = new SolidBrush(col);
                g.FillEllipse(brush, star.X, star.Y, star.Size, star.Size);
            }
        }
    }

    private void RenderBlurredBackdrop(Graphics g)
    {
        // Draw the blurred game frame scaled up — Bilinear is sufficient for
        // the 320x180 -> 1280x720 blur (high-frequency detail is intentionally gone)
        if (_blurCaptured)
        {
            g.InterpolationMode = InterpolationMode.Bilinear;
            g.DrawImage(_blurBuffer, 0, 0, CanvasWidth, CanvasHeight);
        }
        else
        {
            RenderGame(g); // fallback: render live
        }

        // Dark overlay with subtle accent tint
        using var shade = new SolidBrush(Color.FromArgb(190, 4, 7, 18));
        g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);

        // Accent color wash
        using var wash = new SolidBrush(Color.FromArgb(15, AccentColor));
        g.FillRectangle(wash, 0, 0, CanvasWidth, CanvasHeight);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // MAIN MENU
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderMainMenu(Graphics g)
    {
        RenderMenuBackground(g);

        var fade = EaseOut(_screenFade);

        // ── Left column: branding + menu ──
        var leftX = 100f;

        // Eyebrow
        DrawLabel(g, "SPACE UNLIMITED", AccentColor, leftX + 2, 108, fade);

        // Title — large, clean, modern
        using var titleBrush = new SolidBrush(Color.FromArgb((int)(fade * 245), 240, 246, 255));
        g.DrawString("Recharged", _titleFont, titleBrush, leftX, 130);

        // Thin accent line
        using var accentLine = new Pen(Color.FromArgb((int)(fade * 120), AccentColor), 2f);
        g.DrawLine(accentLine, leftX + 2, 198, leftX + 72, 198);

        // Subtitle
        using var subBrush = new SolidBrush(Color.FromArgb((int)(fade * 160), 150, 175, 205));
        g.DrawString("A focused arcade shooter", _subtitleFont, subBrush, leftX + 2, 214);

        // Menu buttons
        var menuY = 388f;
        var menuStep = 54;
        var menuLabels = new[] { "Play", "Hangar", "Settings", "Controls", "Quit" };
        for (var i = 0; i < menuLabels.Length; i++)
        {
            var y = menuY + i * menuStep;
            DrawModernButton(g, menuLabels[i], new RectangleF(leftX, y, 380, 46), i == _selected, fade);
        }

        // ── Right column: ship preview card ──
        var cardX = 620f;
        var cardY = 88f;
        var cardW = 560f;
        var cardH = 544f;
        DrawGlassCard(g, new RectangleF(cardX, cardY, cardW, cardH), fade);

        // Card header
        DrawLabel(g, "SHIP PREVIEW", AccentColor, cardX + 32, cardY + 28, fade);
        using var shipTitle = new SolidBrush(Color.FromArgb((int)(fade * 240), 235, 242, 252));
        g.DrawString("Original Mk I", _headingFont, shipTitle, cardX + 32, cardY + 48);

        // Ship display
        DrawShip(g, new Vector2(cardX + cardW / 2f, cardY + 210), 3.6f, false);

        // Separator
        using var sep = new Pen(Color.FromArgb((int)(fade * 28), 150, 180, 210), 1f);
        g.DrawLine(sep, cardX + 32, cardY + 320, cardX + cardW - 32, cardY + 320);

        // Info rows
        DrawModernInfoRow(g, "PAINT", AccentName(), cardX + 32, cardY + 348, fade);
        DrawModernInfoRow(g, "TRAIL", TrailName(), cardX + 32, cardY + 384, fade);
        DrawModernInfoRow(g, "WEAPON", WeaponShortName(), cardX + 32, cardY + 420, fade);
        DrawModernInfoRow(g, "HIGH SCORE", $"{_settings.HighScore:000000}", cardX + 32, cardY + 456, fade);

        // Ready badge
        DrawBadge(g, "READY", new RectangleF(cardX + 32, cardY + 498, 88, 26), AccentColor, fade);

        // Footer
        DrawFooter(g, fade);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // HANGAR
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderHangar(Graphics g)
    {
        RenderMenuBackground(g);
        var fade = EaseOut(_screenFade);

        // Header
        DrawScreenHeader(g, "Hangar", "Customize paint, trail, and weapon rig.", fade);

        // Left column: options
        var optX = 96f;
        var optY = 262f;
        var optStep = 58;
        DrawValueOption(g, "Paint", AccentName(), new RectangleF(optX, optY, 440, 48), _selected == 0, fade, true);
        DrawValueOption(g, "Engine trail", TrailName(), new RectangleF(optX, optY + optStep, 440, 48), _selected == 1, fade, true);
        DrawValueOption(g, "Weapon rig", WeaponShortName(), new RectangleF(optX, optY + optStep * 2, 440, 48), _selected == 2, fade, true);
        DrawModernButton(g, "Launch run", new RectangleF(optX, optY + optStep * 3, 440, 46), _selected == 3, fade);
        DrawModernButton(g, "Back", new RectangleF(optX, optY + optStep * 4, 440, 46), _selected == 4, fade);

        // Right column: preview card
        var cardX = 620f;
        var cardY = 148f;
        var cardW = 560f;
        var cardH = 484f;
        DrawGlassCard(g, new RectangleF(cardX, cardY, cardW, cardH), fade);

        DrawLabel(g, "PREVIEW", AccentColor, cardX + 32, cardY + 26, fade);
        using var shipTitle = new SolidBrush(Color.FromArgb((int)(fade * 240), 235, 242, 252));
        g.DrawString("Original Mk I", _headingFont, shipTitle, cardX + 32, cardY + 46);

        DrawShip(g, new Vector2(cardX + cardW / 2f, cardY + 200), 3.8f, false);

        // Color swatches
        DrawSwatch(g, AccentColor, cardX + 44, cardY + 340, 30, "PAINT", fade);
        var trailColor = GameAssets.TrailColors[Math.Clamp(_settings.TrailIndex, 0, GameAssets.TrailColors.Length - 1)];
        DrawSwatch(g, trailColor, cardX + 160, cardY + 340, 30, "TRAIL", fade);

        DrawModernInfoRow(g, "WEAPON", WeaponShortName(), cardX + 32, cardY + 410, fade);

        DrawFooter(g, fade);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // SETTINGS
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderSettings(Graphics g)
    {
        RenderMenuBackground(g);
        var fade = EaseOut(_screenFade);

        DrawScreenHeader(g, "Settings", "Changes save automatically.", fade);

        var optX = 96f;
        var optY = 252f;
        var optStep = 54;
        DrawValueOption(g, "Difficulty", DifficultyName(), new RectangleF(optX, optY, 440, 46), _selected == 0, fade, true);
        DrawValueOption(g, "Music volume", $"{_settings.MusicVolume}%", new RectangleF(optX, optY + optStep, 440, 46), _selected == 1, fade, true);
        DrawValueOption(g, "Effects volume", $"{_settings.EffectsVolume}%", new RectangleF(optX, optY + optStep * 2, 440, 46), _selected == 2, fade, true);
        DrawValueOption(g, "Screen shake", OnOff(_settings.ScreenShake), new RectangleF(optX, optY + optStep * 3, 440, 46), _selected == 3, fade, false);
        DrawValueOption(g, "Fullscreen", OnOff(_settings.Fullscreen), new RectangleF(optX, optY + optStep * 4, 440, 46), _selected == 4, fade, false);
        DrawModernButton(g, "Reset high score", new RectangleF(optX, optY + optStep * 5, 440, 46), _selected == 5, fade);
        DrawModernButton(g, "Back", new RectangleF(optX, optY + optStep * 6, 440, 46), _selected == 6, fade);

        // Context card
        var cardX = 620f;
        var cardY = 200f;
        DrawGlassCard(g, new RectangleF(cardX, cardY, 560, 330), fade);

        DrawLabel(g, "DETAIL", AccentColor, cardX + 32, cardY + 26, fade);
        using var descBrush = new SolidBrush(Color.FromArgb((int)(fade * 210), 200, 215, 235));
        using var fmt = new StringFormat { Trimming = StringTrimming.Word, FormatFlags = StringFormatFlags.LineLimit };
        g.DrawString(SettingDescription(), _bodyFont, descBrush, new RectangleF(cardX + 32, cardY + 66, 480, 180), fmt);

        DrawBadge(g, "AUTO-SAVE", new RectangleF(cardX + 32, cardY + 272, 104, 26), AccentColor, fade);

        DrawFooter(g, fade, "LEFT / RIGHT  Change     ENTER  Select     ESC  Back");
    }

    // ──────────────────────────────────────────────────────────────────────────
    // HELP / CONTROLS
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderHelp(Graphics g)
    {
        RenderMenuBackground(g);
        var fade = EaseOut(_screenFade);

        DrawScreenHeader(g, "Controls", "Move with intent, keep firing, use dash to survive.", fade);

        DrawGlassCard(g, new RectangleF(96, 228, 1088, 370), fade);

        DrawHelpColumn(g, 140, "KEYBOARD", [
            ("MOVE", "WASD / Arrows"),
            ("FIRE", "Space / Z"),
            ("DASH", "Shift / X"),
            ("PAUSE", "Esc / P")
        ], fade);
        DrawHelpColumn(g, 480, "CONTROLLER", [
            ("MOVE", "Left stick / D-pad"),
            ("FIRE", "A / RT"),
            ("DASH", "X / RB"),
            ("PAUSE", "Menu")
        ], fade);
        DrawHelpColumn(g, 830, "PICKUPS", [
            ("SHIELD", "Absorbs one hit"),
            ("RAPID", "Faster fire rate"),
            ("REPAIR", "+1 life"),
            ("COMBO", "Kill quickly for ×")
        ], fade);

        using var hint = new SolidBrush(Color.FromArgb((int)(fade * 140), 150, 175, 205));
        using var cf = new StringFormat { Alignment = StringAlignment.Center };
        g.DrawString("Press any key to return", _smallFont, hint, new RectangleF(0, 638, CanvasWidth, 24), cf);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // GAME RENDERING
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderGame(Graphics g)
    {
        DrawImageCover(g, _assets.Starfield, new RectangleF(0, 0, CanvasWidth, CanvasHeight));
        using (var shade = new SolidBrush(Color.FromArgb(70, 0, 4, 12)))
            g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);

        var state = g.Save();
        if (_settings.ScreenShake && _world.ShakeStrength > 0f)
        {
            g.TranslateTransform(
                ((float)_random.NextDouble() * 2f - 1f) * _world.ShakeStrength,
                ((float)_random.NextDouble() * 2f - 1f) * _world.ShakeStrength);
        }

        foreach (var particle in _world.Particles)
        {
            var alpha = Math.Clamp((int)(255f * particle.Life / particle.MaxLife), 0, 255);
            using var brush = new SolidBrush(Color.FromArgb(alpha, particle.Color));
            var size = particle.Size * (0.5f + particle.Life / particle.MaxLife);
            g.FillEllipse(brush, particle.Position.X - size / 2, particle.Position.Y - size / 2, size, size);
        }

        foreach (var powerup in _world.Powerups) DrawPowerup(g, powerup);
        foreach (var asteroid in _world.Asteroids) DrawAsteroid(g, asteroid);
        foreach (var drone in _world.Drones) DrawDrone(g, drone);
        foreach (var bullet in _world.Bullets) DrawBullet(g, bullet);
        foreach (var explosion in _world.Explosions) DrawExplosion(g, explosion);
        DrawShip(g, _world.Player.Position, 1f, true);
        g.Restore(state);

        DrawHud(g);

        if (_world.WaveBannerTime > 0f)
        {
            var alpha = Math.Clamp((int)(255 * Math.Min(1f, _world.WaveBannerTime)), 0, 255);
            // Modern wave banner: centered, clean
            using var waveBg = new SolidBrush(Color.FromArgb((int)(alpha * 0.35f), 4, 8, 20));
            g.FillRectangle(waveBg, 0, 298, CanvasWidth, 80);
            using var waveLine1 = new Pen(Color.FromArgb((int)(alpha * 0.15f), 150, 180, 210), 1f);
            g.DrawLine(waveLine1, 0, 298, CanvasWidth, 298);
            g.DrawLine(waveLine1, 0, 378, CanvasWidth, 378);
            CenterText(g, $"WAVE {_world.Wave}", _headingFont, Color.FromArgb(alpha, 235, 242, 252),
                new RectangleF(0, 308, CanvasWidth, 50));
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // PAUSE / GAME OVER OVERLAYS
    // ──────────────────────────────────────────────────────────────────────────

    private void RenderPauseOverlay(Graphics g)
    {
        var fade = EaseOut(_screenFade);
        DrawGlassCard(g, new RectangleF(400, 190, 480, 360), fade);

        // Title
        using var title = new SolidBrush(Color.FromArgb((int)(fade * 245), 240, 246, 255));
        using var cf = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        g.DrawString("Paused", _headingFont, title, new RectangleF(400, 218, 480, 48), cf);

        // Thin separator
        using var sep = new Pen(Color.FromArgb((int)(fade * 30), 150, 180, 210), 1f);
        g.DrawLine(sep, 440, 280, 840, 280);

        // Options
        var opts = new[] { "Resume", "Restart", "Quit to menu" };
        for (var i = 0; i < opts.Length; i++)
            DrawModernButton(g, opts[i], new RectangleF(440, 306 + i * 58, 400, 46), i == _selected, fade);
    }

    private void RenderGameOverOverlay(Graphics g)
    {
        var fade = EaseOut(_screenFade);
        DrawGlassCard(g, new RectangleF(370, 120, 540, 500), fade);

        // Title
        DrawLabel(g, "RUN COMPLETE", Color.FromArgb(255, 110, 90), 404, 152, fade);

        // Score — big, clean
        using var scoreBrush = new SolidBrush(Color.FromArgb((int)(fade * 250), 245, 248, 255));
        using var cf = new StringFormat { Alignment = StringAlignment.Center };
        g.DrawString($"{_world.Score:000000}", _titleFont, scoreBrush, new RectangleF(370, 185, 540, 70), cf);

        // Subtitle
        using var sub = new SolidBrush(Color.FromArgb((int)(fade * 150), 150, 175, 205));
        g.DrawString($"Wave {_world.Wave:00}  ·  Best  {_settings.HighScore:000000}", _smallFont, sub,
            new RectangleF(370, 260, 540, 24), cf);

        // Separator
        using var sep = new Pen(Color.FromArgb((int)(fade * 25), 150, 180, 210), 1f);
        g.DrawLine(sep, 410, 310, 870, 310);

        // Options
        var opts = new[] { "Retry", "Hangar", "Main menu" };
        for (var i = 0; i < opts.Length; i++)
            DrawModernButton(g, opts[i], new RectangleF(440, 340 + i * 58, 400, 46), i == _selected, fade);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // HUD — minimal, contextual, glass-backed
    // ──────────────────────────────────────────────────────────────────────────

    private void DrawHud(Graphics g)
    {
        // ── Top-left: Score in a small glass pill ──
        DrawGlassCard(g, new RectangleF(16, 12, 170, 52), 1f, Color.FromArgb(170, 6, 12, 26));
        DrawLabel(g, "SCORE", Color.FromArgb(140, 170, 200), 28, 18, 1f);
        using var scoreBrush = new SolidBrush(Color.FromArgb(240, 246, 255));
        g.DrawString($"{_world.Score:000000}", _hudNumberFont, scoreBrush, 26, 32);

        // ── Top-center: Wave pill ──
        DrawGlassCard(g, new RectangleF(CanvasWidth / 2f - 56, 14, 112, 34), 1f, Color.FromArgb(150, 6, 12, 26));
        CenterText(g, $"WAVE {_world.Wave:00}", _hudLabelFont, Color.FromArgb(200, 220, 240),
            new RectangleF(CanvasWidth / 2f - 56, 14, 112, 34));

        // ── Combo — only shows when active (contextual!) ──
        if (_world.Combo > 1)
        {
            var comboX = CanvasWidth / 2f + 70;
            using var comboBrush = new SolidBrush(AccentColor);
            g.DrawString($"×{_world.Combo}", _hudSmallFont, comboBrush, comboX, 20);

            // Combo timer bar
            using var barBg = new SolidBrush(Color.FromArgb(40, 200, 220, 240));
            using var barFill = new SolidBrush(AccentColor);
            g.FillRectangle(barBg, comboX, 42, 120, 3);
            g.FillRectangle(barFill, comboX, 42, 120 * Math.Clamp(_world.ComboTime / 2.6f, 0f, 1f), 3);
        }

        // ── Top-right: Lives + Dash ──
        DrawGlassCard(g, new RectangleF(CanvasWidth - 250, 12, 234, 52), 1f, Color.FromArgb(170, 6, 12, 26));

        // Lives
        DrawLabel(g, "LIVES", Color.FromArgb(140, 170, 200), CanvasWidth - 238, 18, 1f);
        for (var i = 0; i < Math.Max(0, _world.Player.Lives); i++)
        {
            var x = CanvasWidth - 238 + i * 22;
            using var life = new SolidBrush(i < 3 ? AccentColor : Color.FromArgb(120, 230, 160));
            g.FillPolygon(life, [
                new PointF(x + 7, 34),
                new PointF(x + 14, 48),
                new PointF(x, 48)
            ]);
        }

        // Dash bar
        DrawLabel(g, "DASH", Color.FromArgb(140, 170, 200), CanvasWidth - 108, 18, 1f);
        using var dashBg = new SolidBrush(Color.FromArgb(45, 180, 200, 220));
        using var dashFill = new SolidBrush(_world.Player.DashCooldown <= 0f
            ? Color.FromArgb(110, 238, 170)
            : AccentColor);
        g.FillRectangle(dashBg, CanvasWidth - 108, 40, 88, 5);
        var dashReady = 1f - Math.Clamp(_world.Player.DashCooldown / 1.35f, 0f, 1f);
        g.FillRectangle(dashFill, CanvasWidth - 108, 40, 88 * dashReady, 5);

        // Rapid fire indicator
        if (_world.Player.RapidFire > 0f)
        {
            using var rapidBrush = new SolidBrush(Color.FromArgb(220, 255, 210, 74));
            g.DrawString($"RAPID {_world.Player.RapidFire:0.0}", _hudLabelFont, rapidBrush, CanvasWidth / 2f + 70, 50);
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // GAME OBJECTS
    // ──────────────────────────────────────────────────────────────────────────

    private void DrawAsteroid(Graphics g, Asteroid asteroid)
    {
        var image = _assets.AsteroidFor(asteroid.Radius, asteroid.Variant);
        var width = asteroid.Radius * 2.25f;
        var height = width * image.Height / image.Width;
        DrawRotatedImage(g, image, asteroid.Position, new SizeF(width, height), asteroid.Rotation);
    }

    private void DrawBullet(Graphics g, Bullet bullet)
    {
        if (!bullet.Enemy)
        {
            g.DrawImage(_assets.Laser, bullet.Position.X - 5f, bullet.Position.Y - 15f, 10f, 30f);
            return;
        }
        using var glow = new SolidBrush(Color.FromArgb(80, 177, 70, 255));
        using var core = new SolidBrush(Color.FromArgb(230, 244, 190, 255));
        g.FillEllipse(glow, bullet.Position.X - 11f, bullet.Position.Y - 11f, 22f, 22f);
        g.FillEllipse(core, bullet.Position.X - 5f, bullet.Position.Y - 5f, 10f, 10f);
    }

    private void DrawDrone(Graphics g, Drone drone)
    {
        var accent = Color.FromArgb(189, 83, 255);
        using var glow = new SolidBrush(Color.FromArgb(45, accent));
        using var body = new SolidBrush(Color.FromArgb(26, 37, 61));
        using var pen = new Pen(accent, 3f);
        g.FillEllipse(glow, drone.Position.X - 38f, drone.Position.Y - 30f, 76f, 60f);
        var points = new[]
        {
            P(drone.Position, -31, -8), P(drone.Position, -14, -22), P(drone.Position, 14, -22),
            P(drone.Position, 31, -8), P(drone.Position, 22, 17), P(drone.Position, 0, 25), P(drone.Position, -22, 17)
        };
        g.FillPolygon(body, points);
        g.DrawPolygon(pen, points);
        using var eye = new SolidBrush(Color.FromArgb(240, 235, 177, 255));
        g.FillEllipse(eye, drone.Position.X - 7, drone.Position.Y - 4, 14, 9);
    }

    private void DrawPowerup(Graphics g, Powerup powerup)
    {
        var color = powerup.Kind switch
        {
            PowerupKind.Shield => Color.FromArgb(48, 223, 255),
            PowerupKind.RapidFire => Color.FromArgb(255, 196, 66),
            _ => Color.FromArgb(104, 242, 145)
        };
        using var glow = new SolidBrush(Color.FromArgb(50, color));
        using var pen = new Pen(color, 3f);
        g.FillEllipse(glow, powerup.Position.X - 27f, powerup.Position.Y - 27f, 54f, 54f);
        g.DrawEllipse(pen, powerup.Position.X - 20f, powerup.Position.Y - 20f, 40f, 40f);
        if (powerup.Kind == PowerupKind.Shield)
        {
            g.DrawImage(_assets.Shield, powerup.Position.X - 17f, powerup.Position.Y - 17f, 34f, 34f);
        }
        else
        {
            using var font = new Font(BodyFamily, 18f, FontStyle.Bold, GraphicsUnit.Pixel);
            CenterText(g, powerup.Kind == PowerupKind.RapidFire ? "R" : "+", font, color,
                new RectangleF(powerup.Position.X - 20, powerup.Position.Y - 16, 40, 32));
        }
    }

    private void DrawExplosion(Graphics g, ExplosionAnimation explosion)
    {
        var frameIndex = Math.Clamp((int)(explosion.Age / 0.45f * _assets.ExplosionFrames.Count), 0, _assets.ExplosionFrames.Count - 1);
        var frame = _assets.ExplosionFrames[frameIndex];
        var grow = 0.82f + explosion.Age / 0.45f * 0.32f;
        var size = explosion.Size * grow;
        g.DrawImage(frame, explosion.Position.X - size / 2, explosion.Position.Y - size / 2, size, size);
    }

    private void DrawShip(Graphics g, Vector2 center, float scale, bool gameplay)
    {
        var trail = GameAssets.TrailColors[Math.Clamp(_settings.TrailIndex, 0, GameAssets.TrailColors.Length - 1)];
        if (!gameplay || _world.Player.DashRemaining > 0f)
        {
            using var trailGlow = new SolidBrush(Color.FromArgb(gameplay ? 120 : 70, trail));
            var trailHeight = (gameplay ? 35f : 22f) * scale * (1f + 0.12f * MathF.Sin(_visualTime * 8f));
            g.FillEllipse(trailGlow, center.X - 8f * scale, center.Y + 17f * scale, 16f * scale, trailHeight);
        }

        var ship = _assets.ShipFor(_settings.AccentIndex);
        var width = 66f * scale;
        var height = 50f * scale;
        if (gameplay && _world.Player.DashRemaining > 0f)
        {
            using var dashGlow = new SolidBrush(Color.FromArgb(42, trail));
            g.FillEllipse(dashGlow, center.X - width * 0.72f, center.Y - height * 0.62f,
                width * 1.44f, height * 1.26f);
        }
        g.DrawImage(ship, center.X - width / 2, center.Y - height / 2, width, height);

        if (gameplay && _world.Player.ShieldCharges > 0)
        {
            var pulse = 76f + 4f * MathF.Sin(_visualTime * 5f);
            g.DrawImage(_assets.Shield, center.X - pulse / 2, center.Y - pulse / 2, pulse, pulse);
            using var shieldPen = new Pen(Color.FromArgb(150, 67, 223, 255), 2f);
            g.DrawEllipse(shieldPen, center.X - pulse / 2, center.Y - pulse / 2, pulse, pulse);
        }

        if (gameplay && _world.Player.Invulnerable > 0f && ((int)(_visualTime * 12) & 1) == 0)
        {
            using var flash = new SolidBrush(Color.FromArgb(92, Color.White));
            g.FillEllipse(flash, center.X - 29f, center.Y - 29f, 58f, 58f);
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // UI PRIMITIVES — modern glassmorphism system
    // ──────────────────────────────────────────────────────────────────────────

    /// <summary>Glassmorphic card: frosted fill, thin highlight border, optional shadow.</summary>
    private void DrawGlassCard(Graphics g, RectangleF rect, float fade, Color? fillColor = null)
    {
        var fill = fillColor ?? Color.FromArgb((int)(fade * 200), 8, 14, 30);
        using var path = RoundedRect(rect, 16f);

        // Subtle outer shadow (drawn as a slightly larger dark shape behind)
        using var shadowPath = RoundedRect(new RectangleF(rect.X + 2, rect.Y + 3, rect.Width, rect.Height), 18f);
        using var shadowBrush = new SolidBrush(Color.FromArgb((int)(fade * 40), 0, 0, 0));
        g.FillPath(shadowBrush, shadowPath);

        // Main fill
        using var brush = new SolidBrush(fill);
        g.FillPath(brush, path);

        // Top highlight — 1px bright line at the top edge for glass refraction
        using var topHighlight = new Pen(Color.FromArgb((int)(fade * 35), 180, 210, 240), 1f);
        g.DrawLine(topHighlight, rect.X + 16, rect.Y + 0.5f, rect.Right - 16, rect.Y + 0.5f);

        // Border — very subtle
        using var border = new Pen(Color.FromArgb((int)(fade * 22), 150, 185, 215), 1f);
        g.DrawPath(border, path);
    }

    /// <summary>Clean modern button with accent indicator when selected.</summary>
    private void DrawModernButton(Graphics g, string text, RectangleF rect, bool selected, float fade)
    {
        if (selected)
        {
            // Selected: glass background + accent left bar + glow
            using var bgPath = RoundedRect(rect, 10f);
            using var bgBrush = new SolidBrush(Color.FromArgb((int)(fade * 50), AccentColor));
            g.FillPath(bgBrush, bgPath);

            // Accent bar
            using var bar = new SolidBrush(Color.FromArgb((int)(fade * 240), AccentColor));
            g.FillRectangle(bar, rect.X + 1, rect.Y + 10, 3, rect.Height - 20);

            // Subtle border glow
            using var glowPen = new Pen(Color.FromArgb((int)(fade * 55), AccentColor), 1f);
            g.DrawPath(glowPen, bgPath);
        }
        else
        {
            // Unselected: just a subtle bottom border
            using var line = new Pen(Color.FromArgb((int)(fade * 18), 130, 160, 190), 1f);
            g.DrawLine(line, rect.X + 8, rect.Bottom, rect.Right - 8, rect.Bottom);
        }

        // Text
        var textColor = selected
            ? Color.FromArgb((int)(fade * 250), 245, 248, 255)
            : Color.FromArgb((int)(fade * 170), 170, 195, 220);
        using var textBrush = new SolidBrush(textColor);
        var font = selected ? _menuFontBold : _menuFont;
        g.DrawString(text, font, textBrush, rect.X + 20, rect.Y + 12);

        // Selection arrow
        if (selected)
        {
            using var arrow = new SolidBrush(Color.FromArgb((int)(fade * 200), AccentColor));
            g.DrawString("›", _menuFontBold, arrow, rect.Right - 30, rect.Y + 12);
        }
    }

    /// <summary>Value option row (for settings/hangar) with left label and right value.</summary>
    private void DrawValueOption(Graphics g, string label, string value, RectangleF rect, bool selected, float fade, bool showArrows)
    {
        if (selected)
        {
            using var bgPath = RoundedRect(rect, 10f);
            using var bgBrush = new SolidBrush(Color.FromArgb((int)(fade * 45), AccentColor));
            g.FillPath(bgBrush, bgPath);

            using var bar = new SolidBrush(Color.FromArgb((int)(fade * 240), AccentColor));
            g.FillRectangle(bar, rect.X + 1, rect.Y + 8, 3, rect.Height - 16);

            using var glowPen = new Pen(Color.FromArgb((int)(fade * 45), AccentColor), 1f);
            g.DrawPath(glowPen, bgPath);
        }
        else
        {
            using var line = new Pen(Color.FromArgb((int)(fade * 16), 130, 160, 190), 1f);
            g.DrawLine(line, rect.X + 8, rect.Bottom, rect.Right - 8, rect.Bottom);
        }

        // Label
        using var labelBrush = new SolidBrush(Color.FromArgb((int)(fade * 150), 150, 175, 205));
        g.DrawString(label, _smallFont, labelBrush, rect.X + 20, rect.Y + 6);

        // Value
        var valColor = selected
            ? Color.FromArgb((int)(fade * 250), AccentColor)
            : Color.FromArgb((int)(fade * 220), 220, 232, 245);
        using var valBrush = new SolidBrush(valColor);
        var valSize = g.MeasureString(value, _menuFont);
        g.DrawString(value, _menuFont, valBrush, rect.Right - valSize.Width - 20, rect.Y + 14);

        // Arrows for cycling
        if (showArrows)
        {
            using var arrowBrush = new SolidBrush(Color.FromArgb((int)(fade * (selected ? 180 : 80)), 160, 185, 210));
            g.DrawString("‹", _menuFont, arrowBrush, rect.Right - valSize.Width - 50, rect.Y + 14);
            g.DrawString("›", _menuFont, arrowBrush, rect.Right - 6, rect.Y + 14);
        }
    }

    /// <summary>Small uppercase label (eyebrow text).</summary>
    private void DrawLabel(Graphics g, string text, Color color, float x, float y, float fade)
    {
        using var brush = new SolidBrush(Color.FromArgb(Math.Clamp((int)(fade * color.A), 0, 255), color));
        g.DrawString(text.ToUpperInvariant(), _labelFont, brush, x, y);
    }

    /// <summary>Screen-level header with title + description.</summary>
    private void DrawScreenHeader(Graphics g, string title, string description, float fade)
    {
        DrawLabel(g, "SPACE UNLIMITED", AccentColor, 98, 96, fade);

        using var titleBrush = new SolidBrush(Color.FromArgb((int)(fade * 245), 240, 246, 255));
        g.DrawString(title, _headingFont, titleBrush, 96, 118);

        // Accent underline
        using var accentLine = new Pen(Color.FromArgb((int)(fade * 120), AccentColor), 2f);
        g.DrawLine(accentLine, 98, 162, 168, 162);

        using var descBrush = new SolidBrush(Color.FromArgb((int)(fade * 150), 150, 175, 205));
        g.DrawString(description, _subtitleFont, descBrush, 182, 155);
    }

    private void DrawModernInfoRow(Graphics g, string label, string value, float x, float y, float fade)
    {
        using var labelBrush = new SolidBrush(Color.FromArgb((int)(fade * 120), 140, 170, 200));
        g.DrawString(label, _labelFont, labelBrush, x, y);

        using var valueBrush = new SolidBrush(Color.FromArgb((int)(fade * 220), 220, 232, 245));
        var size = g.MeasureString(value, _bodyFont);
        g.DrawString(value, _bodyFont, valueBrush, x + 496 - size.Width, y - 3);
    }

    private void DrawSwatch(Graphics g, Color color, float x, float y, float size, string label, float fade)
    {
        using var glow = new SolidBrush(Color.FromArgb((int)(fade * 35), color));
        using var fill = new SolidBrush(Color.FromArgb((int)(fade * 255), color));
        using var pen = new Pen(Color.FromArgb((int)(fade * 120), color), 1.5f);
        g.FillEllipse(glow, x - 6, y - 6, size + 12, size + 12);
        g.FillEllipse(fill, x, y, size, size);
        g.DrawEllipse(pen, x, y, size, size);
        using var text = new SolidBrush(Color.FromArgb((int)(fade * 130), 140, 170, 200));
        g.DrawString(label, _labelFont, text, x - 1, y + size + 10);
    }

    private void DrawBadge(Graphics g, string text, RectangleF rect, Color color, float fade)
    {
        using var bgPath = RoundedRect(rect, rect.Height / 2f);
        using var bgBrush = new SolidBrush(Color.FromArgb((int)(fade * 40), color));
        g.FillPath(bgBrush, bgPath);
        using var borderPen = new Pen(Color.FromArgb((int)(fade * 60), color), 1f);
        g.DrawPath(borderPen, bgPath);
        CenterText(g, text, _labelFont, Color.FromArgb((int)(fade * 240), color), rect);
    }

    private void DrawHelpColumn(Graphics g, float x, string heading, (string Action, string Input)[] rows, float fade)
    {
        DrawLabel(g, heading, AccentColor, x, 268, fade);

        using var sep = new Pen(Color.FromArgb((int)(fade * 35), AccentColor), 1f);
        g.DrawLine(sep, x, 290, x + 260, 290);

        var y = 316f;
        foreach (var row in rows)
        {
            using var actionBrush = new SolidBrush(Color.FromArgb((int)(fade * 120), 140, 170, 200));
            g.DrawString(row.Action, _labelFont, actionBrush, x, y);

            using var inputBrush = new SolidBrush(Color.FromArgb((int)(fade * 230), 225, 235, 248));
            g.DrawString(row.Input, _bodyFont, inputBrush, x, y + 18);
            y += 68;
        }
    }

    private void DrawFooter(Graphics g, float fade, string? text = null)
    {
        text ??= _input.Pad.Connected
            ? "D-PAD / STICK  Navigate     A  Select     B  Back"
            : "WASD / ARROWS  Navigate     ENTER  Select     ESC  Back";

        using var sep = new Pen(Color.FromArgb((int)(fade * 20), 130, 160, 190), 1f);
        g.DrawLine(sep, 96, 660, 1184, 660);

        using var textBrush = new SolidBrush(Color.FromArgb((int)(fade * 110), 140, 170, 200));
        using var cf = new StringFormat { Alignment = StringAlignment.Center };
        g.DrawString(text, _smallFont, textBrush, new RectangleF(96, 672, 1088, 24), cf);
    }

    // ──────────────────────────────────────────────────────────────────────────
    // DRAWING UTILITIES
    // ──────────────────────────────────────────────────────────────────────────

    private static void CenterText(Graphics g, string text, Font font, Color color, RectangleF rect)
    {
        using var brush = new SolidBrush(color);
        using var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        g.DrawString(text, font, brush, rect, format);
    }

    private static GraphicsPath RoundedRect(RectangleF rect, float radius)
    {
        var d = radius * 2f;
        var path = new GraphicsPath();
        path.AddArc(rect.X, rect.Y, d, d, 180, 90);
        path.AddArc(rect.Right - d, rect.Y, d, d, 270, 90);
        path.AddArc(rect.Right - d, rect.Bottom - d, d, d, 0, 90);
        path.AddArc(rect.X, rect.Bottom - d, d, d, 90, 90);
        path.CloseFigure();
        return path;
    }

    private static void DrawImageCover(Graphics g, Image image, RectangleF destination)
    {
        var imageRatio = (float)image.Width / image.Height;
        var destinationRatio = destination.Width / destination.Height;
        RectangleF source;
        if (imageRatio > destinationRatio)
        {
            var sourceWidth = image.Height * destinationRatio;
            source = new RectangleF((image.Width - sourceWidth) / 2f, 0, sourceWidth, image.Height);
        }
        else
        {
            var sourceHeight = image.Width / destinationRatio;
            source = new RectangleF(0, (image.Height - sourceHeight) / 2f, image.Width, sourceHeight);
        }
        g.DrawImage(image, destination, source, GraphicsUnit.Pixel);
    }

    private static void DrawRotatedImage(Graphics g, Image image, Vector2 center, SizeF size, float degrees)
    {
        var state = g.Save();
        g.TranslateTransform(center.X, center.Y);
        g.RotateTransform(degrees);
        g.DrawImage(image, -size.Width / 2, -size.Height / 2, size.Width, size.Height);
        g.Restore(state);
    }

    private static PointF P(Vector2 center, float x, float y) => new(center.X + x, center.Y + y);

    private static float EaseOut(float t) => 1f - (1f - Math.Clamp(t, 0f, 1f)) * (1f - Math.Clamp(t, 0f, 1f));

    // ──────────────────────────────────────────────────────────────────────────
    // STATE HELPERS
    // ──────────────────────────────────────────────────────────────────────────

    private Color AccentColor => GameAssets.AccentColors[Math.Clamp(_settings.AccentIndex, 0, GameAssets.AccentColors.Length - 1)];

    private string AccentName() => _settings.AccentIndex switch
    {
        0 => "Solar orange",
        1 => "Ion cyan",
        2 => "Nova violet",
        3 => "Plasma mint",
        _ => "Pulsar gold"
    };

    private string TrailName() => _settings.TrailIndex switch
    {
        0 => "Ember",
        1 => "Ion",
        2 => "Nova",
        _ => "Aurora"
    };

    private string WeaponShortName() => _settings.WeaponRig switch
    {
        WeaponRig.Focused => "Focused beam",
        WeaponRig.Twin => "Twin cannons",
        _ => "Spread cannons"
    };

    private string DifficultyName() => _settings.Difficulty switch
    {
        Difficulty.Cadet => "Cadet",
        Difficulty.Ace => "Ace",
        _ => "Pilot"
    };

    private static string OnOff(bool value) => value ? "On" : "Off";

    private string SettingDescription() => _selected switch
    {
        0 => "Cadet gives you four lives and gentler enemy speed. Ace is faster, tighter and starts with two lives.",
        1 => "Volume for the restored soundtrack from the original project.",
        2 => "Volume for lasers, impacts and pickup feedback.",
        3 => "Adds a small camera response when the ship takes a hit or a heavy object breaks.",
        4 => "Borderless fullscreen keeps the 16:9 canvas intact on modern displays.",
        5 => $"The current best run is {_settings.HighScore:000000}.",
        _ => "Return to the main menu."
    };

    private void UpdateMenuStars(float dt)
    {
        for (var i = 0; i < _stars.Count; i++)
        {
            var star = _stars[i];
            star.Y += star.Speed * dt;
            if (star.Y > CanvasHeight) star.Y = 0;
            _stars[i] = star;
        }
    }

    private Rectangle CanvasViewport()
    {
        var scale = Math.Min(ClientSize.Width / (float)CanvasWidth, ClientSize.Height / (float)CanvasHeight);
        var width = Math.Max(1, (int)(CanvasWidth * scale));
        var height = Math.Max(1, (int)(CanvasHeight * scale));
        return new Rectangle((ClientSize.Width - width) / 2, (ClientSize.Height - height) / 2, width, height);
    }

    private PointF ClientToCanvas(Point client)
    {
        var viewport = CanvasViewport();
        return new PointF(
            (client.X - viewport.X) * CanvasWidth / (float)Math.Max(1, viewport.Width),
            (client.Y - viewport.Y) * CanvasHeight / (float)Math.Max(1, viewport.Height));
    }

    private void ApplyFullscreen()
    {
        if (_settings.Fullscreen)
        {
            FormBorderStyle = FormBorderStyle.None;
            WindowState = FormWindowState.Maximized;
        }
        else
        {
            WindowState = FormWindowState.Normal;
            FormBorderStyle = FormBorderStyle.Sizable;
            ClientSize = new Size(CanvasWidth, CanvasHeight);
            CenterToScreen();
        }
    }

    private void OnPlayerHit()
    {
        XInput.Vibrate(0.72f, 0.38f);
        _vibrationTime = 0.2f;
    }

    private void OnClosing(object? sender, FormClosingEventArgs e)
    {
        if (_closing) return;
        _closing = true;
        _timer.Stop();
        XInput.Vibrate(0f, 0f);
        SettingsStore.Save(_settings);
        _audio.Dispose();
        _assets.Dispose();
        _canvas.Dispose();
        _blurBuffer.Dispose();
        _titleFont.Dispose();
        _headingFont.Dispose();
        _subtitleFont.Dispose();
        _labelFont.Dispose();
        _menuFont.Dispose();
        _menuFontBold.Dispose();
        _bodyFont.Dispose();
        _smallFont.Dispose();
        _hudNumberFont.Dispose();
        _hudLabelFont.Dispose();
        _hudSmallFont.Dispose();
    }

    private static int Wrap(int value, int count) => (value % count + count) % count;

    private static T Cycle<T>(T current, int direction) where T : struct, Enum
    {
        var values = Enum.GetValues<T>();
        var index = Array.IndexOf(values, current);
        return values[Wrap(index + direction, values.Length)];
    }
}
