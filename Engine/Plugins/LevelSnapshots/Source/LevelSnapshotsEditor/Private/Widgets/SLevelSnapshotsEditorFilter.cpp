// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorFilter.h"

#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsEditorFilters.h"

#include "EditorStyleSet.h"
#include "NegatableFilter.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

DECLARE_DELEGATE(FOnFocusLost);

/* Handles visuals for negation. */
class SFilterCheckBox : public SCheckBox
{
public:

	void SetOnFilterCtrlClicked(const FOnClicked& NewFilterCtrlClicked)
	{
		OnFilterCtrlClicked = NewFilterCtrlClicked;
	}

	void SetOnFilterAltClicked(const FOnClicked& NewFilteAltClicked)
	{
		OnFilterAltClicked = NewFilteAltClicked;
	}

	void SetOnFilterMiddleButtonClicked( const FOnClicked& NewFilterMiddleButtonClicked )
	{
		OnFilterMiddleButtonClicked = NewFilterMiddleButtonClicked;
	}

	void SetOnFilterRightButtonClicked( const FOnClicked& NewOnFilterRightButtonClicked )
	{
		OnFilterRightButtonClicked = NewOnFilterRightButtonClicked;
	}

	void SetOnFilterClickedOnce(const FOnClicked& NewOnFilterClickedOnce)
	{
		OnFilterClickedOnce = NewOnFilterClickedOnce;
	}

	void SetOnFocusLost(const FOnFocusLost& NewOnFocusLostCallback)
	{
		OnFocusLostCallback = NewOnFocusLostCallback;
	}

	void OnFocusLost(const FFocusEvent& InFocusEvent) override
	{
		OnFocusLostCallback.Execute();
	}
	
	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Override check box behaviour
		return FReply::Handled();
	}
	FReply OnMouseButtonUp( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
	{
		if (InMouseEvent.IsControlDown())
		{
			return OnFilterCtrlClicked.Execute().ReleaseMouseCapture();
		}
		else if (InMouseEvent.IsAltDown())
		{
			return OnFilterAltClicked.Execute().ReleaseMouseCapture();
		}
		else if(InMouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
		{
			return OnFilterMiddleButtonClicked.Execute().ReleaseMouseCapture();
		}
		else if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			return OnFilterRightButtonClicked.Execute().ReleaseMouseCapture();
		}
		else if(OnFilterClickedOnce.IsBound())
		{
			return OnFilterClickedOnce.Execute().ReleaseMouseCapture();
		}
		
		return FReply::Handled().ReleaseMouseCapture();
	}

private:
	FOnClicked OnFilterCtrlClicked;
	FOnClicked OnFilterAltClicked;
	FOnClicked OnFilterMiddleButtonClicked;
	FOnClicked OnFilterRightButtonClicked;
	FOnClicked OnFilterClickedOnce;

	FOnFocusLost OnFocusLostCallback;
};

/* Child of SFilterCheckBox. Like text but allows click events. */
class SClickableText : public STextBlock
{
public:
	
	void SetOnClicked(const FOnClicked& Callback)
	{
		OnClicked = Callback;
	}

	FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
	{
		return OnClicked.Execute();
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
		FilterModel->GetOnSetActiveFilter().Remove(ActiveFilterChangedDelegateHandle);
	}
}

void SLevelSnapshotsEditorFilter::Construct(const FArguments& InArgs, const TWeakObjectPtr<UNegatableFilter>& InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	if (!ensure(InFilter.IsValid()))
	{
		return;
	}
	
	SnapshotFilter = InFilter;
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
						return SnapshotFilter->ShouldNegate() ? LOCTEXT("NegationTrue", "Click to not negate filter result.") : LOCTEXT("NegationFalse", "Click to negate filter result.");
					}
					return LOCTEXT("Invalid", "");
				})))
				.Style(FEditorStyle::Get(), "ContentBrowser.FilterButton")
				.Padding(FMargin(3, 2, 4, 0))
				.IsChecked(ECheckBoxState::Checked)
				.ForegroundColor(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateLambda([this]() {return SnapshotFilter.IsValid() && SnapshotFilter->ShouldNegate() ? FLinearColor::Red : FLinearColor::Green; })))
				[
					SAssignNew(FilterNamePtr, SClickableText)
						.ToolTipText(LOCTEXT("RightClickToRemove", "Right-click to remove filter."))
						.ColorAndOpacity(FLinearColor::White)
						.Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
						.ShadowOffset(FVector2D(1.f, 1.f))
						.Text(InFilter->GetDisplayName())
				]
			]
		];

	ToggleButtonPtr->SetOnFilterClickedOnce(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnNegateFilter));
	// Remove filter for alt, strg, middle and right-clicks
	ToggleButtonPtr->SetOnFilterAltClicked(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter));
	ToggleButtonPtr->SetOnFilterCtrlClicked(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter));
	ToggleButtonPtr->SetOnFilterMiddleButtonClicked(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter));
	ToggleButtonPtr->SetOnFilterRightButtonClicked(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnRemoveFilter));
	// Deselect when user clicks away
	ToggleButtonPtr->SetOnFocusLost(FOnFocusLost::CreateLambda([this]()
	{
		if (TSharedPtr<FLevelSnapshotsEditorFilters> Model = FiltersModelPtr.Pin())
		{
			Model->SetActiveFilter(nullptr);
		}
	}));

	// Hightlight & unhighlight filter when being edited
	FilterNamePtr->SetOnClicked(FOnClicked::CreateRaw(this, &SLevelSnapshotsEditorFilter::OnSelectFilterForEdit));
	ActiveFilterChangedDelegateHandle = InFilters->GetOnSetActiveFilter().AddRaw(this, &SLevelSnapshotsEditorFilter::OnActiveFilterChanged);
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

	if (bIsBeingEdited)
	{
		const FVector2D ShadowSize(14, 14);
		const FSlateBrush* ShadowBrush = FEditorStyle::GetBrush("Graph.CompactNode.ShadowSelected");
	
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
	TSharedPtr<FLevelSnapshotsEditorFilters> Model = FiltersModelPtr.Pin();
	if (ensure(Model))
	{
		bIsBeingEdited = true;
		Model->SetActiveFilter(SnapshotFilter->GetChildFilter());
	}
	return FReply::Handled();
}

void SLevelSnapshotsEditorFilter::OnActiveFilterChanged(ULevelSnapshotFilter* NewFilter)
{
	if (ensure(SnapshotFilter.IsValid()))
	{
		bIsBeingEdited = NewFilter == SnapshotFilter->GetChildFilter();
	}

	// This ensures SFilterCheckBox::OnFocusLost gets called later on and unhighlights the widget when user clicks away. Needed for when this widget was just created by drag-drop. 
	if (ensure(FiltersModelPtr.IsValid()) && FiltersModelPtr.Pin()->GetActiveFilter() == SnapshotFilter->GetChildFilter())
	{
		FSlateApplication::Get().SetAllUserFocus(ToggleButtonPtr, EFocusCause::SetDirectly);
	}
}

FReply SLevelSnapshotsEditorFilter::OnNegateFilter()
{
	if (ensure(SnapshotFilter.IsValid()))
	{
		const bool bNewShouldNegate = !SnapshotFilter->ShouldNegate();
		SnapshotFilter->SetShouldNegate(bNewShouldNegate);
	}
	return FReply::Handled();
}

FReply SLevelSnapshotsEditorFilter::OnRemoveFilter()
{
	OnClickRemoveFilter.Execute(SharedThis(this));
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
