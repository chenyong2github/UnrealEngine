// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/FoliageInfoData.h"

#include "InstancedFoliageActor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

FArchive& UE::LevelSnapshots::Foliage::Private::FFoliageInfoData::SerializeInternal(FArchive& Ar)
{
	Ar << ImplType;
	Ar << ComponentName;
	Ar << SerializedData;
	return Ar;
}

void UE::LevelSnapshots::Foliage::Private::FFoliageInfoData::Save(FFoliageInfo& DataToReadFrom, const FCustomVersionContainer& VersionInfo)
{
	FMemoryWriter MemoryWriter(SerializedData, true);
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryWriter, false);
	RootArchive.SetCustomVersions(VersionInfo);
	
	ImplType = DataToReadFrom.Type;
	ComponentName = DataToReadFrom.GetComponent() ? DataToReadFrom.GetComponent()->GetFName() : FName(NAME_None);
	RootArchive << DataToReadFrom;
}

void UE::LevelSnapshots::Foliage::Private::FFoliageInfoData::ApplyTo(FFoliageInfo& DataToWriteInto, const FCustomVersionContainer& VersionInfo) const
{
	FMemoryReader MemoryReader(SerializedData, true);
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryReader, false);
	RootArchive.SetCustomVersions(VersionInfo);
	
	// Avoid foliage internal checks
	DataToWriteInto.Implementation.Reset();
	
	RootArchive << DataToWriteInto;
	DataToWriteInto.RecomputeHash();
}