#pragma once
#include "CoreMinimal.h"
#include "AssetPreferencesHandler.h"


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