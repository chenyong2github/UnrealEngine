// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimedDataMonitorPanel.h"

#include "Engine/Engine.h"
#include "ITimedDataInput.h"
#include "ITimeManagementModule.h"
#include "LevelEditor.h"
#include "TimedDataInputCollection.h"
#include "TimedDataMonitorSubsystem.h"

#include "STimecodeProvider.h"
#include "STimedDataListView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/TimedDataMonitorStyle.h"

#define LOCTEXT_NAMESPACE "STimedDataMonitorPanel"


TWeakPtr<STimedDataMonitorPanel> STimedDataMonitorPanel::WidgetInstance;
FDelegateHandle STimedDataMonitorPanel::LevelEditorTabManagerChangedHandle;


namespace Utilities
{
	static const FName NAME_App = FName("TimedDataSourceMonitorPanelApp");
	static const FName NAME_LevelEditorModuleName("LevelEditor");

	TSharedRef<SDockTab> CreateTab(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(STimedDataMonitorPanel)
			];
	}
}


void STimedDataMonitorPanel::RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	auto RegisterTabSpawner = [InWorkspaceItem]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(Utilities::NAME_LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		LevelEditorTabManager->RegisterTabSpawner(Utilities::NAME_App, FOnSpawnTab::CreateStatic(&Utilities::CreateTab))
			.SetDisplayName(LOCTEXT("TabTitle", "Timed Input Monitor"))
			.SetTooltipText(LOCTEXT("TooltipText", "Monitor inputs that can be time synchronized"))
			.SetGroup(InWorkspaceItem)
			.SetIcon(FSlateIcon(FTimedDataMonitorStyle::Get().GetStyleSetName(), "Img.Timecode.Small"));
	};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(Utilities::NAME_LevelEditorModuleName);
	if (LevelEditorModule.GetLevelEditorTabManager())
	{
		RegisterTabSpawner();
	}
	else
	{
		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda(RegisterTabSpawner);
	}
}


void STimedDataMonitorPanel::UnregisterNomadTabSpawner()
{
	if (FSlateApplication::IsInitialized() && FModuleManager::Get().IsModuleLoaded(Utilities::NAME_LevelEditorModuleName))
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(Utilities::NAME_LevelEditorModuleName);
		TSharedPtr<FTabManager> LevelEditorTabManager;
		if (LevelEditorModule)
		{
			LevelEditorTabManager = LevelEditorModule->GetLevelEditorTabManager();
			LevelEditorModule->OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
		}

		if (LevelEditorTabManager.IsValid())
		{
			LevelEditorTabManager->UnregisterTabSpawner(Utilities::NAME_App);
		}
	}
}


TSharedPtr<STimedDataMonitorPanel> STimedDataMonitorPanel::GetPanelInstance()
{
	return STimedDataMonitorPanel::WidgetInstance.Pin();
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void STimedDataMonitorPanel::Construct(const FArguments& InArgs)
{
	WidgetInstance = StaticCastSharedRef<STimedDataMonitorPanel>(AsShared());

	ChildSlot
	[
		SNew(SVerticalBox)
		// toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 10.f, 10.f, 10.f)
		[
			SNew(SBorder)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("Calibrate_ToolTip", "Calibrate"))
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(FMargin(4, 0))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(FTimedDataMonitorStyle::Get(), "TextBlock.Large")
						.Text(LOCTEXT("Calibrate", "Calibrate"))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				.Padding(0.f)
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("ResetErrors_ToolTip", "Reset Errors"))
					.ContentPadding(FMargin(4, 0))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.OnClicked(this, &STimedDataMonitorPanel::OnResetErrors)
					[
						SNew(STextBlock)
						.TextStyle(FTimedDataMonitorStyle::Get(), "TextBlock.Regular")
						.Text(LOCTEXT("ResetErrors", "Reset Errors"))
					]
				]
			]
		]
		// timing element
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SBorder)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Placeholder", "Placehoder for GL"))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.Padding(0.f)
			[
				SNew(SBorder)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STimecodeProvider)
					.DisplayFrameRate(true)
				]
			]
		]
		// sources list
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(10.f, 0.f, 10.f, 10.f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				SAssignNew(TimedDataSourceList, STimedDataInputListView)
			]
		]
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION


FReply STimedDataMonitorPanel::OnResetErrors()
{
	UTimedDataMonitorSubsystem* TimedDataMonitorSubsystem = GEngine->GetEngineSubsystem<UTimedDataMonitorSubsystem>();
	check(TimedDataMonitorSubsystem);
	TimedDataMonitorSubsystem->ResetAllBufferStats();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
