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

    private readonly Font _titleFont = new("Segoe UI", 54f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _subtitleFont = new("Segoe UI", 19f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _menuFont = new("Segoe UI", 21f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _bodyFont = new("Segoe UI", 17f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _smallFont = new("Segoe UI", 14f, FontStyle.Regular, GraphicsUnit.Pixel);
    private readonly Font _hudFont = new("Segoe UI", 18f, FontStyle.Bold, GraphicsUnit.Pixel);
    private readonly Font _numberFont = new("Consolas", 23f, FontStyle.Bold, GraphicsUnit.Pixel);

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
        UpdateMouseSelection(330, 58, count, 135, 405);
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
        const int count = 6;
        UpdateMouseSelection(216, 62, count, 145, 520);
        NavigateVertical(count);
        var delta = HorizontalDelta();
        if (ConfirmPressed() && _selected < 4) delta = 1;

        if (delta != 0)
        {
            switch (_selected)
            {
                case 0: _settings.ShipStyle = Cycle(_settings.ShipStyle, delta); break;
                case 1: _settings.AccentIndex = Wrap(_settings.AccentIndex + delta, GameAssets.AccentColors.Length); break;
                case 2: _settings.TrailIndex = Wrap(_settings.TrailIndex + delta, GameAssets.TrailColors.Length); break;
                case 3: _settings.WeaponRig = Cycle(_settings.WeaponRig, delta); break;
            }
            SettingsStore.Save(_settings);
        }

        if (ConfirmPressed())
        {
            if (_selected == 4) StartGame();
            if (_selected == 5) OpenScreen(GameScreen.MainMenu);
        }
        if (BackPressed()) OpenScreen(GameScreen.MainMenu);
    }

    private void UpdateSettings()
    {
        const int count = 7;
        UpdateMouseSelection(202, 59, count, 145, 520);
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
        UpdateMouseSelection(420, 62, count, 440, 400);
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
                RenderOverlay(g, "PAUSED", ["RESUME", "RESTART RUN", "QUIT TO MENU"], 338);
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
        using var accent = new SolidBrush(AccentColor);
        using var white = new SolidBrush(Color.White);
        using var muted = new SolidBrush(Color.FromArgb(170, 191, 214));
        g.DrawString("SPACE", _titleFont, white, 132f, 92f);
        g.DrawString("UNLIMITED", _titleFont, accent, 128f, 146f);
        g.DrawString("R E C H A R G E D", _subtitleFont, muted, 134f, 219f);
        g.DrawString("A native Windows re-imagining", _smallFont, muted, 136f, 254f);

        DrawMenuButtons(g, ["START RUN", "HANGAR", "SETTINGS", "HOW TO PLAY", "QUIT"], 135, 330, 405, 58);

        DrawGlassPanel(g, new RectangleF(815, 110, 330, 475), 20f, Color.FromArgb(120, 3, 14, 35));
        DrawShip(g, new Vector2(980, 276), 2.25f, false);
        CenterText(g, "YOUR LOADOUT", _subtitleFont, Color.FromArgb(198, 217, 236), new RectangleF(815, 385, 330, 30));
        CenterText(g, ShipName(), _menuFont, AccentColor, new RectangleF(815, 425, 330, 34));
        CenterText(g, $"{_settings.WeaponRig.ToString().ToUpperInvariant()} RIG", _bodyFont, Color.White, new RectangleF(815, 466, 330, 30));
        CenterText(g, $"HIGH SCORE   {_settings.HighScore:000000}", _smallFont, Color.FromArgb(174, 195, 218), new RectangleF(815, 522, 330, 28));
        DrawFooter(g);
    }

    private void RenderHangar(Graphics g)
    {
        RenderMenuBackground(g);
        DrawSectionTitle(g, "HANGAR", "Tune the ship to fit your style. Every option is available from the start.");
        DrawGlassPanel(g, new RectangleF(700, 150, 435, 480), 24f, Color.FromArgb(130, 4, 15, 35));
        DrawShip(g, new Vector2(918, 325), 2.7f, false);
        CenterText(g, ShipName(), _menuFont, AccentColor, new RectangleF(730, 458, 375, 36));
        CenterText(g, WeaponDescription(), _smallFont, Color.FromArgb(188, 211, 232), new RectangleF(752, 505, 330, 70));

        var values = new[]
        {
            ShipName(),
            AccentName(),
            TrailName(),
            _settings.WeaponRig.ToString().ToUpperInvariant(),
            "LAUNCH",
            "BACK"
        };
        var labels = new[] { "HULL", "PAINT", "ENGINE TRAIL", "WEAPON RIG", "", "" };
        DrawValueMenu(g, labels, values, 145, 216, 520, 62);
        DrawFooter(g, "D-PAD / LEFT STICK  Navigate     A  Select     B  Back");
    }

    private void RenderSettings(Graphics g)
    {
        RenderMenuBackground(g);
        DrawSectionTitle(g, "SETTINGS", "Changes save automatically to your Windows profile.");
        var labels = new[] { "DIFFICULTY", "MUSIC", "EFFECTS", "SCREEN SHAKE", "FULLSCREEN", "", "" };
        var values = new[]
        {
            _settings.Difficulty.ToString().ToUpperInvariant(),
            $"{_settings.MusicVolume}%",
            $"{_settings.EffectsVolume}%",
            _settings.ScreenShake ? "ON" : "OFF",
            _settings.Fullscreen ? "ON" : "OFF",
            "RESET HIGH SCORE",
            "BACK"
        };
        DrawValueMenu(g, labels, values, 145, 202, 520, 59);

        DrawGlassPanel(g, new RectangleF(760, 196, 385, 345), 22f, Color.FromArgb(125, 4, 15, 35));
        var description = _selected switch
        {
            0 => "CADET: gentler speed and four lives\nPILOT: the intended balance\nACE: faster enemies and two lives",
            1 => "Sets the volume of the restored original soundtrack.",
            2 => "Sets laser, impact and pickup volume.",
            3 => "Disable camera impact motion for comfort or accessibility.",
            4 => "Borderless fullscreen keeps the game at the correct 16:9 ratio.",
            5 => $"Current high score: {_settings.HighScore:000000}",
            _ => "Return to the command deck."
        };
        DrawWrappedText(g, description, _bodyFont, Color.FromArgb(206, 222, 238), new RectangleF(805, 262, 295, 190));
        DrawFooter(g, "LEFT / RIGHT  Change     A / ENTER  Select     B / ESC  Back");
    }

    private void RenderHelp(Graphics g)
    {
        RenderMenuBackground(g);
        DrawSectionTitle(g, "HOW TO PLAY", "Survive escalating waves, protect your combo, and keep moving.");
        DrawGlassPanel(g, new RectangleF(100, 190, 1080, 395), 24f, Color.FromArgb(130, 4, 15, 35));
        DrawHelpColumn(g, 150, "KEYBOARD", [
            ("MOVE", "WASD / ARROWS"),
            ("FIRE", "SPACE / Z"),
            ("DASH", "SHIFT / X"),
            ("PAUSE", "ESC / P")
        ]);
        DrawHelpColumn(g, 485, "XBOX CONTROLLER", [
            ("MOVE", "LEFT STICK / D-PAD"),
            ("FIRE", "A / RIGHT TRIGGER"),
            ("DASH", "X / RIGHT BUMPER"),
            ("PAUSE", "MENU")
        ]);
        DrawHelpColumn(g, 855, "PICKUPS", [
            ("SHIELD", "ABSORBS A HIT"),
            ("RAPID", "FASTER FIRE"),
            ("REPAIR", "+1 LIFE"),
            ("COMBO", "KILL QUICKLY")
        ]);
        CenterText(g, "Press A, ENTER, B, or ESC to return", _smallFont, Color.FromArgb(185, 205, 226), new RectangleF(0, 625, CanvasWidth, 30));
    }

    private void RenderMenuBackground(Graphics g)
    {
        DrawImageCover(g, _assets.Starfield, new RectangleF(0, 0, CanvasWidth, CanvasHeight));
        using (var shade = new SolidBrush(Color.FromArgb(200, 2, 8, 25))) g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);
        using (var nebula = new LinearGradientBrush(new Rectangle(0, 0, CanvasWidth, CanvasHeight),
                   Color.FromArgb(95, AccentColor), Color.FromArgb(5, 15, 40, 80), 18f))
            g.FillRectangle(nebula, 0, 0, CanvasWidth, CanvasHeight);
        foreach (var star in _stars)
        {
            var alpha = (int)(80 + 120 * (0.5 + 0.5 * Math.Sin(_visualTime * 1.8 + star.X)));
            using var brush = new SolidBrush(Color.FromArgb(alpha, 205, 232, 255));
            g.FillEllipse(brush, star.X, star.Y, star.Size, star.Size);
        }
        using var edge = new Pen(Color.FromArgb(70, AccentColor), 2f);
        g.DrawLine(edge, 95, 70, 1185, 70);
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
        var accent = AccentColor;
        var trail = GameAssets.TrailColors[Math.Clamp(_settings.TrailIndex, 0, GameAssets.TrailColors.Length - 1)];
        if (!gameplay || _world.Player.DashRemaining > 0f)
        {
            using var trailGlow = new SolidBrush(Color.FromArgb(gameplay ? 120 : 70, trail));
            var trailHeight = (gameplay ? 35f : 22f) * scale * (1f + 0.12f * MathF.Sin(_visualTime * 8f));
            g.FillEllipse(trailGlow, center.X - 8f * scale, center.Y + 17f * scale, 16f * scale, trailHeight);
        }

        if (_settings.ShipStyle == ShipStyle.Classic)
        {
            var width = 66f * scale;
            var height = 50f * scale;
            g.DrawImage(_assets.ClassicShip, center.X - width / 2, center.Y - height / 2, width, height);
        }
        else
        {
            var state = g.Save();
            g.TranslateTransform(center.X, center.Y);
            g.ScaleTransform(scale, scale);
            using var shadow = new SolidBrush(Color.FromArgb(50, accent));
            g.FillEllipse(shadow, -40, -32, 80, 66);
            using var hull = new SolidBrush(Color.FromArgb(216, 225, 233));
            using var dark = new SolidBrush(Color.FromArgb(24, 36, 55));
            using var paint = new SolidBrush(accent);
            using var outline = new Pen(Color.FromArgb(225, accent), 2.2f);

            if (_settings.ShipStyle == ShipStyle.Interceptor)
            {
                var body = new[] { new PointF(0, -32), new PointF(15, 15), new PointF(8, 27), new PointF(-8, 27), new PointF(-15, 15) };
                var wings = new[] { new PointF(-10, -2), new PointF(-37, 22), new PointF(-25, 27), new PointF(0, 10), new PointF(25, 27), new PointF(37, 22), new PointF(10, -2) };
                g.FillPolygon(dark, wings);
                g.DrawPolygon(outline, wings);
                g.FillPolygon(hull, body);
                g.FillPolygon(paint, new[] { new PointF(0, -27), new PointF(6, 11), new PointF(0, 17), new PointF(-6, 11) });
            }
            else
            {
                var wings = new[] { new PointF(0, -30), new PointF(31, -2), new PointF(28, 25), new PointF(10, 16), new PointF(0, 27), new PointF(-10, 16), new PointF(-28, 25), new PointF(-31, -2) };
                g.FillPolygon(dark, wings);
                g.DrawPolygon(outline, wings);
                g.FillPolygon(hull, new[] { new PointF(0, -29), new PointF(13, 10), new PointF(0, 24), new PointF(-13, 10) });
                g.FillEllipse(paint, -8, -3, 16, 18);
            }
            g.Restore(state);
        }

        if (gameplay && _world.Player.ShieldCharges > 0)
        {
            var pulse = 76f + 4f * MathF.Sin(_visualTime * 5f);
            g.DrawImage(_assets.Shield, center.X - pulse / 2, center.Y - pulse / 2, pulse, pulse);
            using var shieldPen = new Pen(Color.FromArgb(140, 67, 223, 255), 2f);
            g.DrawEllipse(shieldPen, center.X - pulse / 2, center.Y - pulse / 2, pulse, pulse);
        }

        if (gameplay && _world.Player.Invulnerable > 0f && ((int)(_visualTime * 12) & 1) == 0)
        {
            using var flash = new SolidBrush(Color.FromArgb(100, Color.White));
            g.FillEllipse(flash, center.X - 29f, center.Y - 29f, 58f, 58f);
        }
    }

    private void DrawHud(Graphics g)
    {
        using var panel = new SolidBrush(Color.FromArgb(185, 2, 10, 25));
        using var line = new Pen(Color.FromArgb(105, AccentColor), 2f);
        g.FillRectangle(panel, 0, 0, CanvasWidth, 74);
        g.DrawLine(line, 0, 73, CanvasWidth, 73);
        using var white = new SolidBrush(Color.White);
        using var muted = new SolidBrush(Color.FromArgb(173, 199, 222));
        using var accent = new SolidBrush(AccentColor);
        g.DrawString("SCORE", _smallFont, muted, 32, 13);
        g.DrawString($"{_world.Score:000000}", _numberFont, white, 30, 32);
        g.DrawString($"WAVE  {_world.Wave}", _hudFont, white, 338, 25);
        if (_world.Combo > 1)
        {
            g.DrawString($"COMBO  x{_world.Combo}", _hudFont, accent, 510, 25);
            using var comboBack = new SolidBrush(Color.FromArgb(80, Color.White));
            using var comboFill = new SolidBrush(AccentColor);
            g.FillRectangle(comboBack, 510, 54, 130, 4);
            g.FillRectangle(comboFill, 510, 54, 130 * Math.Clamp(_world.ComboTime / 2.6f, 0f, 1f), 4);
        }
        g.DrawString("LIVES", _smallFont, muted, 875, 13);
        for (var i = 0; i < Math.Max(0, _world.Player.Lives); i++)
        {
            using var brush = new SolidBrush(i < 3 ? AccentColor : Color.FromArgb(118, 237, 158));
            var x = 875 + i * 25;
            g.FillPolygon(brush, [new PointF(x + 9, 35), new PointF(x + 18, 52), new PointF(x, 52)]);
        }
        g.DrawString("DASH", _smallFont, muted, 1085, 13);
        using var dashBack = new SolidBrush(Color.FromArgb(80, Color.White));
        using var dashFill = new SolidBrush(_world.Player.DashCooldown <= 0f ? Color.FromArgb(100, 238, 170) : AccentColor);
        g.FillRectangle(dashBack, 1085, 41, 150, 8);
        var dashReady = 1f - Math.Clamp(_world.Player.DashCooldown / 1.35f, 0f, 1f);
        g.FillRectangle(dashFill, 1085, 41, 150 * dashReady, 8);
        if (_world.Player.RapidFire > 0f)
            g.DrawString($"RAPID {_world.Player.RapidFire:0.0}", _smallFont, Brushes.Gold, 705, 26);
    }

    private void RenderOverlay(Graphics g, string title, string[] options, int firstY)
    {
        using var shade = new SolidBrush(Color.FromArgb(185, 1, 5, 15));
        g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);
        DrawGlassPanel(g, new RectangleF(390, 180, 500, 390), 26f, Color.FromArgb(210, 5, 14, 32));
        CenterText(g, title, _titleFont, AccentColor, new RectangleF(390, 225, 500, 70));
        DrawMenuButtons(g, options, 440, firstY, 400, 62);
    }

    private void RenderGameOver(Graphics g)
    {
        using var shade = new SolidBrush(Color.FromArgb(190, 4, 5, 17));
        g.FillRectangle(shade, 0, 0, CanvasWidth, CanvasHeight);
        DrawGlassPanel(g, new RectangleF(360, 105, 560, 535), 26f, Color.FromArgb(220, 8, 14, 31));
        CenterText(g, "RUN ENDED", _titleFont, Color.FromArgb(255, 101, 88), new RectangleF(360, 145, 560, 70));
        CenterText(g, $"SCORE  {_world.Score:000000}", _numberFont, Color.White, new RectangleF(360, 238, 560, 40));
        CenterText(g, $"WAVE {_world.Wave}    BEST {_settings.HighScore:000000}", _smallFont,
            Color.FromArgb(180, 205, 226), new RectangleF(360, 287, 560, 30));
        DrawMenuButtons(g, ["RETRY", "HANGAR", "MAIN MENU"], 440, 420, 400, 62);
    }

    private void DrawSectionTitle(Graphics g, string title, string subtitle)
    {
        using var white = new SolidBrush(Color.White);
        using var accent = new SolidBrush(AccentColor);
        using var muted = new SolidBrush(Color.FromArgb(179, 203, 224));
        g.DrawString(title, _titleFont, white, 112, 83);
        g.FillRectangle(accent, 114, 151, 82, 5);
        g.DrawString(subtitle, _smallFont, muted, 216, 143);
    }

    private void DrawMenuButtons(Graphics g, string[] labels, int x, int y, int width, int step)
    {
        for (var i = 0; i < labels.Length; i++)
        {
            DrawButton(g, labels[i], new RectangleF(x, y + i * step, width, 48), i == _selected);
        }
    }

    private void DrawValueMenu(Graphics g, string[] labels, string[] values, int x, int y, int width, int step)
    {
        for (var i = 0; i < values.Length; i++)
        {
            var rect = new RectangleF(x, y + i * step, width, 48);
            DrawGlassPanel(g, rect, 10f, i == _selected ? Color.FromArgb(185, 16, 46, 75) : Color.FromArgb(95, 4, 14, 31));
            if (i == _selected)
            {
                using var accent = new SolidBrush(AccentColor);
                g.FillRectangle(accent, rect.X, rect.Y + 7, 5, rect.Height - 14);
            }
            if (!string.IsNullOrEmpty(labels[i]))
            {
                using var muted = new SolidBrush(Color.FromArgb(165, 193, 216));
                g.DrawString(labels[i], _smallFont, muted, rect.X + 22, rect.Y + 16);
                using var value = new SolidBrush(i == _selected ? AccentColor : Color.White);
                var size = g.MeasureString(values[i], _menuFont);
                g.DrawString(values[i], _menuFont, value, rect.Right - size.Width - 35, rect.Y + 10);
                if (i <= 4 && i != 5)
                {
                    g.DrawString("‹", _menuFont, value, rect.X + 200, rect.Y + 9);
                    g.DrawString("›", _menuFont, value, rect.Right - 20, rect.Y + 9);
                }
            }
            else
            {
                CenterText(g, values[i], _menuFont, i == _selected ? AccentColor : Color.White, rect);
            }
        }
    }

    private void DrawButton(Graphics g, string text, RectangleF rect, bool selected)
    {
        DrawGlassPanel(g, rect, 10f, selected ? Color.FromArgb(190, 17, 49, 79) : Color.FromArgb(90, 4, 14, 31));
        if (selected)
        {
            using var accent = new SolidBrush(AccentColor);
            g.FillRectangle(accent, rect.X, rect.Y + 7, 5, rect.Height - 14);
            using var glowPen = new Pen(Color.FromArgb(120, AccentColor), 1.5f);
            g.DrawRectangle(glowPen, rect.X + 0.75f, rect.Y + 0.75f, rect.Width - 1.5f, rect.Height - 1.5f);
        }
        using var brush = new SolidBrush(selected ? Color.White : Color.FromArgb(190, 210, 229));
        g.DrawString(text, _menuFont, brush, rect.X + 23, rect.Y + 10);
    }

    private void DrawHelpColumn(Graphics g, float x, string heading, (string Action, string Input)[] rows)
    {
        using var accent = new SolidBrush(AccentColor);
        using var white = new SolidBrush(Color.White);
        using var muted = new SolidBrush(Color.FromArgb(164, 192, 216));
        g.DrawString(heading, _subtitleFont, accent, x, 230);
        var y = 289f;
        foreach (var row in rows)
        {
            g.DrawString(row.Action, _smallFont, muted, x, y);
            g.DrawString(row.Input, _bodyFont, white, x, y + 24);
            y += 72;
        }
    }

    private void DrawFooter(Graphics g, string? text = null)
    {
        text ??= _input.Pad.Connected
            ? "D-PAD / LEFT STICK  Navigate     A  Select     B  Back"
            : "WASD / ARROWS  Navigate     ENTER  Select     ESC  Back";
        CenterText(g, text, _smallFont, Color.FromArgb(155, 187, 214), new RectangleF(0, 680, CanvasWidth, 28));
    }

    private void DrawGlassPanel(Graphics g, RectangleF rect, float radius, Color fill)
    {
        using var path = RoundedRect(rect, radius);
        using var brush = new SolidBrush(fill);
        using var pen = new Pen(Color.FromArgb(60, 175, 215, 245), 1f);
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

    private string ShipName() => _settings.ShipStyle switch
    {
        ShipStyle.Classic => "ORIGINAL MK I",
        ShipStyle.Interceptor => "VX INTERCEPTOR",
        _ => "AEGIS HEAVY"
    };

    private string AccentName() => _settings.AccentIndex switch
    {
        0 => "SOLAR ORANGE",
        1 => "ION CYAN",
        2 => "NOVA VIOLET",
        3 => "PLASMA MINT",
        _ => "PULSAR GOLD"
    };

    private string TrailName() => _settings.TrailIndex switch
    {
        0 => "EMBER",
        1 => "ION",
        2 => "NOVA",
        _ => "AURORA"
    };

    private string WeaponDescription() => _settings.WeaponRig switch
    {
        WeaponRig.Focused => "Heavy single beam. High damage and precise control.",
        WeaponRig.Twin => "Balanced paired cannons. Reliable in every wave.",
        _ => "Three-way coverage. Slower cycle, excellent crowd control."
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
        _subtitleFont.Dispose();
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
