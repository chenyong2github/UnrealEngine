// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "UObject/WeakObjectPtr.h"

class SFilterCheckBox;
class ULevelSnapshotFilter;
class FLevelSnapshotsEditorFilters;

enum class ECheckBoxState : uint8;

struct FSlateColor;

class SLevelSnapshotsEditorFilter : public SCompoundWidget
{
public:
	friend class SFilterCheckBox;

	~SLevelSnapshotsEditorFilter();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilter)
	{}

		SLATE_ATTRIBUTE(FText, Text)

		SLATE_ATTRIBUTE(FLinearColor, FilterColor)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, ULevelSnapshotFilter* InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters);

private:
	ECheckBoxState IsChecked() const;

	FMargin GetFilterNamePadding() const;

	void FilterToggled(ECheckBoxState NewState);

	TSharedRef<SWidget> GetRightClickMenuContent();

	FSlateColor GetFilterForegroundColor() const;

	FSlateColor GetFilterNameColorAndOpacity() const;

	FText GetFilterName() const;

	void OnClick() const;

private:
	/** The button to toggle the filter on or off */
	TSharedPtr<SFilterCheckBox> ToggleButtonPtr;

	TAttribute<FText> Name;

	TAttribute<FLinearColor> FilterColor;

	TWeakObjectPtr<ULevelSnapshotFilter> SnapshotFilter;

	TWeakPtr<FLevelSnapshotsEditorFilters> FiltersModelPtr;
};
