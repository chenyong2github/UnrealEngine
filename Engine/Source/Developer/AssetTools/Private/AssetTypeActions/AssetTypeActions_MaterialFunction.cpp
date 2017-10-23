// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_MaterialFunction.h"
#include "Factories/MaterialFunctionInstanceFactory.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "EditorStyleSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInstance.h"
#include "ThumbnailRendering/SceneThumbnailInfoWithPrimitive.h"
#include "AssetTools.h"
#include "MaterialEditorModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

void FAssetTypeActions_MaterialFunction::GetActions( const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder )
{
	auto Functions = GetTypedWeakObjectPtrs<UMaterialFunctionInterface>(InObjects);

	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	if (MaterialEditorModule.MaterialLayersEnabled())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Material_NewMFI", "Create Function Instance"),
			LOCTEXT("Material_NewMFITooltip", "Creates a parameterized function using this function as a base."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.MaterialInstanceActor"),
			FUIAction(
				FExecuteAction::CreateSP(this, &FAssetTypeActions_MaterialFunction::ExecuteNewMFI, Functions)
			)
		);
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MaterialFunction_FindMaterials", "Find Materials Using This"),
		LOCTEXT("MaterialFunction_FindMaterialsTooltip", "Finds the materials that reference this material function in the content browser."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.GenericFind"),
		FUIAction(
			FExecuteAction::CreateSP( this, &FAssetTypeActions_MaterialFunction::ExecuteFindMaterials, Functions ),
			FCanExecuteAction()
			)
		);
}

void FAssetTypeActions_MaterialFunction::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Function = Cast<UMaterialFunction>(*ObjIt);
		if (Function != NULL)
		{
			IMaterialEditorModule* MaterialEditorModule = &FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
			MaterialEditorModule->CreateMaterialEditor(Mode, EditWithinLevelEditor, Function);
		}
	}
}

void FAssetTypeActions_MaterialFunction::ExecuteNewMFI(TArray<TWeakObjectPtr<UMaterialFunctionInterface>> Objects)
{
	const FString DefaultSuffix = TEXT("_Inst");

	if ( Objects.Num() == 1 )
	{
		auto Object = Objects[0].Get();

		if ( Object )
		{
			// Create an appropriate and unique name 
			FString Name;
			FString PackageName;
			CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

			UMaterialFunctionInstanceFactory* Factory = NewObject<UMaterialFunctionInstanceFactory>();
			Factory->InitialParent = Object;
			
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackageName), UMaterialFunctionInstance::StaticClass(), Factory);
		}
	}
	else
	{
		TArray<UObject*> ObjectsToSync;
		for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
		{
			auto Object = (*ObjIt).Get();
			if ( Object )
			{
				// Determine an appropriate name
				FString Name;
				FString PackageName;
				CreateUniqueAssetName(Object->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

				// Create the factory used to generate the asset
				UMaterialFunctionInstanceFactory* Factory = NewObject<UMaterialFunctionInstanceFactory>();
				Factory->InitialParent = Object;

				FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
				UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UMaterialFunctionInstance::StaticClass(), Factory);

				if ( NewAsset )
				{
					ObjectsToSync.Add(NewAsset);
				}
			}
		}

		if ( ObjectsToSync.Num() > 0 )
		{
			FAssetTools::Get().SyncBrowserToAssets(ObjectsToSync);
		}
	}
}

void FAssetTypeActions_MaterialFunction::ExecuteFindMaterials(TArray<TWeakObjectPtr<UMaterialFunctionInterface>> Objects)
{
	TArray<UObject*> ObjectsToSync;

	for (auto ObjIt = Objects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto Object = (*ObjIt).Get();
		if ( Object )
		{
			// @todo This only considers loaded materials! Find a good way to make this use the asset registry.
			for (TObjectIterator<UMaterial> It; It; ++It)
			{
				UMaterial* CurrentMaterial = *It;

				for (int32 FunctionIndex = 0; FunctionIndex < CurrentMaterial->MaterialFunctionInfos.Num(); FunctionIndex++)
				{
					if (CurrentMaterial->MaterialFunctionInfos[FunctionIndex].Function == Object)
					{
						ObjectsToSync.Add(CurrentMaterial);
						break;
					}
				}
			}
		}
	}

	if (ObjectsToSync.Num() > 0)
	{
		FAssetTools::Get().SyncBrowserToAssets(ObjectsToSync);
	}
}

UThumbnailInfo* FAssetTypeActions_MaterialFunction::GetThumbnailInfo(UObject* Asset) const
{
	if (UMaterialFunctionInterface* MaterialFunc = CastChecked<UMaterialFunctionInterface>(Asset))
	{
		if (!MaterialFunc->ThumbnailInfo)
		{
			MaterialFunc->ThumbnailInfo = NewObject<USceneThumbnailInfoWithPrimitive>(MaterialFunc, NAME_None, RF_Transactional);
		}
	}

	return nullptr;
}

UClass* FAssetTypeActions_MaterialFunctionLayer::GetSupportedClass() const
{
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	UClass* SupportedClass = MaterialEditorModule.MaterialLayersEnabled() ? UMaterialFunctionMaterialLayer::StaticClass() : nullptr;
	return SupportedClass;
}

bool FAssetTypeActions_MaterialFunctionLayer::CanFilter()
{
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	return MaterialEditorModule.MaterialLayersEnabled();
}

UClass* FAssetTypeActions_MaterialFunctionLayerBlend::GetSupportedClass() const
{
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	UClass* SupportedClass = MaterialEditorModule.MaterialLayersEnabled() ? UMaterialFunctionMaterialLayerBlend::StaticClass() : nullptr;
	return SupportedClass;
}

bool FAssetTypeActions_MaterialFunctionLayerBlend::CanFilter()
{
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	return MaterialEditorModule.MaterialLayersEnabled();
}

#undef LOCTEXT_NAMESPACE
