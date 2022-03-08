// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dialog/SCustomDialog.h"

#include "HAL/PlatformApplicationMisc.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Logging/LogMacros.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"

DEFINE_LOG_CATEGORY_STATIC(LogCustomDialog, Log, All);

void SCustomDialog::Construct(const FArguments& InArgs)
{
	UE_LOG(LogCustomDialog, Log, TEXT("Dialog displayed:"), *InArgs._Title.ToString());
	check(InArgs._Buttons.Num() > 0);
	
	OnClosed = InArgs._OnClosed;
	SWindow::Construct( SWindow::FArguments()
		.Title(InArgs._Title)
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SBorder)
			.Padding(InArgs._RootPadding)
			.BorderImage(FAppStyle::Get().GetBrush( "ToolPanel.GroupBorder" ))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					CreateContentBox(InArgs)
				]
				
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoHeight()
				.Padding(InArgs._ButtonAreaPadding)
				[
					CreateButtonBox(InArgs)
				]
			]
		] );
}

int32 SCustomDialog::ShowModal()
{
	FSlateApplication::Get().AddModalWindow(StaticCastSharedRef<SWindow>(this->AsShared()), FGlobalTabmanager::Get()->GetRootWindow());
	return LastPressedButton;
}

void SCustomDialog::Show()
{
	TSharedRef<SWindow> Window = FSlateApplication::Get().AddWindow(StaticCastSharedRef<SWindow>(this->AsShared()), true);
	if (OnClosed.IsBound())
	{
		Window->GetOnWindowClosedEvent().AddLambda([this](const TSharedRef<SWindow>& Window) { OnClosed.Execute(); });
	}
}

TSharedRef<SWidget> SCustomDialog::CreateContentBox(const FArguments& InArgs)
{
	TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

	if (InArgs._IconBrush.IsValid())
	{
		const FSlateBrush* ImageBrush = FAppStyle::Get().GetBrush(InArgs._IconBrush);
		if (ensureMsgf(ImageBrush != nullptr, TEXT("Brush %s is unknown"), *InArgs._IconBrush.ToString()))
		{
			ContentBox->AddSlot()
				.AutoWidth()
				.VAlign(InArgs._VAlignIcon)
				.HAlign(InArgs._HAlignIcon)
				.Padding(0, 0, 8, 0)
				[
					SNew(SImage)
					.DesiredSizeOverride(InArgs._IconDesiredSizeOverride)
					.Image(ImageBrush)
				];
		}
	}

	if (InArgs._UseScrollBox)
	{
		ContentBox->AddSlot()
		.Padding(InArgs._ContentAreaPadding)
		[
			SNew(SBox)
			.MaxDesiredHeight(InArgs._ScrollBoxMaxHeight)
			[
				SNew(SScrollBox)
				+SScrollBox::Slot()
				[
					InArgs._Content.Widget
				]
			]
		];
	}
	else
	{
		ContentBox->AddSlot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(InArgs._ContentAreaPadding)
			[
				InArgs._Content.Widget
			];
	}
	
	return ContentBox;
}

TSharedRef<SWidget> SCustomDialog::CreateButtonBox(const FArguments& InArgs)
{
	TSharedPtr<SUniformGridPanel> ButtonPanel;
	TSharedRef<SHorizontalBox> ButtonBox =
		SNew(SHorizontalBox)
	
		// Before buttons
		+SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				InArgs._BeforeButtons.Widget
			]

		// Buttons
		+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SAssignNew(ButtonPanel, SUniformGridPanel)
				.SlotPadding(FAppStyle::Get().GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FAppStyle::Get().GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FAppStyle::Get().GetFloat("StandardDialog.MinDesiredSlotHeight"))
			];

	for (int32 i = 0; i < InArgs._Buttons.Num(); ++i)
	{
		const FButton& Button = InArgs._Buttons[i];

		const FButtonStyle* ButtonStyle = Button.bIsPrimary ?
				&FAppStyle::Get().GetWidgetStyle< FButtonStyle >( "PrimaryButton" ) :
				&FAppStyle::Get().GetWidgetStyle< FButtonStyle >("Button");
		ButtonPanel->AddSlot(ButtonPanel->GetChildren()->Num(), 0)
		[
			SNew(SButton)
			.OnClicked(FOnClicked::CreateSP(this, &SCustomDialog::OnButtonClicked, Button.OnClicked, i))
			.ButtonStyle(ButtonStyle)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(Button.ButtonText)
				]
			]
		];
	}

	return ButtonBox;
}

/** Handle the button being clicked */
FReply SCustomDialog::OnButtonClicked(FSimpleDelegate OnClicked, int32 ButtonIndex)
{
	LastPressedButton = ButtonIndex;

	FSlateApplication::Get().RequestDestroyWindow(StaticCastSharedRef<SWindow>(this->AsShared()));

	OnClicked.ExecuteIfBound();
	return FReply::Handled();
}
