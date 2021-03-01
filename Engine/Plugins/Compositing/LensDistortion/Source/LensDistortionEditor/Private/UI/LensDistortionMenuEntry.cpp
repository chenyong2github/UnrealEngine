// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/LensDistortionMenuEntry.h"

#include "AssetEditor/LensDistortionCommands.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Factories/LensFileFactoryNew.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IAssetTools.h"
#include "Misc/FeedbackContext.h"
#include "LensDistortionSettings.h"
#include "LensDistortionSubsystem.h"
#include "LensFile.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "UI/LensDistortionEditorStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Textures/SlateIcon.h"

#define LOCTEXT_NAMESPACE "LensDistortionMenu"

struct FLensDistortionMenuEntryImpl
{
	FLensDistortionMenuEntryImpl()
	{
		TSharedPtr<FUICommandList> Actions = MakeShared<FUICommandList>();

		// Action to edit the current selected lens file
		Actions->MapAction(FLensDistortionCommands::Get().Edit,
			FExecuteAction::CreateLambda([this]()
			{
				if (ULensFile* LensFile = GetDefaultLensFile())
				{
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LensFile);
				}
			}),
			FCanExecuteAction::CreateLambda([this] 
			{ 
				return GetDefaultLensFile() != nullptr;
			}),
			FIsActionChecked::CreateLambda([this]
			{ 
				return GetDefaultLensFile() != nullptr;
			})
		);

		ToolBarExtender = MakeShared<FExtender>();
		ToolBarExtender->AddToolBarExtension("Settings", EExtensionHook::After, Actions, FToolBarExtensionDelegate::CreateRaw(this, &FLensDistortionMenuEntryImpl::FillToolbar));

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolBarExtender);
	}

	~FLensDistortionMenuEntryImpl()
	{
		if (!IsEngineExitRequested() && ToolBarExtender.IsValid())
		{
			FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
			if (LevelEditorModule)
			{
				LevelEditorModule->GetToolBarExtensibilityManager()->RemoveExtender(ToolBarExtender);
			}
		}
	}

	ULensFile* GetDefaultLensFile()
	{
		ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>();
		return SubSystem->GetDefaultLensFile();
	}

	void CreateNewLensFile()
	{
		ULensFileFactoryNew* FactoryInstance = DuplicateObject<ULensFileFactoryNew>(GetDefault<ULensFileFactoryNew>(), GetTransientPackage());
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		ULensFile* NewAsset = Cast<ULensFile>(FAssetToolsModule::GetModule().Get().CreateAssetWithDialog(FactoryInstance->GetSupportedClass(), FactoryInstance));
		if (NewAsset != nullptr)
		{
			//If a new lens is created from the toolbar, assign it as startup user lens file 
			//and current default engine lens file.
			GetMutableDefault<ULensDistortionEditorSettings>()->SetUserLensFile(NewAsset);
			ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>();
			return SubSystem->SetDefaultLensFile(NewAsset);

			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
		}

	}

	void NewLensFileSelected(const FAssetData& AssetData)
	{
		FSlateApplication::Get().DismissAllMenus();

		GWarn->BeginSlowTask(LOCTEXT("LensFileLoadPackage", "Loading Lens File"), true, false);
		ULensFile* Asset = Cast<ULensFile>(AssetData.GetAsset());
		GWarn->EndSlowTask();

		//If a new lens is selected from the toolbar, assign it as startup user lens file 
		//and current default engine lens file.
		GetMutableDefault<ULensDistortionEditorSettings>()->SetUserLensFile(Asset);
		ULensDistortionSubsystem* SubSystem = GEngine->GetEngineSubsystem<ULensDistortionSubsystem>();
		return SubSystem->SetDefaultLensFile(Asset);
	}

	void FillToolbar(FToolBarBuilder& ToolbarBuilder)
	{
		ToolbarBuilder.BeginSection("Lens Distortion");
		{
			auto TooltipLambda = [this]()
			{
				ULensFile* LensFile = GetDefaultLensFile();
				if (LensFile == nullptr)
				{
					return LOCTEXT("NoFile_ToolTip", "Select a Lens File to edit it.");
				}
				return FText::Format(LOCTEXT("LensFile_ToolTip", "Edit '{0}'") , FText::FromName(LensFile->GetFName()));
			};

			// Add a button to edit the current lens file
			ToolbarBuilder.AddToolBarButton(
				FLensDistortionCommands::Get().Edit,
				NAME_None,
				LOCTEXT("LensFile_Label", "Lens File"),
				MakeAttributeLambda(TooltipLambda),
				FSlateIcon(FLensDistortionEditorStyle::GetStyleSetName(), TEXT("ToolbarIcon.LensFile"))
			);

			// Add a simple drop-down menu (no label, no icon for the drop-down button itself) that list the lens files available
			ToolbarBuilder.AddComboButton(
				FUIAction(),
				FOnGetContent::CreateRaw(this, &FLensDistortionMenuEntryImpl::GenerateMenuContent),
				FText::GetEmpty(),
				LOCTEXT("LensFileButton_ToolTip", "List of Lens Files available to the user for editing."),
				FSlateIcon(),
				true
			);
		}
		ToolbarBuilder.EndSection();
	}

	TSharedRef<SWidget> GenerateMenuContent()
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

		MenuBuilder.BeginSection("LensFile", LOCTEXT("NewLensFileSection", "New"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateMenuLabel", "New Empty Lens File"),
				LOCTEXT("CreateMenuTooltip", "Create a new Lens File asset."),
				FSlateIcon(FLensDistortionEditorStyle::GetStyleSetName(), TEXT("ClassIcon.LensFile")),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FLensDistortionMenuEntryImpl::CreateNewLensFile)
				)
			);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("LensFile", LOCTEXT("LensFileSection", "Lens File"));
		{
			ULensFile* LensFile = GetDefaultLensFile();
			const bool bIsFileValid = (LensFile != nullptr);

			MenuBuilder.AddSubMenu(
				bIsFileValid ? FText::FromName(LensFile->GetFName()) : LOCTEXT("SelectMenuLabel", "Select the default Lens File"),
				LOCTEXT("SelectMenuTooltip", "Select the default lens file for the project."),
				FNewMenuDelegate::CreateRaw(this, &FLensDistortionMenuEntryImpl::AddObjectSubMenu),
				FUIAction(),
				NAME_None,
				EUserInterfaceActionType::RadioButton
			);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	void AddObjectSubMenu(FMenuBuilder& MenuBuilder)
	{
		ULensFile* LensFile = GetDefaultLensFile();
		FAssetData CurrentAssetData = LensFile ? FAssetData(LensFile) : FAssetData();

		TArray<const UClass*> ClassFilters;
		ClassFilters.Add(ULensFile::StaticClass());

		MenuBuilder.AddWidget(
			PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
				CurrentAssetData,
				LensFile != nullptr,
				false,
				ClassFilters,
				TArray<UFactory*>(),
				FOnShouldFilterAsset::CreateLambda([CurrentAssetData](const FAssetData& InAssetData){ return InAssetData == CurrentAssetData; }),
				FOnAssetSelected::CreateRaw(this, &FLensDistortionMenuEntryImpl::NewLensFileSelected),
				FSimpleDelegate()
			),
			FText::GetEmpty(),
			true,
			false
		);
	}

private:

	TSharedPtr<FExtender> ToolBarExtender;

public:

	static TUniquePtr<FLensDistortionMenuEntryImpl> Implementation;
};

TUniquePtr<FLensDistortionMenuEntryImpl> FLensDistortionMenuEntryImpl::Implementation;

void FLensDistortionMenuEntry::Register()
{
	if (!IsRunningCommandlet() && GetDefault<ULensDistortionEditorSettings>()->bShowEditorToolbarButton)
	{
		FLensDistortionMenuEntryImpl::Implementation = MakeUnique<FLensDistortionMenuEntryImpl>();
	}
}

void FLensDistortionMenuEntry::Unregister()
{
	FLensDistortionMenuEntryImpl::Implementation.Reset();
}

#undef LOCTEXT_NAMESPACE
