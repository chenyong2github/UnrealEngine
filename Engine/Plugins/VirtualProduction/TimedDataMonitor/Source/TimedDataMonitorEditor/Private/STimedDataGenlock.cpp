// Copyright Epic Games, Inc. All Rights Reserved.

#include "STimedDataGenlock.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/EngineCustomTimeStep.h"
#include "ISettingsModule.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "ObjectEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "EditorFontGlyphs.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"

#include "STimedDataMonitorPanel.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "STimedDataGenlock"


void STimedDataGenlock::Construct(const FArguments& InArgs, TSharedPtr<STimedDataMonitorPanel> InOwnerPanel)
{
	OwnerPanel = InOwnerPanel;

	UpdateCachedValue();

	FSlateFontInfo TimeFont = FCoreStyle::Get().GetFontStyle(TEXT("EmbossedText"));
	TimeFont.Size += 4;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0)
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0.f, 0.f, 4.f, 0.f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(this, &STimedDataGenlock::GetStateText)
				.ColorAndOpacity(this, &STimedDataGenlock::GetStateColorAndOpacity)
			]
			+ SHorizontalBox::Slot()
			.Padding(0.f, 0.f, 4.f, 0.f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.MinDesiredWidth(500)
				.Font(FCoreStyle::Get().GetFontStyle(TEXT("NormalText")))
				.Text(this, &STimedDataGenlock::GetCustomTimeStepText)
			]
		]
		+ SVerticalBox::Slot()
		.Padding(0)
		.AutoHeight()
		[
			SNew(SProgressBar)
			.BorderPadding(FVector2D::ZeroVector)
			.Percent(this, &STimedDataGenlock::GetFPSFraction)
			.FillColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)))
		]
		+ SVerticalBox::Slot()
		.Padding(0)
		.AutoHeight()
		[
			SNew(SGridPanel)
			.FillColumn(0, 0.25f)
			.FillColumn(1, 0.25f)
			.FillColumn(2, 0.02f)
			.FillColumn(4, 0.49f)

			+ SGridPanel::Slot(0, 0)
			[
				SNew(STextBlock)

				.Text(LOCTEXT("FPSLabel", "FPS: "))
			]
			+ SGridPanel::Slot(1, 0)
			[
				SNew(STextBlock)
				.Text(this, &STimedDataGenlock::GetFPSText)
			]
			+ SGridPanel::Slot(0, 1)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DeltaTimeLabel", "DeltaTime: "))
			]
			+ SGridPanel::Slot(1, 1)
			[
				SNew(STextBlock)
				.Text(this, &STimedDataGenlock::GetDeltaTimeText)
			]
			+ SGridPanel::Slot(0, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("IdleTimeLabel", "Idle Time: "))
			]
			+ SGridPanel::Slot(1, 2)
			[
				SNew(STextBlock)
				.Text(this, &STimedDataGenlock::GetIdleTimeText)
			]
			+ SGridPanel::Slot(3, 0)
			.RowSpan(3)
			[
				SNew(SSeparator)
				.Thickness(2)
				.Orientation(EOrientation::Orient_Vertical)
			]
			+ SGridPanel::Slot(4, 0)
			.RowSpan(3)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Bottom)
				.AutoWidth()
				[
					SNew(SCheckBox)
					.Padding(4.f)
					.ToolTipText(LOCTEXT("ShowTimecodeProviderSetting_Tip", "Show timecode provider setting"))
					.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
					.ForegroundColor(FSlateColor::UseForeground())
					.IsChecked_Lambda([](){return ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &STimedDataGenlock::ShowCustomTimeStepSetting)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
						.Text(FEditorFontGlyphs::Cogs)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(SCheckBox)
					.Padding(4.f)
					.ToolTipText(LOCTEXT("ReapplyMenuToolTip", "Reinitialize the current Custom Time Step."))
					.Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
					.ForegroundColor(FSlateColor::UseForeground())
					.IsEnabled(this, &STimedDataGenlock::IsCustomTimeStepEnabled)
					.IsChecked_Lambda([]() {return ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &STimedDataGenlock::ReinitializeCustomTimeStep)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
						.Text(FEditorFontGlyphs::Undo)
					]
				]
			]
		]
	];
}


void STimedDataGenlock::RequestRefresh()
{
	if (TSharedPtr<STimedDataMonitorPanel> OwnerPanelPin = OwnerPanel.Pin())
	{
		OwnerPanelPin->RequestRefresh();
	}
}


void STimedDataGenlock::UpdateCachedValue()
{
	CachedFPSFraction = FApp::GetDeltaTime() > 0.0 ? (1.f-float(FApp::GetIdleTime() / FApp::GetDeltaTime())) : 1.f;

	FNumberFormattingOptions FPSFormattingOptions = FNumberFormattingOptions()
		.SetUseGrouping(false)
		.SetMaximumFractionalDigits(2);
	FNumberFormattingOptions MsFormattingOptions = FNumberFormattingOptions()
		.SetUseGrouping(false)
		.SetMinimumFractionalDigits(3)
		.SetMaximumFractionalDigits(3);
	CachedFPSText = FText::AsNumber(1.0/FApp::GetDeltaTime(), &FPSFormattingOptions);
	CachedDeltaTimeText = FText::Format(LOCTEXT("WithMilliSeconds", "{0}ms"), FText::AsNumber(FApp::GetDeltaTime()*1000, &MsFormattingOptions));
	CachedIdleTimeText = FText::Format(LOCTEXT("WithMilliSeconds", "{0}ms"), FText::AsNumber(FApp::GetIdleTime()*1000, &MsFormattingOptions));

	if (const UEngineCustomTimeStep* CustomTimeStep = GEngine->GetCustomTimeStep())
	{
		CachedState = CustomTimeStep->GetSynchronizationState();
		CachedCustomTimeStepText = FText::FromName(CustomTimeStep->GetFName());
	}
	else
	{
		CachedState = ECustomTimeStepSynchronizationState::Error;
		CachedCustomTimeStepText = LOCTEXT("Undefined", "<Default Engine Settings>");
	}
}


FText STimedDataGenlock::GetStateText() const
{
	if (!IsCustomTimeStepEnabled())
	{
		return FText::GetEmpty();
	}

	switch (CachedState)
	{
	case ECustomTimeStepSynchronizationState::Synchronized:
		return FEditorFontGlyphs::Clock_O;
	case ECustomTimeStepSynchronizationState::Synchronizing:
		return FEditorFontGlyphs::Hourglass_O;
	case ECustomTimeStepSynchronizationState::Error:
	case ECustomTimeStepSynchronizationState::Closed:
		return FEditorFontGlyphs::Ban;
	}
	return FEditorFontGlyphs::Exclamation;
}


FSlateColor STimedDataGenlock::GetStateColorAndOpacity() const
{
	switch (CachedState)
	{
	case ECustomTimeStepSynchronizationState::Closed:
	case ECustomTimeStepSynchronizationState::Error:
		return FLinearColor::Red;
	case ECustomTimeStepSynchronizationState::Synchronized:
		return FLinearColor::Green;
	case ECustomTimeStepSynchronizationState::Synchronizing:
		return FLinearColor::Yellow;
	}

	return FLinearColor::Red;
}


FText STimedDataGenlock::GetCustomTimeStepText() const
{
	return CachedCustomTimeStepText;
}


bool STimedDataGenlock::IsCustomTimeStepEnabled() const
{
	return GEngine && GEngine->GetCustomTimeStep() != nullptr;
}


TOptional<float> STimedDataGenlock::GetFPSFraction() const
{
	return CachedFPSFraction;
}


void STimedDataGenlock::ShowCustomTimeStepSetting(ECheckBoxState)
{
	if (UEngineCustomTimeStep* CustomTimeStepPtr = GEngine->GetCustomTimeStep())
	{
		if (CustomTimeStepPtr->GetOuter() == GEngine)
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Engine", "General");
		}
		else
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(CustomTimeStepPtr->GetOuter());
		}
	}
	else
	{
		FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Engine", "General");
	}
}


void STimedDataGenlock::ReinitializeCustomTimeStep(ECheckBoxState)
{
	if (GEngine)
	{
		GEngine->ReinitializeCustomTimeStep();
	}
}


#undef LOCTEXT_NAMESPACE
