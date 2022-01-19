// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"



struct FDHIData {
	FString CharacterPath;
	FString CommonPath;
	FString RootPath;
	FString CharacterName;
};



class FImportDHI

{

private:
	FImportDHI() = default;
	static TSharedPtr<FImportDHI> ImportDHIInst;
	TSharedPtr<FDHIData> ParseDHIData(TSharedPtr<FJsonObject> AssetImportJson);

public:
	static TSharedPtr<FImportDHI> Get();
	void ImportAsset(TSharedPtr<FJsonObject> AssetImportJson);
};