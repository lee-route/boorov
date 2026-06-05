#include "../Player.h"

Player::Player()
{
    spawnPos = Vec2(420.0f, 420.0f);
    pos = spawnPos;
    vel = Vec2(0.0f, 0.0f);

    radius = 18.0f;
    speed = 260.0f;

    maxHp = 100;
    hp = maxHp;

    weapon = WeaponType::Pistol;

    hasMachineGun = false;
    hasShotgun = false;

    shootCooldown = 0.16f;
    shootTimer = 0.0f;

    medkitCount = 0;

    fireRateBuffTimer = 0.0f;
    ricochetBuffTimer = 0.0f;
}

void Player::Reset()
{
    pos = spawnPos;
    vel = Vec2(0.0f, 0.0f);

    hp = maxHp;

    weapon = WeaponType::Pistol;

    hasMachineGun = false;
    hasShotgun = false;

    shootCooldown = 0.16f;
    shootTimer = 0.0f;

    medkitCount = 0;

    fireRateBuffTimer = 0.0f;
    ricochetBuffTimer = 0.0f;
}

void Player::Update(float dt)
{
    if (shootTimer > 0.0f)
    {
        shootTimer -= dt;

        if (shootTimer < 0.0f)
        {
            shootTimer = 0.0f;
        }
    }

    if (fireRateBuffTimer > 0.0f)
    {
        fireRateBuffTimer -= dt;

        if (fireRateBuffTimer < 0.0f)
        {
            fireRateBuffTimer = 0.0f;
        }
    }

    if (ricochetBuffTimer > 0.0f)
    {
        ricochetBuffTimer -= dt;

        if (ricochetBuffTimer < 0.0f)
        {
            ricochetBuffTimer = 0.0f;
        }
    }

    if (hp <= 45 && medkitCount > 0)
    {
        hp += 35;

        if (hp > maxHp)
        {
            hp = maxHp;
        }

        medkitCount -= 1;
    }
}

float Player::GetCurrentShootCooldown() const
{
    float cooldown = 0.16f;

    if (weapon == WeaponType::MachineGun)
    {
        cooldown = 0.07f;
    }
    else if (weapon == WeaponType::Shotgun)
    {
        cooldown = 0.48f;
    }

    if (fireRateBuffTimer > 0.0f)
    {
        cooldown *= 0.55f;
    }

    return cooldown;
}