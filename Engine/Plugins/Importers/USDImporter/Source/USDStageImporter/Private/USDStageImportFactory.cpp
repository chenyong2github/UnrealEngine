// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImportFactory.h"

#include "USDConversionUtils.h"
#include "USDStageImporterModule.h"
#include "USDStageImportOptions.h"
#include "USDStageImportOptionsWindow.h"

#include "AssetRegistryModule.h"
#include "AssetSelection.h"
#include "Editor.h"
#include "IAssetRegistry.h"
#include "JsonObjectConverter.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "USDImportFactory"

UUsdStageImportFactory::UUsdStageImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UWorld::StaticClass();

	ImportPriority += 100;

	bEditorImport = true;
	bText = false;

	ImportOptions = ObjectInitializer.CreateDefaultSubobject<UUsdStageImportOptions>(this, TEXT("USDStageImportOptions"));

	Formats.Add(TEXT("usd;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usda;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usdc;Universal Scene Descriptor files"));
}

UObject* UUsdStageImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UObject* ImportedObject = nullptr;

#if USE_USD_SDK
	if (ImportContext.Init(InName.ToString(), Filename, Flags, IsAutomatedImport()))
	{
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport( this, InClass, InParent, InName, Parms );

		UUsdStageImporter* USDImporter = IUsdStageImporterModule::Get().GetImporter();
		USDImporter->ImportFromFile(ImportContext);

		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, ImportContext.World);
		GEditor->BroadcastLevelActorListChanged();
		GEditor->RedrawLevelEditingViewports();

		ImportContext.DisplayErrorMessages(ImportContext.bIsAutomated);

		ImportedObject = ImportContext.SceneActor;
	}
	else
	{
		bOutOperationCanceled = true;
	}

#endif // #if USE_USD_SDK

	return ImportedObject;
}

bool UUsdStageImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc"))
	{
		return true;
	}

	return false;
}

void UUsdStageImportFactory::CleanUp()
{
	ImportContext = FUsdStageImportContext();
	Super::CleanUp();
}

void UUsdStageImportFactory::ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson)
{
	FJsonObjectConverter::JsonObjectToUStruct(ImportSettingsJson, ImportOptions->GetClass(), ImportOptions, 0, CPF_InstancedReference);
}

#undef LOCTEXT_NAMESPACE
