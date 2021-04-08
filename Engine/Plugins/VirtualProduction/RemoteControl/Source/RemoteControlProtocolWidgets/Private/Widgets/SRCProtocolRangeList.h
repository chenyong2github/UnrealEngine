// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SRCProtocolShared.h"

class FProtocolRangeViewModel;
class FProtocolBindingViewModel;

class REMOTECONTROLPROTOCOLWIDGETS_API SRCProtocolRangeList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCProtocolRangeList)
	{}
		SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, PrimaryColumnSizeData)
	    SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, SecondaryColumnSizeData) 
	SLATE_END_ARGS()
 
	void Construct(const FArguments& InArgs, const TSharedRef<FProtocolBindingViewModel>& InViewModel);
	void GetDesiredWidth(float& OutMinDesiredWidth, float& OutMaxDesiredWidth);

private:
	TSharedRef<SWidget> ConstructHeader();
	
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FProtocolRangeViewModel> InViewModel, const TSharedRef<STableViewBase>& InOwnerTable) const;

	void OnRangeMappingAdded(TSharedRef<FProtocolRangeViewModel> InRangeViewModel) const;
	void OnRangeMappingRemoved(FGuid InRangeId) const;

private:
	TSharedPtr<FProtocolBindingViewModel> ViewModel;
	TSharedPtr<SSplitter> Splitter;
	TSharedPtr<SListView<TSharedPtr<FProtocolRangeViewModel>>> ListView;
	
	/** Container used by all primary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> PrimaryColumnSizeData;

	/** Container used by all secondary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> SecondaryColumnSizeData;
};
