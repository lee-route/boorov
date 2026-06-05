#pragma once

#include "Vec2.h"

enum class WeaponType
{
    Pistol,
    MachineGun,
    Shotgun
};

class Player
{
public:
    Vec2 pos;
    Vec2 vel;
    Vec2 spawnPos;

    float radius;
    float speed;

    int hp;
    int maxHp;

    WeaponType weapon;

    bool hasMachineGun;
    bool hasShotgun;

    float shootCooldown;
    float shootTimer;

    int medkitCount;

    float fireRateBuffTimer;
    float ricochetBuffTimer;

public:
    Player();

    void Reset();
    void Update(float dt);

    float GetCurrentShootCooldown() const;
};