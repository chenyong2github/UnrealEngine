// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMasterFilterIndicatorButton.h"

#include "DisjunctiveNormalFormFilter.h"
#include "EditorFilter.h"
#include "SFilterCheckBox.h"

#include "EditorStyleSet.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void SMasterFilterIndicatorButton::Construct(const FArguments& InArgs, UDisjunctiveNormalFormFilter* InUserDefinedFilters)
{
	UserDefinedFiltersWeakPtr = InUserDefinedFilters;

	ChildSlot
		[
			SNew(SBorder)
			.Padding(0)
			.BorderBackgroundColor( FLinearColor(0.2f, 0.2f, 0.2f, 0.2f) )
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
			[
				SNew(SFilterCheckBox)
				.Padding(FMargin(2.f, 0.f, 2.f, 0.f))
				.ForegroundColor(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateLambda([this]()
				{
					if (!ensure(UserDefinedFiltersWeakPtr.IsValid()))
					{
						return FLinearColor::Black;
					}

					switch (UserDefinedFiltersWeakPtr->GetEditorFilterBehavior())
					{
						case EEditorFilterBehavior::DoNotNegate:
							return FLinearColor::Green;
						case EEditorFilterBehavior::Negate:
							return FLinearColor::Red;
						case EEditorFilterBehavior::Ignore:
							return FLinearColor::Gray;
						case EEditorFilterBehavior::Mixed:
							return FLinearColor::White;
						default: 
							return FLinearColor::Black;
					}
				})))
				.OnFilterClickedOnce(this, &SMasterFilterIndicatorButton::OnFilterClickedOnce)
				[
					SNew(SBox)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
						.ToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]()
						{
							if (UserDefinedFiltersWeakPtr.IsValid())
							{
								switch (UserDefinedFiltersWeakPtr->GetEditorFilterBehavior())
								{
								case EEditorFilterBehavior::DoNotNegate:
									return LOCTEXT("DoNotNegate", "Filter result is neither negated nor ignored. Click to toggle.");
								case EEditorFilterBehavior::Negate:
									return LOCTEXT("Negate", "Filter result is negated. Click to toggle.");
								case EEditorFilterBehavior::Ignore:
									return LOCTEXT("Ignore", "Filter result is ignored. Click to toggle.");
								case EEditorFilterBehavior::Mixed:
									return LOCTEXT("Mixed", "Filter result is mixed. Click to toggle.");
								}
							}
							return LOCTEXT("Invalid", "");
						})))
						.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]()
						{
							if (UserDefinedFiltersWeakPtr.IsValid())
							{
								switch (UserDefinedFiltersWeakPtr->GetEditorFilterBehavior())
								{
									case EEditorFilterBehavior::DoNotNegate:
										return LOCTEXT("FilterResult_Text_DoNotNegate", "Do Not Negate");
									case EEditorFilterBehavior::Negate:
										return LOCTEXT("FilterResult_Text_Negate", "Negate");
									case EEditorFilterBehavior::Ignore:
										return LOCTEXT("FilterResult_Text_Ignore", "Ignore");
									case EEditorFilterBehavior::Mixed:
										return LOCTEXT("FilterResult_Text_Mixed", "Mixed");
								}
							}
							return LOCTEXT("Invalid", "");
						})))
					]
				]
			]
		];
}

void SMasterFilterIndicatorButton::SetUserDefinedFilters(UDisjunctiveNormalFormFilter* InUserDefinedFilters)
{
	UserDefinedFiltersWeakPtr = InUserDefinedFilters;
}

void SMasterFilterIndicatorButton::SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren)
{
	if (ensure(UserDefinedFiltersWeakPtr.IsValid()))
	{
		UserDefinedFiltersWeakPtr->SetEditorFilterBehavior(InFilterBehavior, bIncludeChildren);
	}
}

FReply SMasterFilterIndicatorButton::OnFilterClickedOnce()
{
	if (ensure(UserDefinedFiltersWeakPtr.IsValid()))
	{
		UserDefinedFiltersWeakPtr->IncrementEditorFilterBehavior(true);
	}

	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE