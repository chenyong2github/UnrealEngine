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
#include "HeaderViewFunctionListItem.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "EditorStyleSet.h"
#include "String/LineEndings.h"

#define LOCTEXT_NAMESPACE "SBlueprintHeaderView"

// HeaderViewSyntaxDecorators /////////////////////////////////////////////////

namespace HeaderViewSyntaxDecorators
{
	const FString CommentDecorator = TEXT("comment");
	const FString IdentifierDecorator = TEXT("identifier");
	const FString KeywordDecorator = TEXT("keyword");
	const FString MacroDecorator = TEXT("macro");
	const FString TypenameDecorator = TEXT("typename");
}

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
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(HeaderViewSyntaxDecorators::CommentDecorator, SyntaxColors.Comment))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(HeaderViewSyntaxDecorators::IdentifierDecorator, SyntaxColors.Identifier))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(HeaderViewSyntaxDecorators::KeywordDecorator, SyntaxColors.Keyword))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(HeaderViewSyntaxDecorators::MacroDecorator, SyntaxColors.Macro))
			+SRichTextBlock::Decorator(FHeaderViewSyntaxDecorator::Create(HeaderViewSyntaxDecorators::TypenameDecorator, SyntaxColors.Typename))
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
	InComment.ReplaceInline(TEXT("\n"), *FString::Printf(TEXT("</>\n<%s>"), *HeaderViewSyntaxDecorators::CommentDecorator));
	OutRichString = FString::Printf(TEXT("<%s>%s</>"), *HeaderViewSyntaxDecorators::CommentDecorator, *InComment);
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

	if (UBlueprint* Blueprint = SelectedBlueprint.Get())
	{
		// Add the class declaration
		ListItems.Add(FHeaderViewClassListItem::Create(SelectedBlueprint));

		PopulateFunctionItems(Blueprint);

		// Add the closing brace of the class
		ListItems.Add(FHeaderViewListItem::Create(TEXT("};"), TEXT("};")));
	}

	ListView->RequestListRefresh();
}

void SBlueprintHeaderView::PopulateFunctionItems(const UBlueprint* Blueprint)
{
	if (Blueprint)
	{
		// We should only add an access specifier line if the previous function was a different one
		int32 PrevAccessSpecifier = 0;
		for (const UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
		{
			if (FunctionGraph && !UEdGraphSchema_K2::IsConstructionScript(FunctionGraph))
			{
				TArray<UK2Node_FunctionEntry*> EntryNodes;
				FunctionGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);

				if (ensure(EntryNodes.Num() == 1 && EntryNodes[0]))
				{
					int32 AccessSpecifier = EntryNodes[0]->GetFunctionFlags() & FUNC_AccessSpecifiers;

					if (AccessSpecifier != PrevAccessSpecifier)
					{
						switch (AccessSpecifier)
						{
						case FUNC_Public:
							ListItems.Add(FHeaderViewListItem::Create(TEXT("public:"), FString::Printf(TEXT("<%s>public</>:"), *HeaderViewSyntaxDecorators::KeywordDecorator)));
							break;
						case FUNC_Protected:
							ListItems.Add(FHeaderViewListItem::Create(TEXT("protected:"), FString::Printf(TEXT("<%s>protected</>:"), *HeaderViewSyntaxDecorators::KeywordDecorator)));
							break;
						case FUNC_Private:
							ListItems.Add(FHeaderViewListItem::Create(TEXT("private:"), FString::Printf(TEXT("<%s>private</>:"), *HeaderViewSyntaxDecorators::KeywordDecorator)));
							break;
						}
					}
					else
					{
						// add an empty line to space functions out
						ListItems.Add(FHeaderViewListItem::Create(TEXT(""), TEXT("")));
					}

					PrevAccessSpecifier = AccessSpecifier;

					ListItems.Add(FHeaderViewFunctionListItem::Create(EntryNodes[0]));
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
