// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/FoliageInfoData.h"

#include "InstancedFoliageActor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

void FFoliageInfoData::Save(FFoliageInfo& DataToReadFrom, const FCustomVersionContainer& VersionInfo)
{
	FMemoryWriter MemoryWriter(SerializedData, true);
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryWriter, false);
	RootArchive.SetCustomVersions(VersionInfo);
	
	ImplType = DataToReadFrom.Type;
	ComponentName = DataToReadFrom.GetComponent() ? DataToReadFrom.GetComponent()->GetFName() : FName(NAME_None);
	RootArchive << DataToReadFrom;
}

void FFoliageInfoData::ApplyTo(FFoliageInfo& DataToWriteInto, const FCustomVersionContainer& VersionInfo) const
{
	FMemoryReader MemoryReader(SerializedData, true);
	FObjectAndNameAsStringProxyArchive RootArchive(MemoryReader, false);
	RootArchive.SetCustomVersions(VersionInfo);

	// Avoid foliage internal checks
	if (DataToWriteInto.IsInitialized())
	{
		DataToWriteInto.Implementation.Reset();
	}
	
	RootArchive << DataToWriteInto;
	DataToWriteInto.RecomputeHash();
}

FArchive& operator<<(FArchive& Ar, FFoliageInfoData& Data)
{
	Ar << Data.ImplType;
	Ar << Data.ComponentName;
	Ar << Data.SerializedData;
	return Ar;
}