// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithInterchangeModule.h"

#include "InterchangeDatasmithAreaLightFactory.h"
#include "InterchangeDatasmithLog.h"
#include "InterchangeDatasmithPipeline.h"
#include "InterchangeDatasmithSceneFactory.h"
#include "InterchangeDatasmithTranslator.h"

#include "InterchangeReferenceMaterials/DatasmithReferenceMaterialManager.h"
#include "InterchangeReferenceMaterials/DatasmithC4DMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithCityEngineMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithRevitMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithSketchupMaterialSelector.h"
#include "InterchangeReferenceMaterials/DatasmithStdMaterialSelector.h"

#include "DatasmithTranslatorManager.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericScenesPipeline.h"
#include "InterchangeManager.h"
#include "Logging/LogMacros.h"

#if WITH_EDITOR
#include "DatasmithImporterModule.h"
#include "DesktopPlatformModule.h"
#include "Dialogs/DlgPickPath.h"
#include "EditorDirectories.h"
#include "IDesktopPlatform.h"
#include "ObjectTools.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "DatasmithInterchange"


DEFINE_LOG_CATEGORY(LogInterchangeDatasmith);

class FDatasmithInterchangeModule : public IDatasmithInterchangeModule
{
public:
	virtual void StartupModule() override
	{
		using namespace UE::DatasmithInterchange;

		// Load the blueprint asset into memory while wew're on the game thread so that GetAreaLightActorBPClass() can safely be called from other threads.
		UBlueprint* AreaLightBlueprint = Cast<UBlueprint>(FSoftObjectPath(TEXT("/DatasmithContent/Datasmith/DatasmithArealight.DatasmithArealight")).TryLoad());
		ensure(AreaLightBlueprint != nullptr);

		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		InterchangeManager.RegisterTranslator(UInterchangeDatasmithTranslator::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeDatasmithSceneFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeDatasmithAreaLightFactory::StaticClass());

#if WITH_EDITOR
		IDatasmithImporterModule& DatasmithImporterModule = IDatasmithImporterModule::Get();
		DatasmithImporterModule.OnGenerateDatasmithImportMenu().AddRaw(this, &FDatasmithInterchangeModule::ExtendDatasmitMenuOptions);
#endif //WITH_EDITOR

		FDatasmithReferenceMaterialManager::Create();

		//A minimal set of natively supported reference materials
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("C4D"), MakeShared< FDatasmithC4DMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("Revit"), MakeShared< FDatasmithRevitMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("SketchUp"), MakeShared< FDatasmithSketchUpMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("CityEngine"), MakeShared< FDatasmithCityEngineMaterialSelector >());
		FDatasmithReferenceMaterialManager::Get().RegisterSelector(TEXT("StdMaterial"), MakeShared< FDatasmithStdMaterialSelector >());
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (IDatasmithImporterModule::IsAvailable())
		{
			IDatasmithImporterModule& DatasmithImporterModule = IDatasmithImporterModule::Get();
			DatasmithImporterModule.OnGenerateDatasmithImportMenu().RemoveAll(this);
		}
#endif //WITH_EDITOR

		UE::DatasmithInterchange::FDatasmithReferenceMaterialManager::Destroy();
	}

private:
#if WITH_EDITOR
	void ExtendDatasmitMenuOptions(FToolMenuSection& SubSection);

	void OnImportInterchange();

	bool Import(const FString& FilePath, const FString& ContentPath);
#endif //WITH_EDITOR
};

#if WITH_EDITOR
void FDatasmithInterchangeModule::ExtendDatasmitMenuOptions(FToolMenuSection& SubSection)
{
	SubSection.AddMenuEntry(
		TEXT("InterchangeImportFile"),
		LOCTEXT("DatasmithInterchangeFileImport", "Interchange Import..."), // label
		LOCTEXT("DatasmithInterchangeFileImportTooltip", "Experimental: Import Unreal Datasmith file using Interchange"), // description
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FDatasmithInterchangeModule::OnImportInterchange))
	);
}

void FDatasmithInterchangeModule::OnImportInterchange()
{
	const TArray<FString> Formats = FDatasmithTranslatorManager::Get().GetSupportedFormats();
	FString FileTypes;
	FString Extensions;

	FString DefaultLocation(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT));

	ObjectTools::AppendFormatsFileExtensions(Formats, FileTypes, Extensions);

	const FString FormatString = FString::Printf(TEXT("All Files (%s)|%s|%s"), *Extensions, *Extensions, *FileTypes);
	const FString Title = LOCTEXT("BrowseSourceDialogTitle", "Select Datasmith Source File").ToString();

	TArray<FString> OutOpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpened = false;
	if (DesktopPlatform)
	{
		bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			Title,
			DefaultLocation,
			FString(),
			FormatString,
			EFileDialogFlags::None,
			OutOpenFilenames
		);
	}

	if (bOpened && OutOpenFilenames.Num() > 0)
	{
		static FString ContentPath = "/Game/"; // Trailing '/' is needed to set the default path

		TSharedRef<SDlgPickPath> PickContentPathDlg =
			SNew(SDlgPickPath)
			.Title(LOCTEXT("ChooseImportRootContentPath", "Choose Location for importing the Datasmith content"))
			.DefaultPath(FText::FromString(ContentPath));

		if (PickContentPathDlg->ShowModal() != EAppReturnType::Cancel)
		{
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(OutOpenFilenames[0])); // Save path as default for next time.

			ContentPath = PickContentPathDlg->GetPath().ToString() + "/";
			Import(OutOpenFilenames[0], ContentPath);
		}
	}
}

bool FDatasmithInterchangeModule::Import(const FString& FilePath, const FString& ContentPath)
{
	UE::Interchange::FScopedSourceData ScopedSourceData(FilePath);

	if (!UInterchangeManager::GetInterchangeManager().CanTranslateSourceData(ScopedSourceData.GetSourceData()))
	{
		return false;
	}

	FImportAssetParameters ImportAssetParameters;
	ImportAssetParameters.bIsAutomated = false;

	UInterchangeDatasmithPipeline* DatasmithPipeline = NewObject< UInterchangeDatasmithPipeline >();

	ImportAssetParameters.OverridePipelines.Add(DatasmithPipeline);

	TTuple<UE::Interchange::FAssetImportResultRef, UE::Interchange::FSceneImportResultRef> ImportResults
		= UInterchangeManager::GetInterchangeManager().ImportSceneAsync(ContentPath, ScopedSourceData.GetSourceData(), ImportAssetParameters);
	return ImportResults.Key->IsValid() && ImportResults.Value->IsValid();
}
#endif //WITH_EDITOR

IMPLEMENT_MODULE(FDatasmithInterchangeModule, DatasmithInterchange);

#undef LOCTEXT_NAMESPACE