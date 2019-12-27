// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "IMessageContext.h"
#include "MessageEndpoint.h"
#include "Misc/Guid.h"
#include "Widgets/Views/SListView.h"
#include "LiveLinkMessageBusFinder.h"

struct FLiveLinkPongMessage;
struct FMessageAddress;
struct FProviderPollResult;
class ILiveLinkClient;
class ITableRow;
class STableViewBase;

DECLARE_DELEGATE_OneParam(FOnLiveLinkMessageBusSourceSelected, FProviderPollResultPtr);

class SLiveLinkMessageBusSourceFactory : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SLiveLinkMessageBusSourceFactory) {}
		SLATE_EVENT(FOnLiveLinkMessageBusSourceSelected, OnSourceSelected)
	SLATE_END_ARGS()

	~SLiveLinkMessageBusSourceFactory();

	void Construct(const FArguments& Args);

	FProviderPollResultPtr GetSelectedSource() const { return SelectedResult; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);

private:
	TSharedRef<ITableRow> MakeSourceListViewWidget(FProviderPollResultPtr PollResult, const TSharedRef<STableViewBase>& OwnerTable) const;

	void OnSourceListSelectionChanged(FProviderPollResultPtr PollResult, ESelectInfo::Type SelectionType);

	TSharedPtr<SListView<FProviderPollResultPtr>> ListView;

	TArray<FProviderPollResultPtr> PollData;

	FProviderPollResultPtr SelectedResult;

	FOnLiveLinkMessageBusSourceSelected OnSourceSelected;

	double LastUIUpdateSeconds;
};