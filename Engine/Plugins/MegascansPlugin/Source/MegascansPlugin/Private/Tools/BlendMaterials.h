// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

class FMaterialBlend
{
private:
	FMaterialBlend() = default;
	static TSharedPtr<FMaterialBlend> MaterialBlendInst;
	FString DBlendDestinationPath = TEXT("/Game/BlendMaterials");
	FString DBlendInstanceName = TEXT("BlendMaterial");
	FString DBlendMasterMaterial = TEXT("SurfaceBlend_MasterMaterial");

	//TArray<FString> BlendSets = { "Base", "R", "G", "B", "A" };
	TArray<FString> BlendSets = { "Base", "Middle", "Top" };
	//TArray<FString> SupportedMapTypes = { "albedo", "normal", "roughness", "specular", "gloss", "displacement", "opacity", "translucency" };
	TArray<FString> SupportedMapTypes = { "albedo", "normal", "roughness", "displacement", "ao" };

	bool ValidateSelectedAssets(TArray<FString> SelectedMaterials, FString& Failure);

public:
	static TSharedPtr<FMaterialBlend> Get();
	void BlendSelectedMaterials();

};