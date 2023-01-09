// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorFader.h"

#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleRawFader.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Widgets/SDMXControlConsoleEditorSpinBoxVertical.h"

#include "ScopedTransaction.h"
#include "Misc/Optional.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorFader"

void SDMXControlConsoleEditorFader::Construct(const FArguments& InArgs, const TObjectPtr<UDMXControlConsoleFaderBase>& InFader)
{
	Fader = InFader;

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(80.f)
		.HeightOverride(320.f)
		.Padding(5.f, 0.f)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::Fill)
			[
				SNew(SBorder)
				.BorderImage(this, &SDMXControlConsoleEditorFader::GetBorderImage)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Fill)
					.Padding(1.0f, 5.0f, 1.0f, 1.0f)
					.AutoHeight()
					[					
						SNew(SHorizontalBox)
		
						// Fader Name
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Fill)					
						.FillWidth(1.0f)
						[
							SNew(SBorder)
							.BorderBackgroundColor(FLinearColor::Black)
							.OnMouseButtonDown(this, &SDMXControlConsoleEditorFader::OnFaderNameBorderClicked)
							[
								SAssignNew(FaderNameTextBox, SInlineEditableTextBlock)
								.MultiLine(false)
								.Text(this, &SDMXControlConsoleEditorFader::GetFaderNameText)		
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FLinearColor::White)
								.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
								.OnTextCommitted(this, &SDMXControlConsoleEditorFader::OnFaderNameCommitted)
							]
						]

						// Delete Button
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Right)					
						.Padding(FMargin(1.0f, 0.0f, 0.0f, 0.0f))
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
							.OnClicked(this, &SDMXControlConsoleEditorFader::OnDeleteClicked)
							.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleEditorFader::GetDeleteButtonVisibility))
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("x")))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
								.ColorAndOpacity(FLinearColor::White)
							]
						]
					]

					+ SVerticalBox::Slot()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Center)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.MaxWidth(50.0f)
						[
							// Max Value
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Bottom)
							.HAlign(HAlign_Fill)
							.AutoHeight()
							[
								SNew(SBorder)
								.BorderBackgroundColor(FLinearColor::Black)
								.Padding(5.0f)
								[
									SNew(SInlineEditableTextBlock)
									.MultiLine(false)
									.Text(this, &SDMXControlConsoleEditorFader::GetMaxValueAsText)
									.Justification(ETextJustify::Center)
									.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
								]
							]
								
							// Fader Control
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.HAlign(HAlign_Center)
							.Padding(0.0f, 1.0f, 0.0f, 1.0f)
							.AutoHeight()
							[
								SNew(SBorder)
								.BorderBackgroundColor(FLinearColor::Black)
								[
									SAssignNew(FaderSpinBox, SDMXControlConsoleEditorSpinBoxVertical<uint32>)
									.Value(this, &SDMXControlConsoleEditorFader::GetValue)
									.MinValue(this, &SDMXControlConsoleEditorFader::GetMinValue)
									.MaxValue(this, &SDMXControlConsoleEditorFader::GetMaxValue)
									.MinSliderValue(this, &SDMXControlConsoleEditorFader::GetMinValue)
									.MaxSliderValue(this, &SDMXControlConsoleEditorFader::GetMaxValue)
									.OnValueChanged(this, &SDMXControlConsoleEditorFader::HandleValueChanged)
									.IsEnabled(this, &SDMXControlConsoleEditorFader::GetFaderSpinBoxEnabled)
									.Style(FDMXControlConsoleEditorStyle::Get(), "DMXControlConsole.Fader")
									.MinDesiredWidth(45.0f)
								]
							]

							// Fader Min Value
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Bottom)
							.HAlign(HAlign_Fill)
							.AutoHeight()
							[
								SNew(SScaleBox)
								.IgnoreInheritedScale(true)
								[
									SNew(SBorder)
									.BorderBackgroundColor(FLinearColor::Black)
									.Padding(5.0f)
									[
										SNew(SInlineEditableTextBlock)
										.MultiLine(false)
										.Text(this, &SDMXControlConsoleEditorFader::GetMinValueAsText)
										.Justification(ETextJustify::Center)
										.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
									]
								]
							]
						]
					]

					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						GenerateMuteButtonWidget()
					]
				]
			]
		]
	];

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	SelectionHandler->GetOnFaderSelectionChanged().AddSP(this, &SDMXControlConsoleEditorFader::OnSelectionChanged);
}

void SDMXControlConsoleEditorFader::SetValueByPercentage(float InNewPercentage)
{
	if (!ensureMsgf(Fader.IsValid(), TEXT("Invalid fader, cannot set fader value correctly.")))
	{
		return;
	}

	const float Range = Fader->GetMaxValue() - Fader->GetMinValue();
	FaderSpinBox->SetValue(static_cast<uint32>(Range * InNewPercentage / 100.f) + Fader->GetMinValue());
}

FReply SDMXControlConsoleEditorFader::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		if (!Fader.IsValid())
		{
			return FReply::Unhandled();
		}

		UDMXControlConsoleFaderGroup& FaderGroup = Fader->GetOwnerFaderGroupChecked();
		if (FaderGroup.HasFixturePatch())
		{
			return FReply::Unhandled();
		}

		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFadersObjects = SelectionHandler->GetSelectedFaders();

		// If there's only one fader to delete, replace it in selection
		if (SelectedFadersObjects.Num() == 1)
		{
			UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFadersObjects[0]);
			SelectionHandler->ReplaceInSelection(SelectedFader);
		}

		const FScopedTransaction DeleteSelectedFaderTransaction(LOCTEXT("DeleteSelectedFaderTransaction", "Delete selected Faders"));

		// Delete all selected faders
		for (TWeakObjectPtr<UObject> SelectedFaderObject : SelectedFadersObjects)
		{
			UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedFaderObject);
			if (!SelectedFader)
			{
				continue;
			}

			UDMXControlConsoleFaderGroup& SelectedFaderGroup = SelectedFader->GetOwnerFaderGroupChecked();
			SelectedFaderGroup.Modify();

			SelectionHandler->RemoveFromSelection(SelectedFader);
			SelectedFader->Destroy();
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXControlConsoleEditorFader::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (!Fader.IsValid())
		{
			return FReply::Unhandled();
		}

		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const bool bWasInitallySelected = IsSelected();

		if (!MouseEvent.IsLeftShiftDown())
		{
			if (!MouseEvent.IsControlDown())
			{
				SelectionHandler->ClearSelection();
			}

			if (!bWasInitallySelected)
			{
				SelectionHandler->AddToSelection(Fader.Get());
			}
			else
			{
				SelectionHandler->RemoveFromSelection(Fader.Get());
			}
		}
		else
		{
			SelectionHandler->Multiselect(Fader.Get());
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<SWidget> SDMXControlConsoleEditorFader::GenerateMuteButtonWidget()
{
	TSharedRef<SWidget> MuteButtonWidget =
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SBorder)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(20.f)
				.HeightOverride(20.f)
				[
					SNew(SButton)
					.ButtonColorAndOpacity(this, &SDMXControlConsoleEditorFader::GetMuteButtonColor)
					.OnClicked(this, &SDMXControlConsoleEditorFader::OnMuteClicked)
				]
			]
		]
	
		+SVerticalBox::Slot()
		.HAlign(HAlign_Center)
		.Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MuteButton", "On/Off"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		];

	return MuteButtonWidget;
}

bool SDMXControlConsoleEditorFader::IsSelected() const
{
	if (!Fader.IsValid())
	{
		return false;
	}

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	return SelectionHandler->IsSelected(Fader.Get());
}

FString SDMXControlConsoleEditorFader::GetFaderName() const
{ 
	return Fader.IsValid() ? Fader->GetFaderName() : FString();
}

FText SDMXControlConsoleEditorFader::GetFaderNameText() const
{
	return Fader.IsValid() ? FText::FromString(Fader->GetFaderName()) : FText::GetEmpty();
}

FReply SDMXControlConsoleEditorFader::OnFaderNameBorderClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FaderNameTextBox->EnterEditingMode();
	}

	return FReply::Handled();
}

void SDMXControlConsoleEditorFader::OnFaderNameCommitted(const FText& NewFaderName, ETextCommit::Type InCommit)
{
	if (!ensureMsgf(Fader.IsValid(), TEXT("Invalid fader, cannot update fader name correctly.")))
	{
		return;
	}

	if (!FaderNameTextBox.IsValid())
	{
		return;
	}

	const FScopedTransaction EditFaderNameTransaction(LOCTEXT("EditFaderNameTransaction", "Edit Fader Name"));
	Fader->PreEditChange(UDMXControlConsoleFaderBase::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderBase::GetFaderNamePropertyName()));

	Fader->SetFaderName(NewFaderName.ToString());

	FaderNameTextBox->SetText(FText::FromString(Fader->GetFaderName()));

	Fader->PostEditChange();
}

uint32 SDMXControlConsoleEditorFader::GetValue() const
{ 
	return Fader.IsValid() ? Fader->GetValue() : 0;
}

TOptional<uint32> SDMXControlConsoleEditorFader::GetMinValue() const
{
	return Fader.IsValid() ? Fader->GetMinValue() : 0;
}

FText SDMXControlConsoleEditorFader::GetMinValueAsText() const
{
	if (!Fader.IsValid())
	{
		return FText::GetEmpty();
	}

	const uint32 MinValue = Fader->GetMinValue();
	return FText::FromString(FString::FromInt(MinValue));
}

TOptional<uint32> SDMXControlConsoleEditorFader::GetMaxValue() const
{
	return Fader.IsValid() ? Fader->GetMaxValue() : 0;
}

FText SDMXControlConsoleEditorFader::GetMaxValueAsText() const
{
	if (!Fader.IsValid())
	{
		return FText::GetEmpty();
	}

	const uint32 MaxValue = Fader->GetMaxValue();
	return FText::FromString(FString::FromInt(MaxValue));
}

void SDMXControlConsoleEditorFader::HandleValueChanged(uint32 NewValue)
{
	if (!ensureMsgf(Fader.IsValid(), TEXT("Invalid fader, cannot set fader value correctly.")))
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFadersObjects = SelectionHandler->GetSelectedFaders();

	if (SelectedFadersObjects.IsEmpty() || !SelectedFadersObjects.Contains(Fader))
	{
		Fader->SetValue(NewValue);
	}
	else
	{ 
		const float Range = Fader->GetMaxValue() - Fader->GetMinValue();
		const float Percentage = (NewValue - Fader->GetMinValue()) / Range;

		for (const TWeakObjectPtr<UObject> SelectFaderObject : SelectedFadersObjects)
		{
			UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectFaderObject);
			if (!SelectedFader || SelectedFader->IsMuted())
			{
				continue;
			}

			const float SelectedFaderRange = SelectedFader->GetMaxValue() - SelectedFader->GetMinValue();
			const uint32 Value = (uint32)(SelectedFaderRange * Percentage);
			SelectedFader->SetValue(Value);
		}
	}
}

void SDMXControlConsoleEditorFader::OnSelectionChanged(UDMXControlConsoleFaderBase* InFader)
{
	if (!InFader)
	{
		return;
	}

	if (InFader!= Fader || !IsSelected())
	{
		return;
	}

	FSlateApplication::Get().SetKeyboardFocus(AsShared());
}

FReply SDMXControlConsoleEditorFader::OnDeleteClicked()
{
	if (Fader.IsValid())
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFadersObjects = SelectionHandler->GetSelectedFaders();

		if (SelectedFadersObjects.IsEmpty() || !SelectedFadersObjects.Contains(Fader))
		{
			Fader->Destroy();
		}
		else
		{
			for (const TWeakObjectPtr<UObject> SelectFaderObject : SelectedFadersObjects)
			{
				UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectFaderObject);
				if (!SelectedFader)
				{
					continue;
				}

				SelectedFader->Destroy();

				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

FReply SDMXControlConsoleEditorFader::OnMuteClicked()
{
	if (Fader.IsValid())
	{
		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
		const TArray<TWeakObjectPtr<UObject>> SelectedFadersObjects = SelectionHandler->GetSelectedFaders();

		if (SelectedFadersObjects.IsEmpty() || !SelectedFadersObjects.Contains(Fader))
		{
			Fader->ToggleMute();
		}
		else
		{
			for (const TWeakObjectPtr<UObject> SelectFaderObject : SelectedFadersObjects)
			{
				UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectFaderObject);
				if (!SelectedFader)
				{
					continue;
				}

				SelectedFader->ToggleMute();

				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

FSlateColor SDMXControlConsoleEditorFader::GetMuteButtonColor() const
{
	if (Fader.IsValid())
	{
		return Fader->IsMuted() ? FLinearColor(0.1f, 0.1f, 0.1f) : FLinearColor(0.8f, 0.f, 0.f);
	}

	return FLinearColor::Black;
}

bool SDMXControlConsoleEditorFader::GetFaderSpinBoxEnabled() const
{
	if (Fader.IsValid())
	{
		return !Fader->IsMuted();
	}

	return false;
}

EVisibility SDMXControlConsoleEditorFader::GetDeleteButtonVisibility() const
{
	if (!Fader.IsValid())
	{
		return EVisibility::Collapsed;
	}

	const UDMXControlConsoleRawFader* RawFader = Cast<UDMXControlConsoleRawFader>(Fader);
	return RawFader ? EVisibility::Visible : EVisibility::Collapsed;
}

const FSlateBrush* SDMXControlConsoleEditorFader::GetBorderImage() const
{
	if (!Fader.IsValid())
	{
		return nullptr;
	}

	if (IsHovered())
	{
		if (IsSelected())
		{
			return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.FaderGroup_Highlighted");;
		}
		else
		{
			return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.FaderGroup_Hovered");;
		}
	}
	else
	{
		if (IsSelected())
		{
			return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.FaderGroup_Selected");;
		}
		else
		{
			return FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.BlackBrush");
		}
	}
}

#undef LOCTEXT_NAMESPACE
