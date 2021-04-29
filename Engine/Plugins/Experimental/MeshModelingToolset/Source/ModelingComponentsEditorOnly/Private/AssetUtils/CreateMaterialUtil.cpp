// Copyright Epic Games, Inc. All Rights Reserved. 

#include "AssetUtils/CreateMaterialUtil.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"

#include "Misc/Paths.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "FileHelpers.h"


UE::AssetUtils::ECreateMaterialResult UE::AssetUtils::CreateDuplicateMaterial(
	UMaterialInterface* BaseMaterial,
	FMaterialAssetOptions& Options,
	FMaterialAssetResults& ResultsOut)
{
	// Duplicate operation will create a new Package, so calling code should not!
	if (ensure(Options.UsePackage == nullptr) == false)
	{
		return ECreateMaterialResult::InvalidPackage;
	}

	if (ensure(BaseMaterial) == false)
	{
		return ECreateMaterialResult::InvalidBaseMaterial;
	}
	UMaterial* SourceMaterial = BaseMaterial->GetBaseMaterial();
	if (ensure(SourceMaterial) == false)
	{
		return ECreateMaterialResult::InvalidBaseMaterial;
	}

	// cannot create new package if path already exists
	if (FPackageName::DoesPackageExist(Options.NewAssetPath, nullptr, nullptr))
	{
		ensure(false);
		return ECreateMaterialResult::NameError;
	}

	FString DestinationLongPackagePath = FPackageName::GetLongPackagePath(Options.NewAssetPath);
	//FString DestinationObjectName = FPackageName::ObjectPathToObjectName(Options.NewAssetPath);
	FString DestinationObjectName = FPackageName::GetLongPackageAssetName(Options.NewAssetPath);

	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.DuplicateAsset(DestinationObjectName, DestinationLongPackagePath, SourceMaterial);
	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);

	if ( ensure(NewMaterial) == false )
	{
		return ECreateMaterialResult::DuplicateFailed;
	}

	if (Options.bDeferPostEditChange == false)
	{
		NewMaterial->PostEditChange();
	}

	ResultsOut.NewMaterial = NewMaterial;
	return ECreateMaterialResult::Ok;
}

