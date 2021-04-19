// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Serialization/BufferArchive.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Serialization/BufferArchive.h"
#include "IPropertyRowGenerator.h"
#include "UObject/StrongObjectPtr.h"
#include "ViewModels/ProtocolRangeViewModel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SRCProtocolRangeList.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

class FProtocolBindingViewModel;
struct FRemoteControlProtocolMapping;
class IPropertyRowGenerator;
class SRemoteControlProtocolWidgetExtension;

class SRCProtocolRange : public STableRow<TSharedPtr<FProtocolRangeViewModel>>
{
public:
	SLATE_BEGIN_ARGS(SRCProtocolRange)
	{}
		SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, PrimaryColumnSizeData)
	    SLATE_ARGUMENT(TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData>, SecondaryColumnSizeData)
	SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<FProtocolRangeViewModel>& InViewModel);

protected:
	/** Resolve the input widget. */
	TSharedRef<SWidget> MakeInput();

	/** Resolve the output widget. */
	TSharedRef<SWidget> MakeOutput();

	/** Called when this ProtocolRange is deleted. */
	FReply OnDelete() const;

private:
	FDelegateHandle OnInputProxyPropertyChangedHandle;
	FDelegateHandle OnOutputProxyPropertyChangedHandle;

	/** Applies proxy data to actual. */
	void OnInputProxyChanged(const FPropertyChangedEvent& InEvent);

	/** Applies proxy data to actual. */
	void OnOutputProxyChanged(const FPropertyChangedEvent& InEvent);

protected:
	TSharedPtr<FProtocolRangeViewModel> ViewModel;

	TSharedPtr<SSplitter> Splitter;

	TSharedPtr<IPropertyRowGenerator> InputPropertyRowGenerator;
	TSharedPtr<IPropertyHandle> InputProxyPropertyHandle;
	TStrongObjectPtr<UObject> InputProxyPropertyContainer;
	
	TSharedPtr<IPropertyRowGenerator> OutputPropertyRowGenerator;
	TSharedPtr<IPropertyHandle> OutputProxyPropertyHandle;
	TStrongObjectPtr<UObject> OutputProxyPropertyContainer;

	/** Container used by all primary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> PrimaryColumnSizeData;

	/** Container used by all secondary splitters in the details view, so that they move in sync */
	TSharedPtr<RemoteControlProtocolWidgetUtils::FPropertyViewColumnSizeData> SecondaryColumnSizeData;
};
