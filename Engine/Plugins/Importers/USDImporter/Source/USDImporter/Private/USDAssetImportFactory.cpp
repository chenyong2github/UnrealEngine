// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDAssetImportFactory.h"
#include "USDImporter.h"
#include "IUSDImporterModule.h"
#include "ActorFactories/ActorFactoryStaticMesh.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "USDImportOptions.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"
#include "USDAssetImportData.h"
#include "AssetImportTask.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"

void FUSDAssetImportContext::Init(UObject* InParent, const FString& InName, const TUsdStore< pxr::UsdStageRefPtr >& InStage)
{
	FUsdImportContext::Init(InParent, InName, InStage);
}
#endif // #if USE_USD_SDK

UUSDAssetImportFactory::UUSDAssetImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UStaticMesh::StaticClass();

	ImportOptions = ObjectInitializer.CreateDefaultSubobject<UUSDImportOptions>(this, TEXT("USDImportOptions"));

	bEditorImport = true;
	bText = false;

	Formats.Add(TEXT("usd;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usda;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usdc;Universal Scene Descriptor files"));
}

UObject* UUSDAssetImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UObject* ImportedObject = nullptr;

#if USE_USD_SDK
	UUSDImporter* USDImporter = IUSDImporterModule::Get().GetImporter();

	if (IsAutomatedImport() || USDImporter->ShowImportOptions(*ImportOptions))
	{
		TUsdStore< pxr::UsdStageRefPtr > Stage = USDImporter->ReadUsdFile(ImportContext, Filename);
		if (*Stage)
		{
			ImportContext.Init(InParent, InName.ToString(), Stage);
			
			if (AssetImportTask && AssetImportTask->Options)
			{
				ImportContext.ImportOptions = Cast<UUSDImportOptions>(AssetImportTask->Options);
			}
			
			if (ImportContext.ImportOptions == nullptr)
			{
				ImportContext.ImportOptions = ImportOptions;
			}

			ImportContext.bApplyWorldTransformToGeometry = ImportContext.ImportOptions->bApplyWorldTransformToGeometry;

			TArray<FUsdAssetPrimToImport> PrimsToImport;
			UUSDBatchImportOptions* BatchImportOptions = Cast<UUSDBatchImportOptions>(ImportContext.ImportOptions);
			if (BatchImportOptions)
			{
				for (UUSDBatchImportOptionsSubTask* SubTask : BatchImportOptions->SubTasks)
				{
					FUsdAssetPrimToImport NewTopLevelPrim;

					NewTopLevelPrim.Prim = (*Stage)->GetPrimAtPath( pxr::SdfPath(TCHAR_TO_ANSI(*SubTask->SourcePath)) );
					if (!NewTopLevelPrim.Prim.Get().IsValid())
					{
						continue;
					}

					NewTopLevelPrim.AssetPath = SubTask->DestPath;
					NewTopLevelPrim.MeshPrims.Add(NewTopLevelPrim.Prim);

					PrimsToImport.Add(NewTopLevelPrim);
				}
			}
			else
			{
				ImportContext.PrimResolver->FindMeshAssetsToImport(ImportContext, ImportContext.RootPrim, ImportContext.RootPrim, PrimsToImport);
			}
						
			TArray<UObject*> ImportedObjects = USDImporter->ImportMeshes(ImportContext, PrimsToImport);

			// Just return the first one imported
			ImportedObject = ImportedObjects.Num() > 0 ? ImportedObjects[0] : nullptr;
		}

		ImportContext.DisplayErrorMessages(IsAutomatedImport());
	}
	else
	{
		bOutOperationCanceled = true;
	}

#endif // #if USE_USD_SDK

	return ImportedObject;
}

bool UUSDAssetImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc"))
	{
		return true;
	}

	return false;
}

void UUSDAssetImportFactory::CleanUp()
{
	ImportContext = FUSDAssetImportContext();
}

bool UUSDAssetImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if (Mesh != nullptr)
	{
		UUSDAssetImportData* ImportData = Cast<UUSDAssetImportData>(Mesh->AssetImportData);
		if (ImportData)
		{
			OutFilenames.Add(ImportData->GetFirstFilename());
			return true;
		}
	}
	return false;
}

void UUSDAssetImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if (Mesh != nullptr && ensure(NewReimportPaths.Num() == 1))
	{
		UUSDAssetImportData* ImportData = Cast<UUSDAssetImportData>(Mesh->AssetImportData);
		if (ImportData)
		{
			ImportData->UpdateFilenameOnly(NewReimportPaths[0]);
		}
	}
}

EReimportResult::Type UUSDAssetImportFactory::Reimport(UObject* Obj)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if (Mesh != nullptr)
	{
		UUSDAssetImportData* ImportData = Cast<UUSDAssetImportData>(Mesh->AssetImportData);
		if (ImportData)
		{
			bool bOperationCancelled = false;
			UObject* Result = FactoryCreateFile(UStaticMesh::StaticClass(), (UObject*)Mesh->GetOutermost(), Mesh->GetFName(), RF_Transactional | RF_Standalone | RF_Public, ImportData->GetFirstFilename(), nullptr, GWarn, bOperationCancelled);
			if (bOperationCancelled)
			{
				return EReimportResult::Cancelled;
			}
			else
			{
				return Result ? EReimportResult::Succeeded : EReimportResult::Failed;
			}
		}
	}

	return EReimportResult::Failed;
}

void UUSDAssetImportFactory::ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson)
{
	FJsonObjectConverter::JsonObjectToUStruct(ImportSettingsJson, ImportOptions->GetClass(), ImportOptions, 0, CPF_InstancedReference);
}
