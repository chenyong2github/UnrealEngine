// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDImportOptions.h"
#include "UObject/UnrealType.h"

UDEPRECATED_UUSDImportOptions::UDEPRECATED_UUSDImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MeshImportType = EUsdMeshImportType::StaticMesh;
	bApplyWorldTransformToGeometry = true;
	Scale = 1.0;
}

void UDEPRECATED_UUSDImportOptions::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		SaveConfig();
	}
}

UDEPRECATED_UUSDSceneImportOptions::UDEPRECATED_UUSDSceneImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PurposesToImport = (int32) (EUsdPurpose::Default | EUsdPurpose::Proxy);
	bFlattenHierarchy = true;
	bImportMeshes = true;
	PathForAssets.Path = TEXT("/Game");
	bGenerateUniqueMeshes = true;
	bGenerateUniquePathPerUSDPrim = true;
	bApplyWorldTransformToGeometry = false;
}

void UDEPRECATED_UUSDSceneImportOptions::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UDEPRECATED_UUSDSceneImportOptions::CanEditChange(const FProperty* InProperty) const
{
	bool bCanEdit = Super::CanEditChange(InProperty);

	FName PropertyName = InProperty ? InProperty->GetFName() : NAME_None;

	if (GET_MEMBER_NAME_CHECKED(UDEPRECATED_UUSDImportOptions, MeshImportType) == PropertyName ||
		GET_MEMBER_NAME_CHECKED(UDEPRECATED_UUSDImportOptions, bApplyWorldTransformToGeometry) == PropertyName ||
		GET_MEMBER_NAME_CHECKED(UDEPRECATED_UUSDImportOptions, bGenerateUniquePathPerUSDPrim) == PropertyName)
	{
		bCanEdit &= bImportMeshes;
	}

	return bCanEdit;
}

UDEPRECATED_UUSDBatchImportOptions::UDEPRECATED_UUSDBatchImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bImportMeshes = true;
	PathForAssets.Path = TEXT("/Game");
	bGenerateUniqueMeshes = true;
	bGenerateUniquePathPerUSDPrim = true;
	bApplyWorldTransformToGeometry = false;
}

void UDEPRECATED_UUSDBatchImportOptions::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool UDEPRECATED_UUSDBatchImportOptions::CanEditChange(const FProperty* InProperty) const
{
	bool bCanEdit = Super::CanEditChange(InProperty);

	FName PropertyName = InProperty ? InProperty->GetFName() : NAME_None;

	if (GET_MEMBER_NAME_CHECKED(UDEPRECATED_UUSDImportOptions, MeshImportType) == PropertyName ||
		GET_MEMBER_NAME_CHECKED(UDEPRECATED_UUSDImportOptions, bApplyWorldTransformToGeometry) == PropertyName ||
		GET_MEMBER_NAME_CHECKED(UDEPRECATED_UUSDImportOptions, bGenerateUniquePathPerUSDPrim) == PropertyName)
	{
		bCanEdit &= bImportMeshes;
	}

	return bCanEdit;
}

UDEPRECATED_UUSDBatchImportOptionsSubTask::UDEPRECATED_UUSDBatchImportOptionsSubTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}