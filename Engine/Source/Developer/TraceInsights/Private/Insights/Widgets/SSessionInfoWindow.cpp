// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSessionInfoWindow.h"

#include "SlateOptMacros.h"
#include "TraceServices/ModuleService.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_EDITOR
	#include "EngineAnalytics.h"
	#include "Runtime/Analytics/Analytics/Public/AnalyticsEventAttribute.h"
	#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#endif // WITH_EDITOR

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/Version.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SSessionInfoWindow"

////////////////////////////////////////////////////////////////////////////////////////////////////

SSessionInfoWindow::SSessionInfoWindow()
	: DurationActive(0.0f)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SSessionInfoWindow::~SSessionInfoWindow()
{
#if WITH_EDITOR
	if (DurationActive > 0.0f && FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.Insights.SessionInfo"), FAnalyticsEventAttribute(TEXT("Duration"), DurationActive));
	}
#endif // WITH_EDITOR
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SSessionInfoWindow::Construct(const FArguments& InArgs)
{
	TSharedPtr<SVerticalBox> VerticalBox;

	TSharedPtr<SScrollBar> VScrollbar;
	SAssignNew(VScrollbar, SScrollBar)
	.Orientation(Orient_Vertical);

	ChildSlot
		[
			SNew(SOverlay)

			// Version
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(0.0f, -16.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Clipping(EWidgetClipping::ClipToBoundsWithoutIntersecting)
				.Text(LOCTEXT("UnrealInsightsVersion", UNREAL_INSIGHTS_VERSION_STRING_EX))
				.ColorAndOpacity(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
			]

			// Overlay slot for the main window area
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				.ExternalScrollbar(VScrollbar)

				+ SScrollBox::Slot()
				[
					SAssignNew(VerticalBox, SVerticalBox)
				]
			]

			// Overlay slot for the vertical scrollbar
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			[
				SNew(SBox)
				.WidthOverride(FOptionalSize(13.0f))
				[
					VScrollbar.ToSharedRef()
				]
			]
		];

	AddInfoLine(VerticalBox, LOCTEXT("SessionName_HeaderText",	"Session Name:"),		TAttribute<FText>(this, &SSessionInfoWindow::GetSessionNameText));
	AddInfoLine(VerticalBox, LOCTEXT("Uri_HeaderText",			"URI:"),				TAttribute<FText>(this, &SSessionInfoWindow::GetUriText));
	//AddInfoLine(VerticalBox, LOCTEXT("Platform_HeaderText",		"Platform:"),			TAttribute<FText>(this, &SSessionInfoWindow::GetPlatformText));
	//AddInfoLine(VerticalBox, LOCTEXT("AppName_HeaderText",		"Application Name:"),	TAttribute<FText>(this, &SSessionInfoWindow::GetAppNameText));
	//AddInfoLine(VerticalBox, LOCTEXT("BuildConfig_HeaderText",	"Build Config:"),		TAttribute<FText>(this, &SSessionInfoWindow::GetBuildConfigText));
	//AddInfoLine(VerticalBox, LOCTEXT("BuildTarget_HeaderText",	"Build Target:"),		TAttribute<FText>(this, &SSessionInfoWindow::GetBuildTargetText));
	//AddInfoLine(VerticalBox, LOCTEXT("CommandLine_HeaderText",	"Command Line:"),		TAttribute<FText>(this, &SSessionInfoWindow::GetCommandLineText));
	//AddInfoLine(VerticalBox, LOCTEXT("FileSize_HeaderText",		"File Size:"),			TAttribute<FText>(this, &SSessionInfoWindow::GetFileSizeText));
	AddInfoLine(VerticalBox, LOCTEXT("Status_HeaderText",		"Status:"),				TAttribute<FText>(this, &SSessionInfoWindow::GetStatusText));
	AddInfoLine(VerticalBox, LOCTEXT("Modules_HeaderText",		"Modules:"),			TAttribute<FText>(this, &SSessionInfoWindow::GetModulesText));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SSessionInfoWindow::AddInfoLine(TSharedPtr<SVerticalBox> InVerticalBox, const FText& InHeader, const TAttribute<FText>& InValue) const
{
	InVerticalBox->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(InHeader)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		];

	InVerticalBox->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 4.0f)
		[
			SNew(SEditableTextBox)
			.Text(InValue)
			.HintText(LOCTEXT("HintText", "N/A"))
			.BackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f, 1.0f))
			.IsReadOnly(true)
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SSessionInfoWindow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EActiveTimerReturnType SSessionInfoWindow::UpdateActiveDuration(double InCurrentTime, float InDeltaTime)
{
	DurationActive += InDeltaTime;

	// The window will explicitly unregister this active timer when the mouse leaves.
	return EActiveTimerReturnType::Continue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SSessionInfoWindow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SSessionInfoWindow::UpdateActiveDuration));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SSessionInfoWindow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	auto PinnedActiveTimerHandle = ActiveTimerHandle.Pin();
	if (PinnedActiveTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(PinnedActiveTimerHandle.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SSessionInfoWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SSessionInfoWindow::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return SCompoundWidget::OnDragOver(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SSessionInfoWindow::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return SCompoundWidget::OnDrop(MyGeometry, DragDropEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetSessionNameText() const
{
	FText SessionName;
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		SessionName = FText::FromString(FPaths::GetBaseFilename(Session->GetName()));
	}
	return SessionName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetUriText() const
{
	//TODO: update code to use a SessionInfo provider instead
	FText LocalUri;
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		LocalUri = FText::FromString(FString(Session->GetName()));
	}
	return LocalUri;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetPlatformText() const
{
	//TODO
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetAppNameText() const
{
	//TODO
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetBuildConfigText() const
{
	//TODO
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetBuildTargetText() const
{
	//TODO
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetCommandLineText() const
{
	//TODO
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetFileSizeText() const
{
	//TODO
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetStatusText() const
{
	//TODO: add also info from a SessionInfo provider
	FText Status;
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		Status = FText::Format(LOCTEXT("StatusFmt", "{0} Session Duration: {1}"),
			Session->IsAnalysisComplete() ? FText::FromString(FString(TEXT("ANALYSIS COMPLETED."))) : FText::FromString(FString(TEXT("ANALYZING..."))),
			FText::FromString(TimeUtils::FormatTimeAuto(Session->GetDurationSeconds(), 2)));
	}
	return Status;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SSessionInfoWindow::GetModulesText() const
{
	FString ModulesStr;
	TArray<Trace::FModuleInfo> Modules;

	TSharedPtr<Trace::IModuleService> ModuleService = FInsightsManager::Get()->GetModuleService();
	if (ModuleService)
	{
		ModuleService->GetAvailableModules(Modules);
	}

	for (const Trace::FModuleInfo& Module : Modules)
	{
		ModulesStr += Module.DisplayName;
		ModulesStr += TEXT(", ");
	}

	return FText::FromString(ModulesStr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
