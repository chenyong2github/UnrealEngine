// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorFilter.h"

#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsEditorFilters.h"
#include "SFilterCheckBox.h"

#include "EditorStyleSet.h"
#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshotsEditorData.h"
#include "NegatableFilter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

/* Child of SFilterCheckBox. Like text but allows click events. */
class SClickableText : public STextBlock
{
public:
	
	void SetOnClicked(const FOnClicked& Callback)
	{
		OnClicked = Callback;
	}

	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			return OnClicked.Execute();
		}
		// Allow SFilterCheckBox to respond to right-clicks, etc.
		return FReply::Unhandled();
	}

private:
	FOnClicked OnClicked;;
};

SLevelSnapshotsEditorFilter::~SLevelSnapshotsEditorFilter()
{
	TSharedPtr<FLevelSnapshotsEditorFilters> FilterModel =  FiltersModelPtr.Pin();
	if (ensure(FilterModel))
	{
		EditorData->OnEditedFiterChanged.Remove(ActiveFilterChangedDelegateHandle);
	}

	if (SnapshotFilter.IsValid())
	{
		SnapshotFilter->OnFilterDestroyed.Remove(OnFilterDestroyedDelegateHandle);
	}
}

void SLevelSnapshotsEditorFilter::Construct(const FArguments& InArgs, const TWeakObjectPtr<UNegatableFilter>& InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	if (!ensure(InFilter.IsValid()))
	{
		return;
	}
	
	SnapshotFilter = InFilter;
	EditorData = InFilters->GetBuilder()->EditorDataPtr;
	FiltersModelPtr = InFilters;
	OnClickRemoveFilter = InArgs._OnClickRemoveFilter;

	ChildSlot
		[
			SNew(SBorder)
			.Padding(0)
			.BorderBackgroundColor( FLinearColor(0.2f, 0.2f, 0.2f, 0.2f) )
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
			[
				SAssignNew(ToggleButtonPtr, SFilterCheckBox) 
				.ToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([this]()
				{
					if (SnapshotFilter.IsValid())
					{
						switch (SnapshotFilter->GetFilterBehavior())
	                    {
	                        case EFilterBehavior::DoNotNegate:
								return LOCTEXT("DoNotNegate", "Filter result is neither negated nor ignored. Click to toggle.");
	                        case EFilterBehavior::Negate:
								return LOCTEXT("Negate", "Filter result is negated. Click to toggle.");
	                        case EFilterBehavior::Ignore:
								return LOCTEXT("Ignore", "Filter result is ignored. Click to toggle.");
	                    }
					}
					return LOCTEXT("Invalid", "");
				})))
				.OnFilterClickedOnce(this, &SLevelSnapshotsEditorFilter::OnNegateFilter)
				.OnFilterAltClicked(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter)
				.OnFilterCtrlClicked(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter)
				.OnFilterMiddleButtonClicked(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter)
				.OnFilterRightButtonClicked(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter)
				.ForegroundColor(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateLambda([this]()
				{
					if (!ensure(SnapshotFilter.IsValid()))
					{
						return FLinearColor::Black;
					}

					switch (SnapshotFilter->GetFilterBehavior())
					{
						case EFilterBehavior::DoNotNegate:
							return FLinearColor::Green;
						case EFilterBehavior::Negate:
							return FLinearColor::Red;
						case EFilterBehavior::Ignore:
							return FLinearColor::Gray;
						default: 
							return FLinearColor::Black;
					}
				})))
				[
					SAssignNew(FilterNamePtr, SClickableText)
	                    .ToolTipText(LOCTEXT("RightClickToRemove", "Right-click to remove filter."))
	                    .ColorAndOpacity(FLinearColor::White)
	                    .Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
	                    .ShadowOffset(FVector2D(1.f, 1.f))
	                    .Text_Lambda([InFilter]()
	                    {
	                    	if (InFilter.IsValid())
	                    	{
	                    		return InFilter->GetDisplayName();
	                    	}
	                    	return FText();
	                    })	
				]
			]
		];

	// Hightlight & unhighlight filter when being edited
	FilterNamePtr->SetOnClicked(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnSelectFilterForEdit));
	
	ActiveFilterChangedDelegateHandle = InFilters->GetBuilder()->EditorDataPtr.Get()->OnEditedFiterChanged.AddRaw(this, &SLevelSnapshotsEditorFilter::OnActiveFilterChanged);
	OnFilterDestroyedDelegateHandle = InFilter->OnFilterDestroyed.AddLambda([this](UNegatableFilter* Filter)
	{
		if (ensure(EditorData.IsValid()) && EditorData->IsEditingFilter(Filter))
		{
			return EditorData->SetEditedFilter({});
		}
	});
}

const TWeakObjectPtr<UNegatableFilter>& SLevelSnapshotsEditorFilter::GetSnapshotFilter() const
{
	return SnapshotFilter;
}

int32 SLevelSnapshotsEditorFilter::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	
	if (bShouldHighlightFilter)
	{
		const FVector2D ShadowSize(14, 14);
		const FSlateBrush* ShadowBrush = FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.FilterSelected");
	
		// Draw a shadow	
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToInflatedPaintGeometry(ShadowSize),
			ShadowBrush
		);
	}
	
	return LayerId;
}

FReply SLevelSnapshotsEditorFilter::OnSelectFilterForEdit()
{
	if (!ensure(EditorData.IsValid()))
	{
		return FReply::Handled();
	}

	if (EditorData->IsEditingFilter(SnapshotFilter.Get()))
	{
		bShouldHighlightFilter = false;
		EditorData->SetEditedFilter(TOptional<UNegatableFilter*>());
	}
	else
	{
		bShouldHighlightFilter = true;
		EditorData->SetEditedFilter(SnapshotFilter.Get());
	}
	
	return FReply::Handled();
}

void SLevelSnapshotsEditorFilter::OnActiveFilterChanged(const TOptional<UNegatableFilter*>& NewFilter)
{
	if (!SnapshotFilter.IsValid() || !FiltersModelPtr.IsValid()) // This can actually become stale after a save: UI rebuilds next tick but object was already destroyed.
	{
		return;
	}
	bShouldHighlightFilter = NewFilter.IsSet() && *NewFilter == SnapshotFilter.Get();
}

FReply SLevelSnapshotsEditorFilter::OnNegateFilter()
{
	if (ensure(SnapshotFilter.IsValid()))
	{
		SnapshotFilter->IncrementFilterBehaviour();
	}
	return FReply::Handled();
}

FReply SLevelSnapshotsEditorFilter::OnRemoveFilter()
{
	OnClickRemoveFilter.Execute(SharedThis(this));
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
