#pragma once

#include <cmath>

struct Vec2
{
    float x;
    float y;

    Vec2()
    {
        x = 0.0f;
        y = 0.0f;
    }

    Vec2(float _x, float _y)
    {
        x = _x;
        y = _y;
    }
};

inline Vec2 operator+(const Vec2& a, const Vec2& b)
{
    return Vec2(a.x + b.x, a.y + b.y);
}

inline Vec2 operator-(const Vec2& a, const Vec2& b)
{
    return Vec2(a.x - b.x, a.y - b.y);
}

inline Vec2 operator-(const Vec2& a)
{
    return Vec2(-a.x, -a.y);
}

inline Vec2 operator*(const Vec2& a, float s)
{
    return Vec2(a.x * s, a.y * s);
}

inline Vec2 operator*(float s, const Vec2& a)
{
    return Vec2(a.x * s, a.y * s);
}

inline Vec2 operator/(const Vec2& a, float s)
{
    return Vec2(a.x / s, a.y / s);
}

inline Vec2& operator+=(Vec2& a, const Vec2& b)
{
    a.x += b.x;
    a.y += b.y;
    return a;
}

inline Vec2& operator-=(Vec2& a, const Vec2& b)
{
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

inline float Dot(const Vec2& a, const Vec2& b)
{
    return a.x * b.x + a.y * b.y;
}

inline float LengthSq(const Vec2& v)
{
    return v.x * v.x + v.y * v.y;
}

inline float Length(const Vec2& v)
{
    return std::sqrt(LengthSq(v));
}

inline Vec2 Normalize(const Vec2& v)
{
    float len = Length(v);

    if (len <= 0.0001f)
    {
        return Vec2(0.0f, 0.0f);
    }

    return v / len;
}

inline float Distance(const Vec2& a, const Vec2& b)
{
    return Length(a - b);
}

inline float Clamp(float value, float minValue, float maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }

    if (value > maxValue)
    {
        return maxValue;
    }

    return value;
}