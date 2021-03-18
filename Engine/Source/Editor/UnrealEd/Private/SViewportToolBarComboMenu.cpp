// Copyright Epic Games, Inc. All Rights Reserved.

#include "SViewportToolBarComboMenu.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Styling/ToolBarStyle.h"

void SViewportToolBarComboMenu::Construct( const FArguments& InArgs )
{
	const FToolBarStyle& ToolBarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>(InArgs._Style.Get());

	EMultiBlockLocation::Type BlockLocation = InArgs._BlockLocation;

	const FButtonStyle& ButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.ComboMenu.ButtonStyle");
	const FCheckBoxStyle& CheckBoxStyle = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("EditorViewportToolBar.ComboMenu.ToggleButton");
	const FTextBlockStyle& LabelStyle = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("EditorViewportToolBar.ComboMenu.LabelStyle");

	const FSlateIcon& Icon = InArgs._Icon.Get();

	ParentToolBar = InArgs._ParentToolBar;

	TSharedPtr<SCheckBox> ToggleControl;
	{
		ToggleControl = SNew(SCheckBox)
		.Padding(0.f)
		.Style(&CheckBoxStyle)
		.OnCheckStateChanged(InArgs._OnCheckStateChanged)
		.ToolTipText(InArgs._ToggleButtonToolTip)
		.IsChecked(InArgs._IsChecked)
		[
			SNew(SImage)
			.Image(Icon.GetIcon())
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
	}

	{
		TSharedRef<SWidget> ButtonContents =
			SNew(SButton)
			.ButtonStyle(&ButtonStyle)
			.ContentPadding(0.f)
			.ToolTipText(InArgs._MenuButtonToolTip)
			.OnClicked(this, &SViewportToolBarComboMenu::OnMenuClicked)
			[
				SNew(STextBlock)
				.TextStyle(&LabelStyle)
				.Text(InArgs._Label)
			];
		
		if (InArgs._MinDesiredButtonWidth > 0.0f)
		{
			ButtonContents =
				SNew(SBox)
				.MinDesiredWidth(InArgs._MinDesiredButtonWidth)
				[
					ButtonContents
				];
		}

		MenuAnchor = SNew(SMenuAnchor)
		.Placement( MenuPlacement_BelowAnchor )
		[
			ButtonContents
		]
		.OnGetMenuContent( InArgs._OnGetMenuContent );
	}


	ChildSlot
	[

		SNew(SBorder)
		.Padding(FMargin(6.f, 0.f, 6.f, 0.f))
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Group"))
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.0f,0.0f)
			.AutoWidth()
			[
				ToggleControl.ToSharedRef()
			]

			+SHorizontalBox::Slot()
			.Padding(2.0f, 0.0f)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				MenuAnchor.ToSharedRef()
			]
		]
	];
}

FReply SViewportToolBarComboMenu::OnMenuClicked()
{
	// If the menu button is clicked toggle the state of the menu anchor which will open or close the menu
	MenuAnchor->SetIsOpen( !MenuAnchor->IsOpen() );
	ParentToolBar.Pin()->SetOpenMenu( MenuAnchor );
	return FReply::Handled();
}

void SViewportToolBarComboMenu::OnMouseEnter( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	// See if there is another menu on the same tool bar already open
	TWeakPtr<SMenuAnchor> OpenedMenu = ParentToolBar.Pin()->GetOpenMenu();
	if( OpenedMenu.IsValid() && OpenedMenu.Pin()->IsOpen() && OpenedMenu.Pin() != MenuAnchor )
	{
		// There is another menu open so we open this menu and close the other
		ParentToolBar.Pin()->SetOpenMenu( MenuAnchor ); 
		MenuAnchor->SetIsOpen( true );
	}
}
