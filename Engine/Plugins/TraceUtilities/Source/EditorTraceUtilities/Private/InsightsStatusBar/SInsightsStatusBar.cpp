// Copyright Epic Games, Inc. All Rights Reserved.

#include "SInsightsStatusBar.h"

#include "EditorTraceUtilitiesStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Math/Color.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Styling/StyleColors.h"
#include "ToolMenus.h"
#include "Trace/StoreClient.h"
#include "UnrealInsightsLauncher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "InsightsEditor"

const TCHAR* SInsightsStatusBarWidget::DefaultPreset = TEXT("default");
const TCHAR* SInsightsStatusBarWidget::MemoryPreset= TEXT("default,memory");
const TCHAR* SInsightsStatusBarWidget::TaskGraphPreset = TEXT("default,task");
const TCHAR* SInsightsStatusBarWidget::ContextSwitchesPreset = TEXT("default,contextswitches");

TSharedRef<SWidget> CreateInsightsStatusBarWidget()
{
	return SNew(SInsightsStatusBarWidget);
}

void RegisterInsightsStatusWidgetWithToolMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.StatusBar.ToolBar"));

	FToolMenuSection& InsightsSection = Menu->AddSection(TEXT("Insights"), FText::GetEmpty(), FToolMenuInsert(TEXT("Compile"), EToolMenuInsertType::Before));

	InsightsSection.AddEntry(
		FToolMenuEntry::InitWidget(TEXT("InsightsStatusBar"), CreateInsightsStatusBarWidget(), FText::GetEmpty(), true, false)
	);
}

void SInsightsStatusBarWidget::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(6.0f, 0.0f))
			.MenuPlacement(MenuPlacement_AboveAnchor)
			.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("StatusBar.StatusBarComboButton"))
			.OnGetMenuContent(this, &SInsightsStatusBarWidget::MakeTraceMenu)
			.HasDownArrow(true)
			.ToolTipText(this, &SInsightsStatusBarWidget::GetTitleToolTipText)
			.ButtonContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.Padding(0, 0, 3, 0)
				.AutoWidth()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.Trace"))
				]

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Trace", "Trace"))
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("DialogButtonText"))
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.ToolTipText(this, &SInsightsStatusBarWidget::GetRecordingButtonTooltipText)
			.OnClicked_Lambda([this]() { this->ToggleTracing_OnClicked(); return FReply::Handled(); })
			.OnHovered_Lambda([this]() { this->bIsTraceRecordButtonHovered = true; })
			.OnUnhovered_Lambda([this]() { this->bIsTraceRecordButtonHovered = false; })
			.Content()
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SNew(SImage)
					.ColorAndOpacity(this, &SInsightsStatusBarWidget::GetRecordingButtonColor)
					.Image(FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.RecordTrace"))
					.Visibility(this, &SInsightsStatusBarWidget::GetStartTraceIconVisibility)
				]

				+ SOverlay::Slot()
				[
					SNew(SImage)
					.ColorAndOpacity(this, &SInsightsStatusBarWidget::GetRecordingButtonOutlineColor)
					.Image(FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.RecordTraceOutline"))
					.Visibility(this, &SInsightsStatusBarWidget::GetStartTraceIconVisibility)
				]

				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.RecordTraceStop"))
					.Visibility(this, &SInsightsStatusBarWidget::GetStopTraceIconVisibility)
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.0f, 0.0f, 0.0f, 3.0f))
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.OnClicked_Lambda([this]() { SaveSnapshot(); return FReply::Handled(); })
			.Content()
			[
				SNew(SImage)
				.DesiredSizeOverride(CoreStyleConstants::Icon16x16)
				.Image(FEditorTraceUtilitiesStyle::Get().GetBrush("Icons.TraceSnapshot"))
				.ToolTipText(LOCTEXT("SaveSnapShot","Snapshot: Save Current Trace Buffer To File"))
			]
		]
	];

	Channels = DefaultPreset;
}

TSharedRef<SWidget> SInsightsStatusBarWidget::MakeTraceMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	// To be activated
	//MenuBuilder.BeginSection("TraceDataFiltering", LOCTEXT("TraceMenu_Section_DataFiltering", "Trace Data Filtering"));
	//{
	//	MenuBuilder.AddSubMenu
	//	(
	//		LOCTEXT("Channels", "Channels"),
	//		LOCTEXT("Channels_Desc", "Select what trace channels to enable when tracing."),
	//		FNewMenuDelegate::CreateSP(this, &SInsightsStatusBarWidget::Channels_BuildMenu),
	//		false,
	//		FSlateIcon(FEditorTraceUtilitiesStyle::Get().GetStyleSetName(), ("Icons.Trace"))
	//	);
	//}
	//MenuBuilder.EndSection();

	MenuBuilder.BeginSection("TraceDestination", LOCTEXT("TraceMenu_Section_Destination", "Trace Destination"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ServerLabel", "Trace Store"),
			LOCTEXT("ServerLabelDesc", "Set the trace store as the trace destination."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceDestination_Execute, ETraceDestination::TraceStore),
					  FCanExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceDestination_CanExecute),
					  FIsActionChecked::CreateSP(this, &SInsightsStatusBarWidget::SetTraceDestination_IsChecked, ETraceDestination::TraceStore)),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("FileLabel", "File"),
			LOCTEXT("FileLabelDesc", "Set file as the trace destination."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceDestination_Execute, ETraceDestination::File),
					  FCanExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceDestination_CanExecute),
					  FIsActionChecked::CreateSP(this, &SInsightsStatusBarWidget::SetTraceDestination_IsChecked, ETraceDestination::File)),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Tracing", LOCTEXT("TraceMenu_Section_Tracing", "Tracing"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("SaveSnapshotLabel", "Save Trace Snapshot"),
			LOCTEXT("SaveSnapshotTooltip", "Save the current trace buffer to file."),
			FSlateIcon(FEditorTraceUtilitiesStyle::Get().GetStyleSetName(), "Icons.TraceSnapshot"),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SaveSnapshot),
					  FCanExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SaveSnapshot_CanExecute)),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			TAttribute<FText>::CreateSP(this, &SInsightsStatusBarWidget::GetTraceMenuItemText),
			TAttribute<FText>::CreateSP(this, &SInsightsStatusBarWidget::GetTraceMenuItemTooltipText),
			FSlateIcon(FEditorTraceUtilitiesStyle::Get().GetStyleSetName(), "Icons.StartTrace"),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::ToggleTracing_OnClicked)),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Insights", LOCTEXT("TraceMenu_Section_Insights", "Insights"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenInsightsLabel", "Unreal Insights (Session Browser)"),
			LOCTEXT("OpenInsightsTooltip", "Launch the Unreal Insights Session Browser."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "UnrealInsights.MenuIcon"),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::LaunchUnrealInsights_OnClicked)),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenLiveSessionLabel", "Open Live Session"),
			LOCTEXT("OpenLiveSessionTooltip", "Opening the live session is possible only while tracing to the trace store."),
			FSlateIcon(FEditorTraceUtilitiesStyle::Get().GetStyleSetName(), "Icons.OpenLiveSession"),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::OpenLiveSession_OnClicked),
					  FCanExecuteAction::CreateLambda([this]() { return FTraceAuxiliary::IsConnected() && this->TraceDestination == ETraceDestination::TraceStore; })),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();


	MenuBuilder.BeginSection("Locations", LOCTEXT("TraceMenu_Section_Locations", "Locations"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenProfilingDirectoryLabel", "Open Profiling Directory"),
			LOCTEXT("OpenProfilingDirectoryTooltip", "Opens the profiling directory of the current project. This is the location where traces to file are stored."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.FolderOpen"),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::OpenProfilingDirectory_OnClicked)),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenTraceStoreLabel", "Open Trace Store Directory"),
			LOCTEXT("OpenTraceStoreTooltip", "Open Trace Store Directory. This is the location where traces saved to the trace server are stored."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.FolderOpen"),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::OpenTraceStoreDirectory_OnClicked)),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SInsightsStatusBarWidget::Channels_BuildMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("Presets", LOCTEXT("Presets", "Presets"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DefautLabel", "Default"),
			LOCTEXT("DefaultLabelDesc", "Activate the cpu,gpu,frame,log,bookmark channels."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceChannels, DefaultPreset),
				FCanExecuteAction::CreateLambda([]() {return true; }),
				FIsActionChecked::CreateSP(this, &SInsightsStatusBarWidget::IsPresetSet, DefaultPreset)),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("MemoryLabel", "Memory"),
			LOCTEXT("MemoryLabelDesc", "Activate the memtag,memalloc,callstack,module and the default channels."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceChannels, MemoryPreset),
				FCanExecuteAction::CreateLambda([]() {return false; }),
				FIsActionChecked::CreateSP(this, &SInsightsStatusBarWidget::IsPresetSet, MemoryPreset)),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("TaskGraphLabel", "Task Graph"),
			LOCTEXT("TaskGraphLabelDesc", "Activate the task and the default channels."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceChannels, TaskGraphPreset),
				FCanExecuteAction::CreateLambda([]() {return false; }),
				FIsActionChecked::CreateSP(this, &SInsightsStatusBarWidget::IsPresetSet, TaskGraphPreset)),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Context Switches Label", "Context Switches"),
			LOCTEXT("Context Switches LabelDesc", "Activate the contextswitches and the default channels."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SInsightsStatusBarWidget::SetTraceChannels, ContextSwitchesPreset),
				FCanExecuteAction::CreateLambda([]() { return false; }),
				FIsActionChecked::CreateSP(this, &SInsightsStatusBarWidget::IsPresetSet, ContextSwitchesPreset)),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();
}

FText SInsightsStatusBarWidget::GetTitleToolTipText() const
{
	FTextBuilder DescBuilder;

	const TCHAR* Dest = FTraceAuxiliary::GetTraceDestination();
	
	if (*Dest != 0)
	{
		DescBuilder.AppendLineFormat(LOCTEXT("TracingToText", "Tracing to: {0}"), FText::FromString(Dest));
	}
	else
	{
		DescBuilder.AppendLine(LOCTEXT("NotTracingText","Not currently tracing."));
	}
		
	return DescBuilder.ToText();
}

FSlateColor SInsightsStatusBarWidget::GetRecordingButtonColor() const
{
	if (!FTraceAuxiliary::IsConnected())
	{
		return FStyleColors::White;
	}

	return FStyleColors::Error;
}

FSlateColor SInsightsStatusBarWidget::GetRecordingButtonOutlineColor() const
{
	if (!FTraceAuxiliary::IsConnected())
	{
		ConnectionStartTime = FSlateApplication::Get().GetCurrentTime();
		return FLinearColor::White.CopyWithNewOpacity(0.5f);
	}

	double ElapsedTime = FSlateApplication::Get().GetCurrentTime() - ConnectionStartTime;
	return FStyleColors::Error.GetColor(FWidgetStyle()).CopyWithNewOpacity(0.5f + 0.5f * FMath::MakePulsatingValue(ElapsedTime, 0.5f));
}

FText SInsightsStatusBarWidget::GetRecordingButtonTooltipText() const
{
	if (!FTraceAuxiliary::IsConnected())
	{
		return LOCTEXT("StartTracing", "Start tracing. The trace destination is set from the menu.");
	}

	return LOCTEXT("StopTracing", "Stop Tracing.");
}

void SInsightsStatusBarWidget::SendSnapshotNotification()
{
	FNotificationInfo Info(LOCTEXT("SnapshotSaved", "Insights Snapshot saved."));
	Info.bFireAndForget = true;
	Info.FadeOutDuration = 1.0f;
	Info.ExpireDuration = 4.0f;

	Info.Text = LOCTEXT("SnapshotSavedHeading", "Insights Snapshot saved.");
	Info.SubText = LOCTEXT("SnapshotSavedText", "A snapshot .utrace with the most recent events has been saved to your Saved/Profiling/ directory.");

	FSlateNotificationManager::Get().AddNotification(Info);
}

void SInsightsStatusBarWidget::SendTraceStartedNotification()
{
	FNotificationInfo Info(LOCTEXT("TracingStarted", "Tracing started."));
	Info.bFireAndForget = true;
	Info.FadeOutDuration = 1.0f;
	Info.ExpireDuration = 4.0f;

	Info.Text = LOCTEXT("TracingStartedHeading", "Tracing started.");
	Info.SubText = FText::Format(
		LOCTEXT("TracingStartedText", "Trace is now active and saving to the following location (file or tracestore):\n{0}")
		, FText::FromString(FTraceAuxiliary::GetTraceDestination())
	);
	FSlateNotificationManager::Get().AddNotification(Info);
}

void SInsightsStatusBarWidget::LaunchUnrealInsights_OnClicked()
{
	FUnrealInsightsLauncher::Get()->StartUnrealInsights(FUnrealInsightsLauncher::Get()->GetInsightsApplicationPath());
}

void SInsightsStatusBarWidget::OpenLiveSession_OnClicked()
{
	FUnrealInsightsLauncher::Get()->TryOpenTraceFromDestination(FTraceAuxiliary::GetTraceDestination());
}


void SInsightsStatusBarWidget::OpenProfilingDirectory_OnClicked()
{
	FString FullPath(FPaths::ConvertRelativePathToFull(FPaths::ProfilingDir()));
	FPlatformProcess::ExploreFolder(*FullPath);
}

void SInsightsStatusBarWidget::OpenTraceStoreDirectory_OnClicked()
{
	UE::Trace::FStoreClient* StoreClient = UE::Trace::FStoreClient::Connect(TEXT("localhost"));

	if (!StoreClient)
	{
		// TODO: Add Error Message
		return;
	}

	const UE::Trace::FStoreClient::FStatus* Status = StoreClient->GetStatus();
	if (!Status)
	{
		// TODO: Add Error Message
		return;
	}

	FString Path(Status->GetStoreDir());

	FString FullPath(FPaths::ConvertRelativePathToFull(Path));
	FPlatformProcess::ExploreFolder(*FullPath);
}

void SInsightsStatusBarWidget::SetTraceDestination_Execute(ETraceDestination InDestination)
{
	TraceDestination = InDestination;
}

bool SInsightsStatusBarWidget::SetTraceDestination_IsChecked(ETraceDestination InDestination)
{
	return InDestination == TraceDestination;
}

bool SInsightsStatusBarWidget::SetTraceDestination_CanExecute()
{
	if (!FTraceAuxiliary::IsConnected())
	{
		return true;
	}

	return false;
}

void SInsightsStatusBarWidget::SaveSnapshot()
{
	FTraceAuxiliary::WriteSnapshot(nullptr);
	SendSnapshotNotification();
}

bool SInsightsStatusBarWidget::SaveSnapshot_CanExecute()
{
	return true;
}

FText SInsightsStatusBarWidget::GetTraceMenuItemText() const
{
	if (FTraceAuxiliary::IsConnected())
	{
		return LOCTEXT("StopTraceButtonText", "Stop Trace");
	}

	return LOCTEXT("StartTraceButtonText", "Start Trace");
}

FText SInsightsStatusBarWidget::GetTraceMenuItemTooltipText() const
{
	if (FTraceAuxiliary::IsConnected())
	{
		return LOCTEXT("StopTraceButtonTooltip", "Stop tracing");
	}

	return LOCTEXT("StartTraceButtonTooltip", "Start tracing to the selected trace destination.");
}

void SInsightsStatusBarWidget::ToggleTracing_OnClicked()
{
	if (FTraceAuxiliary::IsConnected())
	{
		FTraceAuxiliary::Stop();
	}
	else
	{
		StartTracing();
		SendTraceStartedNotification();
	}
}

void SInsightsStatusBarWidget::StartTracing()
{
	if (TraceDestination == ETraceDestination::TraceStore)
	{
		FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::Network, TEXT("localhost"), Channels);
	}
	else if (TraceDestination == ETraceDestination::File)
	{
		FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, nullptr, Channels);
	}
}

EVisibility SInsightsStatusBarWidget::GetStartTraceIconVisibility() const
{
	if (GetStopTraceIconVisibility() == EVisibility::Hidden)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

EVisibility SInsightsStatusBarWidget::GetStopTraceIconVisibility() const
{
	if (bIsTraceRecordButtonHovered && FTraceAuxiliary::IsConnected())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

void SInsightsStatusBarWidget::SetTraceChannels(const TCHAR* InChannels)
{
	Channels = InChannels;
}

bool SInsightsStatusBarWidget::IsPresetSet(const TCHAR* InChannels) const
{
	return Channels == InChannels;
}

#undef LOCTEXT_NAMESPACE
