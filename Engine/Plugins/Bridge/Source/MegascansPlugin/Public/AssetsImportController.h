// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"

#include "MSAssetImportData.h"
#include "Materials/MaterialInstanceConstant.h"

class FAssetsImportController

{
private:
	FAssetsImportController() = default;
	static TSharedPtr<FAssetsImportController> AssetsImportController;
	

public:	
	static TSharedPtr<FAssetsImportController> Get();
	void DataReceived(const FString DataFromBridge);
	


};