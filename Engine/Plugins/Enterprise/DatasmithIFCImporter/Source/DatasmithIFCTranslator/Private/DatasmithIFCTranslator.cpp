// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithIFCTranslator.h"

#include "DatasmithIFCTranslatorModule.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"

#include "Algo/Count.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IMessageLogListing.h"
#include "Logging/LogMacros.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "Templates/TypeHash.h"

#include "DatasmithIFCImporter.h"

void ShowLogMessages(const TArray<IFC::FLogMessage>& Errors)
{
	if (Errors.Num() > 0)
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		TSharedRef<IMessageLogListing> LogListing = (MessageLogModule.GetLogListing("LoadErrors"));
		LogListing->ClearMessages();

		for (const IFC::FLogMessage& Error : Errors)
		{
			EMessageSeverity::Type Severity = Error.Get<0>();
			LogListing->AddMessage(FTokenizedMessage::Create(Severity, FText::FromString(Error.Get<1>())));
		}

		MessageLogModule.OpenMessageLog("LoadErrors");
	}
}

void FDatasmithIFCTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
	OutCapabilities.bIsEnabled = true;
	OutCapabilities.bParallelLoadStaticMeshSupported = true;

	TArray<FFileFormatInfo>& Formats = OutCapabilities.SupportedFileFormats;
    Formats.Emplace(TEXT("ifc"), TEXT("IFC (Industry Foundation Classes)"));
}

bool FDatasmithIFCTranslator::LoadScene(TSharedRef<IDatasmithScene> OutScene)
{
	OutScene->SetHost(TEXT("IFCTranslator"));
	OutScene->SetProductName(TEXT("IFC"));

    Importer = MakeShared<FDatasmithIFCImporter>(OutScene, ImportOptions.Get());

	const FString& FilePath = GetSource().GetSourceFile();
	if(!Importer->OpenFile(FilePath))
	{
		ShowLogMessages(Importer->GetLogMessages());
		return false;
	}

	bool bSuccess = Importer->SendSceneToDatasmith();
	ShowLogMessages(Importer->GetLogMessages());
	return bSuccess;
}

void FDatasmithIFCTranslator::UnloadScene()
{
	Importer->UnloadScene();
}

bool FDatasmithIFCTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
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

bool FDatasmithIFCTranslator::LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload)
{
	return false;
}

void FDatasmithIFCTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	if (!ImportOptions.IsValid())
	{
		ImportOptions = Datasmith::MakeOptions<UDatasmithIFCImportOptions>();
	}

	Options.Add(ImportOptions);
}

void FDatasmithIFCTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	for (TStrongObjectPtr<UObject>& OptionPtr : Options)
	{
		UObject* Option = OptionPtr.Get();
		if (UDatasmithIFCImportOptions* InImportOptions = Cast<UDatasmithIFCImportOptions>(Option))
		{
			ImportOptions.Reset(InImportOptions);
		}
	}

	if (Importer.IsValid())
	{
		Importer->SetImportOptions(ImportOptions.Get());
	}
}
