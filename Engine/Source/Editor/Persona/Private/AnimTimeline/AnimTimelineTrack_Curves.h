// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimTimelineTrack.h"
#include "Animation/Skeleton.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

struct FSmartNameSortItem
{
	FName SmartName;
	USkeleton::AnimCurveUID ID;

	FSmartNameSortItem(const FName& InSmartName, const USkeleton::AnimCurveUID& InID)
		: SmartName(InSmartName)
		, ID(InID)
	{}
};

typedef TSharedPtr<FSmartNameSortItem> FCurveListItem;
typedef SListView<FCurveListItem> SCurveListView;

class FAnimTimelineTrack_Curves : public FAnimTimelineTrack
{
	ANIMTIMELINE_DECLARE_TRACK(FAnimTimelineTrack_Curves, FAnimTimelineTrack);

public:
	FAnimTimelineTrack_Curves(const TSharedRef<FAnimModel>& InModel);

	/** FAnimTimelineTrack interface */
	virtual TSharedRef<SWidget> GenerateContainerWidgetForOutliner(const TSharedRef<SAnimOutlinerItem>& InRow) override;

private:
	TSharedRef<SWidget> BuildCurvesSubMenu();
	void FillMetadataEntryMenu(FMenuBuilder& Builder);
	void FillVariableCurveMenu(FMenuBuilder& Builder);
	void AddMetadataEntry(USkeleton::AnimCurveUID Uid);
	void CreateNewMetadataEntryClicked();
	void CreateNewMetadataEntry(const FText& CommittedText, ETextCommit::Type CommitType);
	void CreateNewCurveClicked();
	void CreateTrack(const FText& ComittedText, ETextCommit::Type CommitInfo);
	void AddVariableCurve(USkeleton::AnimCurveUID CurveUid);
	void DeleteAllCurves();

	/** Handlers for showing curve points */
	void HandleShowCurvePoints();
	bool IsShowCurvePointsEnabled() const;

	virtual TSharedRef<ITableRow> GenerateCurveListRow(FCurveListItem InItem, const TSharedRef<STableViewBase>& OwnerList);
	void OnTypeSelectionChanged(FCurveListItem Selection, ESelectInfo::Type SelectInfo);
	void OnMouseButtonClicked(FCurveListItem Selection);

	/** Curve searching support */
	FText SearchText;
	void OnCurveFilterTextChanged(const FText& NewText);
	void OnCurveFilterTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo);
	bool GetCurvesMatchingSearch(const FText& InSearchText, const TArray<FCurveListItem>& UnfilteredList, TArray<FCurveListItem>& OutFilteredList);
private:
	TSharedPtr<SWidget>	OutlinerWidget;

	TSharedPtr<SCurveListView>	CurveListView;

	TArray<FCurveListItem >	CurveItems;
	TArray<FCurveListItem >	FilteredCurveItems;

	static const float	CurveListPadding;
};