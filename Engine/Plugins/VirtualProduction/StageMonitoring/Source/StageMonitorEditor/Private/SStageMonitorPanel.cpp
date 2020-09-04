// Copyright Epic Games, Inc. All Rights Reserved.

#include "SStageMonitorPanel.h"

#include "EditorFontGlyphs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ISettingsModule.h"
#include "IStageDataCollection.h"
#include "IStageMonitor.h"
#include "IStageMonitorModule.h"
#include "Modules/ModuleManager.h"
#include "SlateOptMacros.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SDataProviderActivities.h"
#include "Widgets/SDataProviderListView.h"

#define LOCTEXT_NAMESPACE "SStageMonitorPanel"


TWeakPtr<SStageMonitorPanel> SStageMonitorPanel::PanelInstance;
FDelegateHandle SStageMonitorPanel::LevelEditorTabManagerChangedHandle;


namespace StageMonitorUtilities
{
	static const FName NAME_App = FName("StageMonitorPanelApp");
	static const FName NAME_MessageViewerName = FName("StageMessageViewer");
	static const FName NAME_LevelEditorModuleName = FName("LevelEditor");

	TSharedRef<SDockTab> CreateApp(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SStageMonitorPanel)
			];
	}
}


void SStageMonitorPanel::RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	auto RegisterTabSpawner = [InWorkspaceItem]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(StageMonitorUtilities::NAME_LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		LevelEditorTabManager->RegisterTabSpawner(StageMonitorUtilities::NAME_App, FOnSpawnTab::CreateStatic(&StageMonitorUtilities::CreateApp))
			.SetDisplayName(LOCTEXT("TabTitle", "Stage Monitor"))
			.SetTooltipText(LOCTEXT("TooltipText", "Monitor performance data from stage machines"))
			.SetGroup(InWorkspaceItem)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings.Small")); //todo t2
	};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(StageMonitorUtilities::NAME_LevelEditorModuleName);
	if (LevelEditorModule.GetLevelEditorTabManager())
	{
		RegisterTabSpawner();
	}
	else
	{
		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda(RegisterTabSpawner);
	}
}

void SStageMonitorPanel::UnregisterNomadTabSpawner()
{
	if (FSlateApplication::IsInitialized() && FModuleManager::Get().IsModuleLoaded(StageMonitorUtilities::NAME_LevelEditorModuleName))
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(StageMonitorUtilities::NAME_LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager;
		if (LevelEditorModule)
		{
			LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
			LevelEditorModule->OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}

		if (LevelEditorTabManager.IsValid())
		{
			LevelEditorTabManager->UnregisterTabSpawner(StageMonitorUtilities::NAME_App);
		}
	}
}

TSharedPtr<SStageMonitorPanel> SStageMonitorPanel::GetPanelInstance()
{
	return SStageMonitorPanel::PanelInstance.Pin();
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SStageMonitorPanel::Construct(const FArguments& InArgs)
{
	PanelInstance = StaticCastSharedRef<SStageMonitorPanel>(AsShared());

	ChildSlot
	[
		SNew(SVerticalBox)
		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 10.f, 10.f, 10.f)
		[
			MakeToolbarWidget()
		]
		// Data Provider List
		+ SVerticalBox::Slot()
		.FillHeight(.2)
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				SAssignNew(DataProviderList, SDataProviderListView, IStageMonitorModule::Get().GetStageMonitor().GetDataCollection())
			]
		]
		// Data Provider Activities
		+ SVerticalBox::Slot()
		.FillHeight(.8)
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				SAssignNew(DataProviderActivities, SDataProviderActivities, SharedThis<SStageMonitorPanel>(this), IStageMonitorModule::Get().GetStageMonitor().GetDataCollection())
			]
		]

		// Monitor status
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				SNew(SHorizontalBox)
				// Spacer
				+ SHorizontalBox::Slot()
				[
					SNew(SSpacer)
				]
				// Monitor status
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.Padding(10.f, 10.f, 10.f, 10.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MonitorStatus", "Monitor Status : "))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.Padding(10.f, 10.f, 10.f, 10.f)
				[
					SNew(STextBlock)
					.Text(this, &SStageMonitorPanel::GetMonitorStatus)
				]
			]
		]
	];
}

TSharedRef<SWidget> SStageMonitorPanel::MakeToolbarWidget()
{
	TSharedRef<SWidget> Toolbar = SNew(SBorder)
	.VAlign(VAlign_Center)
	.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
	[
		SNew(SHorizontalBox)
		// Clear entries buttons
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.ToolTipText(LOCTEXT("ClearAllEntries", "Clear all entries received by the stage monitor."))
			.ContentPadding(FMargin(4, 4))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &SStageMonitorPanel::OnClearEntriesClicked)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
				.Text(FEditorFontGlyphs::Eraser)
				.ColorAndOpacity(FLinearColor::White)
			]
		]
		// todo : Show Message Viewer 
		/*+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.ToolTipText(LOCTEXT("ShowMessageViewer_ToolTip", "Open the message viewer"))
			.ContentPadding(FMargin(4, 2))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &SStageMonitorPanel::OnShowMessageViewerClicked)
			[
				SNew(SImage)
				.Image(FEditorStyle::Get().GetBrush("GenericViewButton"))
			]
		]*/
		// Settings button
		+ SHorizontalBox::Slot()
		.Padding(4.f)
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
			.ToolTipText(LOCTEXT("ShowProjectSettings_Tip", "Show the StageMonitor project settings"))
			.ContentPadding(FMargin(4, 4))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &SStageMonitorPanel::OnShowProjectSettingsClicked)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
				.Text(FEditorFontGlyphs::Cogs)
				.ColorAndOpacity(FLinearColor::White)
			]
		]
		// Spacer
		+ SHorizontalBox::Slot()
		[
			SNew(SSpacer)
		]
		// Stage status
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.Padding(4.f)
		[
			// Stage status icon
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.18"))
				.Text(FEditorFontGlyphs::Circle)
				.ColorAndOpacity(this, &SStageMonitorPanel::GetStageStatus)
			]
			// Stage status text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f)
			[
				SNew(SVerticalBox)
				// Critical state text
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CriticalStateHeader", "Critical Source Name"))
					.ColorAndOpacity(FLinearColor::White)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SStageMonitorPanel::GetStageActiveStateReasonText)
					.TextStyle(FEditorStyle::Get(), "LargeText")
					.ColorAndOpacity(FLinearColor::White)
				]
			]
		]
	];

	return Toolbar;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SStageMonitorPanel::OnClearEntriesClicked()
{
	IStageMonitorModule::Get().GetStageMonitor().GetDataCollection()->ClearAll();
	return FReply::Handled();
}

FReply SStageMonitorPanel::OnShowProjectSettingsClicked()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "Stage Monitor");
	return FReply::Handled();
}

FSlateColor SStageMonitorPanel::GetStageStatus() const
{
	IStageMonitor& StageMonitor = IStageMonitorModule::Get().GetStageMonitor();
	if (StageMonitor.IsStageInCriticalState())
	{
		return FLinearColor::Red;
	}

	return FLinearColor::Green;
}

FText SStageMonitorPanel::GetStageActiveStateReasonText() const
{
	IStageMonitor& StageMonitor = IStageMonitorModule::Get().GetStageMonitor();
	{
		return FText::FromName(StageMonitor.GetCurrentCriticalStateSource());
	}

	return FText::GetEmpty();
}

FText SStageMonitorPanel::GetMonitorStatus() const
{
	IStageMonitor& StageMonitor = IStageMonitorModule::Get().GetStageMonitor();
	if (StageMonitor.IsActive())
	{
		return LOCTEXT("MonitorStatusActive", "Active");
	}
	else
	{
		return LOCTEXT("MonitorStatusInactive", "Inactive");
	}

	return LOCTEXT("MonitorStatusUnavailable", "Unavailable");
}

#undef LOCTEXT_NAMESPACE
