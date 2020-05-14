// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/MultiBox/SToolBarComboButtonBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/ToolBarStyle.h"


FToolBarComboButtonBlock::FToolBarComboButtonBlock( const FUIAction& InAction, const FOnGetContent& InMenuContentGenerator, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, bool bInSimpleComboBox )
	: FMultiBlock( InAction, NAME_None, EMultiBlockType::ToolBarComboButton )
	, MenuContentGenerator( InMenuContentGenerator )
	, Label( InLabel )
	, ToolTip( InToolTip )
	, Icon( InIcon )
	, LabelVisibility()
	, bSimpleComboBox( bInSimpleComboBox )
	, bForceSmallIcons( false )
{
}

void FToolBarComboButtonBlock::CreateMenuEntry(FMenuBuilder& MenuBuilder) const
{
	FName IconName;
	FText EntryLabel = Label.Get();
	if ( EntryLabel.IsEmpty() )
	{
		EntryLabel = NSLOCTEXT("ToolBar", "CustomControlLabel", "Custom Control");
	}

	MenuBuilder.AddWrapperSubMenu(EntryLabel, FText::GetEmpty(), MenuContentGenerator, Icon.Get());
}

bool FToolBarComboButtonBlock::HasIcon() const
{
	const FSlateIcon& ActualIcon = Icon.Get();
	return ActualIcon.GetIcon()->GetResourceName() != NAME_None;
}

TSharedRef< class IMultiBlockBaseWidget > FToolBarComboButtonBlock::ConstructWidget() const
{
	return SNew( SToolBarComboButtonBlock )
		.LabelVisibility( LabelVisibility.IsSet() ? LabelVisibility.GetValue() : TOptional< EVisibility >() )
		.Icon(Icon)
		.ForceSmallIcons( bForceSmallIcons )
		.Cursor( EMouseCursor::Default );
}

void SToolBarComboButtonBlock::Construct( const FArguments& InArgs )
{
	if ( InArgs._LabelVisibility.IsSet() )
	{
		LabelVisibility = InArgs._LabelVisibility.GetValue();
	}
	else
	{
		LabelVisibility = TAttribute< EVisibility >::Create( TAttribute< EVisibility >::FGetter::CreateSP( SharedThis( this ), &SToolBarComboButtonBlock::GetIconVisibility, false ) );
	}

	Icon = InArgs._Icon;
	bForceSmallIcons = InArgs._ForceSmallIcons;
}

void SToolBarComboButtonBlock::BuildMultiBlockWidget(const ISlateStyle* StyleSet, const FName& StyleName)
{
	TSharedRef< const FMultiBox > MultiBox( OwnerMultiBoxWidget.Pin()->GetMultiBox() );
	
	TSharedRef< const FToolBarComboButtonBlock > ToolBarComboButtonBlock = StaticCastSharedRef< const FToolBarComboButtonBlock >( MultiBlock.ToSharedRef() );

	//TSharedPtr< const FUICommandInfo > UICommand = ToolBarComboButtonBlock->GetAction();

	TAttribute<FText> Label;

	const FToolBarStyle& ToolBarStyle = StyleSet->GetWidgetStyle<FToolBarStyle>(StyleName);

	TSharedRef<SWidget> IconWidget = SNullWidget::NullWidget;
	if (!ToolBarComboButtonBlock->bSimpleComboBox)
	{
		if (MultiBox->GetType() == EMultiBoxType::SlimHorizontalToolBar)
		{
			const FVector2D IconSize = ToolBarStyle.IconSize;

			IconWidget =
				SNew(SBox)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(this, &SToolBarComboButtonBlock::GetIconBrush)
				];
		}
		else
		{
			IconWidget = 
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(this, &SToolBarComboButtonBlock::GetIconBrush);
		}

		Label = ToolBarComboButtonBlock->Label;
	}

	// Add this widget to the search list of the multibox
	if (MultiBlock->GetSearchable())
	{
		OwnerMultiBoxWidget.Pin()->AddSearchElement(this->AsWidget(), Label.Get());
	}

	// Setup the string for the metatag
	FName TagName;
	if (ToolBarComboButtonBlock->GetTutorialHighlightName() == NAME_None)
	{
		TagName = *FString::Printf(TEXT("ToolbarComboButton,%s,0"), *Label.Get().ToString());
	}
	else
	{
		TagName = ToolBarComboButtonBlock->GetTutorialHighlightName();
	}
	
	// Create the content for our button
	TSharedRef<SWidget> ButtonContent = SNullWidget::NullWidget;
	if (MultiBox->GetType() == EMultiBoxType::SlimHorizontalToolBar)
	{
		ButtonContent =
			SNew(SHorizontalBox)
			// Icon image
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				IconWidget
			]
			// Label text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(ToolBarStyle.LabelPadding)
			.VAlign(VAlign_Center)	// Center the label text horizontally
			[
				SNew(STextBlock)
				.Visibility(ToolBarComboButtonBlock->bSimpleComboBox ? EVisibility::Collapsed : LabelVisibility)
				.Text(Label)
				// Smaller font for tool tip labels
				.TextStyle(&ToolBarStyle.LabelStyle)
			];

	}
	else
	{
		ButtonContent =
			SNew(SVerticalBox)
			// Icon image
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)	// Center the icon horizontally, so that large labels don't stretch out the artwork
			[
				IconWidget
			]
			// Label text
			+ SVerticalBox::Slot().AutoHeight()
			.Padding(ToolBarStyle.LabelPadding)
			.AutoHeight()
			.HAlign(HAlign_Center)	// Center the label text horizontally
			[
				SNew(STextBlock)
				.Visibility(LabelVisibility)
				.Text(Label)
				.TextStyle(&ToolBarStyle.LabelStyle)
			];
	}
	
		
	EMultiBlockLocation::Type BlockLocation = GetMultiBlockLocation();
	FName BlockStyle = EMultiBlockLocation::ToName(ISlateStyle::Join(StyleName, ".Button"), BlockLocation);
	const FButtonStyle& ButtonStyle = BlockLocation == EMultiBlockLocation::None ? ToolBarStyle.ButtonStyle : StyleSet->GetWidgetStyle<FButtonStyle>(BlockStyle);

	OpenForegroundColor = ButtonStyle.HoveredForeground;

	const FComboButtonStyle* ComboStyle = &ToolBarStyle.ComboButtonStyle;
	if (ToolBarComboButtonBlock->bSimpleComboBox)
	{
		ComboStyle = &ToolBarStyle.SettingsComboButton;
	}

	ChildSlot
	[
		SAssignNew(ComboButtonWidget, SComboButton)
		.AddMetaData<FTagMetaData>(FTagMetaData(TagName))
		.ContentPadding(0)
		.ComboButtonStyle(ComboStyle)
		.ButtonStyle(&ButtonStyle)
		.ToolTipText(ToolBarComboButtonBlock->ToolTip)
		.ForegroundColor(this, &SToolBarComboButtonBlock::OnGetForegroundColor)
		// Route the content generator event
		.OnGetMenuContent(this, &SToolBarComboButtonBlock::OnGetMenuContent)
		.ButtonContent()
		[
			ButtonContent
		]
	];


	FMargin Padding = ToolBarStyle.ComboButtonPadding;
	if (ToolBarComboButtonBlock->bSimpleComboBox)
	{
		Padding.Left = 0;
		Padding.Right = 10;
	}

	ChildSlot.Padding(Padding);
	// Bind our widget's enabled state to whether or not our action can execute
	SetEnabled( TAttribute< bool >( this, &SToolBarComboButtonBlock::IsEnabled ) );

	// Bind our widget's visible state to whether or not the button should be visible
	SetVisibility( TAttribute<EVisibility>(this, &SToolBarComboButtonBlock::GetVisibility) );
}

TSharedRef<SWidget> SToolBarComboButtonBlock::OnGetMenuContent()
{
	TSharedRef< const FToolBarComboButtonBlock > ToolBarButtonComboBlock = StaticCastSharedRef< const FToolBarComboButtonBlock >( MultiBlock.ToSharedRef() );
	return ToolBarButtonComboBlock->MenuContentGenerator.Execute();
}

bool SToolBarComboButtonBlock::IsEnabled() const
{
	const FUIAction& UIAction = MultiBlock->GetDirectActions();
	if( UIAction.CanExecuteAction.IsBound() )
	{
		return UIAction.CanExecuteAction.Execute();
	}

	return true;
}

EVisibility SToolBarComboButtonBlock::GetVisibility() const
{
	const FUIAction& UIAction = MultiBlock->GetDirectActions();
	if (UIAction.IsActionVisibleDelegate.IsBound())
	{
		return UIAction.IsActionVisibleDelegate.Execute() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return EVisibility::Visible;
}

bool SToolBarComboButtonBlock::HasDynamicIcon() const
{
	return Icon.IsBound();
}

const FSlateBrush* SToolBarComboButtonBlock::GetIconBrush() const
{
	return bForceSmallIcons || FMultiBoxSettings::UseSmallToolBarIcons.Get() ? GetSmallIconBrush() : GetNormalIconBrush();
}

const FSlateBrush* SToolBarComboButtonBlock::GetNormalIconBrush() const
{
	const FSlateIcon& ActualIcon = Icon.Get();
	return ActualIcon.GetIcon();
}

const FSlateBrush* SToolBarComboButtonBlock::GetSmallIconBrush() const
{
	const FSlateIcon& ActualIcon = Icon.Get();
	return ActualIcon.GetSmallIcon();
}

EVisibility SToolBarComboButtonBlock::GetIconVisibility(bool bIsASmallIcon) const
{
	return ((bForceSmallIcons || FMultiBoxSettings::UseSmallToolBarIcons.Get()) ^ bIsASmallIcon) ? EVisibility::Collapsed : EVisibility::Visible;
}

FSlateColor SToolBarComboButtonBlock::OnGetForegroundColor() const
{
	if (ComboButtonWidget->IsOpen())
	{
		return OpenForegroundColor;
	}
	else
	{
		return FSlateColor::UseStyle();
	}
}
