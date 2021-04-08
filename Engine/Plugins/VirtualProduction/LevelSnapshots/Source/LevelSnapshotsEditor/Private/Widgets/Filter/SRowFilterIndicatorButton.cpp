// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRowFilterIndicatorButton.h"

#include "ConjunctionFilter.h"
#include "EditorFilter.h"
#include "LevelSnapshotsEditorStyle.h"
#include "SFilterCheckBox.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"


#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void SRowFilterIndicatorButton::Construct(const FArguments& InArgs, UConjunctionFilter* InManagedFilter)
{
	ManagedFilterWeakPtr = InManagedFilter;

	ChildSlot
		[
			SNew(SBorder)
			.Padding(0)
			.BorderImage(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.GroupBorder"))
			[
				SNew(SFilterCheckBox)
				.ToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]()
				{
					if (ManagedFilterWeakPtr.IsValid())
					{
						switch (ManagedFilterWeakPtr->GetEditorFilterBehavior())
						{
						case EEditorFilterBehavior::DoNotNegate:
							return LOCTEXT("DoNotNegate", "Filter result is neither negated nor ignored. Click to toggle.");
						case EEditorFilterBehavior::Negate:
							return LOCTEXT("Negate", "Filter result is negated. Click to toggle.");
						case EEditorFilterBehavior::Ignore:
							return LOCTEXT("Ignore", "Filter result is ignored. Click to toggle.");
						}
					}
					return LOCTEXT("Invalid", "");
				})))
				.ForegroundColor(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateLambda([this]()
				{
					if (!ensure(ManagedFilterWeakPtr.IsValid()))
					{
						return FLinearColor::Black;
					}

					switch (ManagedFilterWeakPtr->GetEditorFilterBehavior())
					{
						case EEditorFilterBehavior::DoNotNegate:
							return FLinearColor::Green;
						case EEditorFilterBehavior::Negate:
							return FLinearColor::Red;
						case EEditorFilterBehavior::Ignore:
							return FLinearColor::Gray;
						default: 
							return FLinearColor::Black;
					}
				})))
				.OnFilterClickedOnce(this, &SRowFilterIndicatorButton::OnFilterClickedOnce)
				[
					InArgs._Content.Widget
				]
			]
		];
}

void SRowFilterIndicatorButton::SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren)
{
	if (ensure(ManagedFilterWeakPtr.IsValid()))
	{
		ManagedFilterWeakPtr->SetEditorFilterBehavior(InFilterBehavior, bIncludeChildren);
	}
}

FReply SRowFilterIndicatorButton::OnFilterClickedOnce()
{
	if (ensure(ManagedFilterWeakPtr.IsValid()))
	{
		ManagedFilterWeakPtr->IncrementEditorFilterBehavior();
		ManagedFilterWeakPtr->UpdateParentFilterFromChild(true);
	}

	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE