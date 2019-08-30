// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SLiveLinkMessageBusSourceFactory.h"

#include "ILiveLinkClient.h"
#include "ILiveLinkModule.h"
#include "LiveLinkMessages.h"
#include "LiveLinkMessageBusFinder.h"
#include "LiveLinkMessageBusDiscoveryManager.h"
#include "Widgets/Layout/SBox.h"

#include "Features/IModularFeatures.h"
#include "MessageEndpointBuilder.h"
#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "LiveLinkMessageBusSourceEditor"

namespace ProviderPollUI
{
	const FName TypeColumnName(TEXT("Type"));
	const FName MachineColumnName(TEXT("Machine"));
};

class SProviderPollRow : public SMultiColumnTableRow<FProviderPollResultPtr>
{
public:
	SLATE_BEGIN_ARGS(SProviderPollRow) {}

		/** The list item for this row */
		SLATE_ARGUMENT(FProviderPollResultPtr, PollResultPtr)

	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		PollResultPtr = Args._PollResultPtr;

		SMultiColumnTableRow<FProviderPollResultPtr>::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			OwnerTableView
		);
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == ProviderPollUI::TypeColumnName)
		{
			return	SNew(STextBlock)
					.Text(FText::FromString(PollResultPtr->Name));
		}
		else if (ColumnName == ProviderPollUI::MachineColumnName)
		{
			return	SNew(STextBlock)
				.Text(FText::FromString(PollResultPtr->MachineName));
		}

		return SNullWidget::NullWidget;
	}

private:
	FProviderPollResultPtr PollResultPtr;
};


SLiveLinkMessageBusSourceFactory::~SLiveLinkMessageBusSourceFactory()
{
	if (ILiveLinkModule* ModulePtr = FModuleManager::GetModulePtr<ILiveLinkModule>("LiveLink"))
	{
		ModulePtr->GetMessageBusDiscoveryManager().RemoveDiscoveryMessageRequest();
	}
}

void SLiveLinkMessageBusSourceFactory::Construct(const FArguments& Args)
{
	//LastTickTime = 0.0;
	OnSourceSelected = Args._OnSourceSelected;
	LastUIUpdateSeconds = 0;

	ILiveLinkModule::Get().GetMessageBusDiscoveryManager().AddDiscoveryMessageRequest();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1)
		[
			SNew(SBox)
			.HeightOverride(200)
			.WidthOverride(200)
			[
				SAssignNew(ListView, SListView<FProviderPollResultPtr>)
				.ListItemsSource(&PollData)
				.SelectionMode(ESelectionMode::SingleToggle)
				.OnGenerateRow(this, &SLiveLinkMessageBusSourceFactory::MakeSourceListViewWidget)
				.OnSelectionChanged(this, &SLiveLinkMessageBusSourceFactory::OnSourceListSelectionChanged)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ProviderPollUI::TypeColumnName)
					.FillWidth(43.0f)
					.DefaultLabel(LOCTEXT("TypeColumnHeaderName", "Source Type"))
					+ SHeaderRow::Column(ProviderPollUI::MachineColumnName)
					.FillWidth(43.0f)
					.DefaultLabel(LOCTEXT("MachineColumnHeaderName", "Source Machine"))
				)
			]
		]
	];
}

void SLiveLinkMessageBusSourceFactory::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (FApp::GetCurrentTime() - LastUIUpdateSeconds > 0.5)
	{
		PollData.Reset();
		PollData.Append(ILiveLinkModule::Get().GetMessageBusDiscoveryManager().GetDiscoveryResults());
		PollData.Sort([](const FProviderPollResultPtr& A, const FProviderPollResultPtr& B) { return A->Name < B->Name; });
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SLiveLinkMessageBusSourceFactory::MakeSourceListViewWidget(FProviderPollResultPtr PollResult, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SProviderPollRow, OwnerTable).PollResultPtr(PollResult);
}

void SLiveLinkMessageBusSourceFactory::OnSourceListSelectionChanged(FProviderPollResultPtr PollResult, ESelectInfo::Type SelectionType)
{
	SelectedResult = PollResult;
	OnSourceSelected.ExecuteIfBound(SelectedResult);
}

#undef LOCTEXT_NAMESPACE
