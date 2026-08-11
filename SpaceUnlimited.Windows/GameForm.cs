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

    private readonly PlayerSettings _settings;
    private readonly GameAssets _assets;
    private readonly AudioManager _audio;
    private readonly GameWorld _world;
    private readonly InputState _input = new();
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 15 };
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly Bitmap _canvas = new(CanvasWidth, CanvasHeight, PixelFormat.Format32bppPArgb);
    private readonly Random _random = new(8791);
    private readonly List<(float X, float Y, float Size, float Speed)> _stars = [];

    // Bahnschrift gives the interface a deliberate cockpit/instrument feel without
    // resorting to the oversized, generic sci-fi type used by the old menus.
    private readonly Font _titleFont = new("Bahnschrift", 56f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _sectionFont = new("Bahnschrift", 35f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _subtitleFont = new("Segoe UI", 17f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _eyebrowFont = new("Segoe UI", 11f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _menuFont = new("Bahnschrift", 19f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _bodyFont = new("Segoe UI", 16f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _smallFont = new("Segoe UI", 13f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _hudFont = new("Bahnschrift", 17f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _numberFont = new("Bahnschrift", 25f, FontStyle.Bold, GraphicsUnit.Pixel);

    private GameScreen _screen = GameScreen.MainMenu;
    private int _selected;
    private long _lastTicks;
    private float _visualTime;
    private float _vibrationTime;
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

        for (var i = 0; i < 120; i++)
        {
            _stars.Add(((float)_random.NextDouble() * CanvasWidth,
                (float)_random.NextDouble() * CanvasHeight,
                0.7f + (float)_random.NextDouble() * 2.1f,
                4f + (float)_random.NextDouble() * 15f));
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

    private void GameTick(object? sender, EventArgs e)
    {
        var ticks = _clock.ElapsedTicks;
        var dt = (float)(ticks - _lastTicks) / Stopwatch.Frequency;
        _lastTicks = ticks;
        dt = Math.Clamp(dt, 0f, 0.05f);
        _visualTime += dt;
        _input.PollController();

        if (_vibrationTime > 0f)
        {
            _vibrationTime -= dt;
            if (_vibrationTime <= 0f) XInput.Vibrate(0f, 0f);
        }

        switch (_screen)
        {
            case GameScreen.MainMenu: UpdateMainMenu(); break;
            case GameScreen.Hangar: UpdateHangar(); break;
            case GameScreen.Settings: UpdateSettings(); break;
            case GameScreen.Help: UpdateHelp(); break;
            case GameScreen.Playing: UpdateGame(dt); break;
            case GameScreen.Paused: UpdatePause(); break;
            case GameScreen.GameOver: UpdateGameOver(); break;
        }

        UpdateMenuStars(dt);
        _input.EndFrame();
        Invalidate();
    }

    private void UpdateMainMenu()
    {
        const int count = 5;
        UpdateMouseSelection(344, 55, count, 112, 430);
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
        UpdateMouseSelection(214, 62, count, 112, 555);
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
        UpdateMouseSelection(206, 57, count, 112, 555);
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
        UpdateMouseSelection(338, 62, count, 440, 400);
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
        UpdateMouseSelection(404, 62, count, 440, 400);
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
        _audio.PlayMenuMusic();
    }

    private void StartGame()
    {
        _world.Start();
        _screen = GameScreen.Playing;
        _selected = 0;
        _audio.PlayGameMusic();
    }

    private void PauseGame()
    {
        _screen = GameScreen.Paused;
        _selected = 0;
    }

    private void ResumeGame()
    {
        _screen = GameScreen.Playing;
        _lastTicks = _clock.ElapsedTicks;
    }

    private void ReturnToMenu()
    {
        OpenScreen(GameScreen.MainMenu);
        _audio.PlayMenuMusic();
    }

    private void OpenScreen(GameScreen screen)
    {
        _screen = screen;
        _selected = 0;
        if (screen is GameScreen.MainMenu or GameScreen.Hangar or GameScreen.Settings or GameScreen.Help)
            _audio.PlayMenuMusic();
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
            var rect = new RectangleF(x, y + i * step, width, 48f);
            if (!rect.Contains(mouse)) continue;
            _selected = i;
            _mouseOverMenuItem = true;
            break;
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        using (var graphics = Graphics.FromImage(_canvas))
        {
            graphics.SmoothingMode = SmoothingMode.AntiAlias;
            graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            Render(graphics);
        }

        var viewport = CanvasViewport();
        e.Graphics.Clear(Color.Black);
        e.Graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
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
                RenderGame(g);
                RenderOverlay(g, "Paused", ["Resume", "Restart run", "Quit to menu"], 338);
                break;
            case GameScreen.GameOver:
                RenderGame(g);
                RenderGameOver(g);
                break;
        }
    }

    private void RenderMainMenu(Graphics g)
    {
        RenderMenuBackground(g);
        DrawTopBar(g, "COMMAND DECK", "CLASSIC EDITION");

        using var white = new SolidBrush(Color.FromArgb(246, 250, 255));
        using var accent = new SolidBrush(AccentColor);
        using var muted = new SolidBrush(Color.FromArgb(166, 193, 216));
        g.DrawString("SPACE", _titleFont, white, 112f, 104f);
        g.DrawString("UNLIMITED", _titleFont, accent, 108f, 157f);
        g.DrawString("recharged", _subtitleFont, muted, 116f, 226f);
        using var rule = new Pen(Color.FromArgb(165, AccentColor), 2f);
        g.DrawLine(rule, 116, 266, 198, 266);
        g.DrawString("A focused arcade shooter built around the original ship.", _smallFont, muted, 116f, 284f);

        DrawMenuButtons(g, ["Start run", "Hangar", "Settings", "How to play", "Quit"], 112, 344, 430, 55);

        DrawGlassPanel(g, new RectangleF(748, 112, 420, 512), 22f, Color.FromArgb(188, 7, 20, 36));
        using var label = new SolidBrush(Color.FromArgb(137, 171, 199));
        using var panelAccent = new SolidBrush(AccentColor);
        g.DrawString("SHIP PROFILE", _eyebrowFont, label, 786, 144);
        g.DrawString("ORIGINAL MK I", _sectionFont, white, 786, 168);
        g.DrawString("THE CLASSIC SILHOUETTE", _eyebrowFont, panelAccent, 789, 219);
        DrawShip(g, new Vector2(958, 310), 3.15f, false);
        using var divider = new Pen(Color.FromArgb(48, 151, 184, 210), 1f);
        g.DrawLine(divider, 786, 402, 1130, 402);
        DrawInfoRow(g, "PAINT", AccentName(), 786, 424);
        DrawInfoRow(g, "WEAPON", WeaponShortName(), 786, 457);
        DrawInfoRow(g, "BEST RUN", $"{_settings.HighScore:000000}", 786, 490);
        using var tagFill = new SolidBrush(Color.FromArgb(42, AccentColor));
        g.FillRectangle(tagFill, 786, 540, 112, 25);
        CenterText(g, "READY", _eyebrowFont, Color.White, new RectangleF(786, 540, 112, 25));
        g.DrawString("Paint is changed in the hangar.", _smallFont, muted, 914, 544);
        DrawFooter(g);
    }

    private void RenderHangar(Graphics g)
    {
        RenderMenuBackground(g);
        DrawTopBar(g, "LOADOUT", "ORIGINAL SHIP");
        DrawSectionTitle(g, "Hangar", "Tune the paint, trail and weapon rig. The original ship stays at the center.");

        DrawValueMenu(g,
            ["Paint", "Engine trail", "Weapon rig", "", ""],
            [AccentName(), TrailName(), WeaponShortName(), "Launch run", "Back to deck"],
            112, 216, 555, 62);

        DrawGlassPanel(g, new RectangleF(728, 150, 440, 476), 22f, Color.FromArgb(188, 7, 20, 36));
        using var muted = new SolidBrush(Color.FromArgb(149, 181, 208));
        using var bright = new SolidBrush(Color.FromArgb(242, 247, 252));
        using var accent = new SolidBrush(AccentColor);
        g.DrawString("PREVIEW", _eyebrowFont, muted, 768, 181);
        g.DrawString("ORIGINAL MK I", _sectionFont, bright, 768, 202);
        g.DrawString("No alternate hulls. Just better paint.", _smallFont, muted, 770, 252);
        DrawShip(g, new Vector2(947, 342), 3.55f, false);
        DrawSwatch(g, AccentColor, 773, 459, 34, "PAINT");
        DrawSwatch(g, GameAssets.TrailColors[Math.Clamp(_settings.TrailIndex, 0, GameAssets.TrailColors.Length - 1)], 914, 459, 34, "TRAIL");
        DrawInfoRow(g, "LOADOUT", WeaponShortName(), 770, 548);
        DrawFooter(g, "ARROWS / STICK  Navigate     ENTER / A  Select     ESC / B  Back");
    }

    private void RenderSettings(Graphics g)
    {
        RenderMenuBackground(g);
        DrawTopBar(g, "SYSTEM", "LOCAL SETTINGS");
        DrawSectionTitle(g, "Settings", "Small adjustments, saved automatically to your Windows profile.");
        DrawValueMenu(g,
            ["Difficulty", "Music volume", "Effects volume", "Screen shake", "Fullscreen", "", ""],
            [DifficultyName(), $"{_settings.MusicVolume}%", $"{_settings.EffectsVolume}%",
             OnOff(_settings.ScreenShake), OnOff(_settings.Fullscreen), "Reset high score", "Back to deck"],
            112, 206, 555, 57);

        DrawGlassPanel(g, new RectangleF(728, 186, 440, 340), 22f, Color.FromArgb(188, 7, 20, 36));
        using var muted = new SolidBrush(Color.FromArgb(149, 181, 208));
        using var bright = new SolidBrush(Color.FromArgb(233, 241, 248));
        g.DrawString("SETTING NOTE", _eyebrowFont, muted, 770, 220);
        DrawWrappedText(g, SettingDescription(), _bodyFont, bright.Color, new RectangleF(770, 260, 340, 160));
        using var tip = new SolidBrush(Color.FromArgb(40, AccentColor));
        g.FillRectangle(tip, 770, 462, 175, 28);
        CenterText(g, "CHANGES SAVE LIVE", _eyebrowFont, Color.White, new RectangleF(770, 462, 175, 28));
        DrawFooter(g, "LEFT / RIGHT  Change     ENTER / A  Select     ESC / B  Back");
    }

    private void RenderHelp(Graphics g)
    {
        RenderMenuBackground(g);
        DrawTopBar(g, "FIELD GUIDE", "QUICK START");
        DrawSectionTitle(g, "How to play", "Move with intent, keep firing, and use your dash before the screen closes in.");
        DrawGlassPanel(g, new RectangleF(112, 190, 1056, 398), 22f, Color.FromArgb(188, 7, 20, 36));
        DrawHelpColumn(g, 156, "KEYBOARD", [
            ("MOVE", "WASD / ARROWS"),
            ("FIRE", "SPACE / Z"),
            ("DASH", "SHIFT / X"),
            ("PAUSE", "ESC / P")
        ]);
        DrawHelpColumn(g, 496, "CONTROLLER", [
            ("MOVE", "LEFT STICK / D-PAD"),
            ("FIRE", "A / RIGHT TRIGGER"),
            ("DASH", "X / RIGHT BUMPER"),
            ("PAUSE", "MENU")
        ]);
        DrawHelpColumn(g, 844, "IN A RUN", [
            ("SHIELD", "ABSORBS A HIT"),
            ("RAPID", "FASTER FIRE"),
            ("REPAIR", "+1 LIFE"),
            ("COMBO", "KILL QUICKLY")
        ]);
        CenterText(g, "Press Enter, A, Esc or B to return", _smallFont, Color.FromArgb(176, 204, 227), new RectangleF(0, 635, CanvasWidth, 28));
    }

    private void RenderMenuBackground(Graphics g)
    {
        DrawImageCover(g, _assets.Starfield, new RectangleF(0, 0, CanvasWidth, CanvasHeight));
        using (var shade = new SolidBrush(Color.FromArgb(208, 3, 10, 24)))
            g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);
        using (var wash = new LinearGradientBrush(new Rectangle(0, 0, CanvasWidth, CanvasHeight),
                   Color.FromArgb(72, AccentColor), Color.FromArgb(8, 7, 16, 38), 28f))
            g.FillRectangle(wash, 0, 0, CanvasWidth, CanvasHeight);
        using (var glow = new SolidBrush(Color.FromArgb(20, AccentColor)))
            g.FillEllipse(glow, 770, 82, 460, 460);

        using var grid = new Pen(Color.FromArgb(15, 164, 208, 236), 1f);
        for (var x = 0; x <= CanvasWidth; x += 64) g.DrawLine(grid, x, 72, x, CanvasHeight);
        for (var y = 104; y <= CanvasHeight; y += 64) g.DrawLine(grid, 0, y, CanvasWidth, y);
        foreach (var star in _stars)
        {
            var alpha = (int)(65 + 115 * (0.5 + 0.5 * Math.Sin(_visualTime * 1.8 + star.X)));
            using var brush = new SolidBrush(Color.FromArgb(alpha, 205, 232, 255));
            g.FillEllipse(brush, star.X, star.Y, star.Size, star.Size);
        }
    }

    private void RenderGame(Graphics g)
    {
        DrawImageCover(g, _assets.Starfield, new RectangleF(0, 0, CanvasWidth, CanvasHeight));
        using (var shade = new SolidBrush(Color.FromArgb(80, 0, 5, 15))) g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);

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
            CenterText(g, $"WAVE {_world.Wave}", _titleFont, Color.FromArgb(alpha, AccentColor), new RectangleF(0, 275, CanvasWidth, 80));
        }
    }

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
            using var font = new Font("Segoe UI", 18f, FontStyle.Bold, GraphicsUnit.Pixel);
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

        // This is the original ship costume from spaceshooter.sb3. Only its warm
        // wing paint is remapped; the silhouette, cockpit and proportions stay true.
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

    private void DrawHud(Graphics g)
    {
        using var panel = new SolidBrush(Color.FromArgb(208, 3, 13, 27));
        using var line = new Pen(Color.FromArgb(110, AccentColor), 1f);
        g.FillRectangle(panel, 0, 0, CanvasWidth, 78);
        g.DrawLine(line, 0, 77, CanvasWidth, 77);

        using var muted = new SolidBrush(Color.FromArgb(145, 178, 204));
        using var white = new SolidBrush(Color.FromArgb(242, 248, 253));
        using var accent = new SolidBrush(AccentColor);
        g.DrawString("SCORE", _eyebrowFont, muted, 30, 12);
        g.DrawString($"{_world.Score:000000}", _numberFont, white, 28, 28);

        DrawPill(g, new RectangleF(360, 18, 116, 38), $"WAVE {_world.Wave:00}", Color.White, false);
        if (_world.Combo > 1)
        {
            g.DrawString($"COMBO x{_world.Combo}", _hudFont, accent, 515, 21);
            using var comboBack = new SolidBrush(Color.FromArgb(55, Color.White));
            using var comboFill = new SolidBrush(AccentColor);
            g.FillRectangle(comboBack, 515, 52, 148, 4);
            g.FillRectangle(comboFill, 515, 52, 148 * Math.Clamp(_world.ComboTime / 2.6f, 0f, 1f), 4);
        }

        g.DrawString("LIVES", _eyebrowFont, muted, 805, 12);
        for (var i = 0; i < Math.Max(0, _world.Player.Lives); i++)
        {
            var x = 805 + i * 27;
            using var life = new SolidBrush(i < 3 ? AccentColor : Color.FromArgb(118, 237, 158));
            g.FillPolygon(life, [new PointF(x + 9, 31), new PointF(x + 18, 49), new PointF(x, 49)]);
        }

        g.DrawString("DASH", _eyebrowFont, muted, 1058, 12);
        using var dashBack = new SolidBrush(Color.FromArgb(58, Color.White));
        using var dashFill = new SolidBrush(_world.Player.DashCooldown <= 0f ? Color.FromArgb(100, 238, 170) : AccentColor);
        g.FillRectangle(dashBack, 1058, 36, 170, 7);
        var dashReady = 1f - Math.Clamp(_world.Player.DashCooldown / 1.35f, 0f, 1f);
        g.FillRectangle(dashFill, 1058, 36, 170 * dashReady, 7);
        if (_world.Player.RapidFire > 0f)
            g.DrawString($"RAPID { _world.Player.RapidFire:0.0}", _eyebrowFont, Brushes.Gold, 680, 22);
    }

    private void RenderOverlay(Graphics g, string title, string[] options, int firstY)
    {
        using var shade = new SolidBrush(Color.FromArgb(192, 1, 6, 17));
        g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);
        DrawGlassPanel(g, new RectangleF(390, 174, 500, 394), 24f, Color.FromArgb(226, 7, 18, 34));
        CenterText(g, title, _sectionFont, AccentColor, new RectangleF(390, 216, 500, 58));
        using var rule = new Pen(Color.FromArgb(90, AccentColor), 1f);
        g.DrawLine(rule, 452, 292, 828, 292);
        DrawMenuButtons(g, options, 440, firstY, 400, 62);
    }

    private void RenderGameOver(Graphics g)
    {
        using var shade = new SolidBrush(Color.FromArgb(195, 4, 5, 17));
        g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);
        DrawGlassPanel(g, new RectangleF(360, 104, 560, 536), 25f, Color.FromArgb(226, 8, 15, 30));
        CenterText(g, "RUN ENDED", _sectionFont, Color.FromArgb(255, 101, 88), new RectangleF(360, 145, 560, 58));
        CenterText(g, $"{_world.Score:000000}", _numberFont, Color.White, new RectangleF(360, 228, 560, 42));
        CenterText(g, $"WAVE {_world.Wave:00}   •   BEST {_settings.HighScore:000000}", _smallFont,
            Color.FromArgb(180, 205, 226), new RectangleF(360, 278, 560, 28));
        using var rule = new Pen(Color.FromArgb(60, 180, 100, 93), 1f);
        g.DrawLine(rule, 430, 330, 850, 330);
        DrawMenuButtons(g, ["Retry", "Open hangar", "Main menu"], 440, 404, 400, 62);
    }

    private void DrawSectionTitle(Graphics g, string title, string subtitle)
    {
        using var white = new SolidBrush(Color.FromArgb(245, 250, 255));
        using var accent = new SolidBrush(AccentColor);
        using var muted = new SolidBrush(Color.FromArgb(165, 194, 217));
        g.DrawString(title, _sectionFont, white, 112, 92);
        g.FillRectangle(accent, 114, 143, 58, 3);
        g.DrawString(subtitle, _subtitleFont, muted, 190, 137);
    }

    private void DrawTopBar(Graphics g, string context, string mode)
    {
        using var muted = new SolidBrush(Color.FromArgb(133, 168, 197));
        using var accent = new SolidBrush(AccentColor);
        using var line = new Pen(Color.FromArgb(42, 145, 184, 214), 1f);
        g.DrawString("SPACE UNLIMITED", _eyebrowFont, accent, 112, 30);
        g.DrawString("/  RECHARGED", _eyebrowFont, muted, 235, 30);
        g.DrawString(context, _eyebrowFont, muted, 530, 30);
        g.DrawString(mode, _eyebrowFont, accent, 1040, 30);
        g.DrawLine(line, 112, 58, 1168, 58);
    }

    private void DrawInfoRow(Graphics g, string label, string value, float x, float y)
    {
        using var muted = new SolidBrush(Color.FromArgb(135, 169, 197));
        using var valueBrush = new SolidBrush(Color.FromArgb(237, 245, 251));
        g.DrawString(label, _eyebrowFont, muted, x, y);
        var size = g.MeasureString(value, _smallFont);
        g.DrawString(value, _smallFont, valueBrush, x + 344 - size.Width, y - 2);
    }

    private void DrawSwatch(Graphics g, Color color, float x, float y, float size, string label)
    {
        using var glow = new SolidBrush(Color.FromArgb(38, color));
        using var fill = new SolidBrush(color);
        using var pen = new Pen(Color.FromArgb(175, color), 1f);
        g.FillEllipse(glow, x - 7, y - 7, size + 14, size + 14);
        g.FillEllipse(fill, x, y, size, size);
        g.DrawEllipse(pen, x, y, size, size);
        using var text = new SolidBrush(Color.FromArgb(145, 178, 204));
        g.DrawString(label, _eyebrowFont, text, x - 1, y + size + 10);
    }

    private void DrawPill(Graphics g, RectangleF rect, string text, Color color, bool filled)
    {
        DrawGlassPanel(g, rect, rect.Height / 2f, filled
            ? Color.FromArgb(54, color)
            : Color.FromArgb(36, 164, 201, 227));
        CenterText(g, text, _eyebrowFont, color, rect);
    }

    private void DrawMenuButtons(Graphics g, string[] labels, int x, int y, int width, int step)
    {
        for (var i = 0; i < labels.Length; i++)
        {
            DrawButton(g, labels[i], new RectangleF(x, y + i * step, width, 48), i == _selected, i + 1);
        }
    }

    private void DrawValueMenu(Graphics g, string[] labels, string[] values, int x, int y, int width, int step)
    {
        for (var i = 0; i < values.Length; i++)
        {
            var rect = new RectangleF(x, y + i * step, width, 48);
            var selected = i == _selected;
            DrawGlassPanel(g, rect, 11f, selected
                ? Color.FromArgb(194, 14, 41, 64)
                : Color.FromArgb(76, 5, 16, 30));
            if (selected)
            {
                using var accent = new SolidBrush(AccentColor);
                g.FillRectangle(accent, rect.X, rect.Y + 8, 4, rect.Height - 16);
            }

            if (!string.IsNullOrEmpty(labels[i]))
            {
                using var muted = new SolidBrush(Color.FromArgb(153, 184, 210));
                using var value = new SolidBrush(selected ? AccentColor : Color.FromArgb(238, 245, 250));
                g.DrawString(labels[i], _smallFont, muted, rect.X + 22, rect.Y + 16);
                var size = g.MeasureString(values[i], _menuFont);
                g.DrawString(values[i], _menuFont, value, rect.Right - size.Width - 37, rect.Y + 10);
                if (i < values.Length - 2)
                {
                    g.DrawString("‹", _menuFont, value, rect.X + 220, rect.Y + 9);
                    g.DrawString("›", _menuFont, value, rect.Right - 23, rect.Y + 9);
                }
            }
            else
            {
                CenterText(g, values[i], _menuFont, selected ? AccentColor : Color.FromArgb(238, 245, 250), rect);
            }
        }
    }

    private void DrawButton(Graphics g, string text, RectangleF rect, bool selected, int number)
    {
        if (selected)
        {
            DrawGlassPanel(g, rect, 11f, Color.FromArgb(194, 14, 41, 64));
            using var accent = new SolidBrush(AccentColor);
            g.FillRectangle(accent, rect.X, rect.Y + 8, 4, rect.Height - 16);
            using var glowPen = new Pen(Color.FromArgb(110, AccentColor), 1f);
            g.DrawRectangle(glowPen, rect.X + 0.5f, rect.Y + 0.5f, rect.Width - 1f, rect.Height - 1f);
        }
        else
        {
            using var rule = new Pen(Color.FromArgb(30, 144, 179, 205), 1f);
            g.DrawLine(rule, rect.X, rect.Bottom, rect.Right, rect.Bottom);
        }

        using var index = new SolidBrush(selected ? AccentColor : Color.FromArgb(106, 143, 174));
        using var brush = new SolidBrush(selected ? Color.White : Color.FromArgb(190, 214, 233));
        g.DrawString($"{number:00}", _eyebrowFont, index, rect.X + 20, rect.Y + 17);
        g.DrawString(text, _menuFont, brush, rect.X + 62, rect.Y + 10);
    }

    private void DrawHelpColumn(Graphics g, float x, string heading, (string Action, string Input)[] rows)
    {
        using var accent = new SolidBrush(AccentColor);
        using var white = new SolidBrush(Color.FromArgb(238, 245, 250));
        using var muted = new SolidBrush(Color.FromArgb(143, 177, 204));
        g.DrawString(heading, _eyebrowFont, accent, x, 229);
        using var rule = new Pen(Color.FromArgb(45, AccentColor), 1f);
        g.DrawLine(rule, x, 251, x + 230, 251);
        var y = 280f;
        foreach (var row in rows)
        {
            g.DrawString(row.Action, _eyebrowFont, muted, x, y);
            g.DrawString(row.Input, _bodyFont, white, x, y + 20);
            y += 72;
        }
    }

    private void DrawFooter(Graphics g, string? text = null)
    {
        text ??= _input.Pad.Connected
            ? "D-PAD / LEFT STICK  Navigate     A  Select     B  Back"
            : "WASD / ARROWS  Navigate     ENTER  Select     ESC  Back";
        using var rule = new Pen(Color.FromArgb(30, 144, 179, 205), 1f);
        g.DrawLine(rule, 112, 665, 1168, 665);
        CenterText(g, text, _smallFont, Color.FromArgb(149, 183, 210), new RectangleF(112, 675, 1056, 28));
    }

    private void DrawGlassPanel(Graphics g, RectangleF rect, float radius, Color fill)
    {
        using var path = RoundedRect(rect, radius);
        using var brush = new SolidBrush(fill);
        using var pen = new Pen(Color.FromArgb(54, 157, 193, 221), 1f);
        g.FillPath(brush, path);
        g.DrawPath(pen, path);
    }

    private void DrawWrappedText(Graphics g, string text, Font font, Color color, RectangleF rect)
    {
        using var brush = new SolidBrush(color);
        using var format = new StringFormat { Trimming = StringTrimming.Word, FormatFlags = StringFormatFlags.LineLimit };
        g.DrawString(text, font, brush, rect, format);
    }

    private static void CenterText(Graphics g, string text, Font font, Color color, RectangleF rect)
    {
        using var brush = new SolidBrush(color);
        using var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        g.DrawString(text, font, brush, rect, format);
    }

    private static GraphicsPath RoundedRect(RectangleF rect, float radius)
    {
        var diameter = radius * 2f;
        var path = new GraphicsPath();
        path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
        path.AddArc(rect.Right - diameter, rect.Y, diameter, diameter, 270, 90);
        path.AddArc(rect.Right - diameter, rect.Bottom - diameter, diameter, diameter, 0, 90);
        path.AddArc(rect.X, rect.Bottom - diameter, diameter, diameter, 90, 90);
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
        _ => "Return to the command deck."
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
        _titleFont.Dispose();
        _sectionFont.Dispose();
        _subtitleFont.Dispose();
        _eyebrowFont.Dispose();
        _menuFont.Dispose();
        _bodyFont.Dispose();
        _smallFont.Dispose();
        _hudFont.Dispose();
        _numberFont.Dispose();
    }

    private static int Wrap(int value, int count) => (value % count + count) % count;

    private static T Cycle<T>(T current, int direction) where T : struct, Enum
    {
        var values = Enum.GetValues<T>();
        var index = Array.IndexOf(values, current);
        return values[Wrap(index + direction, values.Length)];
    }
}
