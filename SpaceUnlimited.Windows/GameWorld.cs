using System.Numerics;

namespace SpaceUnlimited;

internal readonly record struct GameplayInput(Vector2 Move, bool Fire, bool Dash);

internal sealed class PlayerShip
{
    public Vector2 Position;
    public Vector2 DashDirection;
    public float Radius = 23f;
    public int Lives;
    public int ShieldCharges;
    public float FireCooldown;
    public float DashCooldown;
    public float DashRemaining;
    public float Invulnerable;
    public float RapidFire;
}

internal sealed class Asteroid
{
    public Vector2 Position;
    public Vector2 Velocity;
    public float Radius;
    public float Rotation;
    public float RotationSpeed;
    public int HitPoints;
    public int Variant;
}

internal sealed class Bullet
{
    public Vector2 Position;
    public Vector2 Velocity;
    public float Radius;
    public float Life;
    public int Damage;
    public bool Enemy;
}

internal sealed class Drone
{
    public Vector2 Position;
    public Vector2 Velocity;
    public float Radius = 24f;
    public float ShootTimer;
    public float Phase;
    public int HitPoints;
}

internal enum PowerupKind
{
    Shield,
    RapidFire,
    Repair
}

internal sealed class Powerup
{
    public Vector2 Position;
    public Vector2 Velocity;
    public PowerupKind Kind;
    public float Rotation;
}

internal sealed class Particle
{
    public Vector2 Position;
    public Vector2 Velocity;
    public Color Color;
    public float Life;
    public float MaxLife;
    public float Size;
}

internal sealed class ExplosionAnimation
{
    public Vector2 Position;
    public float Size;
    public float Age;
}

internal sealed class GameWorld
{
    public const float Width = 1280f;
    public const float Height = 720f;
    public const float HudFloor = 82f;

    private readonly Random _random = new();
    private readonly PlayerSettings _settings;
    private float _intermission;
    private float _elapsed;

    public PlayerShip Player { get; } = new();
    public List<Asteroid> Asteroids { get; } = [];
    public List<Bullet> Bullets { get; } = [];
    public List<Drone> Drones { get; } = [];
    public List<Powerup> Powerups { get; } = [];
    public List<Particle> Particles { get; } = [];
    public List<ExplosionAnimation> Explosions { get; } = [];

    public int Score { get; private set; }
    public int Wave { get; private set; }
    public int Combo { get; private set; } = 1;
    public float ComboTime { get; private set; }
    public bool IsGameOver { get; private set; }
    public float WaveBannerTime { get; private set; }
    public float ShakeStrength { get; private set; }

    public event Action? LaserFired;
    public event Action? Explosion;
    public event Action? Pickup;
    public event Action? PlayerHit;

    private float DifficultySpeed => _settings.Difficulty switch
    {
        Difficulty.Cadet => 0.86f,
        Difficulty.Ace => 1.2f,
        _ => 1f
    };

    public GameWorld(PlayerSettings settings)
    {
        _settings = settings;
    }

    public void Start()
    {
        Asteroids.Clear();
        Bullets.Clear();
        Drones.Clear();
        Powerups.Clear();
        Particles.Clear();
        Explosions.Clear();
        Score = 0;
        Wave = 0;
        Combo = 1;
        ComboTime = 0f;
        IsGameOver = false;
        WaveBannerTime = 0f;
        ShakeStrength = 0f;
        _elapsed = 0f;
        _intermission = 0.8f;

        Player.Position = new Vector2(Width / 2f, Height - 92f);
        Player.Lives = _settings.Difficulty switch
        {
            Difficulty.Cadet => 4,
            Difficulty.Ace => 2,
            _ => 3
        };
        Player.ShieldCharges = _settings.Difficulty == Difficulty.Cadet ? 1 : 0;
        Player.FireCooldown = 0f;
        Player.DashCooldown = 0f;
        Player.DashRemaining = 0f;
        Player.Invulnerable = 1.5f;
        Player.RapidFire = 0f;
    }

    public void Update(float deltaTime, GameplayInput input)
    {
        var dt = Math.Clamp(deltaTime, 0f, 0.033f);
        _elapsed += dt;
        WaveBannerTime = Math.Max(0f, WaveBannerTime - dt);
        ShakeStrength = Math.Max(0f, ShakeStrength - 25f * dt);
        UpdateParticles(dt);

        if (IsGameOver)
        {
            return;
        }

        ComboTime = Math.Max(0f, ComboTime - dt);
        if (ComboTime <= 0f) Combo = 1;

        Player.FireCooldown = Math.Max(0f, Player.FireCooldown - dt);
        Player.DashCooldown = Math.Max(0f, Player.DashCooldown - dt);
        Player.DashRemaining = Math.Max(0f, Player.DashRemaining - dt);
        Player.Invulnerable = Math.Max(0f, Player.Invulnerable - dt);
        Player.RapidFire = Math.Max(0f, Player.RapidFire - dt);

        var move = input.Move;
        if (move.LengthSquared() > 1f) move = Vector2.Normalize(move);

        if (input.Dash && Player.DashCooldown <= 0f)
        {
            Player.DashDirection = move.LengthSquared() > 0.01f ? Vector2.Normalize(move) : new Vector2(0f, -1f);
            Player.DashRemaining = 0.22f;
            Player.DashCooldown = 1.35f;
            Player.Invulnerable = Math.Max(Player.Invulnerable, 0.28f);
            EmitTrailBurst();
        }

        var speed = Player.DashRemaining > 0f ? 980f : 360f;
        var direction = Player.DashRemaining > 0f ? Player.DashDirection : move;
        Player.Position += direction * speed * dt;
        Player.Position = new Vector2(
            Math.Clamp(Player.Position.X, Player.Radius + 8f, Width - Player.Radius - 8f),
            Math.Clamp(Player.Position.Y, HudFloor + Player.Radius, Height - Player.Radius - 8f));

        if (move.LengthSquared() > 0.05f || Player.DashRemaining > 0f)
        {
            EmitEngineParticle();
        }

        if (input.Fire && Player.FireCooldown <= 0f)
        {
            FirePlayerWeapon();
        }

        UpdateBullets(dt);
        UpdateAsteroids(dt);
        UpdateDrones(dt);
        UpdatePowerups(dt);
        ResolveCollisions();

        if (Asteroids.Count == 0 && Drones.Count == 0)
        {
            _intermission -= dt;
            if (_intermission <= 0f)
            {
                BeginWave();
            }
        }
        else
        {
            _intermission = 1.25f;
        }
    }

    private void BeginWave()
    {
        Wave++;
        WaveBannerTime = 2f;
        var difficultyExtra = _settings.Difficulty == Difficulty.Ace ? 2 : 0;
        var count = Math.Min(14, 3 + Wave + difficultyExtra);
        for (var i = 0; i < count; i++)
        {
            var largeChance = Math.Min(0.62, 0.24 + Wave * 0.035);
            var radius = _random.NextDouble() < largeChance ? 42f : 26f;
            SpawnAsteroid(radius, new Vector2(
                NextFloat(45f, Width - 45f),
                -NextFloat(60f + i * 32f, 360f + i * 55f)));
        }

        if (Wave >= 3)
        {
            var droneCount = Math.Min(4, Wave / 3);
            for (var i = 0; i < droneCount; i++)
            {
                Drones.Add(new Drone
                {
                    Position = new Vector2(180f + i * (Width - 360f) / Math.Max(1, droneCount - 1), -80f - i * 95f),
                    Velocity = new Vector2(0f, 95f * DifficultySpeed),
                    ShootTimer = NextFloat(1.1f, 2.2f),
                    Phase = NextFloat(0f, MathF.Tau),
                    HitPoints = _settings.Difficulty == Difficulty.Cadet ? 2 : 3
                });
            }
        }
    }

    private void SpawnAsteroid(float radius, Vector2 position, Vector2? velocity = null)
    {
        var baseSpeed = radius >= 36f ? NextFloat(72f, 118f) : NextFloat(105f, 165f);
        Asteroids.Add(new Asteroid
        {
            Position = position,
            Velocity = velocity ?? new Vector2(NextFloat(-52f, 52f), baseSpeed * DifficultySpeed),
            Radius = radius,
            Rotation = NextFloat(0f, 360f),
            RotationSpeed = NextFloat(-75f, 75f),
            HitPoints = radius >= 36f ? 2 : 1,
            Variant = _random.Next()
        });
    }

    private void FirePlayerWeapon()
    {
        var rapidMultiplier = Player.RapidFire > 0f ? 0.55f : 1f;
        switch (_settings.WeaponRig)
        {
            case WeaponRig.Focused:
                AddPlayerBullet(Player.Position + new Vector2(0f, -31f), new Vector2(0f, -760f), 2);
                Player.FireCooldown = 0.19f * rapidMultiplier;
                break;
            case WeaponRig.Twin:
                AddPlayerBullet(Player.Position + new Vector2(-11f, -25f), new Vector2(0f, -730f), 1);
                AddPlayerBullet(Player.Position + new Vector2(11f, -25f), new Vector2(0f, -730f), 1);
                Player.FireCooldown = 0.22f * rapidMultiplier;
                break;
            default:
                AddPlayerBullet(Player.Position + new Vector2(0f, -27f), new Vector2(0f, -700f), 1);
                AddPlayerBullet(Player.Position + new Vector2(-9f, -22f), new Vector2(-155f, -680f), 1);
                AddPlayerBullet(Player.Position + new Vector2(9f, -22f), new Vector2(155f, -680f), 1);
                Player.FireCooldown = 0.3f * rapidMultiplier;
                break;
        }
        LaserFired?.Invoke();
    }

    private void AddPlayerBullet(Vector2 position, Vector2 velocity, int damage)
    {
        Bullets.Add(new Bullet
        {
            Position = position,
            Velocity = velocity,
            Radius = 5f,
            Damage = damage,
            Enemy = false,
            Life = 1.4f
        });
    }

    private void UpdateBullets(float dt)
    {
        for (var i = Bullets.Count - 1; i >= 0; i--)
        {
            var bullet = Bullets[i];
            bullet.Position += bullet.Velocity * dt;
            bullet.Life -= dt;
            if (bullet.Life <= 0f || bullet.Position.Y < -40f || bullet.Position.Y > Height + 40f ||
                bullet.Position.X < -40f || bullet.Position.X > Width + 40f)
            {
                Bullets.RemoveAt(i);
            }
        }
    }

    private void UpdateAsteroids(float dt)
    {
        foreach (var asteroid in Asteroids)
        {
            asteroid.Position += asteroid.Velocity * dt;
            asteroid.Rotation += asteroid.RotationSpeed * dt;
            if (asteroid.Position.X < -asteroid.Radius) asteroid.Position.X = Width + asteroid.Radius;
            if (asteroid.Position.X > Width + asteroid.Radius) asteroid.Position.X = -asteroid.Radius;
            if (asteroid.Position.Y > Height + asteroid.Radius)
            {
                asteroid.Position = new Vector2(NextFloat(asteroid.Radius, Width - asteroid.Radius), -asteroid.Radius);
            }
        }
    }

    private void UpdateDrones(float dt)
    {
        foreach (var drone in Drones)
        {
            if (drone.Position.Y < 130f)
            {
                drone.Position += drone.Velocity * dt;
            }
            else
            {
                drone.Position.X += MathF.Sin(_elapsed * 1.4f + drone.Phase) * 105f * dt;
                drone.Position.X = Math.Clamp(drone.Position.X, 55f, Width - 55f);
            }

            drone.ShootTimer -= dt;
            if (drone.Position.Y > 60f && drone.ShootTimer <= 0f)
            {
                var aim = Player.Position - drone.Position;
                if (aim.LengthSquared() > 0.01f) aim = Vector2.Normalize(aim);
                Bullets.Add(new Bullet
                {
                    Position = drone.Position + aim * 28f,
                    Velocity = aim * 285f * DifficultySpeed,
                    Radius = 7f,
                    Damage = 1,
                    Enemy = true,
                    Life = 4.5f
                });
                drone.ShootTimer = NextFloat(1.2f, 2f) / DifficultySpeed;
            }
        }
    }

    private void UpdatePowerups(float dt)
    {
        for (var i = Powerups.Count - 1; i >= 0; i--)
        {
            var powerup = Powerups[i];
            powerup.Position += powerup.Velocity * dt;
            powerup.Rotation += 90f * dt;
            if (powerup.Position.Y > Height + 40f) Powerups.RemoveAt(i);
        }
    }

    private void ResolveCollisions()
    {
        for (var bulletIndex = Bullets.Count - 1; bulletIndex >= 0; bulletIndex--)
        {
            if (bulletIndex >= Bullets.Count) continue;
            var bullet = Bullets[bulletIndex];
            if (bullet.Enemy)
            {
                if (Player.Invulnerable <= 0f && Overlap(bullet.Position, bullet.Radius, Player.Position, Player.Radius))
                {
                    Bullets.RemoveAt(bulletIndex);
                    DamagePlayer();
                }
                continue;
            }

            var consumed = false;
            for (var asteroidIndex = Asteroids.Count - 1; asteroidIndex >= 0; asteroidIndex--)
            {
                var asteroid = Asteroids[asteroidIndex];
                if (!Overlap(bullet.Position, bullet.Radius, asteroid.Position, asteroid.Radius * 0.82f)) continue;
                asteroid.HitPoints -= bullet.Damage;
                Bullets.RemoveAt(bulletIndex);
                consumed = true;
                if (asteroid.HitPoints <= 0) DestroyAsteroid(asteroidIndex, true);
                break;
            }

            if (consumed) continue;
            for (var droneIndex = Drones.Count - 1; droneIndex >= 0; droneIndex--)
            {
                var drone = Drones[droneIndex];
                if (!Overlap(bullet.Position, bullet.Radius, drone.Position, drone.Radius)) continue;
                drone.HitPoints -= bullet.Damage;
                Bullets.RemoveAt(bulletIndex);
                if (drone.HitPoints <= 0) DestroyDrone(droneIndex);
                break;
            }
        }

        if (Player.Invulnerable <= 0f)
        {
            for (var i = Asteroids.Count - 1; i >= 0; i--)
            {
                if (!Overlap(Player.Position, Player.Radius * 0.78f, Asteroids[i].Position, Asteroids[i].Radius * 0.78f)) continue;
                DestroyAsteroid(i, false);
                DamagePlayer();
                break;
            }

            for (var i = Drones.Count - 1; i >= 0; i--)
            {
                if (!Overlap(Player.Position, Player.Radius, Drones[i].Position, Drones[i].Radius)) continue;
                DestroyDrone(i, false);
                DamagePlayer();
                break;
            }
        }

        for (var i = Powerups.Count - 1; i >= 0; i--)
        {
            if (!Overlap(Player.Position, Player.Radius, Powerups[i].Position, 18f)) continue;
            ApplyPowerup(Powerups[i].Kind);
            Powerups.RemoveAt(i);
        }
    }

    private void DestroyAsteroid(int index, bool awardScore)
    {
        var asteroid = Asteroids[index];
        Asteroids.RemoveAt(index);
        CreateExplosion(asteroid.Position, asteroid.Radius * 2.4f, Color.FromArgb(255, 168, 78));

        if (awardScore)
        {
            AwardKill(asteroid.Radius >= 36f ? 60 : asteroid.Radius >= 22f ? 35 : 20);
            if (asteroid.Radius >= 22f)
            {
                var childRadius = asteroid.Radius >= 36f ? 25f : 14f;
                for (var i = 0; i < 2; i++)
                {
                    var direction = Vector2.Normalize(new Vector2(i == 0 ? -1f : 1f, NextFloat(0.45f, 1f)));
                    SpawnAsteroid(childRadius, asteroid.Position + direction * 12f,
                        direction * NextFloat(135f, 205f) * DifficultySpeed);
                }
            }
            TrySpawnPowerup(asteroid.Position);
        }
    }

    private void DestroyDrone(int index, bool awardScore = true)
    {
        var drone = Drones[index];
        Drones.RemoveAt(index);
        CreateExplosion(drone.Position, 70f, Color.FromArgb(83, 218, 255));
        if (awardScore)
        {
            AwardKill(110);
            TrySpawnPowerup(drone.Position, 0.28);
        }
    }

    private void AwardKill(int points)
    {
        Score += points * Combo;
        Combo = Math.Min(8, Combo + 1);
        ComboTime = 2.6f;
    }

    private void DamagePlayer()
    {
        if (Player.ShieldCharges > 0)
        {
            Player.ShieldCharges--;
            Player.Invulnerable = 1f;
            CreateExplosion(Player.Position, 75f, Color.FromArgb(61, 224, 255));
        }
        else
        {
            Player.Lives--;
            Player.Invulnerable = 1.8f;
            Player.Position = new Vector2(Width / 2f, Height - 92f);
            CreateExplosion(Player.Position, 105f, Color.FromArgb(255, 98, 55));
            if (Player.Lives <= 0)
            {
                IsGameOver = true;
            }
        }

        Combo = 1;
        ComboTime = 0f;
        ShakeStrength = 12f;
        PlayerHit?.Invoke();
    }

    private void TrySpawnPowerup(Vector2 position, double chance = 0.15)
    {
        if (_random.NextDouble() > chance) return;
        var roll = _random.NextDouble();
        var kind = roll < 0.43 ? PowerupKind.Shield : roll < 0.82 ? PowerupKind.RapidFire : PowerupKind.Repair;
        Powerups.Add(new Powerup
        {
            Position = position,
            Velocity = new Vector2(NextFloat(-18f, 18f), 78f),
            Kind = kind,
            Rotation = NextFloat(0f, 360f)
        });
    }

    private void ApplyPowerup(PowerupKind kind)
    {
        switch (kind)
        {
            case PowerupKind.Shield:
                Player.ShieldCharges = Math.Min(3, Player.ShieldCharges + 1);
                break;
            case PowerupKind.RapidFire:
                Player.RapidFire = Math.Max(Player.RapidFire, 9f);
                break;
            case PowerupKind.Repair:
                Player.Lives = Math.Min(5, Player.Lives + 1);
                break;
        }
        Score += 75;
        Pickup?.Invoke();
    }

    private void CreateExplosion(Vector2 position, float size, Color color)
    {
        Explosions.Add(new ExplosionAnimation { Position = position, Size = size });
        for (var i = 0; i < Math.Clamp((int)(size / 3f), 10, 32); i++)
        {
            var angle = NextFloat(0f, MathF.Tau);
            var speed = NextFloat(55f, 280f);
            var life = NextFloat(0.25f, 0.75f);
            Particles.Add(new Particle
            {
                Position = position,
                Velocity = new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * speed,
                Color = color,
                Life = life,
                MaxLife = life,
                Size = NextFloat(2f, 7f)
            });
        }
        Explosion?.Invoke();
    }

    private void EmitEngineParticle()
    {
        if (_random.NextDouble() > 0.55) return;
        var life = NextFloat(0.18f, 0.38f);
        Particles.Add(new Particle
        {
            Position = Player.Position + new Vector2(NextFloat(-7f, 7f), 25f),
            Velocity = new Vector2(NextFloat(-24f, 24f), NextFloat(130f, 220f)),
            Color = GameAssets.TrailColors[Math.Clamp(_settings.TrailIndex, 0, GameAssets.TrailColors.Length - 1)],
            Life = life,
            MaxLife = life,
            Size = NextFloat(2.5f, 5.5f)
        });
    }

    private void EmitTrailBurst()
    {
        for (var i = 0; i < 18; i++) EmitEngineParticle();
    }

    private void UpdateParticles(float dt)
    {
        for (var i = Particles.Count - 1; i >= 0; i--)
        {
            Particles[i].Life -= dt;
            if (Particles[i].Life <= 0f)
            {
                Particles.RemoveAt(i);
                continue;
            }
            Particles[i].Position += Particles[i].Velocity * dt;
            Particles[i].Velocity *= MathF.Pow(0.08f, dt);
        }

        for (var i = Explosions.Count - 1; i >= 0; i--)
        {
            Explosions[i].Age += dt;
            if (Explosions[i].Age >= 0.45f) Explosions.RemoveAt(i);
        }
    }

    private float NextFloat(float min, float max) => min + (float)_random.NextDouble() * (max - min);

    private static bool Overlap(Vector2 a, float aRadius, Vector2 b, float bRadius)
        => Vector2.DistanceSquared(a, b) <= (aRadius + bRadius) * (aRadius + bRadius);
}
