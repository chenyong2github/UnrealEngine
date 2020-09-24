// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/DMXGDTFFactory.h"
#include "Factories/DMXGDTFImportUI.h"
#include "DMXEditorLog.h"
#include "Factories/DMXGDTFImporter.h"
#include "Library/DMXImportGDTF.h"

#include "Misc/Paths.h"
#include "Editor.h"
#include "EditorReimportHandler.h"
#include "AssetImportTask.h"
#include "Misc/FeedbackContext.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"

const TCHAR* UDMXGDTFFactory::Extension = TEXT("gdtf");

UDMXGDTFImportUI::UDMXGDTFImportUI()
    : bUseSubDirectory(false)
    , bImportXML(true)
    , bImportTextures(false)
    , bImportModels(false)
{
}

void UDMXGDTFImportUI::ResetToDefault()
{
    bUseSubDirectory = false;
    bImportXML = true;
    bImportTextures = false;
    bImportModels = false;
}

UDMXGDTFFactory::UDMXGDTFFactory()
{
    SupportedClass = nullptr;
	Formats.Add(TEXT("gdtf;General Device Type Format"));

	bCreateNew = false;
	bText = false;
	bEditorImport = true;
	bOperationCanceled = false;
}

void UDMXGDTFFactory::CleanUp()
{
	Super::CleanUp();

    bShowOption = true;
}

bool UDMXGDTFFactory::ConfigureProperties()
{
    Super::ConfigureProperties();
    EnableShowOption();

    return true;
}

void UDMXGDTFFactory::PostInitProperties()
{
	Super::PostInitProperties();

    ImportUI = NewObject<UDMXGDTFImportUI>(this, NAME_None, RF_NoFlags);
}

bool UDMXGDTFFactory::DoesSupportClass(UClass* Class)
{
    return Class == UDMXImportGDTF::StaticClass();
}

UClass* UDMXGDTFFactory::ResolveSupportedClass()
{
    return UDMXImportGDTF::StaticClass();
}

UObject* UDMXGDTFFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& InFilename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    FString FileExtension = FPaths::GetExtension(InFilename);
    const TCHAR* Type = *FileExtension;

    if (!IFileManager::Get().FileExists(*InFilename))
    {
        UE_LOG_DMXEDITOR(Error, TEXT("Failed to load file '%s'"), *InFilename)
        return nullptr;
    }

    ParseParms(Parms);

    CA_ASSUME(InParent);

    if( bOperationCanceled )
    {
        bOutOperationCanceled = true;
        GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr);
        return nullptr;
    }

    GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Type);

    UDMXImportGDTF* CreatedObject = nullptr;
    UObject* ExistingObject = nullptr;
    if (InParent != nullptr)
    {
        ExistingObject = StaticFindObject(UObject::StaticClass(), InParent, *(InName.ToString()));
        if (ExistingObject)
        {
            bShowOption = false;
        }
    }

    // Prepare import options
    FDMXGDTFImportArgs ImportArgs;
    ImportArgs.ImportUI = ImportUI;
    ImportArgs.Name = InName;
    ImportArgs.Parent = InParent;
    ImportArgs.CurrentFilename = CurrentFilename;
    ImportArgs.Flags = Flags;
    ImportArgs.bCancelOperation = bOperationCanceled;
    TUniquePtr<FDMXGDTFImporter> Importer = MakeUnique<FDMXGDTFImporter>(ImportArgs);

    // Set Import UI
    if (FParse::Param(FCommandLine::Get(), TEXT("NoDMXImportOption")))
    {
        bShowOption = false;
    }
    bool bIsAutomated = IsAutomatedImport();
    bool bShowImportDialog = bShowOption && !bIsAutomated;
    bool bImportAll = false;
    FDMXGDTFImporter::GetImportOptions(Importer, ImportUI, bShowImportDialog, InParent->GetPathName(), bOperationCanceled, bImportAll, UFactory::CurrentFilename);
    bOutOperationCanceled = bOperationCanceled;
    if( bImportAll )
    {
        // If the user chose to import all, we don't show the dialog again and use the same settings for each object until importing another set of files
        bShowOption = false;
    }

    bool bImportError = false;

    if (!ImportUI->bImportXML && !ImportUI->bImportModels && !ImportUI->bImportTextures)
    {
        Warn->Log(ELogVerbosity::Error, TEXT("Nothing to Import") );
        bImportError = true;
    }

    // Try to load and parse the content
    if ( !Importer->AttemptImportFromFile() )
    {
        Warn->Log(ELogVerbosity::Error, TEXT("Attempt Import") );
        bImportError = true;
    }

    // Import was successful, create new file
    if (!bImportError)
    {
		// Import to the Editor
		CreatedObject = Importer->Import();

        if (CreatedObject != nullptr)
        {
            CreatedObject->SourceFilename = GetCurrentFilename();
        }
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, CreatedObject);
    }

    // We do not need importer any more
    Importer.Reset();

    return CreatedObject;
}

bool UDMXGDTFFactory::FactoryCanImport(const FString& Filename)
{
	const FString TargetExtension = FPaths::GetExtension(Filename);

	if(TargetExtension == UDMXGDTFFactory::Extension)
	{
		return true;
	}

	return false;
}

bool UDMXGDTFFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UDMXImportGDTF* GDTFReimport = Cast<UDMXImportGDTF>(Obj);
	if (GDTFReimport)
	{
		OutFilenames.Add(GDTFReimport->SourceFilename);
		return true;
	}
	return false;
}

void UDMXGDTFFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UDMXImportGDTF* GDTFReimport = Cast<UDMXImportGDTF>(Obj);
	if (GDTFReimport && ensure(NewReimportPaths.Num() == 1))
	{
        GDTFReimport->SourceFilename = NewReimportPaths[0];
	}
}

EReimportResult::Type UDMXGDTFFactory::Reimport(UObject* InObject)
{
	UDMXImportGDTF* GDTFReimport = Cast<UDMXImportGDTF>(InObject);

	if (!GDTFReimport)
	{
		return EReimportResult::Failed;
	}

	if (GDTFReimport->SourceFilename.IsEmpty() || !FPaths::FileExists(GDTFReimport->SourceFilename))
	{
		return EReimportResult::Failed;
	}

	bool OutCanceled = false;
	if (ImportObject(InObject->GetClass(), InObject->GetOuter(), *InObject->GetName(), RF_Public | RF_Standalone, GDTFReimport->SourceFilename, nullptr, OutCanceled))
	{
		return EReimportResult::Succeeded;
	}

	return OutCanceled ? EReimportResult::Cancelled : EReimportResult::Failed;
}

int32 UDMXGDTFFactory::GetPriority() const
{
    return ImportPriority;
}
