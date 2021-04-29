// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/RPCDoSDetectionConfig.h"


/**
 * URPCDoSDetectionConfig
 */
URPCDoSDetectionConfig::URPCDoSDetectionConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URPCDoSDetectionConfig::OverridePerObjectConfigSection(FString& SectionName)
{
	SectionName = GetName() + TEXT(" ") + GetConfigSectionName();
}

URPCDoSDetectionConfig* URPCDoSDetectionConfig::Get(FName NetDriverName)
{
	UClass* ClassRef = URPCDoSDetectionConfig::StaticClass();
	URPCDoSDetectionConfig* ReturnVal = FindObject<URPCDoSDetectionConfig>(ClassRef, *NetDriverName.ToString());

	if (ReturnVal == nullptr)
	{
		ReturnVal = NewObject<URPCDoSDetectionConfig>(ClassRef, NetDriverName);
	}

	return ReturnVal;
}

const TCHAR* URPCDoSDetectionConfig::GetConfigSectionName()
{
	return TEXT("RPCDoSDetection");
}
