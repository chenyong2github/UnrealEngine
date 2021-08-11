// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkLegacyFactory.h"
#include "NeuralNetworkLegacy.h"
#include "NeuralNetworkInferenceEditorUtils.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/Paths.h"



/* UNeuralNetworkLegacyFactory structors
 *****************************************************************************/

UNeuralNetworkLegacyFactory::UNeuralNetworkLegacyFactory()
{
	SupportedClass = UNeuralNetworkLegacy::StaticClass();
	Formats.Add(TEXT("onnx;ONNX file"));
	Formats.Add(TEXT("onnx2;Empty file with the same name than the ONNX file but renamed as ONNX2 to avoid issues with UNeuralNetwork"));

	bCreateNew = true;
	bEditorImport = true;
	bEditAfterNew = true;
	bText = false;

	// Required to allow other re importers to do their CanReimport checks first, and if they fail this re importer will catch it.
	// UNeuralNetwork factory should be called first too
	ImportPriority = DefaultImportPriority - 2;
}



/* UNeuralNetworkLegacyFactory public functions
 *****************************************************************************/

UObject* UNeuralNetworkLegacyFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	// If created with right-click on Content Browser --> Neural Network
	return NewObject<UNeuralNetworkLegacy>(InParent, InName, InFlags);
}

UObject* UNeuralNetworkLegacyFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags,
	const FString& InFilename, const TCHAR* Params, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	// If created by dragging a new file into the UE Editor Content Browser
	if (InFilename.Len() > 0)
	{
		const FString ActualFileName = InFilename.LeftChop(1);
		UNeuralNetworkLegacy* Network = NewObject<UNeuralNetworkLegacy>(InParent, InClass, InName, InFlags);
		// Try to load neural network from file.
		UE_LOG(LogNeuralNetworkInferenceEditor, Display, TEXT("Importing \"%s\"."), *ActualFileName);
		if (Network && Network->Load(ActualFileName))
		{
			Network->GetAssetImportData()->Update(ActualFileName);
			return Network;
		}
		else
		{
			UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkLegacyFactory::FactoryCreateFile(): Import failed."));
			// Invalid file or parameters.
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogNeuralNetworkInferenceEditor, Warning, TEXT("UNeuralNetworkLegacyFactory::FactoryCreateFile(): No filename provided, creating default UNeuralNetworkLegacy."));
		// If created with right-click on Content Browser --> NeuralNetwork
		return NewObject<UNeuralNetworkLegacy>(InParent, InName, InFlags);
	}
}

bool UNeuralNetworkLegacyFactory::CanCreateNew() const
{
	// If true --> It will always call FactoryCreateNew(), not allowing me to use FactoryCreateFile().
	// If false --> It will ignore the FactoryCreateFile (thus the txt file) when creating a new UNeuralNetworkLegacy.
	return CurrentFilename.IsEmpty();
}

bool UNeuralNetworkLegacyFactory::DoesSupportClass(UClass * Class)
{
	return (Class == UNeuralNetworkLegacy::StaticClass());
}

UClass* UNeuralNetworkLegacyFactory::ResolveSupportedClass()
{
	return UNeuralNetworkLegacy::StaticClass();
}

bool UNeuralNetworkLegacyFactory::FactoryCanImport(const FString& InFilename)
{
	return IsValidFile(InFilename);
}

bool UNeuralNetworkLegacyFactory::CanImportBeCanceled() const
{
	return false;
}



/* UNeuralNetworkLegacyFactory protected functions
 *****************************************************************************/

bool UNeuralNetworkLegacyFactory::IsValidFile(const FString& InFilename) const
{
	const FString FileExtension = FPaths::GetExtension(InFilename, /*bIncludeDot*/ false);
	return FileExtension.Equals(TEXT("onnx"), ESearchCase::IgnoreCase) || FileExtension.Equals(TEXT("onnx2"), ESearchCase::IgnoreCase);
}
