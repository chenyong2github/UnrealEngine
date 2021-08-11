// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkReimportFactory.h"
#if PLATFORM_WINDOWS
#include "NeuralNetwork.h"
#endif
#include "NeuralNetworkInferenceEditorUtils.h"
#include "EditorFramework/AssetImportData.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"


/* UNeuralNetworkReimportFactory structors
 *****************************************************************************/

UNeuralNetworkReimportFactory::UNeuralNetworkReimportFactory()
{
#if PLATFORM_WINDOWS
	SupportedClass = UNeuralNetwork::StaticClass();
	Formats.Add(TEXT("onnx;ONNX file"));
	Formats.Add(TEXT("ort;ONNX Runtime (ORT) file"));

	bCreateNew = false;
	bText = false;

	// Required to allow other re importers to do their CanReimport checks first, and if they fail this re importer will catch it.
	ImportPriority = DefaultImportPriority - 1;
#endif
}



/* UNeuralNetworkReimportFactory public functions
 *****************************************************************************/
bool UNeuralNetworkReimportFactory::CanCreateNew() const
{
	// Return false, can only reimport
	return false;
}

bool UNeuralNetworkReimportFactory::FactoryCanImport(const FString& Filename)
{
	// Return false, can only reimport
	return false;
}

bool UNeuralNetworkReimportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
#if PLATFORM_WINDOWS
	UNeuralNetwork* Network = Cast<UNeuralNetwork>(Obj);
	if (Network)
	{
		UAssetImportData* AssetImportData = Network->GetAssetImportData();
		if (AssetImportData)
		{
			const bool bIsValidFile = UNeuralNetworkFactory::IsValidFile(AssetImportData->GetFirstFilename());
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
#endif
	return false;
}

void UNeuralNetworkReimportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
#if PLATFORM_WINDOWS
	UNeuralNetwork* Network = Cast<UNeuralNetwork>(Obj);
	if (Network && ensure(NewReimportPaths.Num() == 1))
	{
		Network->Modify();
		if (UAssetImportData* AssetImportData = Network->GetAssetImportData())
		{
			AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
		}
		else
		{
			UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkReimportFactory::Reimport(): AssetImportData was nullptr."));
		}
	}
#endif
}

EReimportResult::Type UNeuralNetworkReimportFactory::Reimport(UObject* Obj)
{
#if PLATFORM_WINDOWS
	UNeuralNetwork* Network = Cast<UNeuralNetwork>(Obj);
	if (!Network)
	{
		return EReimportResult::Failed;
	}

	UAssetImportData* AssetImportData = Network->GetAndMaybeCreateAssetImportData();

	//Get the re-import filename
	const FString ImportedFilename = AssetImportData->GetFirstFilename();
	const bool bIsValidFile = UNeuralNetworkFactory::IsValidFile(ImportedFilename);
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
		UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkReimportFactory::Reimport(): Cannot reimport, source file cannot be found."));
		return EReimportResult::Failed;
	}

	//Note: Setting CurrentFilename here will mess with the logic in FNeuralNetworkFactory::CanCreateNew
	//CurrentFilename = ImportedFilename;
	UE_LOG(LogNeuralNetworkInferenceEditor, Display, TEXT("Performing atomic reimport of \"%s\"."), *ImportedFilename);

	if (Network->Load(ImportedFilename))
	{
		AssetImportData->Update(ImportedFilename);
		return EReimportResult::Succeeded;
	}
#endif
	UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkReimportFactory::Reimport(): Load failed."));
	return EReimportResult::Failed;
}


int32 UNeuralNetworkReimportFactory::GetPriority() const
{
	return ImportPriority;
}

bool UNeuralNetworkReimportFactory::IsAutomatedImport() const
{
	return Super::IsAutomatedImport() || IsAutomatedReimport();
}
