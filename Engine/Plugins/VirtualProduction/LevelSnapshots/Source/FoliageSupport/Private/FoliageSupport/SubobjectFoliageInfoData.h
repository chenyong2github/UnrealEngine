// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FoliageSupport/FoliageInfoData.h"
#include "Templates/SubclassOf.h"

class UFoliageType;

class FSubobjectFoliageInfoData : public FFoliageInfoData
{
	TSubclassOf<UFoliageType> Class;
	FName SubobjectName;
	TArray<uint8> SerializedSubobjectData;
	
public:

	void Save(UFoliageType* FoliageSubobject, FFoliageInfo& FoliageInfo, const FCustomVersionContainer& VersionInfo);
	
	UFoliageType* FindOrRecreateSubobject(AInstancedFoliageActor* Outer) const;
	void ApplyTo(UFoliageType* FoliageSubobject, const FCustomVersionContainer& VersionInfo) const;
	void ApplyTo(FFoliageInfo& DataToWriteInto, const FCustomVersionContainer& VersionInfo) const;

	
	friend FArchive& operator<<(FArchive& Ar, FSubobjectFoliageInfoData& Data);
};
