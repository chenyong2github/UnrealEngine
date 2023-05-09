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
			
			check(ActorDescAr.ClassDesc || Ar.IsSaving());
			check(ActorDescAr.ActorDesc);

			Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

			auto GetClassDefaultValue = [&V, &ActorDescAr]() -> const T*
			{
				const UPTRINT PropertyOffset = (UPTRINT)&V.Value - *(UPTRINT*)&ActorDescAr.ActorDesc;

				if (PropertyOffset < ActorDescAr.ClassDescSizeof)
				{
					check((PropertyOffset + sizeof(V)) <= ActorDescAr.ClassDescSizeof);
					const T* RefValue = (const T*)(*(UPTRINT*)&ActorDescAr.ClassDesc + PropertyOffset);
					return RefValue;
				}

				check(ActorDescAr.bIsMissingClassDesc);
				return nullptr;
			};

			uint8 bSerialize = 1;

			if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) >= FFortniteMainBranchObjectVersion::WorldPartitionActorClassDescSerialize)
			{
				if (Ar.IsSaving() && ActorDescAr.ClassDesc)
				{
					// When saving, we expect the class descriptor to be the exact type as what we are serializing.
					const T* ClassDefaultValue = GetClassDefaultValue();
					check(ClassDefaultValue);

					bSerialize = (V.Value != *ClassDefaultValue) ? 1 : 0;
				}

				Ar << bSerialize;
			}

			if (bSerialize)
			{
				Ar << V.Value;
			}
			else if (Ar.IsLoading())
			{
				// When loading, we need to handle a different class descriptor in case of missing classes, etc.
				if (const T* ClassDefaultValue = GetClassDefaultValue())
				{
					V.Value = *ClassDefaultValue;
				}
			}

			return Ar;
		}

		T& Value;
	};

	FWorldPartitionActorDesc* ActorDesc;
	const FWorldPartitionActorDesc* ClassDesc;
	uint32 ClassDescSizeof;
	bool bIsMissingClassDesc;
};

template <typename T>
using TDeltaSerialize = FActorDescArchive::TDeltaSerializer<T>;
#endif