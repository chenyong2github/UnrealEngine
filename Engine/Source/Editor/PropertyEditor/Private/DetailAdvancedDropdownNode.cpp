// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailAdvancedDropdownNode.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "SDetailTableRowBase.h"
#include "PropertyCustomizationHelpers.h"

class SAdvancedDropdownRow : public SDetailTableRowBase
{
public:
	SLATE_BEGIN_ARGS( SAdvancedDropdownRow )
		: _IsExpanded( false )
		, _IsButtonEnabled( true )
		, _ShouldShowAdvancedButton( false )
	{}
		SLATE_ATTRIBUTE( bool, IsExpanded )
		SLATE_ATTRIBUTE( bool, IsButtonEnabled )
		SLATE_ARGUMENT( bool, ShouldShowAdvancedButton )
		SLATE_EVENT( FOnClicked, OnClicked )
	SLATE_END_ARGS()

	/**
	 * Construct the widget
	 *
	 * @param InArgs   A declaration from which to construct the widget
	 */
	void Construct( const FArguments& InArgs, IDetailsViewPrivate* InDetailsView, const TSharedRef<STableViewBase>& InOwnerTableView, bool bIsTopNode, bool bInDisplayShowAdvancedMessage)
	{
		IsExpanded = InArgs._IsExpanded;
		DetailsView = InDetailsView;
		bDisplayShowAdvancedMessage = bInDisplayShowAdvancedMessage;

		TSharedPtr<SWidget> ContentWidget;
		if( bIsTopNode )
		{
			ContentWidget = SNew(SImage)
				.Image(FEditorStyle::GetBrush("DetailsView.AdvancedDropdownBorder.Open"));
		}
		else if( InArgs._ShouldShowAdvancedButton )
		{
			ContentWidget = 		
				SNew( SVerticalBox )
				+ SVerticalBox::Slot()
				.HAlign( HAlign_Center )
				.AutoHeight()
				[
					SNew( STextBlock )
					.Text( NSLOCTEXT("DetailsView", "NoSimpleProperties", "Click the arrow to display advanced properties") )
					.Font( IDetailLayoutBuilder::GetDetailFont() )
					.Visibility( this, &SAdvancedDropdownRow::OnGetHelpTextVisibility )
					.ColorAndOpacity(FLinearColor(1,1,1,.5))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ExpanderButton, SButton)
					.ButtonStyle(FEditorStyle::Get(), "NoBorder")
					.HAlign(HAlign_Center)
					.ContentPadding(2)
					.OnClicked(InArgs._OnClicked)
					.IsEnabled(InArgs._IsButtonEnabled)
					.ToolTipText(this, &SAdvancedDropdownRow::GetAdvancedPulldownToolTipText )
					[
						SNew(SImage)
						.Image(this, &SAdvancedDropdownRow::GetAdvancedPulldownImage)
					]
				];
		}
		else
		{
			ContentWidget = SNew(SSpacer);
		}

		const FSlateBrush* ScrollBarWellBrush = FAppStyle::Get().GetBrush("DetailsView.GridLine");
		const FSlateBrush* BackgroundBrush = FAppStyle::Get().GetBrush("DetailsView.AdvancedDropdownBorder");

		TWeakPtr<STableViewBase> OwnerTableViewWeak = InOwnerTableView;
		auto GetScrollbarWellBrush = [OwnerTableViewWeak, ScrollBarWellBrush, BackgroundBrush]()
		{
			return SDetailTableRowBase::IsScrollBarVisible(OwnerTableViewWeak) ? ScrollBarWellBrush : BackgroundBrush;
		};

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("DetailsView.GridLine"))
			.Padding(bIsTopNode ? FMargin(0) : FMargin(0, 0, 0, 1))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("DetailsView.AdvancedDropdownBorder"))
					.Padding(bIsTopNode ? FMargin(0, 0, 0, 2) : FMargin(0, 3, 0, 2))
					[
						ContentWidget.ToSharedRef()
					]
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SBorder)
					.BorderImage_Lambda(GetScrollbarWellBrush)
					.Padding(FMargin(0, 0, SDetailTableRowBase::ScrollBarPadding, 0))
				]
			]
		];

		STableRow< TSharedPtr< FDetailTreeNode > >::ConstructInternal(
			STableRow::FArguments()
				.Style(FEditorStyle::Get(), "DetailsView.TreeView.TableRow")
				.ShowSelection(false),
			InOwnerTableView
		);	
	}

private:

	EVisibility OnGetHelpTextVisibility() const
	{
		return bDisplayShowAdvancedMessage && !IsExpanded.Get() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetAdvancedPulldownToolTipText() const
	{
		return IsExpanded.Get() ? NSLOCTEXT("DetailsView", "HideAdvanced", "Hide Advanced") : NSLOCTEXT("DetailsView", "ShowAdvanced", "Show Advanced");
	}

	const FSlateBrush* GetAdvancedPulldownImage() const
	{
		if( ExpanderButton->IsHovered() )
		{
			return IsExpanded.Get() ? FEditorStyle::GetBrush("DetailsView.PulldownArrow.Up.Hovered") : FEditorStyle::GetBrush("DetailsView.PulldownArrow.Down.Hovered");
		}
		else
		{
			return IsExpanded.Get() ? FEditorStyle::GetBrush("DetailsView.PulldownArrow.Up") : FEditorStyle::GetBrush("DetailsView.PulldownArrow.Down");
		}
	}

private:
	TAttribute<bool> IsExpanded;
	TSharedPtr<SButton> ExpanderButton;
	bool bDisplayShowAdvancedMessage;
	IDetailsViewPrivate* DetailsView;
};

TSharedRef< ITableRow > FAdvancedDropdownNode::GenerateWidgetForTableView( const TSharedRef<STableViewBase>& OwnerTable, bool bAllowFavoriteSystem)
{
	return SNew( SAdvancedDropdownRow, ParentCategory.GetDetailsView(), OwnerTable, bIsTopNode, bDisplayShowAdvancedMessage )
		.OnClicked( this, &FAdvancedDropdownNode::OnAdvancedDropDownClicked )
		.IsButtonEnabled( IsEnabled )
		.IsExpanded( IsExpanded )
		.ShouldShowAdvancedButton( bShouldShowAdvancedButton );
}

bool FAdvancedDropdownNode::GenerateStandaloneWidget(FDetailWidgetRow& OutRow) const
{
	// Not supported
	return false;
}

FReply FAdvancedDropdownNode::OnAdvancedDropDownClicked()
{
	ParentCategory.OnAdvancedDropdownClicked();

	return FReply::Handled();
}
