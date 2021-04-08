// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SRCProtocolShared.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SRCProtocolRangeList;
class FProtocolRangeViewModel;
class FProtocolBindingViewModel;

class REMOTECONTROLPROTOCOLWIDGETS_API SRCProtocolBinding : public STableRow<TSharedPtr<FProtocolBindingViewModel>>
{
public:
	SLATE_BEGIN_ARGS(SRCProtocolBinding) 
	{}	
		SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, PrimaryColumnSizeData)
		SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, SecondaryColumnSizeData)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<FProtocolBindingViewModel>& InViewModel);

private:
	FReply OnDelete() const;

private:
	TSharedPtr<FProtocolBindingViewModel> ViewModel;

	TSharedPtr<SRCProtocolRangeList> RangeList;

	/** Container used by all primary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> PrimaryColumnSizeData;

	/** Container used by all secondary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> SecondaryColumnSizeData;
};
