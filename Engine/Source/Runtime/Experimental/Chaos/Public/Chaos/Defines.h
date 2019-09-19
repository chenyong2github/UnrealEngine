// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#ifndef COMPILE_ID_TYPES_AS_INTS
#define COMPILE_ID_TYPES_AS_INTS 0
#endif

#include <functional>
#if COMPILE_WITHOUT_UNREAL_SUPPORT
	#include <stdint.h>
#else
	#include "CoreTypes.h"
	#include "Logging/MessageLog.h"
	#include "Misc/CoreMiscDefines.h"
#endif
#include "Serializable.h"

#if COMPILE_WITHOUT_UNREAL_SUPPORT
#define PI 3.14159
#define check(condition)
typedef int32_t int32;
#endif


namespace Chaos
{
#if COMPILE_ID_TYPES_AS_INTS
typedef uint32 IslandId;

static FORCEINLINE uint32 ToValue(uint32 Id) { return Id; }
#else
#define CREATEIDTYPE(IDNAME) \
    class IDNAME \
    { \
      public: \
        IDNAME() {} \
        IDNAME(const uint32 InValue) : Value(InValue) {} \
        bool operator==(const IDNAME& Other) const { return Value == Other.Value; } \
        uint32 Value; \
    }

CREATEIDTYPE(IslandId);

template<class T_ID>
static uint32 ToValue(T_ID Id)
{
    return Id.Value;
}
#endif
template<class T>
class TChaosPhysicsMaterial
{
public:
	T Friction;
	T Restitution;
	T SleepingLinearThreshold;
	T SleepingAngularThreshold;
	T DisabledLinearThreshold;
	T DisabledAngularThreshold;

	TChaosPhysicsMaterial()
		: Friction((T)0.5)
		, Restitution((T)0.1)
		, SleepingLinearThreshold((T)1)
		, SleepingAngularThreshold((T)1)
		, DisabledLinearThreshold((T)0)
		, DisabledAngularThreshold((T)0)
	{
	}

	static constexpr bool IsSerializablePtr = true;

	static void StaticSerialize(FArchive& Ar, TSerializablePtr<TChaosPhysicsMaterial<T>>& Serializable)
	{
		TChaosPhysicsMaterial<T>* Material = const_cast<TChaosPhysicsMaterial<T>*>(Serializable.Get());
		
		if (Ar.IsLoading())
		{
			Material = new TChaosPhysicsMaterial<T>();
			Serializable.SetFromRawLowLevel(Material);
		}
		
		Material->Serialize(Ar);
	}

	void Serialize(FArchive& Ar)
	{
		Ar << Friction << Restitution << SleepingLinearThreshold << SleepingAngularThreshold << DisabledLinearThreshold << DisabledAngularThreshold;
	}
};


template <typename T>
FORCEINLINE FArchive& operator<<(FArchive& Ar, TChaosPhysicsMaterial<T>& Value)
{
	Value.Serialize(Ar);
	return Ar;
}
}