// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDAssetImportFactory.h"

#include "ActorFactories/ActorFactoryStaticMesh.h"
#include "AssetImportTask.h"
#include "Engine/StaticMesh.h"
#include "IUSDImporterModule.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "USDAssetImportData.h"
#include "USDImporter.h"
#include "USDImportOptions.h"
#include "UsdWrappers/SdfPath.h"

#if USE_USD_SDK

void FUSDAssetImportContext::Init(UObject* InParent, const FString& InName, const UE::FUsdStage& InStage)
{
	FUsdImportContext::Init(InParent, InName, InStage);
}
#endif // #if USE_USD_SDK

UDEPRECATED_UUSDAssetImportFactory::UDEPRECATED_UUSDAssetImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UStaticMesh::StaticClass();

	ImportOptions_DEPRECATED = ObjectInitializer.CreateDefaultSubobject<UDEPRECATED_UUSDImportOptions>(this, TEXT("USDImportOptions"));

	bEditorImport = true;
	bText = false;

	// Factory is deprecated
	ImportPriority = -1;

	Formats.Add(TEXT("usd;Universal Scene Description files"));
	Formats.Add(TEXT("usda;Universal Scene Description files"));
	Formats.Add(TEXT("usdc;Universal Scene Description files"));
}

UObject* UDEPRECATED_UUSDAssetImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UObject* ImportedObject = nullptr;
	AdditionalImportedObjects.Empty();

#if USE_USD_SDK
	UDEPRECATED_UUSDImporter* USDImporter = IUSDImporterModule::Get().GetImporter();

	if (IsAutomatedImport() || USDImporter->ShowImportOptions(*ImportOptions_DEPRECATED))
	{
		UE::FUsdStage Stage = USDImporter->ReadUsdFile(ImportContext, Filename);
		if (Stage)
		{
			ImportContext.Init(InParent, InName.ToString(), Stage);

			if (AssetImportTask && AssetImportTask->Options)
			{
				ImportContext.ImportOptions_DEPRECATED = Cast<UDEPRECATED_UUSDImportOptions>(AssetImportTask->Options);
			}

			if (ImportContext.ImportOptions_DEPRECATED == nullptr)
			{
				ImportContext.ImportOptions_DEPRECATED = ImportOptions_DEPRECATED;
			}

			ImportContext.bApplyWorldTransformToGeometry = ImportContext.ImportOptions_DEPRECATED->bApplyWorldTransformToGeometry;

			TArray<FUsdAssetPrimToImport> PrimsToImport;
			UDEPRECATED_UUSDBatchImportOptions* BatchImportOptions = Cast<UDEPRECATED_UUSDBatchImportOptions>(ImportContext.ImportOptions_DEPRECATED);
			if (BatchImportOptions)
			{
				for (UDEPRECATED_UUSDBatchImportOptionsSubTask* SubTask : BatchImportOptions->SubTasks_DEPRECATED)
				{
					FUsdAssetPrimToImport NewTopLevelPrim;

					NewTopLevelPrim.Prim = Stage.GetPrimAtPath( UE::FSdfPath( *SubTask->SourcePath ) );
					if (!NewTopLevelPrim.Prim.IsValid())
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
				ImportContext.PrimResolver_DEPRECATED->FindMeshAssetsToImport(ImportContext, ImportContext.RootPrim, ImportContext.RootPrim, PrimsToImport);
			}

			TArray<UObject*> ImportedObjects = USDImporter->ImportMeshes(ImportContext, PrimsToImport);

			// Just return the first one imported
			if (ImportedObjects.Num() > 0)
			{
				ImportedObject = ImportedObjects[0];
				AdditionalImportedObjects.Reserve(ImportedObjects.Num() - 1);
				for (int32 ImportedObjectIndex = 1; ImportedObjectIndex < ImportedObjects.Num(); ++ImportedObjectIndex)
				{
					AdditionalImportedObjects.Add(ImportedObjects[ImportedObjectIndex]);
				}
			}
		}

		// Reset this cache or else reimport will not work properly
		ImportContext.PathToImportAssetMap.Empty();

		ImportContext.DisplayErrorMessages(IsAutomatedImport());
	}
	else
	{
		bOutOperationCanceled = true;
	}

#endif // #if USE_USD_SDK

	return ImportedObject;
}

bool UDEPRECATED_UUSDAssetImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc"))
	{
		return true;
	}

	return false;
}

void UDEPRECATED_UUSDAssetImportFactory::CleanUp()
{
	ImportContext = FUSDAssetImportContext();
	Super::CleanUp();
}

bool UDEPRECATED_UUSDAssetImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if (Mesh != nullptr)
	{
		UUsdAssetImportData* ImportData = Cast<UUsdAssetImportData>(Mesh->AssetImportData);
		if (ImportData)
		{
			OutFilenames.Add(ImportData->GetFirstFilename());
			return true;
		}
	}
	return false;
}

void UDEPRECATED_UUSDAssetImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if (Mesh != nullptr && ensure(NewReimportPaths.Num() == 1))
	{
		UUsdAssetImportData* ImportData = Cast<UUsdAssetImportData>(Mesh->AssetImportData);
		if (ImportData)
		{
			ImportData->UpdateFilenameOnly(NewReimportPaths[0]);
		}
	}
}

EReimportResult::Type UDEPRECATED_UUSDAssetImportFactory::Reimport(UObject* Obj)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if (Mesh != nullptr)
	{
		UUsdAssetImportData* ImportData = Cast<UUsdAssetImportData>(Mesh->AssetImportData);
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

void UDEPRECATED_UUSDAssetImportFactory::ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson)
{
	FJsonObjectConverter::JsonObjectToUStruct(ImportSettingsJson, ImportOptions_DEPRECATED->GetClass(), ImportOptions_DEPRECATED, 0, CPF_InstancedReference);
}
