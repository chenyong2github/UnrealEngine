// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkLegacyReimportFactory.h"
#include "NeuralNetworkInferenceEditorUtils.h"
#include "NeuralNetworkLegacy.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"


/* UNeuralNetworkLegacyReimportFactory structors
 *****************************************************************************/

UNeuralNetworkLegacyReimportFactory::UNeuralNetworkLegacyReimportFactory()
{
	SupportedClass = UNeuralNetworkLegacy::StaticClass();
	Formats.Add(TEXT("onnx;ONNX file"));
	Formats.Add(TEXT("onnx2;Empty file with the same name than the ONNX file but renamed as ONNX2 to avoid issues with UNeuralNetwork"));

	bCreateNew = false;
	bText = false;

	// Required to allow other re importers to do their CanReimport checks first, and if they fail this re importer will catch it.
	// UNeuralNetwork factory should be called first too
	ImportPriority = DefaultImportPriority - 3;
}



/* UNeuralNetworkLegacyReimportFactory public functions
 *****************************************************************************/
bool UNeuralNetworkLegacyReimportFactory::CanCreateNew() const
{
	// Return false, can only reimport
	return false;
}

bool UNeuralNetworkLegacyReimportFactory::FactoryCanImport(const FString& Filename)
{
	// Return false, can only reimport
	return false;
}

bool UNeuralNetworkLegacyReimportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UNeuralNetworkLegacy* Network = Cast<UNeuralNetworkLegacy>(Obj);
	if (Network)
	{
		UAssetImportData* AssetImportData = Network->GetAssetImportData();
		if (AssetImportData)
		{
			const bool bIsValidFile = UNeuralNetworkLegacyFactory::IsValidFile(AssetImportData->GetFirstFilename());
			if (!bIsValidFile)
			{
				return false;
			}
			OutFilenames.Add(AssetImportData->GetFirstFilename());
		}
		else
		{
			OutFilenames.Add(TEXT(""));
		}
		return true;
	}
	return false;
}

void UNeuralNetworkLegacyReimportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UNeuralNetworkLegacy* Network = Cast<UNeuralNetworkLegacy>(Obj);
	if (Network && ensure(NewReimportPaths.Num() == 1))
	{
		Network->Modify();
		if (UAssetImportData* AssetImportData = Network->GetAssetImportData())
		{
			AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
		}
		else
		{
			UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkLegacyReimportFactory::Reimport(): AssetImportData was nullptr."));
		}
	}
}

EReimportResult::Type UNeuralNetworkLegacyReimportFactory::Reimport(UObject* Obj)
{
	UNeuralNetworkLegacy* Network = Cast<UNeuralNetworkLegacy>(Obj);
	if (!Network)
	{
		return EReimportResult::Failed;
	}

	UAssetImportData* AssetImportData = Network->GetAndMaybeCreateAssetImportData();

	//Get the re-import filename
	const FString ImportedFilename = AssetImportData->GetFirstFilename();
	const bool bIsValidFile = UNeuralNetworkLegacyFactory::IsValidFile(ImportedFilename);
	if (!bIsValidFile)
	{
		return EReimportResult::Failed;
	}
	if (!ImportedFilename.Len())
	{
		// Since neural network can be created from scratch i.e., don't have paths, logging has been commented out.
		return EReimportResult::Failed;
	}
	// Ensure that the file provided by the path exists
	if (IFileManager::Get().FileSize(*ImportedFilename) == INDEX_NONE)
	{
		UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkLegacyReimportFactory::Reimport(): Cannot reimport, source file cannot be found."));
		return EReimportResult::Failed;
	}

	//Note: Setting CurrentFilename here will mess with the logic in FNeuralNetworkLegacyFactory::CanCreateNew
	//CurrentFilename = ImportedFilename;
	UE_LOG(LogNeuralNetworkInferenceEditor, Display, TEXT("Performing atomic reimport of \"%s\"."), *ImportedFilename);

	if (Network->Load(ImportedFilename))
	{
		AssetImportData->Update(ImportedFilename);
		return EReimportResult::Succeeded;
	}
	UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkLegacyReimportFactory::Reimport(): Load failed."));
	return EReimportResult::Failed;
}


int32 UNeuralNetworkLegacyReimportFactory::GetPriority() const
{
	return ImportPriority;
}

bool UNeuralNetworkLegacyReimportFactory::IsAutomatedImport() const
{
	return Super::IsAutomatedImport() || IsAutomatedReimport();
}
