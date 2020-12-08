// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NegatableFilter.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SFilterCheckBox;
class SClickableText;
class FLevelSnapshotsEditorFilters;

enum class ECheckBoxState : uint8;

struct FSlateColor;

/* Displays a filter in the editor. */
class SLevelSnapshotsEditorFilter : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnClickRemoveFilter, TSharedRef<SLevelSnapshotsEditorFilter>);

	~SLevelSnapshotsEditorFilter();
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilter)
	{}
		SLATE_EVENT(FOnClickRemoveFilter, OnClickRemoveFilter)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakObjectPtr<UNegatableFilter>& InFilter, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters);

	const TWeakObjectPtr<UNegatableFilter>& GetSnapshotFilter() const;

	//~ Begin SWidget Interface
	int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	//~ End SWidget Interface

	
private:

	FReply OnSelectFilterForEdit();
	void OnActiveFilterChanged(ULevelSnapshotFilter* NewFilter);
	FReply OnNegateFilter();
	FReply OnRemoveFilter();

	
	FOnClickRemoveFilter OnClickRemoveFilter;

	FDelegateHandle ActiveFilterChangedDelegateHandle;
	bool bIsBeingEdited = false;;
	
	/** The button to toggle the filter on or off */
	TSharedPtr<SFilterCheckBox> ToggleButtonPtr;
	/* Displays filter name and shows details panel when clicked. */
	TSharedPtr<SClickableText> FilterNamePtr;

	/* Filter managed by this widget */
	TWeakObjectPtr<UNegatableFilter> SnapshotFilter;
	/* Used to set the filter to edit */
	TWeakPtr<FLevelSnapshotsEditorFilters> FiltersModelPtr;
};
