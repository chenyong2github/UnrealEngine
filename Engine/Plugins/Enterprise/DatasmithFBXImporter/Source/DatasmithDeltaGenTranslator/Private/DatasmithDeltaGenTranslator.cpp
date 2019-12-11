// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithDeltaGenTranslator.h"

#include "DatasmithDeltaGenTranslatorModule.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"
#include "DatasmithDeltaGenImporter.h"
#include "DatasmithDeltaGenLog.h"

#include "FbxImporter.h"
#include "MeshDescription.h"

void FDatasmithDeltaGenTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
	OutCapabilities.bIsEnabled = true;
	OutCapabilities.bParallelLoadStaticMeshSupported = true;

	TArray<FFileFormatInfo>& Formats = OutCapabilities.SupportedFileFormats;
    Formats.Emplace(TEXT("fbx"), TEXT("DeltaGen Fbx files"));
}

bool FDatasmithDeltaGenTranslator::IsSourceSupported(const FDatasmithSceneSource& Source)
{
	const FString& FilePath = Source.GetSourceFile();
	const FString& Extension = Source.GetSourceFileExtension();
	if (!Extension.Equals(TEXT("fbx"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	UnFbx::FFbxImporter* FbxImporter = UnFbx::FFbxImporter::GetInstance();
	UnFbx::FBXImportOptions* GlobalImportSettings = FbxImporter->GetImportOptions();
	UnFbx::FBXImportOptions::ResetOptions(GlobalImportSettings);

	if (!FbxImporter->ImportFromFile(FilePath, Extension, false))
	{
		FbxImporter->ReleaseScene();
		return false;
	}

	FString ProductName = UTF8_TO_TCHAR(FbxImporter->Scene->GetSceneInfo()->Original_ApplicationName.Get().Buffer());
	if (ProductName != TEXT("RTT_AG"))
	{
		FbxImporter->ReleaseScene();
		return false;
	}

	FbxImporter->ReleaseScene();
	return true;
}

bool FDatasmithDeltaGenTranslator::LoadScene(TSharedRef<IDatasmithScene> OutScene)
{
	OutScene->SetHost(TEXT("DeltaGenTranslator"));

    Importer = MakeShared<FDatasmithDeltaGenImporter>(OutScene, ImportOptions.Get());

	const FString& FilePath = GetSource().GetSourceFile();
	if(!Importer->OpenFile(FilePath))
	{
		UE_LOG(LogDatasmithDeltaGenImport, Log, TEXT("Failed to open file '%s'!"), *FilePath);
		return false;
	}

	if (!Importer->SendSceneToDatasmith())
	{
		UE_LOG(LogDatasmithDeltaGenImport, Log, TEXT("Failed to convert the DeltaGen FBX scene '%s' to Datasmith!"), OutScene->GetName());
		return false;
	}

	return true;
}

void FDatasmithDeltaGenTranslator::UnloadScene()
{
	Importer->UnloadScene();
}

bool FDatasmithDeltaGenTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
	if (ensure(Importer.IsValid()))
	{
		TArray<FMeshDescription> MeshDescriptions;
		Importer->GetGeometriesForMeshElementAndRelease(MeshElement, MeshDescriptions);
		if (MeshDescriptions.Num() > 0)
		{
			OutMeshPayload.LodMeshes.Add(MoveTemp(MeshDescriptions[0]));
			return true;
		}
	}

	return false;
}

bool FDatasmithDeltaGenTranslator::LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload)
{
	//if (ensure(Importer.IsValid()))
	//{
	//	const TArray<TSharedRef<IDatasmithLevelSequenceElement>>& ImportedSequences = Importer->GetImportedSequences();
	//	if (ImportedSequences.Contains(LevelSequenceElement))
	//	{
	//		// #ueent_todo: move data to OutLevelSequencePayload
	//		// Right now the LevelSequenceElement is imported out of the IDatasmithScene
	//		return true;
	//	}
	//}

	return false;
}

void FDatasmithDeltaGenTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	if (!ImportOptions.IsValid())
	{
		const FString& FilePath = GetSource().GetSourceFile();

		ImportOptions = Datasmith::MakeOptions<UDatasmithDeltaGenImportOptions>();
		ImportOptions->ResetPaths(FilePath, false);
	}

	Options.Add(ImportOptions);
}

void FDatasmithDeltaGenTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	for (TStrongObjectPtr<UObject>& OptionPtr : Options)
	{
		UObject* Option = OptionPtr.Get();
		if (UDatasmithDeltaGenImportOptions* InImportOptions = Cast<UDatasmithDeltaGenImportOptions>(Option))
		{
			ImportOptions.Reset(InImportOptions);
		}
	}

	if (Importer.IsValid())
	{
		Importer->SetImportOptions(ImportOptions.Get());
	}
}
