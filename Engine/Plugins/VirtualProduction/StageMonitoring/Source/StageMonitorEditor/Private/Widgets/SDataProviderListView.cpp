// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataProviderListView.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "IStageDataCollection.h"
#include "IStageMonitor.h"
#include "IStageMonitorModule.h"
#include "Misc/App.h"
#include "StageMessages.h"
#include "StageMonitorEditorSettings.h"

#define LOCTEXT_NAMESPACE "SDataProviderListView"


namespace DataProviderListView
{
	const FName HeaderIdName_State = "State";
	const FName HeaderIdName_MachineName = "MachineName";
	const FName HeaderIdName_ProcessId = "ProcessId";
	const FName HeaderIdName_StageName = "StageName";
	const FName HeaderIdName_Roles = "Roles";
	const FName HeaderIdName_AverageFPS = "AverageFPS";
	const FName HeaderIdName_GameThreadTiming = "GameThreadTiming";
	const FName HeaderIdName_RenderThreadTiming = "RenderThreadTiming";
	const FName HeaderIdName_GPUTiming = "GPUTiming";
}

/**
 * FDataProviderTableRowData
 */
struct FDataProviderTableRowData : TSharedFromThis<FDataProviderTableRowData>
{
	FDataProviderTableRowData(const FGuid& InIdentifier, const FStageInstanceDescriptor& InDescriptor, TWeakPtr<IStageDataCollection> InCollection)
		: Identifier(InIdentifier)
		, Descriptor(InDescriptor)
		, Collection(InCollection)
	{
	}

	/** Fetch latest information for the associated data provider */
	void UpdateCachedValues()
	{
		if (TSharedPtr<IStageDataCollection> CollectionPtr = Collection.Pin())
		{
			CachedState = CollectionPtr->GetProviderState(Identifier);

			TSharedPtr<FStageDataEntry> LatestData = CollectionPtr->GetLatest(Identifier, FFramePerformanceProviderMessage::StaticStruct());
			if (LatestData.IsValid() && LatestData->Data.IsValid())
			{
				//Copy over this message data
				const FFramePerformanceProviderMessage* MessageData = reinterpret_cast<const FFramePerformanceProviderMessage*>(LatestData->Data->GetStructMemory());
				check(MessageData);
				CachedPerformanceData = *MessageData;
			}
		}
	}

public:
	
	/** Identifier and descriptor associated to this list entry */
	FGuid Identifier;
	FStageInstanceDescriptor Descriptor;


	/** Weak pointer to the collection of data */
	TWeakPtr<IStageDataCollection> Collection;

	/** Cached data for the frame performance of this provider */
	FFramePerformanceProviderMessage CachedPerformanceData;

	/** Cached state of this provider */
	EStageDataProviderState CachedState;
};


/**
 * SDataProviderTableRow
 */
void SDataProviderTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwerTableView)
{
	Item = InArgs._Item;
	check(Item.IsValid());

	Super::FArguments Arg;
	Super::Construct(Arg, InOwerTableView);
}

TSharedRef<SWidget> SDataProviderTableRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (DataProviderListView::HeaderIdName_State == ColumnName)
	{
		return SNew(STextBlock)
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
			.Text(this, &SDataProviderTableRow::GetStateGlyphs)
			.ColorAndOpacity(this, &SDataProviderTableRow::GetStateColorAndOpacity);
	}
	if (DataProviderListView::HeaderIdName_MachineName == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderTableRow::GetMachineName)
			];
	}
	if (DataProviderListView::HeaderIdName_ProcessId == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderTableRow::GetProcessId)
			];
	}
	if (DataProviderListView::HeaderIdName_StageName == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderTableRow::GetStageName)
			];
	}
	if (DataProviderListView::HeaderIdName_Roles == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderTableRow::GetRoles)
			];
	}
	if (DataProviderListView::HeaderIdName_AverageFPS == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SDataProviderTableRow::GetAverageFPS))
			];
	}
	if (DataProviderListView::HeaderIdName_GameThreadTiming == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SDataProviderTableRow::GetGameThreadTiming))
			];
	}
	if (DataProviderListView::HeaderIdName_RenderThreadTiming == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SDataProviderTableRow::GetRenderThreadTiming))
			];
	}
	if (DataProviderListView::HeaderIdName_GPUTiming == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SDataProviderTableRow::GetGPUTiming))
			];
	}

	return SNullWidget::NullWidget;
}

FText SDataProviderTableRow::GetStateGlyphs() const
{
	return FEditorFontGlyphs::Circle;
}

FSlateColor SDataProviderTableRow::GetStateColorAndOpacity() const
{
	switch (Item->CachedState)
	{
		case EStageDataProviderState::Active:
		{
			return FLinearColor::Green;
		}
		case EStageDataProviderState::Inactive:
		{
			return FLinearColor::Yellow;
		}
		case EStageDataProviderState::Closed:
		default:
		{
			return FLinearColor::Red;
		}
	}
}

FText SDataProviderTableRow::GetMachineName() const
{
	return FText::FromString(Item->Descriptor.MachineName);
}

FText SDataProviderTableRow::GetProcessId() const
{
	return FText::AsNumber(Item->Descriptor.ProcessId);
}

FText SDataProviderTableRow::GetStageName() const
{
	return FText::FromName(Item->Descriptor.FriendlyName);
}

FText SDataProviderTableRow::GetRoles() const
{
	return FText::FromString(Item->Descriptor.RolesStringified);
}

FText SDataProviderTableRow::GetAverageFPS() const
{
	return FText::AsNumber(Item->CachedPerformanceData.AverageFPS);
}

FText SDataProviderTableRow::GetGameThreadTiming() const
{
	return FText::AsNumber(Item->CachedPerformanceData.GameThreadMS);
}

FText SDataProviderTableRow::GetRenderThreadTiming() const
{
	return FText::AsNumber(Item->CachedPerformanceData.RenderThreadMS);
}

FText SDataProviderTableRow::GetGPUTiming() const
{
	return FText::AsNumber(Item->CachedPerformanceData.GPU_MS);
}

/**
 * SDataProviderListView
 */
void SDataProviderListView::Construct(const FArguments& InArgs, TWeakPtr<IStageDataCollection> InCollection)
{
	Collection = InCollection;
	if (TSharedPtr<IStageDataCollection> CollectionPtr = InCollection.Pin())
	{
		CollectionPtr->OnStageDataProviderListChanged().AddSP(this, &SDataProviderListView::OnStageMonitoringMachineListChanged);
	}

	Super::Construct
	(
		Super::FArguments()
		.ListItemsSource(&ListItemsSource)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SDataProviderListView::OnGenerateRow)
		.HeaderRow
		(
			SNew(SHeaderRow)

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_State)
			.FixedWidth(45)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Center)
			.DefaultLabel(LOCTEXT("HeaderName_State", "State"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_MachineName)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_MachineName", "Machine"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_ProcessId)
			.FillWidth(.15)
			.DefaultLabel(LOCTEXT("HeaderName_ProcessId", "Process Id"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_StageName)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_StageName", "Stage Name"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_Roles)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_Roles", "Roles"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_AverageFPS)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_AverageFPS", "Average FPS"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_GameThreadTiming)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_GameThread", "Game Thread"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_RenderThreadTiming)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_RenderThread", "Render Thread"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_GPUTiming)
			.FillWidth(.2)
			.DefaultLabel(LOCTEXT("HeaderName_GPU", "GPU"))
		)
	);

	RebuildDataProviderList();
}

SDataProviderListView::~SDataProviderListView()
{
	if (TSharedPtr<IStageDataCollection> CollectionPtr = Collection.Pin())
	{
		CollectionPtr->OnStageDataProviderListChanged().RemoveAll(this);
	}
}

void SDataProviderListView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	bool bForceRefresh = false;
	if (bRebuildListRequested)
	{
		RebuildDataProviderList();
		RebuildList();
		bRebuildListRequested = false;
		bForceRefresh = true;
	}

	//Update cached values at specific rate or when list has been rebuilt
	const double RefreshRate = GetDefault<UStageMonitorEditorSettings>()->RefreshRate;
	if (bForceRefresh || (FApp::GetCurrentTime() - LastRefreshTime > RefreshRate))
	{
		LastRefreshTime = FApp::GetCurrentTime();
		for (FDataProviderTableRowDataPtr& RowDataPtr : ListItemsSource)
		{
			RowDataPtr->UpdateCachedValues();
		}
	}

	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

TSharedRef<ITableRow> SDataProviderListView::OnGenerateRow(FDataProviderTableRowDataPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SDataProviderTableRow> Row = SNew(SDataProviderTableRow, OwnerTable)
		.Item(InItem);
	ListRowWidgets.Add(Row);
	return Row;
}

void SDataProviderListView::OnStageMonitoringMachineListChanged()
{
	bRebuildListRequested = true;
}

void SDataProviderListView::RebuildDataProviderList()
{
	ListItemsSource.Reset();

	if (TSharedPtr<IStageDataCollection> CollectionPtr = Collection.Pin())
	{
		const TArray<FCollectionProviderEntry> Providers = CollectionPtr->GetProviders();

		for (const FCollectionProviderEntry& Provider : Providers)
		{
			TSharedRef<FDataProviderTableRowData> RowData = MakeShared<FDataProviderTableRowData>(Provider.Identifier, Provider.Descriptor, Collection);
			ListItemsSource.Add(RowData);
		}

		for (FDataProviderTableRowDataPtr& TableRowData : ListItemsSource)
		{
			TableRowData->UpdateCachedValues();
		}
	}

	RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE

