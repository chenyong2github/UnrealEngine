// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilter.h"

#include "LevelSnapshotFilters.h"

#include "Views/Filter/LevelSnapshotsEditorFilters.h"

#include "EditorStyleSet.h"
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


SLevelSnapshotsEditorFilter::~SLevelSnapshotsEditorFilter()
{
}

void SLevelSnapshotsEditorFilter::Construct(const FArguments& InArgs, ULevelSnapshotFilter* InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	// TODO. For now we can initialize the UObject here
	// Outer the Editor UObject

	//UBlueprint* const Blueprint = LoadObject<UBlueprint>(GetTransientPackage(), TEXT("Blueprint'/Game/LevelSnapshots/TestFilter.TestFilter'"));
	//UObject* const NewObj = NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass, Blueprint->GeneratedClass->GetFName());

	SnapshotFilter = InFilter;
	FiltersModelPtr = InFilters;

	TAttribute<FText> FilterToolTip;

	Name = InArgs._Text;
	FilterColor = InArgs._FilterColor;

	ChildSlot
		[
			SNew(SBorder)
			.Padding(0)
			.BorderBackgroundColor( FLinearColor(0.2f, 0.2f, 0.2f, 0.2f) )
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
			[
				SAssignNew( ToggleButtonPtr, SFilterCheckBox)
				.Style(FEditorStyle::Get(), "ContentBrowser.FilterButton")
				.ToolTipText(FilterToolTip)
				.Padding(this, &SLevelSnapshotsEditorFilter::GetFilterNamePadding)
				.IsChecked(this, &SLevelSnapshotsEditorFilter::IsChecked)
				.OnCheckStateChanged(this, &SLevelSnapshotsEditorFilter::FilterToggled)
				.OnGetMenuContent(this, &SLevelSnapshotsEditorFilter::GetRightClickMenuContent)
				.ForegroundColor(this, &SLevelSnapshotsEditorFilter::GetFilterForegroundColor)
				[
					SNew(STextBlock)
					.ColorAndOpacity(this, &SLevelSnapshotsEditorFilter::GetFilterNameColorAndOpacity)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
					.ShadowOffset(FVector2D(1.f, 1.f))
					.Text(this, &SLevelSnapshotsEditorFilter::GetFilterName)
				]
			]
		];

	ToggleButtonPtr->SetEditorFilter(SharedThis(this));
}

ECheckBoxState SLevelSnapshotsEditorFilter::IsChecked() const
{
	return ECheckBoxState::Checked;
}

FMargin SLevelSnapshotsEditorFilter::GetFilterNamePadding() const
{
	return FMargin(3, 2, 4, 0);
}

void SLevelSnapshotsEditorFilter::FilterToggled(ECheckBoxState NewState)
{
}

TSharedRef<SWidget> SLevelSnapshotsEditorFilter::GetRightClickMenuContent()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, NULL);

	MenuBuilder.BeginSection("FilterOptions", LOCTEXT("FilterContextHeading", "Filter Options"));
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("RemoveFilter", "Remove: {0}"), GetFilterName()),
			LOCTEXT("RemoveFilterTooltip", "Remove this filter from the list. It can be added again in the filters menu."),
			FSlateIcon(),
			FUIAction()
			);

		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("EnableOnlyThisFilter", "Enable Only This: {0}"), GetFilterName()),
			LOCTEXT("EnableOnlyThisFilterTooltip", "Enable only this filter from the list."),
			FSlateIcon(),
			FUIAction()
			);

	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

FSlateColor SLevelSnapshotsEditorFilter::GetFilterForegroundColor() const
{
	return FilterColor.Get();
}

FSlateColor SLevelSnapshotsEditorFilter::GetFilterNameColorAndOpacity() const
{
	return FLinearColor::White;
}

FText SLevelSnapshotsEditorFilter::GetFilterName() const
{
	return Name.Get();
}

void SLevelSnapshotsEditorFilter::OnClick() const
{
	FiltersModelPtr.Pin()->SetActiveFilter(SnapshotFilter.Get());
}

#undef LOCTEXT_NAMESPACE
