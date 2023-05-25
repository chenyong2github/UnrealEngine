// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothTrainingToolProperties.h"

#include "ChaosClothAsset/ClothTrainingTool.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ContentBrowserModule.h"
#include "GeometryCache.h"
#include "IContentBrowserSingleton.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "ClothTrainingToolProperties"

namespace UE::ClothTrainingTool::Private
{
	TOptional<FString> ExportGeometryCacheDialog(const UObject& ClothAsset)
	{
		FSaveAssetDialogConfig Config;
		{
			const FString PackageName = ClothAsset.GetOutermost()->GetName();
			Config.DefaultPath = FPackageName::GetLongPackagePath(PackageName);
			Config.DefaultAssetName = FString::Printf(TEXT("GC_%s"), *ClothAsset.GetName());
			Config.AssetClassNames.Add(UGeometryCache::StaticClass()->GetClassPathName());
			Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
			Config.DialogTitleOverride = LOCTEXT("ExportGeometryCacheDialogTitle", "Export Geometry Cache As");
		}
	
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	
		FString NewPackageName;
		FText OutError;
		for (bool bFilenameValid = false; !bFilenameValid; bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError))
		{
			const FString AssetPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(Config);
			if (AssetPath.IsEmpty())
			{
				return TOptional<FString>();
			}
			NewPackageName = FPackageName::ObjectPathToPackageName(AssetPath);
		}
		return NewPackageName;
	}
}

void UClothTrainingToolProperties::SetSimulatedCacheName()
{
	if (const UObject* ClothAsset = GetClothAsset())
	{
		using UE::ClothTrainingTool::Private::ExportGeometryCacheDialog;
		if (TOptional<FString> OptionalName = ExportGeometryCacheDialog(*ClothAsset); OptionalName.IsSet())
		{
			SimulatedCacheName = OptionalName.GetValue();
		}
	}
}

void UClothTrainingToolProperties::SetDebugCacheName()
{
	if (const UObject* ClothAsset = GetClothAsset())
	{
		using UE::ClothTrainingTool::Private::ExportGeometryCacheDialog;
		if (TOptional<FString> OptionalName = ExportGeometryCacheDialog(*ClothAsset); OptionalName.IsSet())
		{
			DebugCacheName = OptionalName.GetValue();
		}
	}
}

UObject* UClothTrainingToolProperties::GetClothAsset()
{
	if (const UClothTrainingTool* Tool = Cast<UClothTrainingTool>(GetOuter()))
	{
		if (Tool->ClothComponent)
		{
			return Tool->ClothComponent->GetClothAsset();
		}
	}
	return nullptr;
}

void UClothTrainingToolActionProperties::Initialize(UClothTrainingTool* ParentToolIn)
{
	ParentTool = ParentToolIn;
}

void UClothTrainingToolActionProperties::StartGenerating()
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(EClothTrainingToolActions::StartTrain);
	}
}

#undef LOCTEXT_NAMESPACE