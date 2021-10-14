// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/SubobjectFoliageInfoData.h"

#include "LevelSnapshotsLog.h"

#include "InstancedFoliageActor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

void FSubobjectFoliageInfoData::Save(UFoliageType* FoliageSubobject, FFoliageInfo& FoliageInfo, const FCustomVersionContainer& VersionInfo)
{
	FFoliageInfoData::Save(FoliageInfo, VersionInfo);

	Class = FoliageSubobject->GetClass();
	SubobjectName = FoliageSubobject->GetFName();
	
	FMemoryWriter MemoryWriter(SerializedSubobjectData, true);
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryWriter, false);
	RootArchive.SetCustomVersions(VersionInfo);
	FoliageSubobject->Serialize(RootArchive);
}

UFoliageType* FSubobjectFoliageInfoData::FindOrRecreateSubobject(AInstancedFoliageActor* Outer) const
{
	if (UObject* FoundObject = StaticFindObjectFast(nullptr, Outer, SubobjectName))
	{
		UE_CLOG(FoundObject->GetClass() != Class, LogLevelSnapshots, Warning, TEXT("Name collision for foliage type %s"), *FoundObject->GetPathName());
		return Cast<UFoliageType>(FoundObject);
	}

	return NewObject<UFoliageType>(Outer, Class, SubobjectName);
}

void FSubobjectFoliageInfoData::ApplyTo(UFoliageType* FoliageSubobject, const FCustomVersionContainer& VersionInfo) const
{
	FMemoryReader MemoryReader(SerializedSubobjectData, true);
	constexpr bool bLoadIfFindFails = true;
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryReader, bLoadIfFindFails);
	FoliageSubobject->Serialize(RootArchive);
}

void FSubobjectFoliageInfoData::ApplyTo(FFoliageInfo& DataToWriteInto, const FCustomVersionContainer& VersionInfo) const
{
	FFoliageInfoData::ApplyTo(DataToWriteInto, VersionInfo);
}

FArchive& operator<<(FArchive& Ar, FSubobjectFoliageInfoData& Data)
{
	Ar << static_cast<FFoliageInfoData&>(Data);
	Ar << Data.Class;
	Ar << Data.SubobjectName;
	Ar << Data.SerializedSubobjectData;
	return Ar;
}
