// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlueprintHeaderView.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
//#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"
#include "Framework/Text/SlateTextRun.h"
#include "HeaderViewClassListItem.h"
#include "EditorStyleSet.h"
#include "String/LineEndings.h"

#define LOCTEXT_NAMESPACE "SBlueprintHeaderView"

// FHeaderViewSyntaxDecorator /////////////////////////////////////////////////

namespace
{
	class FHeaderViewSyntaxDecorator : public ITextDecorator
	{
	public:

		static TSharedRef<FHeaderViewSyntaxDecorator> Create(FString InName, const FSlateColor& InColor)
		{
			return MakeShareable(new FHeaderViewSyntaxDecorator(MoveTemp(InName), InColor));
		}

		bool Supports(const FTextRunParseResults& RunInfo, const FString& Text) const override
		{
			return RunInfo.Name == DecoratorName;
		}

		TSharedRef<ISlateRun> Create(const TSharedRef<class FTextLayout>& TextLayout, const FTextRunParseResults& RunParseResult, const FString& OriginalText, const TSharedRef<FString>& ModelText, const ISlateStyle* Style) override
		{
			FRunInfo RunInfo(RunParseResult.Name);
			for (const TPair<FString, FTextRange>& Pair : RunParseResult.MetaData)
			{
				RunInfo.MetaData.Add(Pair.Key, OriginalText.Mid(Pair.Value.BeginIndex, Pair.Value.EndIndex - Pair.Value.BeginIndex));
			}

			ModelText->Append(OriginalText.Mid(RunParseResult.ContentRange.BeginIndex, RunParseResult.ContentRange.Len()));

			return FSlateTextRun::Create(RunInfo, ModelText, TextStyle);
		}

	private:

		FHeaderViewSyntaxDecorator(FString&& InName, const FSlateColor& InColor)
			: DecoratorName(MoveTemp(InName))
			, TextStyle(FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("Log.Normal"))
		{
			TextStyle.SetColorAndOpacity(InColor);
		}

	private:

		/** Name of this decorator */
		FString DecoratorName;

		/** Style of this decorator */
		FTextBlockStyle TextStyle;

	};

}

// FHeaderViewListItem ////////////////////////////////////////////////////////

TSharedRef<SWidget> FHeaderViewListItem::GenerateWidgetForItem()
{
	// TODO: colors not final, move this struct into config
	static const struct
	{
		FSlateColor Comment = FLinearColor(0.3f, 0.7f, 0.1f, 1.0f);
		FSlateColor Macro = FLinearColor(0.6f, 0.2f, 0.8f, 1.0f);
		FSlateColor Typename = FLinearColor::White;
		FSlateColor Identifier = FLinearColor::White;
		FSlateColor Keyword = FLinearColor(0.0f, 0.4f, 0.8f, 1.0f);
	} SyntaxColors;

	return SNew(SBox)
		.HAlign(HAlign_Fill)
		.Padding(FMargin(4.0f))
		[
			SNew(SRichTextBlock)
			.Text(FText::FromString(RichTextString))
			.TextStyle(FEditorStyle::Get(), "Log.Normal")
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(TEXT("comment"), SyntaxColors.Comment))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(TEXT("macro"), SyntaxColors.Macro))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(TEXT("typename"), SyntaxColors.Typename))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(TEXT("identifier"), SyntaxColors.Identifier))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(TEXT("keyword"), SyntaxColors.Keyword))
		];
}

TSharedPtr<FHeaderViewListItem> FHeaderViewListItem::Create(FString InRawString, FString InRichText)
{
	return MakeShareable(new FHeaderViewListItem(MoveTemp(InRawString), MoveTemp(InRichText)));
}

FHeaderViewListItem::FHeaderViewListItem(FString&& InRawString, FString&& InRichText)
	: RichTextString(MoveTemp(InRichText))
	, RawItemString(MoveTemp(InRawString))
{
}

void FHeaderViewListItem::FormatCommentString(FString InComment, FString& OutRawString, FString& OutRichString)
{
	// normalize newlines to \n
	UE::String::FromHostLineEndingsInline(InComment);

	if (InComment.Contains("\n"))
	{
		/**
		 * Format into a multiline C++ comment, like this one
		 */
		InComment = TEXT("/**\n") + InComment;
		InComment.ReplaceInline(TEXT("\n"), TEXT("\n * "));
		InComment.Append(TEXT("\n */"));
	}
	else
	{
		/** Format into a single line C++ comment, like this one */
		InComment = FString::Printf(TEXT("/** %s */"), *InComment);
	}

	// add the comment to the raw string representation
	OutRawString = InComment;

	// mark each line of the comment as the beginning and end of a comment style for the rich text representation
	InComment.ReplaceInline(TEXT("\n"), TEXT("</>\n<comment>"));
	OutRichString = FString::Printf(TEXT("<comment>%s</>"), *InComment);
}

// SBlueprintHeaderView ///////////////////////////////////////////////////////

void SBlueprintHeaderView::Construct(const FArguments& InArgs)
{
	const float PaddingAmount = 8.0f;
	SelectedBlueprint = nullptr;

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(PaddingAmount))
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClassPickerLabel", "Displaying Blueprint:"))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SSpacer)
				.Size(FVector2D(PaddingAmount))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(400.0f)
				[
					SAssignNew(ClassPickerComboButton, SComboButton)
					.OnGetMenuContent(this, &SBlueprintHeaderView::GetClassPickerMenuContent)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(this, &SBlueprintHeaderView::GetClassPickerText)
					]
				]
			]
		]
		+SVerticalBox::Slot()
		.Padding(FMargin(PaddingAmount))
		[
			//TODO: Add a scroll bar when this overflows from the available space
// 			SNew(SScrollBox)
// 			.Orientation(Orient_Horizontal)
// 			+SScrollBox::Slot()
// 			[
				SAssignNew(ListView, SListView<FHeaderViewListItemPtr>)
				.ListItemsSource(&ListItems)
				.OnGenerateRow(this, &SBlueprintHeaderView::GenerateRowForItem)
//			]
		]
	];
}

FText SBlueprintHeaderView::GetClassPickerText() const
{
	if (const UBlueprint* Blueprint = SelectedBlueprint.Get())
	{
		return FText::FromName(Blueprint->GetFName());
	}

	return LOCTEXT("ClassPickerPickClass", "Select Blueprint Class");
}

TSharedRef<SWidget> SBlueprintHeaderView::GetClassPickerMenuContent()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SBlueprintHeaderView::OnAssetSelected);
	AssetPickerConfig.Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	TSharedRef<SWidget> AssetPickerWidget = ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig);

	return SNew(SBox)
		.HeightOverride(500.f)
		[
			AssetPickerWidget
		];
}

void SBlueprintHeaderView::OnAssetSelected(const FAssetData& SelectedAsset)
{
	ClassPickerComboButton->SetIsOpen(false);

	SelectedBlueprint = Cast<UBlueprint>(SelectedAsset.GetAsset());

	RepopulateListView();
}

TSharedRef<ITableRow> SBlueprintHeaderView::GenerateRowForItem(FHeaderViewListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(STableRow<FHeaderViewListItemPtr>, OwnerTable)
		.Content()
		[
			Item->GenerateWidgetForItem()
		];
}

void SBlueprintHeaderView::RepopulateListView()
{
	ListItems.Empty();

	// Add the class declaration
	ListItems.Add(FHeaderViewClassListItem::Create(SelectedBlueprint));


	// Add the closing brace of the class
	ListItems.Add(FHeaderViewListItem::Create(TEXT("};"), TEXT("};")));
	
	ListView->RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE
