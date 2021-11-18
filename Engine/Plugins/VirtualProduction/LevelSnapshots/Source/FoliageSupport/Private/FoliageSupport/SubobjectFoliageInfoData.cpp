// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/SubobjectFoliageInfoData.h"

#include "LevelSnapshotsLog.h"

#include "InstancedFoliageActor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

FArchive& UE::LevelSnapshots::Foliage::Private::FSubobjectFoliageInfoData::SerializeInternal(FArchive& Ar)
{
	Ar << static_cast<FFoliageInfoData&>(*this);
	Ar << Class;
	Ar << SubobjectName;
	Ar << SerializedSubobjectData;
	return Ar;
}

void UE::LevelSnapshots::Foliage::Private::FSubobjectFoliageInfoData::Save(UFoliageType* FoliageSubobject, FFoliageInfo& FoliageInfo, const FCustomVersionContainer& VersionInfo)
{
	FFoliageInfoData::Save(FoliageInfo, VersionInfo);

	Class = FoliageSubobject->GetClass();
	SubobjectName = FoliageSubobject->GetFName();
	
	FMemoryWriter MemoryWriter(SerializedSubobjectData, true);
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryWriter, false);
	RootArchive.SetCustomVersions(VersionInfo);
	FoliageSubobject->Serialize(RootArchive);
}

UFoliageType* UE::LevelSnapshots::Foliage::Private::FSubobjectFoliageInfoData::FindOrRecreateSubobject(AInstancedFoliageActor* Outer) const
{
	if (UObject* FoundObject = StaticFindObjectFast(nullptr, Outer, SubobjectName))
	{
		UE_CLOG(FoundObject->GetClass() != Class, LogLevelSnapshots, Warning, TEXT("Name collision for foliage type %s"), *FoundObject->GetPathName());
		return Cast<UFoliageType>(FoundObject);
	}

	return NewObject<UFoliageType>(Outer, Class, SubobjectName);
}

void UE::LevelSnapshots::Foliage::Private::FSubobjectFoliageInfoData::ApplyTo(UFoliageType* FoliageSubobject, const FCustomVersionContainer& VersionInfo) const
{
	FMemoryReader MemoryReader(SerializedSubobjectData, true);
	constexpr bool bLoadIfFindFails = true;
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryReader, bLoadIfFindFails);
	FoliageSubobject->Serialize(RootArchive);
}

void UE::LevelSnapshots::Foliage::Private::FSubobjectFoliageInfoData::ApplyTo(FFoliageInfo& DataToWriteInto, const FCustomVersionContainer& VersionInfo) const
{
	FFoliageInfoData::ApplyTo(DataToWriteInto, VersionInfo);
}
