// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSceneImportFactory.h"
#include "USDImportOptions.h"
#include "ScopedTransaction.h"
#include "IAssetRegistry.h"
#include "AssetRegistryModule.h"
#include "IUSDImporterModule.h"
#include "USDConversionUtils.h"
#include "ObjectTools.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageTools.h"
#include "Editor.h"
#include "PropertySetter.h"
#include "AssetSelection.h"
#include "JsonObjectConverter.h"
#include "USDPrimResolver.h"

#define LOCTEXT_NAMESPACE "USDImportPlugin"

UUSDSceneImportFactory::UUSDSceneImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UWorld::StaticClass();

	bEditorImport = true;
	bText = false;

	ImportOptions = ObjectInitializer.CreateDefaultSubobject<UUSDSceneImportOptions>(this, TEXT("USDSceneImportOptions"));

	Formats.Add(TEXT("usd;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usda;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usdc;Universal Scene Descriptor files"));
}

UObject* UUSDSceneImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UUSDImporter* USDImporter = IUSDImporterModule::Get().GetImporter();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<UObject*> AllAssets;

	if(IsAutomatedImport() || USDImporter->ShowImportOptions(*ImportOptions))
	{
#if USE_USD_SDK
		// @todo: Disabled.  This messes with the ability to replace existing actors since actors with this name could still be in the transaction buffer
		//FScopedTransaction ImportUSDScene(LOCTEXT("ImportUSDSceneTransaction", "Import USD Scene"));

		TUsdStore< pxr::UsdStageRefPtr > Stage = USDImporter->ReadUsdFile(ImportContext, Filename);
		if (*Stage)
		{
			ImportContext.Init(InParent, InName.ToString(), Stage);
			ImportContext.ImportOptions = ImportOptions;
			ImportContext.bIsAutomated = IsAutomatedImport();

			if (IsAutomatedImport() && InParent && ImportOptions->PathForAssets.Path == TEXT("/Game"))
			{
				ImportOptions->PathForAssets.Path = ImportContext.ImportPathName;
			}

			ImportContext.ImportPathName = ImportOptions->PathForAssets.Path;
	
			// Actors will have the transform
			ImportContext.bApplyWorldTransformToGeometry = false;

			USDImporter->ImportUsdStage(ImportContext);
		}

		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, ImportContext.World);

		GEditor->BroadcastLevelActorListChanged();

		ImportContext.DisplayErrorMessages(IsAutomatedImport());
#endif // #if USE_USD_SDK

		return ImportContext.World;
	}
	else
	{
		bOutOperationCanceled = true;
		return nullptr;
	}
}

bool UUSDSceneImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc"))
	{
		return true;
	}

	return false;
}

void UUSDSceneImportFactory::CleanUp()
{
	ImportContext = FUSDSceneImportContext();
}

void UUSDSceneImportFactory::ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson)
{
	FJsonObjectConverter::JsonObjectToUStruct(ImportSettingsJson, ImportOptions->GetClass(), ImportOptions, 0, CPF_InstancedReference);
}

#undef LOCTEXT_NAMESPACE
