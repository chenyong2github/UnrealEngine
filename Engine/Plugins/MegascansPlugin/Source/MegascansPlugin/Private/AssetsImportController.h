// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"



class FAssetsImportController

{
private:
	FAssetsImportController() = default;
	static TSharedPtr<FAssetsImportController> AssetsImportController;
	void ImportAssets(const FString & AssetsImportJson);

public:	
	static TSharedPtr<FAssetsImportController> Get();
	void DataReceived(const FString DataFromBridge);

};