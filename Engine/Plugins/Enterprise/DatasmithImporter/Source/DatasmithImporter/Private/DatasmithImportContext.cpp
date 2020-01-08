// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithImportContext.h"

#include "DatasmithActorImporter.h"
#include "DatasmithImportOptions.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithTranslator.h"
#include "DatasmithUtils.h"
#include "UI/DatasmithImportOptionsWindow.h"
#include "Utility/DatasmithImporterUtils.h"

#include "Misc/FeedbackContext.h"
#include "PackageTools.h"
#include "Misc/Paths.h"
#include "Interfaces/IMainFrameModule.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Engine/StaticMesh.h"


#define LOCTEXT_NAMESPACE "DatasmithImportPlugin"

const TCHAR* UserOptionPath = TEXT("Unreal Engine/Enterprise/Datasmith/Config/UserOptions.ini");

class FDatasmithImportOptionHelper
{
public:
	/**
	 * Display options' dialog
	 *
	 * @param	ImportOptions	The options to display
	 * @param	DatasmithScene	The DatasmithScene for which we are displaying the import options
	 *
	 * @returns true if user accepted the import.
	 */
	static bool DisplayOptionsDialog(const TArray<UObject*>& ImportOptions, TSharedRef< IDatasmithScene > DatasmithScene);

	/** Update options based on content in JSON object*/
	static void LoadOptions(const TArray<UObject*>& ImportOptions, const TSharedPtr<FJsonObject>& ImportSettingsJson);

	/** Work-around to name uniqueness for UObject class */
	static void CleanUpOptions(const TArray<UObject*>& ImportOptions);
};

bool FDatasmithImportOptionHelper::DisplayOptionsDialog(const TArray<UObject*>& ImportOptions, TSharedRef< IDatasmithScene > DatasmithScene)
{
	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DatasmithImportSettingsTitle", "Datasmith Import Options"))
		.SizingRule(ESizingRule::Autosized);

	// First option object is always a UDatasmithImportOptions object
	UDatasmithImportOptions* MainOptions = Cast<UDatasmithImportOptions>(ImportOptions[0]);

	float SceneVersion;
	LexFromString( SceneVersion, DatasmithScene->GetExporterVersion() );

	TSharedPtr<SDatasmithOptionsWindow> OptionsWindow;
	Window->SetContent
	(
		SAssignNew(OptionsWindow, SDatasmithOptionsWindow)
		.ImportOptions(ImportOptions)
		.WidgetWindow(Window)
		// note: Spacing in text below is intentional for text alignment
		.FileNameText(FText::Format(LOCTEXT("DatasmithImportSettingsFileName", "  Import File  :    {0}"), FText::FromString(MainOptions->FileName)))
		.FilePathText(FText::FromString(MainOptions->FilePath))
		.FileFormatVersion(SceneVersion)
		.FileSDKVersion( FText::FromString( DatasmithScene->GetExporterSDKVersion() ) )
		.PackagePathText(FText::Format(LOCTEXT("DatasmithImportSettingsPackagePath", "  Import To   :    {0}"), FText::FromString(MainOptions->BaseOptions.AssetOptions.PackagePath.ToString())))
		.ProceedButtonLabel(LOCTEXT("DatasmithOptionWindow_ImportCurLevel", "Import"))
		.ProceedButtonTooltip(LOCTEXT("DatasmithOptionWindow_ImportCurLevel_ToolTip", "Import the file and add to the current Level"))
		.CancelButtonLabel(LOCTEXT("DatasmithOptionWindow_Cancel", "Cancel"))
		.CancelButtonTooltip(LOCTEXT("DatasmithOptionWindow_Cancel_ToolTip", "Cancel importing this file"))
		.MinDetailHeight(320.f)
		.MinDetailWidth(450.f)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	MainOptions->bUseSameOptions = OptionsWindow->UseSameOptions();

	return OptionsWindow->ShouldImport();
}

void FDatasmithImportOptionHelper::LoadOptions(const TArray<UObject*>& ImportOptions, const TSharedPtr<FJsonObject>& ImportSettingsJson)
{
	if (!ImportSettingsJson.IsValid() || ImportSettingsJson->Values.Num() == 0)
	{
		return;
	}

	for (UObject* Object : ImportOptions)
	{
		const TSharedPtr<FJsonObject>& OptionDataJsonObject = ImportSettingsJson->GetObjectField(Object->GetName());
		if (OptionDataJsonObject.IsValid())
		{
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				if (OptionDataJsonObject->HasField((*It)->GetNameCPP()))
				{
					FString PropertyValue = OptionDataJsonObject->GetStringField((*It)->GetNameCPP());
					uint8* PropertyAddr = (*It)->ContainerPtrToValuePtr<uint8>(Object);

					(*It)->ImportText(*PropertyValue, PropertyAddr, 0, Object);
				}
			}
		}
	}
}

void FDatasmithImportOptionHelper::CleanUpOptions(const TArray<UObject*>& ImportOptions)
{
	// The names of the option objects is just used for UI. And, the option objects are used once
	// But the names of UObjects have to be unique and those UObjects are garbaged collected instead of immediately freed.
	// So, the options are renamed to some generic names which remove any future potential name collision
	for (UObject* Option : ImportOptions)
	{
		Option->Rename(nullptr);
	}
}

FDatasmithImportContext::FDatasmithImportContext(const FString& FileName, bool bLoadConfig, const FName& LoggerName, const FText& LoggerLabel, TSharedPtr<IDatasmithTranslator> InSceneTranslator)
	: SceneTranslator(InSceneTranslator)
	, Options(NewObject<UDatasmithImportOptions>(GetTransientPackage(), TEXT("Datasmith Import Settings")))
	, RootBlueprint(nullptr)
	, SceneAsset(nullptr)
	, bUserCancelled(false)
	, bIsAReimport(false)
	, bImportedViaScript(false)
	, FeedbackContext(nullptr)
	, AssetsContext(*this)
	, ContextExtension(nullptr)
	, Logger(LoggerName, LoggerLabel)
	, CurrentSceneActorIndex(0)
	, ReferenceCollector(this)
{
	ImportOptions.Add(Options.Get());
	SetFileName(FileName);

	if (bLoadConfig && Options.IsValid())
	{
		FString UserDatasmithOptionsFile = FPaths::Combine(FPlatformProcess::UserSettingsDir(), UserOptionPath);
		Options->LoadConfig(nullptr, *UserDatasmithOptionsFile);
	}

	// Force the SceneHandling to be on current level by default.
	// Note: This is done because this option was previously persisted and can get overwritten
	Options->BaseOptions.SceneHandling = EDatasmithImportScene::CurrentLevel;

	if (SceneTranslator)
	{
		SceneTranslator->GetSceneImportOptions(AdditionalImportOptions);
		for (const TStrongObjectPtr<UObject>& Option : AdditionalImportOptions)
		{
			ImportOptions.Add(Option.Get());
		}

		// Temporarily give Rhino translator access to BaseOptions (JIRA UE-81278)
		AdditionalImportOptions.Add(Options);
	}
}

void FDatasmithImportContext::SetFileName(const FString& FileName)
{
	Options->FileName = FPaths::GetCleanFilename(FileName);
	Options->FilePath = FPaths::ConvertRelativePathToFull(FileName);

	FileHash = FMD5Hash::HashFile(*Options->FilePath);
}

void FDatasmithImportContext::AddOption(UObject* InOption, bool bLoadConfig)
{
	if (InOption != nullptr)
	{
		ImportOptions.Add(InOption);
		if (bLoadConfig)
		{
			FString UserDatasmithOptionsFile = FPaths::Combine(FPlatformProcess::UserSettingsDir(), UserOptionPath);
			InOption->LoadConfig(nullptr, *UserDatasmithOptionsFile);
		}
	}
}

TSharedRef<FTokenizedMessage> FDatasmithImportContext::LogError(const FText& InErrorMessage)
{
	return Logger.Push(EMessageSeverity::Error, InErrorMessage);
}

TSharedRef<FTokenizedMessage> FDatasmithImportContext::LogWarning(const FText& InWarningMessage, bool bPerformance)
{
	return Logger.Push(bPerformance ? EMessageSeverity::PerformanceWarning : EMessageSeverity::Warning, InWarningMessage);
}

TSharedRef<FTokenizedMessage> FDatasmithImportContext::LogInfo(const FText& InInfoMessage)
{
	return Logger.Push(EMessageSeverity::Info, InInfoMessage);
}

bool FDatasmithImportContext::Init(const FString& InFileName, TSharedRef< IDatasmithScene > InScene, const FString& InImportPath, EObjectFlags InFlags, FFeedbackContext* InWarn, const TSharedPtr<FJsonObject>& ImportSettingsJson, bool bSilent)
{
	SetFileName(InFileName);
	return Init(InScene, InImportPath, InFlags, InWarn, ImportSettingsJson, bSilent);
}

bool FDatasmithImportContext::Init(TSharedRef< IDatasmithScene > InScene, const FString& InImportPath, EObjectFlags InFlags, FFeedbackContext* InWarn, const TSharedPtr<FJsonObject>& ImportSettingsJson, bool bSilent)
{
	check(Options->FileName.Len() > 0);
	check(Options->FilePath.Len() > 0);

	if (!FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		UE_LOG(LogDatasmithImport, Warning, TEXT("Import failed. The AssetTools module can't be loaded."));
		return false;
	}

	Options->BaseOptions.AssetOptions.PackagePath = FName( *InImportPath );

	if (bSilent)
	{
		// Update options based on given JSON object
		if (ImportSettingsJson.IsValid() && ImportSettingsJson->Values.Num() > 0)
		{
			FDatasmithImportOptionHelper::LoadOptions(ImportOptions, ImportSettingsJson);
		}
	}
	else
	{
		SetupBaseOptionsVisibility();

		bool bShouldImport = FDatasmithImportOptionHelper::DisplayOptionsDialog(ImportOptions, InScene);

		ResetBaseOptionsVisibility();

		if ( !bShouldImport )
		{
			FDatasmithImportOptionHelper::CleanUpOptions(ImportOptions);
			UE_LOG(LogDatasmithImport, Display, TEXT("Import canceled."));
			return false;
		}

		// Update config file with new values
		FString UserDatasmithOptionsFile = FPaths::Combine(FPlatformProcess::UserSettingsDir(), UserOptionPath);
		for (UObject* Option : ImportOptions)
		{
			Option->SaveConfig(CPF_Config, *UserDatasmithOptionsFile);
		}
	}

	if (SceneTranslator)
	{
		SceneTranslator->SetSceneImportOptions(AdditionalImportOptions);
	}

	Options->UpdateNotDisplayedConfig( bIsAReimport );

	FDatasmithImportOptionHelper::CleanUpOptions(ImportOptions);

	if ( !ActorsContext.ImportWorld )
	{
		// User is asking to import model in a new level
		// Check to see if there is nothing to save and act according to user's selection
		// Note: The code below has been borrowed from UEditorEngine::CreateNewMapForEditing
		if (Options->BaseOptions.SceneHandling == EDatasmithImportScene::NewLevel)
		{
			if (!bSilent)
			{
				// Check to see if there are unsaved data and user wants to save them
				// Import will abort if user selects cancel on Save dialog
				bool bPromptUserToSave = true; // Ask user if he/she wants to save the unsaved data
				bool bSaveMapPackages = true;
				bool bSaveContentPackages = true;
				if (FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages) == false)
				{
					// The user pressed cancel. Abort the import so the user doesn't lose any changes
					return false;
				}
			}

			// Force the creation of a new level
			ActorsContext.ImportWorld = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		}
		else if (Options->BaseOptions.SceneHandling == EDatasmithImportScene::CurrentLevel)
		{
			ActorsContext.ImportWorld = GWorld;
			if (ActorsContext.ImportWorld == nullptr)
			{
				UE_LOG(LogDatasmithImport, Warning, TEXT("Import failed. There is no World/Map open in the Editor."));
				return false;
			}
		}
		else
		{
			ActorsContext.ImportWorld = nullptr;
		}
	}

	FeedbackContext = InWarn;
	Scene = InScene;

	// Initialize the filtered scene as a copy of the original scene. We will use it to then filter out items to import.
	FilteredScene = FDatasmithSceneFactory::DuplicateScene(Scene.ToSharedRef());

	SceneName = Scene->GetName();
	bUserCancelled = false;

	ObjectFlags = InFlags | RF_Transactional;

	bool bResult = AssetsContext.Init();

	if ( ShouldImportActors() )
	{
		bResult = bResult && ActorsContext.Init();
	}

	return bResult;
}

void FDatasmithImportContext::DisplayMessages()
{
	Logger.Dump();
}

void FDatasmithImportContext::SetupBaseOptionsVisibility()
{
	if ( FProperty* ReimportOptionsProperty = FindField< FProperty >( Options->GetClass(), GET_MEMBER_NAME_CHECKED( UDatasmithImportOptions, ReimportOptions ) ) )
	{
		if ( bIsAReimport )
		{
			ReimportOptionsProperty->SetMetaData( TEXT("Category"), TEXT("Reimport") );
			ReimportOptionsProperty->SetMetaData( TEXT("ShowOnlyInnerProperties"), TEXT("1") );
		}
		else
		{
			ReimportOptionsProperty->SetMetaData( TEXT("Category"), TEXT("NotVisible") );
			ReimportOptionsProperty->RemoveMetaData( TEXT("ShowOnlyInnerProperties") );
		}
	}
}

void FDatasmithImportContext::ResetBaseOptionsVisibility()
{
	FProperty* ReimportOptionsProperty = FindField< FProperty >( Options->GetClass(), GET_MEMBER_NAME_CHECKED( UDatasmithImportOptions, ReimportOptions ) );

	if ( ReimportOptionsProperty )
	{
		ReimportOptionsProperty->SetMetaData( TEXT("ShowOnlyInnerProperties"), TEXT("1") );
		ReimportOptionsProperty->SetMetaData( TEXT("Category"), TEXT("NotVisible") );
	}
}

void FDatasmithImportContext::AddImportedActor(AActor* InActor)
{
	ImportedActorMap.Add(InActor->GetName(), InActor);
}

TArray<AActor*> FDatasmithImportContext::GetImportedActors() const
{
	TArray<AActor*> Result;
	if (ImportedActorMap.Num())
	{
		Result.Reserve(ImportedActorMap.Num());
		for (const auto& Itt : ImportedActorMap)
		{
			Result.Add(Itt.Value);
		}
	}

	return Result;
}

void FDatasmithImportContext::AddSceneComponent(const FString & InName, USceneComponent* InMeshComponent)
{
	ImportedSceneComponentMap.Add(InName, InMeshComponent);
}

bool FDatasmithImportContext::ShouldImportActors() const
{
	return ActorsContext.ImportWorld && Options->BaseOptions.SceneHandling != EDatasmithImportScene::AssetsOnly;
}

FDatasmithImportContext::FInternalReferenceCollector::FInternalReferenceCollector(FDatasmithImportContext* InImportContext)
	: ImportContext(InImportContext)
{
}

namespace DatasmithImportContextInternal
{
	template<typename T>
	void AddReferenceList(FReferenceCollector& Collector, T& List)
	{
		for (auto& Ref : List)
		{
			Collector.AddReferencedObject(Ref.Value);
		}
	}
}

void FDatasmithImportContext::FInternalReferenceCollector::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ImportContext->ActorsContext.ImportSceneActor);
	Collector.AddReferencedObject(ImportContext->ActorsContext.ImportWorld);
	Collector.AddReferencedObject(ImportContext->ActorsContext.FinalWorld);

	Collector.AddReferencedObject(ImportContext->RootBlueprint);
	Collector.AddReferencedObject(ImportContext->SceneAsset);

	for ( TMap< TSharedRef< IDatasmithMeshElement >, UStaticMesh* >::TIterator It = ImportContext->ImportedStaticMeshes.CreateIterator(); It; ++It )
	{
		Collector.AddReferencedObject( It->Value );
	}

	Collector.AddReferencedObjects(ImportContext->ImportedMaterials);
	Collector.AddReferencedObjects(ImportContext->ImportedParentMaterials);

	for ( TMap< TSharedRef< IDatasmithLevelSequenceElement >, ULevelSequence* >::TIterator It = ImportContext->ImportedLevelSequences.CreateIterator(); It; ++It )
	{
		if ( It->Value )
		{
			Collector.AddReferencedObject(It->Value);
		}
	}

	for ( TMap< TSharedRef< IDatasmithLevelVariantSetsElement >, ULevelVariantSets* >::TIterator It = ImportContext->ImportedLevelVariantSets.CreateIterator(); It; ++It )
	{
		if ( It->Value )
		{
			Collector.AddReferencedObject(It->Value);
		}
	}

	DatasmithImportContextInternal::AddReferenceList(Collector, ImportContext->ImportedActorMap);
	DatasmithImportContextInternal::AddReferenceList(Collector, ImportContext->ImportedSceneComponentMap);
}

FDatasmithActorUniqueLabelProvider::FDatasmithActorUniqueLabelProvider(UWorld* World)
{
	PopulateLabelFrom(World);
}


void FDatasmithActorUniqueLabelProvider::PopulateLabelFrom(UWorld* World)
{
	if (World)
	{
		Clear();
		for (FActorIterator It(World); It; ++It)
		{
			AddExistingName(It->GetActorLabel());
		}
	}
}

FDatasmithActorImportContext::FDatasmithActorImportContext(UWorld* World)
	: ImportSceneActor(nullptr)
	, CurrentTargetedScene(nullptr)
	, ImportWorld(World)
	, FinalWorld(World)
{
}

bool FDatasmithActorImportContext::Init()
{
	UniqueNameProvider.PopulateLabelFrom( ImportWorld ); // TODO: Should probably populate based on FinalWorld
	return true;
}

FDatasmithAssetsImportContext::FDatasmithAssetsImportContext( FDatasmithImportContext& ImportContext )
	: ParentContext( ImportContext )
{
}

bool FDatasmithAssetsImportContext::Init()
{
	FString NewRootFolder;
	if ( ParentContext.SceneAsset )
	{
		NewRootFolder = FPackageName::GetLongPackagePath( ParentContext.SceneAsset->GetOutermost()->GetName() );
	}
	else
	{
		NewRootFolder = FPaths::Combine( ParentContext.Options->BaseOptions.AssetOptions.PackagePath.ToString(), ParentContext.SceneName );
	}

	ReInit(NewRootFolder);

	return true;
}

void FDatasmithAssetsImportContext::ReInit(const FString& NewRootFolder)
{
	RootFolderPath = UPackageTools::SanitizePackageName(NewRootFolder);

	StaticMeshesFinalPackage.Reset( CreatePackage( nullptr, *FPaths::Combine( RootFolderPath, TEXT("Geometries") ) ) );
	MaterialsFinalPackage.Reset( CreatePackage( nullptr, *FPaths::Combine( RootFolderPath, TEXT("Materials") ) ) );
	TexturesFinalPackage.Reset( CreatePackage( nullptr, *FPaths::Combine( RootFolderPath, TEXT("Textures") ) ) );
	LightPackage.Reset( CreatePackage( nullptr, *FPaths::Combine( RootFolderPath, TEXT("Lights") ) ) );
	LevelSequencesFinalPackage.Reset( CreatePackage( nullptr, *FPaths::Combine( RootFolderPath, TEXT("Animations") ) ) );
	LevelVariantSetsFinalPackage.Reset( CreatePackage( nullptr, *FPaths::Combine( RootFolderPath, TEXT("Variants") ) ) );

	TransientFolderPath = FPaths::Combine( RootFolderPath, TEXT("Temp") );

	StaticMeshesImportPackage.Reset( NewObject< UPackage >( nullptr, *FPaths::Combine( TransientFolderPath, TEXT("Geometries") ), RF_Transient ) );
	StaticMeshesImportPackage->FullyLoad();

	TexturesImportPackage.Reset( NewObject< UPackage >( nullptr, *FPaths::Combine( TransientFolderPath, TEXT("Textures") ), RF_Transient ) );
	TexturesImportPackage->FullyLoad();

	MaterialsImportPackage.Reset( NewObject< UPackage >( nullptr, *FPaths::Combine( TransientFolderPath, TEXT("Materials") ), RF_Transient ) );
	MaterialsImportPackage->FullyLoad();

	MasterMaterialsImportPackage.Reset(NewObject< UPackage >(nullptr, *FPaths::Combine(TransientFolderPath, TEXT("Materials/Master")), RF_Transient));
	MasterMaterialsImportPackage->FullyLoad();

	MaterialFunctionsImportPackage.Reset( NewObject< UPackage >(nullptr, *FPaths::Combine(TransientFolderPath, TEXT("Materials/Master/Functions")), RF_Transient));
	MaterialFunctionsImportPackage->FullyLoad();

	LevelSequencesImportPackage.Reset( NewObject< UPackage >( nullptr, *FPaths::Combine( TransientFolderPath, TEXT("Animations") ), RF_Transient ) );
	LevelSequencesImportPackage->FullyLoad();

	LevelVariantSetsImportPackage.Reset( NewObject< UPackage >( nullptr, *FPaths::Combine( TransientFolderPath, TEXT("Variants") ), RF_Transient ) );
	LevelVariantSetsImportPackage->FullyLoad();
}

#undef LOCTEXT_NAMESPACE
