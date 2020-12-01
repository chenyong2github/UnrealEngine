// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/MultiBox/SWidgetBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/ToolBarStyle.h"


/**
 * Constructor
 *
 * @param	InHeadingText	Heading text
 */
FWidgetBlock::FWidgetBlock( TSharedRef<SWidget> InContent, const FText& InLabel, bool bInNoIndent, EHorizontalAlignment InHorizontalAlignment)
	: FMultiBlock( nullptr, nullptr, NAME_None, EMultiBlockType::Widget )
	, ContentWidget( InContent )
	, Label( InLabel )
	, bNoIndent( bInNoIndent )
	, HorizontalAlignment(InHorizontalAlignment)
{
}


void FWidgetBlock::CreateMenuEntry(FMenuBuilder& MenuBuilder) const
{
	FText EntryLabel = (!Label.IsEmpty()) ? Label : NSLOCTEXT("WidgetBlock", "CustomControl", "Custom Control");
	MenuBuilder.AddWidget(ContentWidget, FText::GetEmpty(), true);
}


/**
 * Allocates a widget for this type of MultiBlock.  Override this in derived classes.
 *
 * @return  MultiBlock widget object
 */
TSharedRef< class IMultiBlockBaseWidget > FWidgetBlock::ConstructWidget() const
{
	return SNew( SWidgetBlock )
			.Cursor(EMouseCursor::Default);
}


bool FWidgetBlock::GetAlignmentOverrides(EHorizontalAlignment& OutHorizontalAlignment, EVerticalAlignment& OutVerticalAlignment, bool& bOutAutoWidth) const
{
	OutHorizontalAlignment = HorizontalAlignment;
	OutVerticalAlignment = VAlign_Fill;
	bOutAutoWidth = HorizontalAlignment != HAlign_Fill ? false : true;
	return true;
}

/**
 * Construct this widget
 *
 * @param	InArgs	The declaration data for this widget
 */
void SWidgetBlock::Construct( const FArguments& InArgs )
{
}



/**
 * Builds this MultiBlock widget up from the MultiBlock associated with it
 */
void SWidgetBlock::BuildMultiBlockWidget(const ISlateStyle* StyleSet, const FName& StyleName)
{
	TSharedRef< const FWidgetBlock > WidgetBlock = StaticCastSharedRef< const FWidgetBlock >( MultiBlock.ToSharedRef() );

	// Support menus which do not have a defined widget style yet
	bool bHasLabel = !WidgetBlock->Label.IsEmpty();
	FMargin Padding;
	const FTextBlockStyle* LabelStyle = nullptr;
	if(StyleSet->HasWidgetStyle<FToolBarStyle>(StyleName))
	{
		const FToolBarStyle& ToolBarStyle = StyleSet->GetWidgetStyle<FToolBarStyle>(StyleName);

		Padding = WidgetBlock->bNoIndent ? ToolBarStyle.BlockPadding : ToolBarStyle.IndentedBlockPadding;
		LabelStyle = &ToolBarStyle.LabelStyle;

	}
	else
	{
		Padding = WidgetBlock->bNoIndent ? StyleSet->GetMargin(StyleName, ".Block.Padding") : StyleSet->GetMargin(StyleName, ".Block.IndentedPadding");

		LabelStyle = &StyleSet->GetWidgetStyle<FTextBlockStyle>(ISlateStyle::Join(StyleName, ".Label"));
	}

	TSharedPtr<SMultiBoxWidget> OwnerMultiBoxWidgetPinned = OwnerMultiBoxWidget.Pin();

	if(OwnerMultiBoxWidgetPinned->GetMultiBox()->GetType() == EMultiBoxType::Menu)
	{
		// Account for checkmark used in other menu blocks but not used in for widget rows
		Padding = Padding + FMargin(14, 0, 8, 0);
	}

	// Add this widget to the search list of the multibox
	OwnerMultiBoxWidgetPinned->AddElement(this->AsWidget(), WidgetBlock->Label, MultiBlock->GetSearchable());

	// This widget holds the search text, set it as the search block widget
	if (OwnerMultiBoxWidgetPinned->GetSearchTextWidget() == WidgetBlock->ContentWidget)
	{
		OwnerMultiBoxWidgetPinned->SetSearchBlockWidget(this->AsWidget());
		this->AsWidget()->SetVisibility(EVisibility::Collapsed);
	}

	ChildSlot
	.Padding( Padding )	// Large left margin mimics the indent of normal menu items when bNoIndent is false
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SHorizontalBox)
			.Visibility( bHasLabel ? EVisibility::Visible : EVisibility::Collapsed )
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 4.f, 0.f)
			.VAlign( VAlign_Center )
			[
				SNew( STextBlock )
				.TextStyle(LabelStyle)
				.Text(WidgetBlock->Label)
				.ColorAndOpacity(FAppStyle::Get().GetSlateColor("Colors.ForegroundHover"))
			]
		]
		+SHorizontalBox::Slot()
		.VAlign(bHasLabel ? VAlign_Bottom : VAlign_Fill)
		.FillWidth(1.f)
		[
			WidgetBlock->ContentWidget
		]
	];
}
