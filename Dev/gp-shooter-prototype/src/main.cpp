#define _CRT_SECURE_NO_WARNINGS
#include <SDL.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>
#include <fstream>
#include <cstring>
#include <cwchar>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#include "../Player.h"

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr float FIXED_DT = 1.0f / 120.0f;
constexpr float PI = 3.1415926535f;
constexpr float WORLD_WIDTH = 3600.0f;
constexpr float WORLD_HEIGHT = 2600.0f;
constexpr float WORLD_MARGIN = 100.0f;

enum class ItemType
{
    MachineGun,
    Shotgun,
    Medkit
};

enum class BuffType
{
    FireRate,
    Ricochet
};

struct RectF
{
    float x;
    float y;
    float w;
    float h;
};

struct Circle
{
    Vec2 center;
    float radius;
};

struct Camera
{
    float x;
    float y;
};

enum class EnemyType
{
    Wolf,
    Charger,
    Bat,
    Gun
};

struct Bullet
{
    Vec2 pos;
    Vec2 vel;
    float radius;
    int damage;
    bool fromPlayer;
    bool alive;
    float life;
    int bouncesRemaining;
    bool ricochetBullet;
};

struct Enemy
{
    Vec2 pos;
    Vec2 vel;
    float radius;
    int hp;
    int maxHp;
    float shootTimer;
    float contactCooldown;
    bool alive;
    EnemyType type;
};

struct TextureAsset
{
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

struct GameTextures
{
    TextureAsset playerBoo;
    TextureAsset weaponPistol;
    TextureAsset weaponMachineGun;
    TextureAsset weaponShotgun;
    TextureAsset wolf;
    TextureAsset charger;
    TextureAsset batEnemy;
    TextureAsset gunEnemy;
    TextureAsset chest;
    TextureAsset ricochetItem;
    TextureAsset fireRateItem;
};

struct Chest
{
    Vec2 pos;
    float radius;
    bool opened;
    bool itemTaken;
    ItemType item;
};

struct BuffPickup
{
    Vec2 pos;
    float radius;
    bool alive;
    BuffType type;
};

struct ContactInfo
{
    bool hit = false;
    Vec2 normal = Vec2(0.0f, -1.0f);
    float penetration = 0.0f;
};

struct ChestUiState
{
    bool open = false;
    int chestIndex = -1;
    bool confirmTake = false;
    int confirmChoice = 0;
};

static std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return {};
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);

    if (size <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), size);

    return wide;
}

static bool FileExistsUtf8(const std::string& path)
{
#ifdef _WIN32
    std::wstring wide = Utf8ToWide(path);
    DWORD attrs = GetFileAttributesW(wide.c_str());

    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    std::ifstream file(path, std::ios::binary);

    return file.good();
#endif
}

static std::string ResolveAssetPath(const char* filename)
{
    const char* prefixes[] = {
        "assets/",
        "../assets/",
        "../../assets/"
    };

    for (const char* prefix : prefixes)
    {
        std::string path = std::string(prefix) + filename;

        if (FileExistsUtf8(path))
        {
            return path;
        }
    }

    return std::string("assets/") + filename;
}

static bool ImageHasRealAlpha(const unsigned char* pixels, int width, int height)
{
    int transparentCount = 0;
    const int pixelCount = width * height;
    const int threshold = pixelCount / 200;

    for (int i = 0; i < pixelCount; ++i)
    {
        if (pixels[i * 4 + 3] < 16)
        {
            transparentCount += 1;

            if (transparentCount > threshold)
            {
                return true;
            }
        }
    }

    return false;
}

static bool IsBakedBackgroundPixel(Uint8 r, Uint8 g, Uint8 b)
{
    int maxChannel = static_cast<int>(r);

    if (static_cast<int>(g) > maxChannel)
    {
        maxChannel = static_cast<int>(g);
    }

    if (static_cast<int>(b) > maxChannel)
    {
        maxChannel = static_cast<int>(b);
    }

    int minChannel = static_cast<int>(r);

    if (static_cast<int>(g) < minChannel)
    {
        minChannel = static_cast<int>(g);
    }

    if (static_cast<int>(b) < minChannel)
    {
        minChannel = static_cast<int>(b);
    }

    int saturation = maxChannel - minChannel;

    if (minChannel >= 232)
    {
        return true;
    }

    if (maxChannel <= 40 && saturation < 24)
    {
        return true;
    }

    if (saturation < 10 && minChannel >= 100 && maxChannel <= 245)
    {
        return true;
    }

    return false;
}

static void FlipImageVertical(unsigned char* pixels, int width, int height)
{
    const int rowBytes = width * 4;
    std::vector<unsigned char> row(static_cast<size_t>(rowBytes));

    for (int y = 0; y < height / 2; ++y)
    {
        unsigned char* top = pixels + y * rowBytes;
        unsigned char* bottom = pixels + (height - 1 - y) * rowBytes;

        std::memcpy(row.data(), top, static_cast<size_t>(rowBytes));
        std::memcpy(top, bottom, static_cast<size_t>(rowBytes));
        std::memcpy(bottom, row.data(), static_cast<size_t>(rowBytes));
    }
}

static void RemoveBakedBackground(unsigned char* pixels, int width, int height)
{
    const int pixelCount = width * height;
    std::vector<bool> visited(static_cast<size_t>(pixelCount), false);
    std::vector<int> queue;
    queue.reserve(static_cast<size_t>(pixelCount / 4));

    auto tryPush = [&](int x, int y)
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
        {
            return;
        }

        int index = y * width + x;

        if (visited[static_cast<size_t>(index)])
        {
            return;
        }

        Uint8 r = pixels[index * 4 + 0];
        Uint8 g = pixels[index * 4 + 1];
        Uint8 b = pixels[index * 4 + 2];

        if (!IsBakedBackgroundPixel(r, g, b))
        {
            return;
        }

        visited[static_cast<size_t>(index)] = true;
        queue.push_back(index);
    };

    for (int x = 0; x < width; ++x)
    {
        tryPush(x, 0);
        tryPush(x, height - 1);
    }

    for (int y = 0; y < height; ++y)
    {
        tryPush(0, y);
        tryPush(width - 1, y);
    }

    while (!queue.empty())
    {
        int index = queue.back();
        queue.pop_back();

        pixels[index * 4 + 3] = 0;

        int x = index % width;
        int y = index / width;

        tryPush(x - 1, y);
        tryPush(x + 1, y);
        tryPush(x, y - 1);
        tryPush(x, y + 1);
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<Uint8> nextAlpha(static_cast<size_t>(pixelCount));

        for (int i = 0; i < pixelCount; ++i)
        {
            nextAlpha[static_cast<size_t>(i)] = pixels[i * 4 + 3];
        }

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int index = y * width + x;

                if (pixels[index * 4 + 3] == 0)
                {
                    continue;
                }

                bool touchesTransparent = false;

                if (x > 0 && pixels[(index - 1) * 4 + 3] == 0) touchesTransparent = true;
                if (x < width - 1 && pixels[(index + 1) * 4 + 3] == 0) touchesTransparent = true;
                if (y > 0 && pixels[(index - width) * 4 + 3] == 0) touchesTransparent = true;
                if (y < height - 1 && pixels[(index + width) * 4 + 3] == 0) touchesTransparent = true;

                if (!touchesTransparent)
                {
                    continue;
                }

                Uint8 r = pixels[index * 4 + 0];
                Uint8 g = pixels[index * 4 + 1];
                Uint8 b = pixels[index * 4 + 2];

                if (IsBakedBackgroundPixel(r, g, b))
                {
                    nextAlpha[static_cast<size_t>(index)] = 0;
                }
            }
        }

        for (int i = 0; i < pixelCount; ++i)
        {
            pixels[i * 4 + 3] = nextAlpha[static_cast<size_t>(i)];
        }
    }
}

static bool LoadTextureFromFile(
    SDL_Renderer* renderer,
    const char* filename,
    TextureAsset& out,
    bool stripWhiteBackground = false,
    bool flipVertical = false)
{
    std::string path = ResolveAssetPath(filename);

#ifdef _WIN32
    std::wstring wide = Utf8ToWide(path);
    FILE* file = _wfopen(wide.c_str(), L"rb");
#else
    FILE* file = std::fopen(path.c_str(), "rb");
#endif

    if (!file)
    {
        std::cerr << "Failed to open asset: " << path << "\n";
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    long fileSize = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (fileSize <= 0)
    {
        std::fclose(file);
        return false;
    }

    std::vector<unsigned char> buffer(static_cast<size_t>(fileSize));
    std::fread(buffer.data(), 1, buffer.size(), file);
    std::fclose(file);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        buffer.data(),
        static_cast<int>(buffer.size()),
        &width,
        &height,
        &channels,
        4
    );

    if (!pixels)
    {
        std::cerr << "Failed to decode asset: " << path << "\n";
        return false;
    }

    if (stripWhiteBackground && !ImageHasRealAlpha(pixels, width, height))
    {
        RemoveBakedBackground(pixels, width, height);
    }

    if (flipVertical)
    {
        FlipImageVertical(pixels, width, height);
    }

    out.texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height
    );

    if (!out.texture)
    {
        stbi_image_free(pixels);
        return false;
    }

    std::vector<Uint32> converted(static_cast<size_t>(width) * static_cast<size_t>(height));

    for (int i = 0; i < width * height; ++i)
    {
        Uint8 r = pixels[i * 4 + 0];
        Uint8 g = pixels[i * 4 + 1];
        Uint8 b = pixels[i * 4 + 2];
        Uint8 a = pixels[i * 4 + 3];

        converted[static_cast<size_t>(i)] =
            (static_cast<Uint32>(a) << 24) |
            (static_cast<Uint32>(b) << 16) |
            (static_cast<Uint32>(g) << 8) |
            static_cast<Uint32>(r);
    }

    SDL_UpdateTexture(
        out.texture,
        nullptr,
        converted.data(),
        width * static_cast<int>(sizeof(Uint32))
    );

    SDL_SetTextureBlendMode(out.texture, SDL_BLENDMODE_BLEND);

    out.width = width;
    out.height = height;

    stbi_image_free(pixels);

    return true;
}

static bool LoadGameTextures(SDL_Renderer* renderer, GameTextures& textures)
{
    bool ok = true;

    ok &= LoadTextureFromFile(renderer, "BOO.png", textures.playerBoo, false);
    ok &= LoadTextureFromFile(renderer, "\xEB\x8A\x91\xEB\x8C\x80.png", textures.wolf, true);
    ok &= LoadTextureFromFile(renderer, "\xEB\x8B\xA4\xEA\xB0\x80\xEC\x98\xA4\xEB\x8A\x94\xEC\xA0\x81.png", textures.charger, true);
    ok &= LoadTextureFromFile(renderer, "\xEC\x95\xBC\xEA\xB5\xAC\xEB\xB0\xA9\xEB\xA7\x9D\xEC\x9D\xB4\xEC\xA0\x81.png", textures.batEnemy, true);
    ok &= LoadTextureFromFile(renderer, "\xEC\xB4\x9D\xEB\x93\xA0\xEC\xA0\x81.png", textures.gunEnemy, true);
    ok &= LoadTextureFromFile(
        renderer,
        "item\xEB\xB3\xB4\xEB\xAC\xBC\xEC\x83\x81\xEC\x9E\x90.png",
        textures.chest,
        true
    );
    ok &= LoadTextureFromFile(
        renderer,
        "item\xEB\xB0\x98\xEC\x82\xAC.png",
        textures.ricochetItem,
        true
    );
    ok &= LoadTextureFromFile(
        renderer,
        "item\xEC\xB4\x9D\xEC\x95\x8C\xEC\x86\x8D\xEB\x8F\x84.png",
        textures.fireRateItem,
        true
    );
    ok &= LoadTextureFromFile(renderer, "\xEA\xB6\x8C\xEC\xB4\x9D.png", textures.weaponPistol, true);
    ok &= LoadTextureFromFile(renderer, "\xEA\xB8\xB0\xEA\xB4\x80\xEC\xB4\x9D.png", textures.weaponMachineGun, true);
    ok &= LoadTextureFromFile(renderer, "\xEC\x83\xB7\xEA\xB1\xB4.png", textures.weaponShotgun, true);

    return ok;
}

static void DestroyGameTextures(GameTextures& textures)
{
    if (textures.playerBoo.texture) SDL_DestroyTexture(textures.playerBoo.texture);
    if (textures.weaponPistol.texture) SDL_DestroyTexture(textures.weaponPistol.texture);
    if (textures.weaponMachineGun.texture) SDL_DestroyTexture(textures.weaponMachineGun.texture);
    if (textures.weaponShotgun.texture) SDL_DestroyTexture(textures.weaponShotgun.texture);
    if (textures.wolf.texture) SDL_DestroyTexture(textures.wolf.texture);
    if (textures.charger.texture) SDL_DestroyTexture(textures.charger.texture);
    if (textures.batEnemy.texture) SDL_DestroyTexture(textures.batEnemy.texture);
    if (textures.gunEnemy.texture) SDL_DestroyTexture(textures.gunEnemy.texture);
    if (textures.chest.texture) SDL_DestroyTexture(textures.chest.texture);
    if (textures.ricochetItem.texture) SDL_DestroyTexture(textures.ricochetItem.texture);
    if (textures.fireRateItem.texture) SDL_DestroyTexture(textures.fireRateItem.texture);

    textures = {};
}

static int ScreenX(float worldX, const Camera& camera)
{
    return static_cast<int>(worldX - camera.x);
}

static int ScreenY(float worldY, const Camera& camera)
{
    return static_cast<int>(worldY - camera.y);
}

static Vec2 WorldToScreen(const Vec2& world, const Camera& camera)
{
    return Vec2(world.x - camera.x, world.y - camera.y);
}

static void DrawTextureCentered(
    SDL_Renderer* renderer,
    const TextureAsset& asset,
    const Vec2& worldPos,
    const Camera& camera,
    float displaySize,
    SDL_RendererFlip flip = SDL_FLIP_NONE)
{
    if (!asset.texture || asset.width <= 0 || asset.height <= 0)
    {
        return;
    }

    int maxDim = asset.width > asset.height ? asset.width : asset.height;
    float scale = displaySize / static_cast<float>(maxDim);
    int drawW = static_cast<int>(asset.width * scale);
    int drawH = static_cast<int>(asset.height * scale);
    Vec2 screen = WorldToScreen(worldPos, camera);

    SDL_Rect dst{
        static_cast<int>(screen.x - drawW * 0.5f),
        static_cast<int>(screen.y - drawH * 0.5f),
        drawW,
        drawH
    };

    SDL_Point center{ drawW / 2, drawH / 2 };

    SDL_RenderCopyEx(renderer, asset.texture, nullptr, &dst, 0.0, &center, flip);
}

static void DrawTextureRotated(
    SDL_Renderer* renderer,
    const TextureAsset& asset,
    const Vec2& worldPos,
    const Camera& camera,
    float displaySize,
    float angleRadians,
    SDL_RendererFlip flip = SDL_FLIP_NONE)
{
    if (!asset.texture || asset.width <= 0 || asset.height <= 0)
    {
        return;
    }

    int maxDim = asset.width > asset.height ? asset.width : asset.height;
    float scale = displaySize / static_cast<float>(maxDim);
    int drawW = static_cast<int>(asset.width * scale);
    int drawH = static_cast<int>(asset.height * scale);
    Vec2 screen = WorldToScreen(worldPos, camera);

    SDL_Rect dst{
        static_cast<int>(screen.x - drawW * 0.5f),
        static_cast<int>(screen.y - drawH * 0.5f),
        drawW,
        drawH
    };

    SDL_Point center{ drawW / 2, drawH / 2 };
    double angleDeg = static_cast<double>(angleRadians) * 180.0 / PI;

    SDL_RenderCopyEx(
        renderer,
        asset.texture,
        nullptr,
        &dst,
        angleDeg,
        &center,
        flip
    );
}

static const TextureAsset* GetWeaponTexture(const GameTextures& textures, WeaponType weapon)
{
    if (weapon == WeaponType::MachineGun)
    {
        return &textures.weaponMachineGun;
    }

    if (weapon == WeaponType::Shotgun)
    {
        return &textures.weaponShotgun;
    }

    return &textures.weaponPistol;
}

static float GetWeaponDisplaySize(WeaponType weapon)
{
    if (weapon == WeaponType::MachineGun)
    {
        return 38.0f;
    }

    if (weapon == WeaponType::Shotgun)
    {
        return 42.0f;
    }

    return 34.0f;
}

static Vec2 RotateVec2(const Vec2& v, float angleRadians)
{
    float c = std::cos(angleRadians);
    float s = std::sin(angleRadians);
    return Vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

static void DrawPlayerHeldWeapon(
    SDL_Renderer* renderer,
    const TextureAsset& asset,
    WeaponType weapon,
    const Vec2& handPos,
    const Camera& camera,
    float displaySize)
{
    if (!asset.texture || asset.width <= 0 || asset.height <= 0)
    {
        return;
    }

    int maxDim = asset.width > asset.height ? asset.width : asset.height;
    float scale = displaySize / static_cast<float>(maxDim);
    float drawW = asset.width * scale;
    float drawH = asset.height * scale;

    SDL_RendererFlip flip = SDL_FLIP_HORIZONTAL;
    float angleRadians = 0.0f;
    Vec2 gripLocal(-drawW * 0.28f, drawH * 0.04f);

    if (weapon == WeaponType::MachineGun)
    {
        // PNG barrel points upper-right; rotate clockwise to face the enemy on the right.
        flip = SDL_FLIP_NONE;
        angleRadians = PI / 4.0f;
        gripLocal = Vec2(-drawW * 0.24f, drawH * 0.20f);
    }
    else if (weapon == WeaponType::Shotgun)
    {
        // Shotgun PNG already faces right; flipping would point the barrel backward.
        flip = SDL_FLIP_NONE;
    }

    Vec2 weaponCenter = handPos - RotateVec2(gripLocal, angleRadians);

    if (angleRadians == 0.0f)
    {
        DrawTextureCentered(renderer, asset, weaponCenter, camera, displaySize, flip);
    }
    else
    {
        DrawTextureRotated(renderer, asset, weaponCenter, camera, displaySize, angleRadians, flip);
    }
}

static void DrawWorldFloor(SDL_Renderer* renderer, const Camera& camera)
{
    SDL_SetRenderDrawColor(renderer, 42, 46, 56, 255);

    SDL_Rect floor{
        ScreenX(0.0f, camera),
        ScreenY(0.0f, camera),
        static_cast<int>(WORLD_WIDTH),
        static_cast<int>(WORLD_HEIGHT)
    };

    SDL_RenderFillRect(renderer, &floor);

    SDL_SetRenderDrawColor(renderer, 58, 64, 78, 255);

    for (int x = 0; x <= static_cast<int>(WORLD_WIDTH); x += 80)
    {
        SDL_RenderDrawLine(
            renderer,
            ScreenX(static_cast<float>(x), camera),
            ScreenY(0.0f, camera),
            ScreenX(static_cast<float>(x), camera),
            ScreenY(WORLD_HEIGHT, camera)
        );
    }

    for (int y = 0; y <= static_cast<int>(WORLD_HEIGHT); y += 80)
    {
        SDL_RenderDrawLine(
            renderer,
            ScreenX(0.0f, camera),
            ScreenY(static_cast<float>(y), camera),
            ScreenX(WORLD_WIDTH, camera),
            ScreenY(static_cast<float>(y), camera)
        );
    }
}

static Vec2 Reflect(const Vec2& v, const Vec2& n)
{
    return v - n * (2.0f * Dot(v, n));
}

static bool PointInRect(const Vec2& p, const RectF& r)
{
    return p.x >= r.x && p.x <= r.x + r.w &&
        p.y >= r.y && p.y <= r.y + r.h;
}

static ContactInfo CircleVsRect(const Circle& c, const RectF& r)
{
    ContactInfo info;

    float closestX = Clamp(c.center.x, r.x, r.x + r.w);
    float closestY = Clamp(c.center.y, r.y, r.y + r.h);

    Vec2 closest(closestX, closestY);
    Vec2 delta = c.center - closest;
    float distSq = Dot(delta, delta);

    if (distSq > 0.0001f && distSq < c.radius * c.radius)
    {
        float dist = std::sqrt(distSq);

        info.hit = true;
        info.normal = delta / dist;
        info.penetration = c.radius - dist;

        return info;
    }

    if (PointInRect(c.center, r))
    {
        float left = c.center.x - r.x;
        float right = (r.x + r.w) - c.center.x;
        float top = c.center.y - r.y;
        float bottom = (r.y + r.h) - c.center.y;

        float minDist = left;
        info.normal = Vec2(-1.0f, 0.0f);

        if (right < minDist)
        {
            minDist = right;
            info.normal = Vec2(1.0f, 0.0f);
        }

        if (top < minDist)
        {
            minDist = top;
            info.normal = Vec2(0.0f, -1.0f);
        }

        if (bottom < minDist)
        {
            minDist = bottom;
            info.normal = Vec2(0.0f, 1.0f);
        }

        info.hit = true;
        info.penetration = minDist + c.radius;

        return info;
    }

    return info;
}

static bool CircleIntersectsRect(const Circle& c, const RectF& r)
{
    return CircleVsRect(c, r).hit;
}

static bool CircleIntersectsCircle(const Circle& a, const Circle& b)
{
    float rr = a.radius + b.radius;
    return LengthSq(a.center - b.center) <= rr * rr;
}

static void DrawCircle(SDL_Renderer* renderer, int cx, int cy, int radius)
{
    for (int x = -radius; x <= radius; ++x)
    {
        for (int y = -radius; y <= radius; ++y)
        {
            if (x * x + y * y <= radius * radius)
            {
                SDL_RenderDrawPoint(renderer, cx + x, cy + y);
            }
        }
    }
}

static void DrawCircleOutline(SDL_Renderer* renderer, int cx, int cy, int radius)
{
    const int segments = 48;

    for (int i = 0; i < segments; ++i)
    {
        float a0 = (2.0f * PI * i) / segments;
        float a1 = (2.0f * PI * (i + 1)) / segments;

        int x0 = static_cast<int>(cx + std::cos(a0) * radius);
        int y0 = static_cast<int>(cy + std::sin(a0) * radius);
        int x1 = static_cast<int>(cx + std::cos(a1) * radius);
        int y1 = static_cast<int>(cy + std::sin(a1) * radius);

        SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
    }
}

static void DrawWorldRect(SDL_Renderer* renderer, const RectF& r, const Camera& camera)
{
    SDL_Rect rect{
        ScreenX(r.x, camera),
        ScreenY(r.y, camera),
        static_cast<int>(r.w),
        static_cast<int>(r.h)
    };

    SDL_RenderFillRect(renderer, &rect);
}

static void ResolveCircleWorld(Vec2& pos, Vec2& vel, float radius, const std::vector<RectF>& obstacles)
{
    const int iterations = 5;

    for (int i = 0; i < iterations; ++i)
    {
        bool hitAny = false;

        for (const RectF& r : obstacles)
        {
            ContactInfo hit = CircleVsRect(Circle{ pos, radius }, r);

            if (hit.hit)
            {
                hitAny = true;

                pos += hit.normal * (hit.penetration + 0.2f);

                float vn = Dot(vel, hit.normal);

                if (vn < 0.0f)
                {
                    vel -= hit.normal * vn;
                }
            }
        }

        if (!hitAny)
        {
            break;
        }
    }
}

static void MoveCircleSafely(Vec2& pos, Vec2& vel, float radius, const std::vector<RectF>& obstacles, float dt)
{
    Vec2 totalMove = vel * dt;
    float moveLength = Length(totalMove);

    int steps = 1;

    if (moveLength > 4.0f)
    {
        steps = static_cast<int>(std::ceil(moveLength / 4.0f));
    }

    if (steps > 32)
    {
        steps = 32;
    }

    Vec2 stepMove = totalMove / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i)
    {
        pos += stepMove;
        ResolveCircleWorld(pos, vel, radius, obstacles);
    }
}

static ContactInfo BulletWallContact(const Bullet& bullet, const std::vector<RectF>& obstacles)
{
    ContactInfo best;
    float bestPen = -1.0f;

    for (const RectF& r : obstacles)
    {
        ContactInfo hit = CircleVsRect(Circle{ bullet.pos, bullet.radius }, r);

        if (hit.hit && hit.penetration > bestPen)
        {
            best = hit;
            bestPen = hit.penetration;
        }
    }

    return best;
}

static void BuildObstacles(std::vector<RectF>& obstacles)
{
    obstacles.clear();

    const float left = -WORLD_MARGIN;
    const float top = -WORLD_MARGIN;
    const float wall = 80.0f;

    obstacles.push_back({ left, top, WORLD_WIDTH + WORLD_MARGIN * 2.0f, wall });
    obstacles.push_back({ left, WORLD_HEIGHT + WORLD_MARGIN - wall, WORLD_WIDTH + WORLD_MARGIN * 2.0f, wall });
    obstacles.push_back({ left, top, wall, WORLD_HEIGHT + WORLD_MARGIN * 2.0f });
    obstacles.push_back({ WORLD_WIDTH + WORLD_MARGIN - wall, top, wall, WORLD_HEIGHT + WORLD_MARGIN * 2.0f });

    obstacles.push_back({ 520.0f, 320.0f, 420.0f, 80.0f });
    obstacles.push_back({ 1180.0f, 260.0f, 90.0f, 420.0f });
    obstacles.push_back({ 1680.0f, 360.0f, 520.0f, 80.0f });
    obstacles.push_back({ 2480.0f, 280.0f, 90.0f, 360.0f });

    obstacles.push_back({ 360.0f, 920.0f, 340.0f, 90.0f });
    obstacles.push_back({ 980.0f, 980.0f, 160.0f, 420.0f });
    obstacles.push_back({ 1460.0f, 920.0f, 460.0f, 90.0f });
    obstacles.push_back({ 2140.0f, 860.0f, 90.0f, 460.0f });

    obstacles.push_back({ 620.0f, 1540.0f, 500.0f, 90.0f });
    obstacles.push_back({ 1620.0f, 1500.0f, 560.0f, 90.0f });
    obstacles.push_back({ 2860.0f, 1180.0f, 90.0f, 520.0f });

    obstacles.push_back({ 2680.0f, 420.0f, 90.0f, 220.0f });
    obstacles.push_back({ 2920.0f, 1680.0f, 140.0f, 100.0f });
    obstacles.push_back({ 220.0f, 1880.0f, 300.0f, 90.0f });
}

static Enemy MakeEnemy(Vec2 pos, EnemyType type, int hp, float radius)
{
    Enemy enemy;
    enemy.pos = pos;
    enemy.vel = Vec2(0.0f, 0.0f);
    enemy.radius = radius;
    enemy.hp = hp;
    enemy.maxHp = hp;
    enemy.shootTimer = 1.0f;
    enemy.contactCooldown = 0.0f;
    enemy.alive = true;
    enemy.type = type;
    return enemy;
}

static void SpawnEnemies(std::vector<Enemy>& enemies)
{
    enemies.clear();

    struct EnemySpawn
    {
        Vec2 pos;
        EnemyType type;
        int hp;
        float radius;
    };

    const EnemySpawn spawns[] = {
        { Vec2(1180.0f, 520.0f), EnemyType::Wolf, 5, 20.0f },
        { Vec2(1680.0f, 620.0f), EnemyType::Wolf, 5, 20.0f },
        { Vec2(2180.0f, 480.0f), EnemyType::Wolf, 5, 20.0f },
        { Vec2(2680.0f, 700.0f), EnemyType::Wolf, 5, 20.0f },
        { Vec2(1420.0f, 1280.0f), EnemyType::Wolf, 5, 20.0f },
        { Vec2(2320.0f, 1180.0f), EnemyType::Wolf, 5, 20.0f },
        { Vec2(1980.0f, 860.0f), EnemyType::Charger, 26, 20.0f },
        { Vec2(2620.0f, 1420.0f), EnemyType::Bat, 32, 21.0f },
        { Vec2(1280.0f, 1680.0f), EnemyType::Gun, 28, 20.0f }
    };

    for (const EnemySpawn& spawn : spawns)
    {
        enemies.push_back(MakeEnemy(spawn.pos, spawn.type, spawn.hp, spawn.radius));
    }
}

static ItemType RandomChestItem()
{
    if (std::rand() % 2 == 0)
    {
        return ItemType::MachineGun;
    }

    return ItemType::Shotgun;
}

static void SpawnChests(std::vector<Chest>& chests)
{
    chests.clear();

    chests.push_back({ Vec2(720.0f, 560.0f), 28.0f, false, false, RandomChestItem() });
    chests.push_back({ Vec2(1980.0f, 620.0f), 28.0f, false, false, RandomChestItem() });
    chests.push_back({ Vec2(520.0f, 1480.0f), 28.0f, false, false, RandomChestItem() });
    chests.push_back({ Vec2(2620.0f, 1420.0f), 28.0f, false, false, RandomChestItem() });
}

static void SpawnBuffPickups(std::vector<BuffPickup>& buffs)
{
    buffs.clear();

    buffs.push_back({ Vec2(1280.0f, 620.0f), 22.0f, true, BuffType::FireRate });
    buffs.push_back({ Vec2(2480.0f, 980.0f), 22.0f, true, BuffType::Ricochet });
}

static void AddBullet(
    std::vector<Bullet>& bullets,
    const Vec2& pos,
    const Vec2& dir,
    bool fromPlayer,
    float speed,
    float radius,
    int damage,
    int bouncesRemaining)
{
    Bullet b;
    b.pos = pos;
    b.vel = Normalize(dir) * speed;
    b.radius = radius;
    b.damage = damage;
    b.fromPlayer = fromPlayer;
    b.alive = true;
    b.life = 2.2f;
    b.bouncesRemaining = bouncesRemaining;
    b.ricochetBullet = bouncesRemaining > 0;

    bullets.push_back(b);
}

static void FirePlayerWeapon(std::vector<Bullet>& bullets, Player& player, const Vec2& mouseWorld)
{
    Vec2 aimDir = mouseWorld - player.pos;

    if (Length(aimDir) <= 0.001f)
    {
        return;
    }

    Vec2 dir = Normalize(aimDir);
    int bounces = player.ricochetBuffTimer > 0.0f ? 2 : 0;

    if (player.weapon == WeaponType::Pistol)
    {
        AddBullet(bullets, player.pos + dir * 34.0f, dir, true, 650.0f, 5.0f, 1, bounces);
    }
    else if (player.weapon == WeaponType::MachineGun)
    {
        AddBullet(bullets, player.pos + dir * 34.0f, dir, true, 760.0f, 4.0f, 1, bounces);
    }
    else if (player.weapon == WeaponType::Shotgun)
    {
        float baseAngle = std::atan2(dir.y, dir.x);

        for (int i = -2; i <= 2; ++i)
        {
            float spread = static_cast<float>(i) * 0.14f;
            float a = baseAngle + spread;
            Vec2 pelletDir(std::cos(a), std::sin(a));

            AddBullet(bullets, player.pos + pelletDir * 34.0f, pelletDir, true, 620.0f, 4.0f, 1, bounces);
        }
    }

    player.shootTimer = player.GetCurrentShootCooldown();
}

static void StoreChestItemInBag(Chest& chest, Player& player)
{
    if (!chest.opened || chest.itemTaken)
    {
        return;
    }

    if (chest.item == ItemType::MachineGun)
    {
        player.hasMachineGun = true;
        player.weapon = WeaponType::MachineGun;
    }
    else if (chest.item == ItemType::Shotgun)
    {
        player.hasShotgun = true;
        player.weapon = WeaponType::Shotgun;
    }
    else
    {
        player.medkitCount += 1;
    }

    chest.itemTaken = true;
}

static bool HasLineOfSight(const Vec2& from, const Vec2& to, const std::vector<RectF>& obstacles)
{
    Vec2 delta = to - from;
    float dist = Length(delta);

    if (dist < 0.001f)
    {
        return true;
    }

    Vec2 dir = delta / dist;
    int samples = static_cast<int>(dist / 16.0f);

    if (samples < 1)
    {
        samples = 1;
    }

    for (int i = 1; i < samples; ++i)
    {
        Vec2 p = from + dir * (16.0f * static_cast<float>(i));

        for (const RectF& r : obstacles)
        {
            if (PointInRect(p, r))
            {
                return false;
            }
        }
    }

    return true;
}

static void DrawPlayer(
    SDL_Renderer* renderer,
    const Player& player,
    const Camera& camera,
    const GameTextures& textures)
{
    if (textures.playerBoo.texture)
    {
        DrawTextureCentered(renderer, textures.playerBoo, player.pos, camera, 72.0f, SDL_FLIP_HORIZONTAL);
    }
    else
    {
        Vec2 screen = WorldToScreen(player.pos, camera);
        SDL_SetRenderDrawColor(renderer, 174, 218, 238, 255);
        DrawCircle(renderer, static_cast<int>(screen.x), static_cast<int>(screen.y), static_cast<int>(player.radius));
    }

    const TextureAsset* weaponTexture = GetWeaponTexture(textures, player.weapon);

    if (weaponTexture && weaponTexture->texture)
    {
        // BOO always faces right; keep the held weapon horizontal and fixed while moving.
        const Vec2 handOffset{ 11.0f, 8.0f };
        Vec2 handPos = player.pos + handOffset;
        float displaySize = GetWeaponDisplaySize(player.weapon);

        DrawPlayerHeldWeapon(
            renderer,
            *weaponTexture,
            player.weapon,
            handPos,
            camera,
            displaySize
        );
    }
}

static void DrawEnemy(SDL_Renderer* renderer, const Enemy& enemy, const Camera& camera, const GameTextures& textures)
{
    const TextureAsset* sprite = nullptr;
    float displaySize = 64.0f;

    switch (enemy.type)
    {
    case EnemyType::Wolf:
        sprite = &textures.wolf;
        displaySize = 70.0f;
        break;
    case EnemyType::Charger:
        sprite = &textures.charger;
        displaySize = 64.0f;
        break;
    case EnemyType::Bat:
        sprite = &textures.batEnemy;
        displaySize = 68.0f;
        break;
    case EnemyType::Gun:
        sprite = &textures.gunEnemy;
        displaySize = 64.0f;
        break;
    }

    if (sprite && sprite->texture)
    {
        DrawTextureCentered(renderer, *sprite, enemy.pos, camera, displaySize);
    }
    else
    {
        Vec2 screen = WorldToScreen(enemy.pos, camera);
        SDL_SetRenderDrawColor(renderer, 210, 80, 80, 255);
        DrawCircle(renderer, static_cast<int>(screen.x), static_cast<int>(screen.y), static_cast<int>(enemy.radius));
    }
}

static void DrawChest(SDL_Renderer* renderer, const Chest& chest, const Camera& camera, const GameTextures& textures)
{
    if (textures.chest.texture)
    {
        Uint8 alpha = chest.itemTaken ? 120 : 255;
        SDL_SetTextureAlphaMod(textures.chest.texture, alpha);
        DrawTextureCentered(renderer, textures.chest, chest.pos, camera, 72.0f);
        SDL_SetTextureAlphaMod(textures.chest.texture, 255);
        return;
    }

    Vec2 screen = WorldToScreen(chest.pos, camera);
    SDL_Rect body{
        static_cast<int>(screen.x - 22.0f),
        static_cast<int>(screen.y - 16.0f),
        44,
        32
    };

    SDL_SetRenderDrawColor(renderer, 170, 110, 45, 255);
    SDL_RenderFillRect(renderer, &body);
}

static void DrawBuffPickup(SDL_Renderer* renderer, const BuffPickup& buff, const Camera& camera, const GameTextures& textures)
{
    const TextureAsset& sprite = buff.type == BuffType::FireRate
        ? textures.fireRateItem
        : textures.ricochetItem;

    if (sprite.texture)
    {
        DrawTextureCentered(renderer, sprite, buff.pos, camera, 68.0f);
        return;
    }

    Vec2 screen = WorldToScreen(buff.pos, camera);

    if (buff.type == BuffType::FireRate)
    {
        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 190, 100, 255, 255);
    }

    DrawCircle(renderer, static_cast<int>(screen.x), static_cast<int>(screen.y), static_cast<int>(buff.radius));
}

static void DrawHealthBar(SDL_Renderer* renderer, const Player& player)
{
    SDL_Rect back{ 24, 24, 220, 22 };
    SDL_SetRenderDrawColor(renderer, 60, 60, 70, 255);
    SDL_RenderFillRect(renderer, &back);

    float hpRatio = static_cast<float>(player.hp) / static_cast<float>(player.maxHp);
    hpRatio = Clamp(hpRatio, 0.0f, 1.0f);

    SDL_Rect hp{
        24,
        24,
        static_cast<int>(220.0f * hpRatio),
        22
    };

    SDL_SetRenderDrawColor(renderer, 90, 220, 120, 255);
    SDL_RenderFillRect(renderer, &hp);
}

static void DrawStatusUI(SDL_Renderer* renderer, const Player& player)
{
    SDL_Rect weaponBox{ 24, 56, 120, 22 };

    if (player.weapon == WeaponType::Pistol)
    {
        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    }
    else if (player.weapon == WeaponType::MachineGun)
    {
        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 255, 170, 70, 255);
    }

    SDL_RenderFillRect(renderer, &weaponBox);

    SDL_Rect medkitBox{ 24, 88, player.medkitCount * 28, 18 };

    if (medkitBox.w > 112)
    {
        medkitBox.w = 112;
    }

    SDL_SetRenderDrawColor(renderer, 240, 90, 110, 255);
    SDL_RenderFillRect(renderer, &medkitBox);

    if (player.fireRateBuffTimer > 0.0f)
    {
        SDL_Rect fireBox{
            24,
            116,
            static_cast<int>(player.fireRateBuffTimer * 14.0f),
            16
        };

        if (fireBox.w > 112)
        {
            fireBox.w = 112;
        }

        SDL_SetRenderDrawColor(renderer, 80, 220, 255, 255);
        SDL_RenderFillRect(renderer, &fireBox);
    }

    if (player.ricochetBuffTimer > 0.0f)
    {
        SDL_Rect bounceBox{
            24,
            140,
            static_cast<int>(player.ricochetBuffTimer * 14.0f),
            16
        };

        if (bounceBox.w > 112)
        {
            bounceBox.w = 112;
        }

        SDL_SetRenderDrawColor(renderer, 190, 100, 255, 255);
        SDL_RenderFillRect(renderer, &bounceBox);
    }
}

enum class AppScreen
{
    MainMenu,
    HowToPlay,
    Controls,
    Playing
};

struct UiRect
{
    int x;
    int y;
    int w;
    int h;
};

struct TextEntry
{
    std::wstring text;
    int size;
    SDL_Color color;
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

struct MenuState
{
    AppScreen screen = AppScreen::MainMenu;
    int selectedButton = 0;
    int hoveredButton = -1;
};

#ifdef _WIN32
#pragma comment(lib, "gdi32.lib")

static SDL_Texture* CreateTextTexture(
    SDL_Renderer* renderer,
    const wchar_t* text,
    int fontSize,
    SDL_Color color,
    int fontWeight,
    int* outWidth,
    int* outHeight)
{
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    LOGFONTW logFont{};
    logFont.lfHeight = -MulDiv(fontSize, GetDeviceCaps(screenDc, LOGPIXELSY), 72);
    logFont.lfWeight = fontWeight;
    wcscpy_s(logFont.lfFaceName, L"Malgun Gothic");

    HFONT font = CreateFontIndirectW(&logFont);
    HGDIOBJ oldFont = SelectObject(memDc, font);

    SIZE textSize{};
    GetTextExtentPoint32W(memDc, text, static_cast<int>(wcslen(text)), &textSize);

    int width = textSize.cx + 8;
    int height = textSize.cy + 8;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(memDc, dib);

    std::memset(bits, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    SetBkMode(memDc, TRANSPARENT);
    SetTextColor(memDc, RGB(color.r, color.g, color.b));
    TextOutW(memDc, 4, 4, text, static_cast<int>(wcslen(text)));

    std::vector<Uint32> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
    Uint8* src = static_cast<Uint8*>(bits);

    for (int i = 0; i < width * height; ++i)
    {
        Uint8 b = src[i * 4 + 0];
        Uint8 g = src[i * 4 + 1];
        Uint8 r = src[i * 4 + 2];
        Uint8 a = r;

        if (g > a)
        {
            a = g;
        }

        if (b > a)
        {
            a = b;
        }

        if (a == 0)
        {
            pixels[static_cast<size_t>(i)] = 0;
        }
        else
        {
            pixels[static_cast<size_t>(i)] =
                (static_cast<Uint32>(255) << 24) |
                (static_cast<Uint32>(b) << 16) |
                (static_cast<Uint32>(g) << 8) |
                static_cast<Uint32>(r);
        }
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height
    );

    if (texture)
    {
        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * static_cast<int>(sizeof(Uint32)));
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }

    SelectObject(memDc, oldBmp);
    SelectObject(memDc, oldFont);
    DeleteObject(dib);
    DeleteObject(font);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    if (outWidth)
    {
        *outWidth = width;
    }

    if (outHeight)
    {
        *outHeight = height;
    }

    return texture;
}
#endif

static TextEntry* FindOrCreateText(
    SDL_Renderer* renderer,
    std::vector<TextEntry>& cache,
    const wchar_t* text,
    int size,
    SDL_Color color,
    int fontWeight)
{
    for (TextEntry& entry : cache)
    {
        if (entry.text == text && entry.size == size &&
            entry.color.r == color.r && entry.color.g == color.g &&
            entry.color.b == color.b && entry.color.a == color.a)
        {
            return &entry;
        }
    }

    TextEntry entry;
    entry.text = text;
    entry.size = size;
    entry.color = color;

#ifdef _WIN32
    entry.texture = CreateTextTexture(
        renderer,
        text,
        size,
        color,
        fontWeight,
        &entry.width,
        &entry.height
    );
#endif

    cache.push_back(entry);
    return &cache.back();
}

static void DestroyTextCache(std::vector<TextEntry>& cache)
{
    for (TextEntry& entry : cache)
    {
        if (entry.texture)
        {
            SDL_DestroyTexture(entry.texture);
        }
    }

    cache.clear();
}

static void DrawCachedText(SDL_Renderer* renderer, const TextEntry& entry, int x, int y, bool centerX = false)
{
    if (!entry.texture)
    {
        return;
    }

    SDL_Rect dst{
        centerX ? x - entry.width / 2 : x,
        y,
        entry.width,
        entry.height
    };

    SDL_RenderCopy(renderer, entry.texture, nullptr, &dst);
}

static bool PointInUiRect(int mx, int my, const UiRect& rect)
{
    return mx >= rect.x && mx < rect.x + rect.w &&
        my >= rect.y && my < rect.y + rect.h;
}

static void DrawUiRect(SDL_Renderer* renderer, const UiRect& rect, SDL_Color fill, SDL_Color border)
{
    SDL_Rect r{ rect.x, rect.y, rect.w, rect.h };
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &r);
}

static void DrawMenuBackground(SDL_Renderer* renderer)
{
    for (int y = 0; y < WINDOW_HEIGHT; y += 4)
    {
        float t = static_cast<float>(y) / static_cast<float>(WINDOW_HEIGHT);
        Uint8 r = static_cast<Uint8>(18.0f + t * 14.0f);
        Uint8 g = static_cast<Uint8>(22.0f + t * 18.0f);
        Uint8 b = static_cast<Uint8>(38.0f + t * 28.0f);
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_Rect band{ 0, y, WINDOW_WIDTH, 4 };
        SDL_RenderFillRect(renderer, &band);
    }
}

static UiRect GetMainMenuButtonRect(int index)
{
    return UiRect{ WINDOW_WIDTH / 2 - 210, 340 + index * 72, 420, 56 };
}

static void DrawMenuButton(
    SDL_Renderer* renderer,
    std::vector<TextEntry>& textCache,
    const UiRect& rect,
    const wchar_t* label,
    bool selected,
    bool hovered)
{
    SDL_Color fill{ 34, 39, 58, 255 };
    SDL_Color border{ 70, 78, 104, 255 };

    if (hovered)
    {
        fill = { 48, 56, 82, 255 };
        border = { 92, 225, 230, 255 };
    }

    if (selected)
    {
        fill = { 58, 72, 110, 255 };
        border = { 120, 200, 255, 255 };
    }

    DrawUiRect(renderer, rect, fill, border);

    if (selected || hovered)
    {
        SDL_Rect accent{ rect.x, rect.y, 5, rect.h };
        SDL_SetRenderDrawColor(renderer, 92, 225, 230, 255);
        SDL_RenderFillRect(renderer, &accent);
    }

    SDL_Color textColor{ 238, 240, 248, 255 };
    TextEntry* text = FindOrCreateText(renderer, textCache, label, 26, textColor, FW_MEDIUM);
    DrawCachedText(renderer, *text, rect.x + rect.w / 2, rect.y + (rect.h - text->height) / 2, true);
}

static void DrawMainMenu(
    SDL_Renderer* renderer,
    std::vector<TextEntry>& textCache,
    const MenuState& menu)
{
    DrawMenuBackground(renderer);

    TextEntry* title = FindOrCreateText(
        renderer,
        textCache,
        L"BOORKOV",
        96,
        SDL_Color{ 238, 240, 248, 255 },
        FW_BOLD
    );
    DrawCachedText(renderer, *title, WINDOW_WIDTH / 2, 88, true);

    const wchar_t* labels[] = {
        L"\uac8c\uc784 \uc2dc\uc791",
        L"\uac8c\uc784 \ubc29\ubc95",
        L"\uc870\uc791\ubc95"
    };

    for (int i = 0; i < 3; ++i)
    {
        UiRect rect = GetMainMenuButtonRect(i);
        bool selected = menu.selectedButton == i;
        bool hovered = menu.hoveredButton == i;
        DrawMenuButton(renderer, textCache, rect, labels[i], selected, hovered);
    }

    TextEntry* hint = FindOrCreateText(
        renderer,
        textCache,
        L"\u2191\u2193 \uc774\ub3d9  \u00b7  Enter \uc120\ud0dd  \u00b7  ESC \uc885\ub8cc",
        18,
        SDL_Color{ 120, 128, 150, 255 },
        FW_NORMAL
    );
    DrawCachedText(renderer, *hint, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 48, true);
}

static const std::wstring& GetWeaponNameWide(WeaponType weapon)
{
    static const std::wstring pistol = Utf8ToWide("\xEA\xB6\x8C\xEC\xB4\x9D");
    static const std::wstring machineGun = Utf8ToWide("\xEA\xB8\xB0\xEA\xB4\x80\xEB\x8B\xA8\xEC\xB4\x9D");
    static const std::wstring shotgun = Utf8ToWide("\xEC\x83\xB7\xEA\xB1\xB4");

    if (weapon == WeaponType::MachineGun)
    {
        return machineGun;
    }

    if (weapon == WeaponType::Shotgun)
    {
        return shotgun;
    }

    return pistol;
}

static const std::wstring& GetChestItemNameWide(ItemType item)
{
    if (item == ItemType::MachineGun)
    {
        return GetWeaponNameWide(WeaponType::MachineGun);
    }

    if (item == ItemType::Shotgun)
    {
        return GetWeaponNameWide(WeaponType::Shotgun);
    }

    return GetWeaponNameWide(WeaponType::Pistol);
}

static void DrawChestWeaponIcon(
    SDL_Renderer* renderer,
    const TextureAsset& asset,
    const SDL_Rect& iconArea,
    int maxWidth,
    int maxHeight)
{
    if (!asset.texture || asset.width <= 0 || asset.height <= 0)
    {
        return;
    }

    float scaleW = static_cast<float>(maxWidth) / static_cast<float>(asset.width);
    float scaleH = static_cast<float>(maxHeight) / static_cast<float>(asset.height);
    float scale = scaleW < scaleH ? scaleW : scaleH;

    int drawW = static_cast<int>(asset.width * scale);
    int drawH = static_cast<int>(asset.height * scale);

    SDL_Rect dst{
        iconArea.x + (iconArea.w - drawW) / 2,
        iconArea.y + (iconArea.h - drawH) / 2,
        drawW,
        drawH
    };

    SDL_RenderCopy(renderer, asset.texture, nullptr, &dst);
}

static void DrawChestSlotPanel(
    SDL_Renderer* renderer,
    std::vector<TextEntry>& textCache,
    const GameTextures& textures,
    const UiRect& rect,
    const wchar_t* title,
    ItemType chestItem,
    WeaponType currentWeapon,
    bool showChestItem)
{
    SDL_Color fill{ 38, 42, 58, 255 };
    SDL_Color border{ 88, 96, 118, 255 };
    DrawUiRect(renderer, rect, fill, border);

    TextEntry* titleText = FindOrCreateText(
        renderer,
        textCache,
        title,
        24,
        SDL_Color{ 220, 224, 236, 255 },
        FW_BOLD
    );
    DrawCachedText(renderer, *titleText, rect.x + rect.w / 2, rect.y + 16, true);

    SDL_Rect iconArea{
        rect.x + rect.w / 2 - 70,
        rect.y + 58,
        140,
        140
    };

    SDL_SetRenderDrawColor(renderer, 24, 27, 38, 255);
    SDL_RenderFillRect(renderer, &iconArea);
    SDL_SetRenderDrawColor(renderer, 70, 78, 98, 255);
    SDL_RenderDrawRect(renderer, &iconArea);

    if (showChestItem)
    {
        if (chestItem == ItemType::MachineGun)
        {
            DrawChestWeaponIcon(renderer, textures.weaponMachineGun, iconArea, 120, 40);
        }
        else if (chestItem == ItemType::Shotgun)
        {
            DrawChestWeaponIcon(renderer, textures.weaponShotgun, iconArea, 120, 40);
        }
        else
        {
            DrawChestWeaponIcon(renderer, textures.weaponPistol, iconArea, 120, 40);
        }

        TextEntry* itemText = FindOrCreateText(
            renderer,
            textCache,
            GetChestItemNameWide(chestItem).c_str(),
            20,
            SDL_Color{ 180, 188, 204, 255 },
            FW_NORMAL
        );
        DrawCachedText(renderer, *itemText, rect.x + rect.w / 2, rect.y + rect.h - 34, true);
    }
    else
    {
        const TextureAsset* weaponTexture = GetWeaponTexture(textures, currentWeapon);

        if (weaponTexture)
        {
            DrawChestWeaponIcon(renderer, *weaponTexture, iconArea, 120, 40);
        }

        TextEntry* itemText = FindOrCreateText(
            renderer,
            textCache,
            GetWeaponNameWide(currentWeapon).c_str(),
            20,
            SDL_Color{ 180, 188, 204, 255 },
            FW_NORMAL
        );
        DrawCachedText(renderer, *itemText, rect.x + rect.w / 2, rect.y + rect.h - 34, true);
    }
}

static UiRect GetChestConfirmYesRect()
{
    return UiRect{ WINDOW_WIDTH / 2 - 220, 430, 180, 48 };
}

static UiRect GetChestConfirmNoRect()
{
    return UiRect{ WINDOW_WIDTH / 2 + 40, 430, 180, 48 };
}

static void DrawChestUi(
    SDL_Renderer* renderer,
    std::vector<TextEntry>& textCache,
    const ChestUiState& ui,
    const std::vector<Chest>& chests,
    const Player& player,
    const GameTextures& textures)
{
    if (!ui.open || ui.chestIndex < 0 || ui.chestIndex >= static_cast<int>(chests.size()))
    {
        return;
    }

    const Chest& chest = chests[ui.chestIndex];

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_Rect dim{ 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    SDL_RenderFillRect(renderer, &dim);

    SDL_Rect panel{ 220, 120, 840, 420 };
    SDL_SetRenderDrawColor(renderer, 24, 27, 38, 245);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 88, 96, 118, 255);
    SDL_RenderDrawRect(renderer, &panel);

    TextEntry* panelTitle = FindOrCreateText(
        renderer,
        textCache,
        L"\ubcf4\ubb3c\uc0c1\uc790",
        34,
        SDL_Color{ 238, 240, 248, 255 },
        FW_BOLD
    );
    DrawCachedText(renderer, *panelTitle, WINDOW_WIDTH / 2, 145, true);

    DrawChestSlotPanel(
        renderer,
        textCache,
        textures,
        UiRect{ 270, 210, 300, 220 },
        L"\ubcf4\ubb3c\uc0c1\uc790",
        chest.item,
        player.weapon,
        !chest.itemTaken
    );

    DrawChestSlotPanel(
        renderer,
        textCache,
        textures,
        UiRect{ 710, 210, 300, 220 },
        L"\ud604\uc7ac \ubb34\uae30",
        chest.item,
        player.weapon,
        false
    );

    if (!ui.confirmTake)
    {
        TextEntry* hint = FindOrCreateText(
            renderer,
            textCache,
            chest.itemTaken ? L"ESC \ub2eb\uae30" : L"E \ubcc0\uacbd \ud655\uc778  \u00b7  ESC \ub2eb\uae30",
            20,
            SDL_Color{ 150, 158, 176, 255 },
            FW_NORMAL
        );
        DrawCachedText(renderer, *hint, WINDOW_WIDTH / 2, 455, true);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
        SDL_Rect overlay{ panel.x, panel.y, panel.w, panel.h };
        SDL_RenderFillRect(renderer, &overlay);

        TextEntry* question = FindOrCreateText(
            renderer,
            textCache,
            L"\uc544\uc774\ud15c\uc744 \ubcc0\uacbd\ud558\uc2dc\uaca0\uc2b5\ub2c8\uae4c?",
            28,
            SDL_Color{ 238, 240, 248, 255 },
            FW_BOLD
        );
        DrawCachedText(renderer, *question, WINDOW_WIDTH / 2, 360, true);

        UiRect yesRect = GetChestConfirmYesRect();
        UiRect noRect = GetChestConfirmNoRect();

        DrawMenuButton(renderer, textCache, yesRect, L"\uc608", ui.confirmChoice == 0, ui.confirmChoice == 0);
        DrawMenuButton(renderer, textCache, noRect, L"\uc544\ub2c8\uc624", ui.confirmChoice == 1, ui.confirmChoice == 1);

        TextEntry* confirmHint = FindOrCreateText(
            renderer,
            textCache,
            L"\u2190\u2192 \uc120\ud0dd  \u00b7  E \ud655\uc778  \u00b7  ESC \ucde8\uc18c",
            18,
            SDL_Color{ 150, 158, 176, 255 },
            FW_NORMAL
        );
        DrawCachedText(renderer, *confirmHint, WINDOW_WIDTH / 2, 500, true);
    }
}

static void DrawInfoPanel(
    SDL_Renderer* renderer,
    std::vector<TextEntry>& textCache,
    const wchar_t* title,
    const wchar_t* const* lines,
    int lineCount)
{
    DrawMenuBackground(renderer);

    SDL_SetRenderDrawColor(renderer, 30, 34, 50, 245);
    SDL_Rect panel{ 140, 90, 1000, 540 };
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 92, 225, 230, 180);
    SDL_RenderDrawRect(renderer, &panel);

    SDL_Rect accent{ panel.x, panel.y, panel.w, 4 };
    SDL_SetRenderDrawColor(renderer, 92, 225, 230, 255);
    SDL_RenderFillRect(renderer, &accent);

    TextEntry* titleText = FindOrCreateText(
        renderer,
        textCache,
        title,
        40,
        SDL_Color{ 238, 240, 248, 255 },
        FW_BOLD
    );
    DrawCachedText(renderer, *titleText, WINDOW_WIDTH / 2, 130, true);

    int y = 210;

    for (int i = 0; i < lineCount; ++i)
    {
        SDL_Color lineColor{ 200, 206, 220, 255 };

        if (lines[i][0] == L'\x2022' || (lines[i][0] == L' ' && lines[i][1] == L' '))
        {
            lineColor = { 175, 182, 198, 255 };
        }

        TextEntry* lineText = FindOrCreateText(renderer, textCache, lines[i], 24, lineColor, FW_NORMAL);
        DrawCachedText(renderer, *lineText, 200, y);
        y += 42;
    }

    UiRect backBtn{ WINDOW_WIDTH / 2 - 110, 580, 220, 48 };
    DrawMenuButton(renderer, textCache, backBtn, L"\ub4a4\ub85c", false, false);

    TextEntry* hint = FindOrCreateText(
        renderer,
        textCache,
        L"ESC \ub610\ub294 \ub4a4\ub85c \ubc84\ud2bc\uc73c\ub85c \uba54\uc778 \uba54\ub274",
        18,
        SDL_Color{ 120, 128, 150, 255 },
        FW_NORMAL
    );
    DrawCachedText(renderer, *hint, WINDOW_WIDTH / 2, 648, true);
}

static void DrawHowToPlayScreen(SDL_Renderer* renderer, std::vector<TextEntry>& textCache)
{
    const wchar_t* lines[] = {
        L"BOO\ub97c \uc870\uc791\ud574 \ub113\uc740 \ub9f5 \uc548\uc758 \uc801\uc744 \ucc98\uce58\ud558\uace0 \ubcf4\ubb3c\uc744 \ubaa8\uc73c\uc138\uc694.",
        L"",
        L"\x2022 \ubcf4\ubb3c\uc0c1\uc790\uc5d0\uc11c \uc0c8 \ubb34\uae30\uc640 \ud68c\ubcf5 \uc544\uc774\ud15c\uc744 \uc5bb\uc744 \uc218 \uc788\uc2b5\ub2c8\ub2e4.",
        L"\x2022 \ud544\ub4dc \uc544\uc774\ud15c\uc744 \ud68d\ub4dd\ud558\uba74 \ubc1c\uc0ac\uc18d\ub3c4 / \ubc18\uc0ac \ubc84\ud504\uac00 \uc801\uc6a9\ub429\ub2c8\ub2e4.",
        L"\x2022 \uc801\uc744 \ucc98\uce58\ud558\uba74 \uc804\ub9ac\ud488\uc774 \ub4dc\ub86d\ub418\uace0, \uc0ac\ub9dd \uc2dc \uc2a4\ud3f0 \uc9c0\uc810\uc5d0\uc11c \ub2e4\uc2dc \uc2dc\uc791\ud569\ub2c8\ub2e4.",
        L"",
        L"\x2022 \ub2a5\ub300(6HP) / \ub2e4\uac00\uc624\ub294\uc801(3HP) / \uc57c\uad6c\ubc29\ub9dd\uc774\uc801(5HP) : \uadfc\uc811 \uacf5\uaca9",
        L"\x2022 \ucd1d\ub4e0\uc801(4HP) : \uc6d0\uac70\ub9ac \uc0ac\uaca9"
    };

    DrawInfoPanel(renderer, textCache, L"\uac8c\uc784 \ubc29\ubc95", lines, 8);
}

static void DrawControlsScreen(SDL_Renderer* renderer, std::vector<TextEntry>& textCache)
{
    const wchar_t* lines[] = {
        L"W / A / S / D          \uc774\ub3d9",
        L"\ub9c8\uc6b0\uc2a4 \uc67c\uc9c1 \ud074\ub9ad       \uc870\uc900 \ubc0f \ubc1c\uc0ac",
        L"E                     \ubcf4\ubb3c\uc0c1\uc790 \uc5f4\uae30 / \ubcc0\uacbd \ud655\uc778",
        L"\u2190 \u2192                   \uc608 / \uc544\ub2c8\uc624 \uc120\ud0dd",
        L"Q / ESC               \ubcf4\ubb3c\uc0c1\uc790 UI \ub2eb\uae30",
        L"R                     \uc2a4\ud14c\uc774\uc9c0 \ub9ac\uc14b",
        L"ESC                   \uba54\uc778 \uba54\ub274\ub85c \ub3cc\uc544\uac00\uae30"
    };

    DrawInfoPanel(renderer, textCache, L"\uc870\uc791\ubc95", lines, 7);
}

static void ResetGameplay(
    Player& player,
    std::vector<Bullet>& bullets,
    std::vector<Enemy>& enemies,
    std::vector<Chest>& chests,
    std::vector<BuffPickup>& buffs,
    ChestUiState& chestUi)
{
    player.Reset();
    bullets.clear();
    chestUi.open = false;
    chestUi.chestIndex = -1;
    chestUi.confirmTake = false;
    chestUi.confirmChoice = 0;
    SpawnEnemies(enemies);
    SpawnChests(chests);
    SpawnBuffPickups(buffs);
}

static bool HandleMenuInput(
    MenuState& menu,
    const SDL_Event& event,
    int mouseX,
    int mouseY)
{
    if (menu.screen == AppScreen::MainMenu)
    {
        menu.hoveredButton = -1;

        for (int i = 0; i < 3; ++i)
        {
            if (PointInUiRect(mouseX, mouseY, GetMainMenuButtonRect(i)))
            {
                menu.hoveredButton = i;
                break;
            }
        }

        if (event.type == SDL_MOUSEMOTION)
        {
            if (menu.hoveredButton >= 0)
            {
                menu.selectedButton = menu.hoveredButton;
            }
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            if (menu.hoveredButton == 0)
            {
                menu.screen = AppScreen::Playing;
                return true;
            }

            if (menu.hoveredButton == 1)
            {
                menu.screen = AppScreen::HowToPlay;
            }

            if (menu.hoveredButton == 2)
            {
                menu.screen = AppScreen::Controls;
            }
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
        {
            if (event.key.keysym.sym == SDLK_UP)
            {
                menu.selectedButton = (menu.selectedButton + 2) % 3;
            }

            if (event.key.keysym.sym == SDLK_DOWN)
            {
                menu.selectedButton = (menu.selectedButton + 1) % 3;
            }

            if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)
            {
                if (menu.selectedButton == 0)
                {
                    menu.screen = AppScreen::Playing;
                    return true;
                }

                if (menu.selectedButton == 1)
                {
                    menu.screen = AppScreen::HowToPlay;
                }

                if (menu.selectedButton == 2)
                {
                    menu.screen = AppScreen::Controls;
                }
            }
        }
    }
    else if (menu.screen == AppScreen::HowToPlay || menu.screen == AppScreen::Controls)
    {
        UiRect backBtn{ WINDOW_WIDTH / 2 - 110, 580, 220, 48 };

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            if (PointInUiRect(mouseX, mouseY, backBtn))
            {
                menu.screen = AppScreen::MainMenu;
            }
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_BACKSPACE)
            {
                menu.screen = AppScreen::MainMenu;
            }
        }
    }

    return false;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "BOORKOV",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    GameTextures textures;

    if (!LoadGameTextures(renderer, textures))
    {
        std::cerr << "Warning: some textures failed to load. Check the assets folder.\n";
    }

    bool running = true;
    MenuState menu;
    std::vector<TextEntry> textCache;

    Player player;

    std::vector<RectF> obstacles;
    std::vector<Enemy> enemies;
    std::vector<Bullet> bullets;
    std::vector<Chest> chests;
    std::vector<BuffPickup> buffs;

    BuildObstacles(obstacles);
    SpawnEnemies(enemies);
    SpawnChests(chests);
    SpawnBuffPickups(buffs);

    Camera camera{ 0.0f, 0.0f };

    Uint64 previousCounter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;

    bool interactPressed = false;
    bool confirmSelectPressed = false;
    bool closeUiPressed = false;

    ChestUiState chestUi;

    while (running)
    {
        Uint64 currentCounter = SDL_GetPerformanceCounter();

        double deltaSeconds =
            static_cast<double>(currentCounter - previousCounter) /
            static_cast<double>(SDL_GetPerformanceFrequency());

        previousCounter = currentCounter;

        if (deltaSeconds > 0.25)
        {
            deltaSeconds = 0.25;
        }

        accumulator += deltaSeconds;

        interactPressed = false;
        confirmSelectPressed = false;
        closeUiPressed = false;

        int mouseX = 0;
        int mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);

        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }

            if (menu.screen != AppScreen::Playing)
            {
                if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                    event.key.keysym.sym == SDLK_ESCAPE &&
                    menu.screen == AppScreen::MainMenu)
                {
                    running = false;
                }

                if (HandleMenuInput(menu, event, mouseX, mouseY))
                {
                    ResetGameplay(player, bullets, enemies, chests, buffs, chestUi);
                }

                continue;
            }

            if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
            {
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    if (chestUi.open)
                    {
                        if (chestUi.confirmTake)
                        {
                            chestUi.confirmTake = false;
                        }
                        else
                        {
                            chestUi.open = false;
                            chestUi.chestIndex = -1;
                            chestUi.confirmTake = false;
                        }
                    }
                    else
                    {
                        menu.screen = AppScreen::MainMenu;
                    }
                }

                if (event.key.keysym.sym == SDLK_r)
                {
                    ResetGameplay(player, bullets, enemies, chests, buffs, chestUi);
                }

                if (event.key.keysym.sym == SDLK_e)
                {
                    if (chestUi.open)
                    {
                        if (chestUi.confirmTake)
                        {
                            confirmSelectPressed = true;
                        }
                        else if (chestUi.chestIndex >= 0 &&
                            chestUi.chestIndex < static_cast<int>(chests.size()) &&
                            !chests[chestUi.chestIndex].itemTaken)
                        {
                            chestUi.confirmTake = true;
                            chestUi.confirmChoice = 0;
                        }
                        else
                        {
                            closeUiPressed = true;
                        }
                    }
                    else
                    {
                        interactPressed = true;
                    }
                }

                if (chestUi.open && chestUi.confirmTake)
                {
                    if (event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_a)
                    {
                        chestUi.confirmChoice = 0;
                    }

                    if (event.key.keysym.sym == SDLK_RIGHT || event.key.keysym.sym == SDLK_d)
                    {
                        chestUi.confirmChoice = 1;
                    }
                }

                if (event.key.keysym.sym == SDLK_q)
                {
                    if (chestUi.open)
                    {
                        if (chestUi.confirmTake)
                        {
                            chestUi.confirmTake = false;
                        }
                        else
                        {
                            closeUiPressed = true;
                        }
                    }
                }
            }
        }

        if (closeUiPressed)
        {
            chestUi.open = false;
            chestUi.chestIndex = -1;
            chestUi.confirmTake = false;
        }

        if (menu.screen != AppScreen::Playing)
        {
            SDL_SetRenderDrawColor(renderer, 18, 22, 38, 255);
            SDL_RenderClear(renderer);

            if (menu.screen == AppScreen::MainMenu)
            {
                DrawMainMenu(renderer, textCache, menu);
            }
            else if (menu.screen == AppScreen::HowToPlay)
            {
                DrawHowToPlayScreen(renderer, textCache);
            }
            else if (menu.screen == AppScreen::Controls)
            {
                DrawControlsScreen(renderer, textCache);
            }

            SDL_RenderPresent(renderer);
            continue;
        }

        camera.x = player.pos.x - WINDOW_WIDTH * 0.5f;
        camera.y = player.pos.y - WINDOW_HEIGHT * 0.5f;

        Uint32 mouseState = SDL_GetMouseState(&mouseX, &mouseY);

        Vec2 mouseWorld(
            static_cast<float>(mouseX) + camera.x,
            static_cast<float>(mouseY) + camera.y
        );

        bool interactHandled = false;
        bool confirmHandled = false;

        while (accumulator >= FIXED_DT)
        {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);

            if (!chestUi.open)
            {
                Vec2 input(0.0f, 0.0f);

                if (keys[SDL_SCANCODE_W])
                {
                    input.y -= 1.0f;
                }

                if (keys[SDL_SCANCODE_S])
                {
                    input.y += 1.0f;
                }

                if (keys[SDL_SCANCODE_A])
                {
                    input.x -= 1.0f;
                }

                if (keys[SDL_SCANCODE_D])
                {
                    input.x += 1.0f;
                }

                if (Length(input) > 0.001f)
                {
                    input = Normalize(input);
                }

                player.vel = input * player.speed;

                MoveCircleSafely(player.pos, player.vel, player.radius, obstacles, FIXED_DT);

                if ((mouseState & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0 && player.shootTimer <= 0.0f)
                {
                    FirePlayerWeapon(bullets, player, mouseWorld);
                }
            }
            else
            {
                player.vel = Vec2(0.0f, 0.0f);
            }

            player.Update(FIXED_DT);

            if (interactPressed && !interactHandled && !chestUi.open)
            {
                for (int i = 0; i < static_cast<int>(chests.size()); ++i)
                {
                    Chest& chest = chests[i];

                    if (!chest.itemTaken &&
                        CircleIntersectsCircle(
                            Circle{ player.pos, player.radius + 20.0f },
                            Circle{ chest.pos, chest.radius }))
                    {
                        chest.opened = true;
                        chestUi.open = true;
                        chestUi.chestIndex = i;
                        chestUi.confirmTake = false;
                        chestUi.confirmChoice = 0;
                        interactHandled = true;
                        break;
                    }
                }
            }

            if (confirmSelectPressed && !confirmHandled && chestUi.open && chestUi.confirmTake)
            {
                if (chestUi.confirmChoice == 0 &&
                    chestUi.chestIndex >= 0 &&
                    chestUi.chestIndex < static_cast<int>(chests.size()))
                {
                    StoreChestItemInBag(chests[chestUi.chestIndex], player);
                    chestUi.open = false;
                    chestUi.chestIndex = -1;
                    chestUi.confirmTake = false;
                    confirmHandled = true;
                }
                else
                {
                    chestUi.confirmTake = false;
                    confirmHandled = true;
                }
            }

            if (!chestUi.open)
            {
                for (BuffPickup& buff : buffs)
                {
                    if (!buff.alive)
                    {
                        continue;
                    }

                    if (CircleIntersectsCircle(
                        Circle{ buff.pos, buff.radius },
                        Circle{ player.pos, player.radius + 4.0f }))
                    {
                        buff.alive = false;

                        if (buff.type == BuffType::FireRate)
                        {
                            player.fireRateBuffTimer = 8.0f;
                        }
                        else
                        {
                            player.ricochetBuffTimer = 8.0f;
                        }
                    }
                }

                for (Enemy& enemy : enemies)
                {
                    if (!enemy.alive)
                    {
                        continue;
                    }

                    Vec2 toPlayer = player.pos - enemy.pos;
                    float dist = Length(toPlayer);
                    Vec2 desiredVel(0.0f, 0.0f);

                    if (enemy.contactCooldown > 0.0f)
                    {
                        enemy.contactCooldown -= FIXED_DT;
                    }

                    if (enemy.type == EnemyType::Gun)
                    {
                        if (dist > 220.0f)
                        {
                            desiredVel = Normalize(toPlayer) * 75.0f;
                        }
                        else if (dist < 150.0f)
                        {
                            desiredVel = Normalize(-toPlayer) * 55.0f;
                        }

                        enemy.vel = desiredVel;
                        MoveCircleSafely(enemy.pos, enemy.vel, enemy.radius, obstacles, FIXED_DT);

                        enemy.shootTimer -= FIXED_DT;

                        if (enemy.shootTimer <= 0.0f && dist < 720.0f)
                        {
                            if (HasLineOfSight(enemy.pos, player.pos, obstacles))
                            {
                                AddBullet(
                                    bullets,
                                    enemy.pos + Normalize(toPlayer) * 30.0f,
                                    toPlayer,
                                    false,
                                    430.0f,
                                    6.0f,
                                    15,
                                    0
                                );
                                enemy.shootTimer = 0.95f;
                            }
                            else
                            {
                                enemy.shootTimer = 0.3f;
                            }
                        }
                    }
                    else
                    {
                        float chaseSpeed = 95.0f;

                        if (enemy.type == EnemyType::Wolf)
                        {
                            chaseSpeed = 105.0f;
                        }
                        else if (enemy.type == EnemyType::Charger)
                        {
                            chaseSpeed = 125.0f;
                        }
                        else if (enemy.type == EnemyType::Bat)
                        {
                            chaseSpeed = 90.0f;
                        }

                        if (dist > enemy.radius + player.radius + 4.0f)
                        {
                            desiredVel = Normalize(toPlayer) * chaseSpeed;
                        }

                        enemy.vel = desiredVel;
                        MoveCircleSafely(enemy.pos, enemy.vel, enemy.radius, obstacles, FIXED_DT);

                        if (dist <= enemy.radius + player.radius + 8.0f && enemy.contactCooldown <= 0.0f)
                        {
                            int damage = 12;

                            if (enemy.type == EnemyType::Wolf)
                            {
                                damage = 18;
                            }
                            else if (enemy.type == EnemyType::Bat)
                            {
                                damage = 22;
                            }

                            player.hp -= damage;
                            enemy.contactCooldown = 0.8f;

                            if (player.hp <= 0)
                            {
                                player.Reset();
                                bullets.clear();
                                chestUi.open = false;
                                chestUi.chestIndex = -1;
                                chestUi.confirmTake = false;
                                SpawnEnemies(enemies);
                                SpawnChests(chests);
                                SpawnBuffPickups(buffs);
                            }
                        }
                    }
                }

                for (Bullet& bullet : bullets)
                {
                    if (!bullet.alive)
                    {
                        continue;
                    }

                    bullet.pos += bullet.vel * FIXED_DT;
                    bullet.life -= FIXED_DT;

                    if (bullet.life <= 0.0f)
                    {
                        bullet.alive = false;
                        continue;
                    }

                    ContactInfo wallHit = BulletWallContact(bullet, obstacles);

                    if (wallHit.hit)
                    {
                        if (bullet.fromPlayer && bullet.bouncesRemaining > 0)
                        {
                            bullet.vel = Reflect(bullet.vel, wallHit.normal);
                            bullet.pos += wallHit.normal * (wallHit.penetration + bullet.radius + 1.0f);
                            bullet.bouncesRemaining -= 1;
                            bullet.life *= 0.85f;
                        }
                        else
                        {
                            bullet.alive = false;
                        }

                        continue;
                    }
                }

                for (size_t i = 0; i < bullets.size(); ++i)
                {
                    if (!bullets[i].alive)
                    {
                        continue;
                    }

                    for (size_t j = i + 1; j < bullets.size(); ++j)
                    {
                        if (!bullets[j].alive)
                        {
                            continue;
                        }

                        if (bullets[i].fromPlayer == bullets[j].fromPlayer)
                        {
                            continue;
                        }

                        if (CircleIntersectsCircle(
                            Circle{ bullets[i].pos, bullets[i].radius },
                            Circle{ bullets[j].pos, bullets[j].radius }))
                        {
                            bullets[i].alive = false;
                            bullets[j].alive = false;
                        }
                    }
                }

                for (Bullet& bullet : bullets)
                {
                    if (!bullet.alive)
                    {
                        continue;
                    }

                    if (bullet.fromPlayer)
                    {
                        for (Enemy& enemy : enemies)
                        {
                            if (!enemy.alive)
                            {
                                continue;
                            }

                            if (CircleIntersectsCircle(
                                Circle{ bullet.pos, bullet.radius },
                                Circle{ enemy.pos, enemy.radius }))
                            {
                                bullet.alive = false;
                                enemy.hp -= bullet.damage;

                                if (enemy.hp <= 0)
                                {
                                    enemy.alive = false;
                                }

                                break;
                            }
                        }
                    }
                    else
                    {
                        if (CircleIntersectsCircle(
                            Circle{ bullet.pos, bullet.radius },
                            Circle{ player.pos, player.radius }))
                        {
                            bullet.alive = false;
                            player.hp -= bullet.damage;

                            if (player.hp <= 0)
                            {
                                player.Reset();
                                bullets.clear();
                                chestUi.open = false;
                                chestUi.chestIndex = -1;
                                chestUi.confirmTake = false;
                                SpawnEnemies(enemies);
                                SpawnChests(chests);
                                SpawnBuffPickups(buffs);
                                break;
                            }
                        }
                    }
                }

                bullets.erase(
                    std::remove_if(
                        bullets.begin(),
                        bullets.end(),
                        [](const Bullet& b)
                        {
                            return !b.alive;
                        }),
                    bullets.end()
                );

            }

            accumulator -= FIXED_DT;
        }

        SDL_SetRenderDrawColor(renderer, 23, 24, 30, 255);
        SDL_RenderClear(renderer);

        DrawWorldFloor(renderer, camera);

        SDL_SetRenderDrawColor(renderer, 70, 75, 88, 210);

        for (const RectF& r : obstacles)
        {
            DrawWorldRect(renderer, r, camera);
        }

        for (const Chest& chest : chests)
        {
            DrawChest(renderer, chest, camera, textures);
        }

        for (const BuffPickup& buff : buffs)
        {
            if (buff.alive)
            {
                DrawBuffPickup(renderer, buff, camera, textures);
            }
        }

        for (const Enemy& enemy : enemies)
        {
            if (enemy.alive)
            {
                DrawEnemy(renderer, enemy, camera, textures);
            }
        }

        for (const Bullet& bullet : bullets)
        {
            if (bullet.fromPlayer)
            {
                if (bullet.ricochetBullet)
                {
                    SDL_SetRenderDrawColor(renderer, 190, 100, 255, 255);
                }
                else
                {
                    SDL_SetRenderDrawColor(renderer, 120, 220, 255, 255);
                }
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, 255, 90, 90, 255);
            }

            Vec2 screen = WorldToScreen(bullet.pos, camera);
            DrawCircle(renderer, static_cast<int>(screen.x), static_cast<int>(screen.y), static_cast<int>(bullet.radius));
        }

        DrawPlayer(renderer, player, camera, textures);
        DrawHealthBar(renderer, player);
        DrawStatusUI(renderer, player);
        DrawChestUi(renderer, textCache, chestUi, chests, player, textures);

        SDL_RenderPresent(renderer);
    }

    DestroyTextCache(textCache);
    DestroyGameTextures(textures);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}