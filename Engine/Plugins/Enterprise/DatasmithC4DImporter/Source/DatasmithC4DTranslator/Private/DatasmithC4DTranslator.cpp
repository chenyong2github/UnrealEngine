// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithC4DTranslator.h"
#include "DatasmithC4DDynamicImporterModule.h"

#if defined(_MELANGE_SDK_) || defined(_CINEWARE_SDK_)
#include "DatasmithC4DTranslatorModule.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"

#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Templates/TypeHash.h"

typedef TSharedPtr<IDatasmithC4DImporter>(*CreateC4DImporter)(TSharedRef<IDatasmithScene> OutScene, UDatasmithC4DImportOptions* InOptions);

void FDatasmithC4DTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
	OutCapabilities.bIsEnabled = true;
	OutCapabilities.bParallelLoadStaticMeshSupported = true;

	TArray<FFileFormatInfo>& Formats = OutCapabilities.SupportedFileFormats;
    Formats.Emplace(TEXT("c4d"), TEXT("Cinema 4D file format"));
}

bool FDatasmithC4DTranslator::LoadScene(TSharedRef<IDatasmithScene> OutScene)
{
	OutScene->SetHost(TEXT("C4DTranslator"));
	
#ifdef _CHECK_DYNAMIC_IMPORTER_
	if (IDatasmithC4DDynamicImporterModule::IsAvailable())
	{
		FDatasmithC4DImportOptions C4DImportOptions;
		C4DImportOptions.bImportEmptyMesh = ImportOptions->bImportEmptyMesh;
		C4DImportOptions.bExportToUDatasmith = ImportOptions->bExportToUDatasmith;
		C4DImportOptions.bAlwaysGenerateNormals = ImportOptions->bAlwaysGenerateNormals;
		C4DImportOptions.ScaleVertices = ImportOptions->ScaleVertices;
		C4DImportOptions.bOptimizeEmptySingleChildActors = ImportOptions->bOptimizeEmptySingleChildActors;

		IDatasmithC4DDynamicImporterModule& DynamicModule = IDatasmithC4DDynamicImporterModule::Get();
		if (DynamicModule.TryLoadingCineware())
		{
			Importer = DynamicModule.GetDynamicImporter(OutScene, C4DImportOptions);
		}
	}
#endif

	if(!Importer.IsValid())
	{
		FDatasmithC4DImportOptions C4DImportOptions;
		Importer = MakeShared<FDatasmithC4DImporter>(OutScene, C4DImportOptions);
	}

	Importer->OpenFile(GetSource().GetSourceFile());

	return Importer->ProcessScene();
}

void FDatasmithC4DTranslator::UnloadScene()
{
	Importer->UnloadScene();
}

bool FDatasmithC4DTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
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

bool FDatasmithC4DTranslator::LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload)
{
	if (ensure(Importer.IsValid()))
	{
		if (LevelSequenceElement == Importer->GetLevelSequence())
		{
			// #ueent_todo: move data to OutLevelSequencePayload
			return true;
		}
	}

	return false;
}

void FDatasmithC4DTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	Options.Add(GetOrCreateC4DImportOptions());
}

void FDatasmithC4DTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UDatasmithOptionsBase>>& Options)
{
	for (const TStrongObjectPtr<UDatasmithOptionsBase>& OptionPtr : Options)
	{
		if (UDatasmithC4DImportOptions* InImportOptions = Cast<UDatasmithC4DImportOptions>(OptionPtr.Get()))
		{
			ImportOptions.Reset(InImportOptions);
		}
	}

	if (Importer.IsValid())
	{
		FDatasmithC4DImportOptions C4DImportOptions;
		C4DImportOptions.bImportEmptyMesh = ImportOptions->bImportEmptyMesh;
		C4DImportOptions.bExportToUDatasmith = ImportOptions->bExportToUDatasmith;
		C4DImportOptions.bAlwaysGenerateNormals = ImportOptions->bAlwaysGenerateNormals;
		C4DImportOptions.ScaleVertices = ImportOptions->ScaleVertices;
		C4DImportOptions.bOptimizeEmptySingleChildActors = ImportOptions->bOptimizeEmptySingleChildActors;

		Importer->SetImportOptions(C4DImportOptions);
	}
}

TStrongObjectPtr<UDatasmithC4DImportOptions>& FDatasmithC4DTranslator::GetOrCreateC4DImportOptions()
{
	if (!ImportOptions.IsValid())
	{
		ImportOptions = Datasmith::MakeOptions<UDatasmithC4DImportOptions>();
	}
	return ImportOptions;
}

#endif // _MELANGE_SDK_