// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Serialization/Archive.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

class FWorldPartitionActorDesc;

class FActorDescArchive : public FArchiveProxy
{
public:
	FActorDescArchive(FArchive& InArchive, FWorldPartitionActorDesc* InActorDesc);

	//~ Begin FArchive Interface
	virtual FArchive& operator<<(FText& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(UObject*& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FLazyObjectPtr& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FObjectPtr& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FSoftObjectPath& Value) override { unimplemented(); return *this; }
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override { unimplemented(); return *this; }
	//~ End FArchive Interface

	template <typename T>
	struct TDeltaSerializer
	{
		explicit TDeltaSerializer(T& InValue)
			: Value(InValue)
		{}

		friend FArchive& operator<<(FArchive& Ar, const TDeltaSerializer<T>& V)
		{
			FActorDescArchive& ActorDescAr = (FActorDescArchive&)Ar;
			
			check(ActorDescAr.ClassDesc);
			check(ActorDescAr.ActorDesc);

			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

			auto GetClassDefaultValue = [&V, &ActorDescAr]() -> const T&
			{
				const UPTRINT PropertyOffset = (UPTRINT)&V.Value - *(UPTRINT*)&ActorDescAr.ActorDesc;
				const T& RefValue = *(const T*)(*(UPTRINT*)&ActorDescAr.ClassDesc + PropertyOffset);
				return RefValue;
			};

			uint8 bSerialize = 1;

			if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::WorldPartitionActorClassDescSerialize)
			{
				if (Ar.IsSaving())
				{
					bSerialize = (V.Value != GetClassDefaultValue()) ? 1 : 0;
				}

				Ar << bSerialize;
			}

			if (bSerialize)
			{
				Ar << V.Value;
			}
			else if (Ar.IsLoading())
			{
				V.Value = GetClassDefaultValue();
			}

			return Ar;
		}

		T& Value;
	};

	FWorldPartitionActorDesc* ActorDesc;
	const FWorldPartitionActorDesc* ClassDesc;
};

template <typename T>
using TDeltaSerialize = FActorDescArchive::TDeltaSerializer<T>;
#endif