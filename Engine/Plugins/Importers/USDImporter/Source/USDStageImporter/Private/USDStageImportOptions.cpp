// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportOptions.h"

#include "UObject/UnrealType.h"

UUsdStageImportOptions::UUsdStageImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bImportActors = true;
	bImportGeometry = true;
	bImportMaterials = true;
	bImportLights = true;
	bImportCameras = true;
	bImportAnimations = true;
	bImportProperties = true;

	PurposesToImport = (int32) (EUsdPurpose::Default | EUsdPurpose::Proxy | EUsdPurpose::Render | EUsdPurpose::Guide);
	ImportTime = 0.0f;
	MetersPerUnit = 0.01;

	bGenerateUniquePathPerUSDPrim = false;
	bGenerateUniqueMeshes = false;
	MaterialSearchLocation = EMaterialSearchLocation::DoNotSearch;
	ExistingActorPolicy = EReplaceActorPolicy::Replace;
	ExistingAssetPolicy = EReplaceAssetPolicy::Replace;

	bApplyWorldTransformToGeometry = false;
	bFlattenHierarchy = false;
	bCollapse = true;
}

void UUsdStageImportOptions::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		SaveConfig();
	}
}
