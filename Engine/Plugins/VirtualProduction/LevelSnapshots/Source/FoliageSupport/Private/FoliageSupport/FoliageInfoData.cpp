// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/FoliageInfoData.h"

#include "InstancedFoliageActor.h"
#include "LevelSnapshotsLog.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

namespace UE::LevelSnapshots::Foliage::Private
{
	static FString AsName(EFoliageImplType ImplType)
	{
		switch(ImplType)
		{
		case EFoliageImplType::StaticMesh:
			return FString("StaticMesh");
		case EFoliageImplType::Actor:
			return FString("Actor");
		case EFoliageImplType::ISMActor:
			return FString("ISMActor");
			
		case EFoliageImplType::Unknown: 
			return FString("Unknown");
		default:
			ensureMsgf(false, TEXT("Update this switch case."));
			return FString("NewType (Update FoliageInfoData.cpp)");
		}
	}
}

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
	
	UE_CLOG(DataToReadFrom.GetComponent(), LogLevelSnapshots, Warning, TEXT("Could not save component for ImplType %s"), *AsName(ImplType));
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