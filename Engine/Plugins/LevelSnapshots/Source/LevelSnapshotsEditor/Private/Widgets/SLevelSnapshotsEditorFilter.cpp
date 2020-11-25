// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilter.h"

#include "LevelSnapshotFilters.h"

#include "Views/Filter/LevelSnapshotsEditorFilters.h"

#include "EditorStyleSet.h"
#include "NegatableFilter.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

class SFilterCheckBox : public SCheckBox
{
public:
	void SetEditorFilter(const TSharedRef<SLevelSnapshotsEditorFilter>& InFilter)
	{
		SnapshotFilterPtr = InFilter;
	}

	void SetOnFilterCtrlClicked(const FOnClicked& NewFilterCtrlClicked)
	{
		OnFilterCtrlClicked = NewFilterCtrlClicked;
	}

	void SetOnFilterAltClicked(const FOnClicked& NewFilteAltClicked)
	{
		OnFilterAltClicked = NewFilteAltClicked;
	}

	void SetOnFilterDoubleClicked( const FOnClicked& NewFilterDoubleClicked )
	{
		OnFilterDoubleClicked = NewFilterDoubleClicked;
	}

	void SetOnFilterMiddleButtonClicked( const FOnClicked& NewFilterMiddleButtonClicked )
	{
		OnFilterMiddleButtonClicked = NewFilterMiddleButtonClicked;
	}

	virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
	{
		if ( InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnFilterDoubleClicked.IsBound() )
		{
			return OnFilterDoubleClicked.Execute();
		}
		else
		{
			return SCheckBox::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
		}
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SnapshotFilterPtr.Pin()->OnClick();
		return SCheckBox::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonUp( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
	{
		if (InMouseEvent.IsControlDown() && OnFilterCtrlClicked.IsBound())
		{
			return OnFilterCtrlClicked.Execute();
		}
		else if (InMouseEvent.IsAltDown() && OnFilterAltClicked.IsBound())
		{
			return OnFilterAltClicked.Execute();
		}
		else if( InMouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton && OnFilterMiddleButtonClicked.IsBound() )
		{
			return OnFilterMiddleButtonClicked.Execute();
		}
		else
		{
			SCheckBox::OnMouseButtonUp(InMyGeometry, InMouseEvent);
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

private:
	FOnClicked OnFilterCtrlClicked;
	FOnClicked OnFilterAltClicked;
	FOnClicked OnFilterDoubleClicked;
	FOnClicked OnFilterMiddleButtonClicked;

	TWeakPtr<SLevelSnapshotsEditorFilter> SnapshotFilterPtr;
};

void SLevelSnapshotsEditorFilter::Construct(const FArguments& InArgs, const TWeakObjectPtr<UNegatableFilter>& InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	if (!ensure(InFilter.IsValid()))
	{
		return;
	}
	
	OnClickRemoveFilter = InArgs._OnClickRemoveFilter;
	SnapshotFilter = InFilter;
	FiltersModelPtr = InFilters;

	ChildSlot
		[
			SNew(SBorder)
			.Padding(0)
			.BorderBackgroundColor( FLinearColor(0.2f, 0.2f, 0.2f, 0.2f) )
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
			[
				SAssignNew( ToggleButtonPtr, SFilterCheckBox)
				.Style(FEditorStyle::Get(), "ContentBrowser.FilterButton")
				.Padding(FMargin(3, 2, 4, 0))
				.IsChecked(this, &SLevelSnapshotsEditorFilter::IsChecked)
				.OnCheckStateChanged(this, &SLevelSnapshotsEditorFilter::FilterToggled)
				.OnGetMenuContent(this, &SLevelSnapshotsEditorFilter::GetRightClickMenuContent)
				.ForegroundColor(FLinearColor::Green)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor::White)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
					.ShadowOffset(FVector2D(1.f, 1.f))
					.Text(InFilter->GetDisplayName())
				]
			]
		];

	ToggleButtonPtr->SetEditorFilter(SharedThis(this));
}

const TWeakObjectPtr<UNegatableFilter>& SLevelSnapshotsEditorFilter::GetSnapshotFilter() const
{
	return SnapshotFilter;
}

ECheckBoxState SLevelSnapshotsEditorFilter::IsChecked() const
{
	if (ensure(SnapshotFilter.IsValid()))
	{
		return SnapshotFilter->ShouldNegate() ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
	}
	return ECheckBoxState::Undetermined;
}

void SLevelSnapshotsEditorFilter::FilterToggled(ECheckBoxState NewState)
{
	if (ensure(SnapshotFilter.IsValid()))
	{
		SnapshotFilter->SetShouldNegate(NewState == ECheckBoxState::Unchecked);
	}
}

TSharedRef<SWidget> SLevelSnapshotsEditorFilter::GetRightClickMenuContent()
{
	FMenuBuilder MenuBuilder(true, NULL);
	if (!ensure(SnapshotFilter.IsValid()))
	{
		return MenuBuilder.MakeWidget();
	}
	
	MenuBuilder.BeginSection("FilterOptions", LOCTEXT("FilterContextHeading", "Filter Options"));
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("RemoveFilter", "Remove: {0}"), SnapshotFilter->GetDisplayName()),
			LOCTEXT("RemoveFilterTooltip", "Remove this filter from the list. It can be added again in the filters menu."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]()
			{
				OnClickRemoveFilter.ExecuteIfBound(SharedThis(this));
			}))
			);

		// TODO:
		/*MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("EnableOnlyThisFilter", "Enable Only This: {0}"), GetFilterName()),
			LOCTEXT("EnableOnlyThisFilterTooltip", "Enable only this filter from the list."),
			FSlateIcon(),
			FUIAction()
			);*/
	}
	MenuBuilder.EndSection();
	
	return MenuBuilder.MakeWidget();
}

void SLevelSnapshotsEditorFilter::OnClick() const
{
	FiltersModelPtr.Pin()->SetActiveFilter(SnapshotFilter.Get());
}

#undef LOCTEXT_NAMESPACE
