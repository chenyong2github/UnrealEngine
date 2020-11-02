// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"

namespace Insights { class ITimingViewSession; }
namespace Insights { enum class ETimeChangedFlags; }
namespace Trace { class IAnalysisSession; }
class FAnimationSharedData;
class IInsightsManager;
class ITableRow;
class ITimingEvent;
class STableViewBase;
class STextBlock;
class SMultiLineEditableTextBox;

namespace UE
{
namespace SlateInsights
{

class FSlateProvider;
namespace Private { struct FWidgetUniqueInvalidatedInfo; }
namespace Private { struct FWidgetUpdateInfo; }

class SSlateFrameSchematicView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSlateFrameSchematicView) {}
	SLATE_END_ARGS()
	~SSlateFrameSchematicView();

	void Construct(const FArguments& InArgs);

	void SetSession(Insights::ITimingViewSession* InTimingViewSession, const Trace::IAnalysisSession* InAnalysisSession);

private:
	TSharedRef<ITableRow> HandleUniqueInvalidatedMakeTreeRowWidget(TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> InInfo, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleUniqueInvalidatedChildrenForInfo(TSharedPtr<Private::FWidgetUniqueInvalidatedInfo> InInfo, TArray<TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>>& OutChildren);
	TSharedRef<ITableRow> HandleWidgetUpdateInfoGenerateWidget(TSharedPtr<Private::FWidgetUpdateInfo> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker);
	void HandleSelectionChanged(Insights::ETimeChangedFlags InFlags, double StartTime, double EndTime);
	void HandleSelectionEventChanged(const TSharedPtr<const ITimingEvent> InEvent);

	TSharedPtr<SWidget> HandleWidgetInvalidateListContextMenu();
	bool CanWidgetInvalidateListGotoRootWidget();
	void HandleWidgetInvalidateListGotoRootWidget();
	bool CanWidgetInvalidateListViewScriptAndCallStack();
	void HandleWidgetInvalidateListViewScriptAndCallStack();

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	void RefreshNodes();
	void RefreshNodes_Invalidation(const FSlateProvider* SlateProvider);
	void RefreshNodes_Update(const FSlateProvider* SlateProvider);

private:
	const Trace::IAnalysisSession* AnalysisSession;
	Insights::ITimingViewSession* TimingViewSession;

	TSharedPtr<STreeView<TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>>> WidgetInvalidateInfoListView;
	TArray<TSharedPtr<Private::FWidgetUniqueInvalidatedInfo>> WidgetInvalidationInfos;

	TSharedPtr<SListView<TSharedPtr<Private::FWidgetUpdateInfo>>> WidgetUpdateInfoListView;
	TArray<TSharedPtr<Private::FWidgetUpdateInfo>> WidgetUpdateInfos;

	TSharedPtr<STextBlock> InvalidationSummary;
	TSharedPtr<STextBlock> UpdateSummary;
	TSharedPtr<SMultiLineEditableTextBox> ScriptAndCallStackTextBox;

	double StartTime;
	double EndTime;
};

} //namespace SlateInsights
} //namespace UE
