// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundEffectPresetEditor.h"

#include "Blueprint/UserWidget.h"
#include "Containers/Set.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "WidgetBlueprint.h"
#include "AudioEditorModule.h"


#define LOCTEXT_NAMESPACE "SoundEffectPresetEditor"


const FName FSoundEffectPresetEditor::AppIdentifier(TEXT("SoundEffectPresetEditorApp"));
const FName FSoundEffectPresetEditor::PropertiesTabId(TEXT("SoundEffectPresetEditor_Properties"));
const FName FSoundEffectPresetEditor::UserWidgetTabId(TEXT("SoundEffectPresetEditor_UserWidget"));

FSoundEffectPresetEditor::FSoundEffectPresetEditor()
	: SoundEffectPreset(nullptr)
	, UserWidget(nullptr)
{
}

void FSoundEffectPresetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_SoundEffectPresetEditor", "Sound Effect Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FSoundEffectPresetEditor::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));


	if (UserWidget)
	{
		const FString ClassName = SoundEffectPreset->GetClass()->GetName();
		FSlateIcon BPIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.CreateClassBlueprint");
		InTabManager->RegisterTabSpawner(UserWidgetTabId, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args) { return SpawnTab_UserWidgetEditor(Args); }))
			.SetDisplayName(FText::Format(LOCTEXT("UserEditorTabFormat", "{0} Editor"), FText::FromString(ClassName)))
			.SetGroup(WorkspaceMenuCategory.ToSharedRef())
			.SetIcon(BPIcon);
	}
}

void FSoundEffectPresetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(PropertiesTabId);

	if (UserWidget)
	{
		InTabManager->UnregisterTabSpawner(UserWidgetTabId);
	}
}

void FSoundEffectPresetEditor::Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundEffectPreset* InPresetToEdit)
{
	check(InPresetToEdit);

	SoundEffectPreset = InPresetToEdit;
	InitPresetWidget();

	// Support undo/redo
	InPresetToEdit->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertiesView = PropertyModule.CreateDetailView(Args);
	PropertiesView->SetObject(InPresetToEdit);

	TSharedRef<FTabManager::FSplitter> TabSplitter = FTabManager::NewSplitter()
		->SetSizeCoefficient(0.9f)
		->SetOrientation(Orient_Horizontal)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.225f)
			->AddTab(PropertiesTabId, ETabState::OpenedTab)
		);

	if (UserWidget)
	{
		TabSplitter->Split
		(
			FTabManager::NewSplitter()
			->SetSizeCoefficient(0.775f)
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(true)
				->SetSizeCoefficient(0.33f)
				->AddTab(UserWidgetTabId, ETabState::OpenedTab)
			)
		);
	}

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_SoundEffectPresetEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split(TabSplitter)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	const bool bToolbarFocusable = false;
	const bool bUseSmallIcons = true;
	FAssetEditorToolkit::InitAssetEditor(
		Mode,
		InitToolkitHost,
		AppIdentifier,
		StandaloneDefaultLayout,
		bCreateDefaultStandaloneMenu,
		bCreateDefaultToolbar,
		InPresetToEdit,
		bToolbarFocusable,
		bUseSmallIcons);
}

FName FSoundEffectPresetEditor::GetToolkitFName() const
{
	return FName("SoundEffectPresetEditor");
}

FText FSoundEffectPresetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Sound Effect Editor");
}

void FSoundEffectPresetEditor::InitPresetWidget()
{
	if (!SoundEffectPreset)
	{
		return;
	}

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");

		UClass* PresetClass = SoundEffectPreset->GetClass();
		if (const UWidgetBlueprint* WidgetBP = AudioEditorModule->GetSoundEffectPresetWidget(PresetClass))
		{
			if (UClass* GeneratedClass = WidgetBP->GeneratedClass.Get())
			{
				UserWidget = CreateWidget<USoundEffectPresetUserWidget>(World, GeneratedClass);
				UserWidget->Preset = SoundEffectPreset;
			}
		}
	}
}

FString FSoundEffectPresetEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "SoundEffect ").ToString();
}

FLinearColor FSoundEffectPresetEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

EOrientation FSoundEffectPresetEditor::GetSnapLabelOrientation() const
{
	return FMultiBoxSettings::UseSmallToolBarIcons.Get()
		? EOrientation::Orient_Horizontal
		: EOrientation::Orient_Vertical;
}

void FSoundEffectPresetEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (UserWidget && SoundEffectPreset)
	{
		const FName PropertyName = PropertyThatChanged->GetFName();
		UserWidget->OnPresetChanged(PropertyName);
	}
}

void FSoundEffectPresetEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SoundEffectPreset);
	Collector.AddReferencedObject(UserWidget);
}

FString FSoundEffectPresetEditor::GetReferencerName() const
{
	return "SoundEffectPresetEditor";
}

TSharedRef<SDockTab> FSoundEffectPresetEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("SoundSoundEffectDetailsTitle", "Details"))
		[
			PropertiesView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FSoundEffectPresetEditor::SpawnTab_UserWidgetEditor(const FSpawnTabArgs& Args)
{
	const FSlateBrush* IconBrush = FEditorStyle::GetBrush("SoundEffectPresetEditor.Tabs.Properties");
	const FText Label = FText::FromString(GetEditingObject()->GetName());

	if (!UserWidget)
	{
		return SNew(SDockTab)
			.Icon(IconBrush)
			.Label(Label)
			.TabColorScale(GetTabColorScale())
			[
				SNew(STextBlock)
					.Text(LOCTEXT("InvalidPresetEditor", "No editor available for SoundEffectPreset.  Widget Blueprint not found."))
			];
	}

	return SNew(SDockTab)
		.Icon(IconBrush)
		.Label(Label)
		.TabColorScale(GetTabColorScale())
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.0f)
			[
				UserWidget->TakeWidget()
			]
		];
}

void FSoundEffectPresetEditor::PostUndo(bool bSuccess)
{
}

void FSoundEffectPresetEditor::PostRedo(bool bSuccess)
{
}
#undef LOCTEXT_NAMESPACE
