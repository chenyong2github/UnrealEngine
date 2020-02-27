// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if COMPILE_WITHOUT_UNREAL_SUPPORT
	#include <stdint.h>
#else
	#include "CoreTypes.h"
	#include "Logging/MessageLog.h"
	#include "Misc/CoreMiscDefines.h"
#endif
#include "Serializable.h"

#include "UObject/ExternalPhysicsCustomObjectVersion.h"
#include "Chaos/Core.h"

#if COMPILE_WITHOUT_UNREAL_SUPPORT
#define PI 3.14159
#define check(condition)
typedef int32_t int32;
#endif

namespace Chaos
{
	class FChaosPhysicsMaterial
	{
	public:

		Chaos::FReal Friction;
		Chaos::FReal Restitution;
		Chaos::FReal SleepingLinearThreshold;
		Chaos::FReal SleepingAngularThreshold;
		Chaos::FReal DisabledLinearThreshold;
		Chaos::FReal DisabledAngularThreshold;
		int32 SleepCounterThreshold;
		void* UserData;

		FChaosPhysicsMaterial()
			: Friction(0.5)
			, Restitution(0.1)
			, SleepingLinearThreshold(1)
			, SleepingAngularThreshold(1)
			, DisabledLinearThreshold(0)
			, DisabledAngularThreshold(0)
			, SleepCounterThreshold(0)
			, UserData(nullptr)
		{
		}

		static constexpr bool IsSerializablePtr = true;

		static void StaticSerialize(FArchive& Ar, TSerializablePtr<FChaosPhysicsMaterial>& Serializable)
		{
			FChaosPhysicsMaterial* Material = const_cast<FChaosPhysicsMaterial*>(Serializable.Get());

			if(Ar.IsLoading())
			{
				Material = new FChaosPhysicsMaterial();
				Serializable.SetFromRawLowLevel(Material);
			}

			Material->Serialize(Ar);
		}

		void Serialize(FArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);

			Ar << Friction << Restitution << SleepingLinearThreshold << SleepingAngularThreshold << DisabledLinearThreshold << DisabledAngularThreshold;

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::PhysicsMaterialSleepCounterThreshold)
			{
				Ar << SleepCounterThreshold;
			}
		}
	};

	class FChaosPhysicsMaterialMask
	{
	public:

		int32 SizeX;
		int32 SizeY;
		TArray<uint32> MaskData;

		int32 UVChannelIndex;
		int32 AddressX;
		int32 AddressY;

		FChaosPhysicsMaterialMask()
			: SizeX(0)
			, SizeY(0)
			, MaskData()
			, UVChannelIndex(0)
			, AddressX(0)
			, AddressY(0)
		{
		}

		static constexpr bool IsSerializablePtr = true;

		static void StaticSerialize(FArchive& Ar, TSerializablePtr<FChaosPhysicsMaterialMask>& Serializable)
		{
			FChaosPhysicsMaterialMask* MaterialMask = const_cast<FChaosPhysicsMaterialMask*>(Serializable.Get());

			if (Ar.IsLoading())
			{
				MaterialMask = new FChaosPhysicsMaterialMask();
				Serializable.SetFromRawLowLevel(MaterialMask);
			}

			MaterialMask->Serialize(Ar);
		}

		void Serialize(FArchive& Ar)
		{
			Ar << SizeX << SizeY << UVChannelIndex << AddressX << AddressY << MaskData;
		}
	};
}