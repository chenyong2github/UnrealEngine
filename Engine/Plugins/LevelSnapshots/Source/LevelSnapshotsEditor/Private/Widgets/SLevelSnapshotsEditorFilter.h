// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SFilterCheckBox;

enum class ECheckBoxState : uint8;

struct FSlateColor;

class SLevelSnapshotsEditorFilter : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorFilter();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilter)
	{}

		SLATE_ATTRIBUTE(FText, Text)

		SLATE_ATTRIBUTE(FLinearColor, FilterColor)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	ECheckBoxState IsChecked() const;

	FMargin GetFilterNamePadding() const;

	void FilterToggled(ECheckBoxState NewState);

	TSharedRef<SWidget> GetRightClickMenuContent();

	FSlateColor GetFilterForegroundColor() const;

	FSlateColor GetFilterNameColorAndOpacity() const;

	FText GetFilterName() const;

private:
	/** The button to toggle the filter on or off */
	TSharedPtr<SFilterCheckBox> ToggleButtonPtr;

	TAttribute<FText> Name;

	TAttribute<FLinearColor> FilterColor;
};
