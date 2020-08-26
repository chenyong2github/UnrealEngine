// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "AssetImportData.h"
#include "Materials/MaterialInstanceConstant.h"

class FMaterialUtils
{
public:
	static UMaterialInstanceConstant* CreateInstanceMaterial(const FString& MasterMaterialPath, const FString& InstanceDestination, const FString& AssetName);
	

};