// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NegatableFilter.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SFilterCheckBox;
class FLevelSnapshotsEditorFilters;

enum class ECheckBoxState : uint8;

struct FSlateColor;

/* Displays a filter in the editor. */
class SLevelSnapshotsEditorFilter : public SCompoundWidget
{
public:
	friend class SFilterCheckBox;
	DECLARE_DELEGATE_OneParam(FOnClickRemoveFilter, TSharedRef<SLevelSnapshotsEditorFilter>);

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilter)
	{}
		SLATE_EVENT(FOnClickRemoveFilter, OnClickRemoveFilter)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakObjectPtr<UNegatableFilter>& InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters);

	const TWeakObjectPtr<UNegatableFilter>& GetSnapshotFilter() const;
	
private:
	
	ECheckBoxState IsChecked() const;
	void FilterToggled(ECheckBoxState NewState);

	TSharedRef<SWidget> GetRightClickMenuContent();

	void OnClick() const;


	
	FOnClickRemoveFilter OnClickRemoveFilter;
	
	/** The button to toggle the filter on or off */
	TSharedPtr<SFilterCheckBox> ToggleButtonPtr;

	/* Filter managed by this widget */
	TWeakObjectPtr<UNegatableFilter> SnapshotFilter;
	/* Used to set the filter to edit */
	TWeakPtr<FLevelSnapshotsEditorFilters> FiltersModelPtr;
};
