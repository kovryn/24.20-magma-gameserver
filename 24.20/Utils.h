#pragma once
#include <Windows.h>
#include "SDK.hpp"
#include <iostream>
#include <algorithm>
#include "minhook/MinHook.h"
#include "Replication.h"
#include "GameMode.h"
#include "Inventory.h"
#include "API.h"
#include <random>
#include <thread>
#include <intrin.h>
#include "SDK/FortniteGame_parameters.hpp"
#pragma comment(lib, "curl/libcurl.lib")
#pragma comment(lib, "curl/zlib.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment (lib, "crypt32.lib")
#define CURL_STATICLIB
#include "curl/curl.h"

#define PI 					(3.1415926535897932f)
#define INV_PI			(0.31830988618f)
#define HALF_PI			(1.57079632679f)

inline vector<struct Loadout*> Loadouts{};
inline vector<string> JoinedPlayers{};
inline std::vector<SDK::AFortPickupAthena*> Pickups{};
inline std::vector<SDK::AActor*> Builds{};

template<typename Iter, typename RandomGenerator>
Iter properrandom(Iter start, Iter end, RandomGenerator& g) {
    std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
    std::advance(start, dis(g));
    return start;
}

template<typename Iter>
Iter properrandom(Iter start, Iter end) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return properrandom(start, end, gen);
}

struct Loadout
{
    vector<pair<UFortItemDefinition*, pair<int, int>>> Items;

    void AddItem(UFortItemDefinition* Def, int Count = 1, int LoadedAmmo = 0)
    {
        Items.push_back({ Def, {Count, LoadedAmmo} });
    }

    static Loadout* GetRandomLoadout()
    {
        std::ranges::shuffle(Loadouts, std::default_random_engine{});
        return *properrandom(Loadouts.begin(), Loadouts.end());
    }
};

namespace Utils
{
    inline void SwapInstruction(int64 Addr, int offset, uint8 NewInstruction)
    {
        DWORD dwProtection;
        VirtualProtect((void*)(Addr + offset), 1, PAGE_EXECUTE_READWRITE, &dwProtection);

        *(uint8*)(Addr + offset) = NewInstruction;

        DWORD dwTemp;
        VirtualProtect((void*)(Addr + offset), 1, dwProtection, &dwTemp);
    }

	inline SDK::FName MapName = SDK::FName();
	
	inline __int64 GetAddr(__int64 Addr)
	{
		return SDK::InSDKUtils::GetImageBase() + (Addr - 0x7FF73A530000);
	}

	inline __int64 GetOffset(__int64 Addr)
	{
		return Addr - 0x7FF73A530000;
	}

	template <typename T>
	inline T& GetFromMemory(__int32 Offset)
	{
		return *(T*)(SDK::InSDKUtils::GetImageBase() + Offset);
	}

	inline __int64 ReturnZero()
	{
		return 0;
	}

    inline void SwapVFTs(void* Base, uintptr_t Index, void* Detour, void** Original)
    {
        auto VTable = (*(void***)Base);
        if (!VTable) return;

        if (!VTable[Index]) return;

        if (Original) *Original = VTable[Index];

        DWORD dwOld;
        VirtualProtect(&VTable[Index], 8, PAGE_EXECUTE_READWRITE, &dwOld);
        VTable[Index] = Detour;
        DWORD dwTemp;
        VirtualProtect(&VTable[Index], 8, dwOld, &dwTemp);
    }

    static void SendWebhookMessage(std::string msg)
    {
        static bool Once = false;
        static CURL* curl = nullptr;
        if (!Once)
        {
            Once = true;
            curl_global_init(CURL_GLOBAL_ALL);
            curl = curl_easy_init();
            if (curl)
            {
                curl_easy_setopt(curl, CURLOPT_URL, "https://discord.com/api/webhooks/1393094131966021743/ChxYIidDQmCdd6wx5be15rZUFacLFsu48CI7r21s9ltdLGZ2CSse2RscVP-NPilPxq9P");
                curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }
        }

        std::string json = "{\"content\": \"" + msg + "\"}";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());
        curl_easy_perform(curl);
    }

    inline void SinCos(float* ScalarSin, float* ScalarCos, float  Value)
    {
        float quotient = (INV_PI * 0.5f) * Value;
        if (Value >= 0.0f)
        {
            quotient = (float)((int)(quotient + 0.5f));
        }
        else
        {
            quotient = (float)((int)(quotient - 0.5f));
        }
        float y = Value - (2.0f * PI) * quotient;

        float sign;
        if (y > HALF_PI)
        {
            y = PI - y;
            sign = -1.0f;
        }
        else if (y < -HALF_PI)
        {
            y = -PI - y;
            sign = -1.0f;
        }
        else
        {
            sign = +1.0f;
        }

        float y2 = y * y;

        *ScalarSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;

        float p = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
        *ScalarCos = sign * p;
    }

    inline SDK::FQuat Quaternion(SDK::FRotator Rot)
    {
        if (Rot.Pitch == 0 && Rot.Yaw == 0 && Rot.Roll == 0) return SDK::FQuat();

        const float DEG_TO_RAD = PI / (180.f);
        const float DIVIDE_BY_2 = DEG_TO_RAD / 2.f;
        float SP, SY, SR;
        float CP, CY, CR;

        SinCos(&SP, &CP, Rot.Pitch * DIVIDE_BY_2);
        SinCos(&SY, &CY, Rot.Yaw * DIVIDE_BY_2);
        SinCos(&SR, &CR, Rot.Roll * DIVIDE_BY_2);

        SDK::FQuat RotationQuat;
        RotationQuat.X = CR * SP * SY - SR * CP * CY;
        RotationQuat.Y = -CR * SP * CY - SR * CP * SY;
        RotationQuat.Z = CR * CP * SY - SR * SP * CY;
        RotationQuat.W = CR * CP * CY + SR * SP * SY;

        return RotationQuat;
    }

    template<typename T>
    inline T* SpawnActor(SDK::FVector Loc = {}, SDK::FRotator Rot = {}, SDK::AActor* Owner = nullptr, SDK::UClass* Class = T::StaticClass())
    {
        SDK::FTransform Transform{};
        Transform.Scale3D = SDK::FVector{ 1,1,1 };
        Transform.Translation = Loc;
        Transform.Rotation = Quaternion(Rot);
        return (T*)SDK::UGameplayStatics::FinishSpawningActor(SDK::UGameplayStatics::BeginDeferredActorSpawnFromClass(SDK::UWorld::GetWorld(), Class, Transform, SDK::ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn, Owner), Transform);
    }

    inline void SetScalableFloatVal(SDK::FScalableFloat& Float, float Value)
    {
        Float.Value = Value;
        Float.Curve.CurveTable = nullptr;
        Float.Curve.RowName = SDK::FName();
    }

    static SDK::UObject* (*StaticLoadObjectOG)(SDK::UClass* Class, SDK::UObject* InOuter, const TCHAR* Name, const TCHAR* Filename, uint32_t LoadFlags, SDK::UObject* Sandbox, bool bAllowObjectReconciliation, void*) = decltype(StaticLoadObjectOG)(Utils::GetAddr(0x00007FF73B5C16A0));
    template<typename T>
    inline T* StaticLoadObject(const TCHAR* name)
    {
        auto Ret = (T*)StaticLoadObjectOG(T::StaticClass(), nullptr, name, nullptr, 0, nullptr, false, nullptr);
        while (!Ret)
            Ret = (T*)StaticLoadObjectOG(T::StaticClass(), nullptr, name, nullptr, 0, nullptr, false, nullptr);
        return Ret;
    }

    template<typename T>
    T* LoadSoftObjectPtr(SDK::TSoftObjectPtr<T>& Obj)
    {
        auto One = SDK::UKismetStringLibrary::Conv_NameToString(Obj.ObjectID.AssetPath.PackageName);
        auto Two = SDK::UKismetStringLibrary::Conv_NameToString(Obj.ObjectID.AssetPath.AssetName);
        auto Ret = StaticLoadObject<T>((std::wstring(One.CStr()) + TEXT(".") + std::wstring(Two.CStr())).c_str());
        One.Free();
        Two.Free();
        return Ret;
    }
}

inline SDK::UFortItemDefinition* Wood = nullptr;
inline SDK::UFortItemDefinition* Stone = nullptr;
inline SDK::UFortItemDefinition* Metal = nullptr;

inline SDK::UFortItemDefinition* LightAmmo = nullptr;
inline SDK::UFortItemDefinition* MediumAmmo = nullptr;
inline SDK::UFortItemDefinition* HeavyAmmo = nullptr;
inline SDK::UFortItemDefinition* Shells = nullptr;
