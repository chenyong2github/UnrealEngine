// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataProviderListView.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "IStageMonitorSession.h"
#include "Misc/App.h"
#include "StageMonitorUtils.h"
#include "StageMonitorEditorSettings.h"

#define LOCTEXT_NAMESPACE "SDataProviderListView"


namespace DataProviderListView
{
	const FName HeaderIdName_State = TEXT("State");
	const FName HeaderIdName_Timecode = TEXT("Timecode");
	const FName HeaderIdName_MachineName = TEXT("MachineName");
	const FName HeaderIdName_ProcessId = TEXT("ProcessId");
	const FName HeaderIdName_StageName = TEXT("StageName");
	const FName HeaderIdName_Roles = TEXT("Roles");
	const FName HeaderIdName_AverageFPS = TEXT("AverageFPS");
	const FName HeaderIdName_IdleTime = TEXT("IdleTime");
	const FName HeaderIdName_GameThreadTiming = TEXT("GameThreadTiming");
	const FName HeaderIdName_RenderThreadTiming = TEXT("RenderThreadTiming");
	const FName HeaderIdName_GPUTiming = TEXT("GPUTiming");
}

/**
 * FDataProviderTableRowData
 */
struct FDataProviderTableRowData : TSharedFromThis<FDataProviderTableRowData>
{
	FDataProviderTableRowData(const FGuid& InIdentifier, const FStageInstanceDescriptor& InDescriptor, TWeakPtr<IStageMonitorSession> InSession)
		: Identifier(InIdentifier)
		, Descriptor(InDescriptor)
		, Session(InSession)
	{
	}

	/** Fetch latest information for the associated data provider */
	void UpdateCachedValues()
	{
		if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
		{
			CachedState = SessionPtr->GetProviderState(Identifier);

			TSharedPtr<FStageDataEntry> LatestData = SessionPtr->GetLatest(Identifier, FFramePerformanceProviderMessage::StaticStruct());
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


	/** Weak pointer to the session data */
	TWeakPtr<IStageMonitorSession> Session;

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
	if (DataProviderListView::HeaderIdName_Timecode == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderTableRow::GetTimecode)
			];
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
	if (DataProviderListView::HeaderIdName_IdleTime == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SDataProviderTableRow::GetIdleTime))
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

FText SDataProviderTableRow::GetTimecode() const
{
	const FTimecode CachedTimecode = FTimecode::FromFrameNumber(Item->CachedPerformanceData.FrameTime.Time.FrameNumber, Item->CachedPerformanceData.FrameTime.Rate);
	return FText::FromString(CachedTimecode.ToString());
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

FText SDataProviderTableRow::GetIdleTime() const
{
	return FText::AsNumber(Item->CachedPerformanceData.IdleTimeMS);
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
void SDataProviderListView::Construct(const FArguments& InArgs, const TWeakPtr<IStageMonitorSession>& InSession)
{
	AttachToMonitorSession(InSession);

	Super::Construct
	(
		Super::FArguments()
		.ListItemsSource(&ListItemsSource)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SDataProviderListView::OnGenerateRow)
		.HeaderRow
		(
			SNew(SHeaderRow)
			.CanSelectGeneratedColumn(true) //To show/hide columns

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_State)
			.FixedWidth(45.f)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Center)
			.DefaultLabel(LOCTEXT("HeaderName_State", "State"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_Timecode)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_Timecode", "Timecode"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_MachineName)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_MachineName", "Machine"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_ProcessId)
			.FillWidth(.15f)
			.DefaultLabel(LOCTEXT("HeaderName_ProcessId", "Process Id"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_StageName)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_StageName", "Stage Name"))
			.ShouldGenerateWidget(true) //Can't hide this column

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_Roles)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_Roles", "Roles"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_AverageFPS)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_AverageFPS", "Average FPS"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_IdleTime)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_IdleTime", "Idle Time (ms)"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_GameThreadTiming)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_GameThread", "Game Thread (ms)"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_RenderThreadTiming)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_RenderThread", "Render Thread (ms)"))

			+ SHeaderRow::Column(DataProviderListView::HeaderIdName_GPUTiming)
			.FillWidth(.2f)
			.DefaultLabel(LOCTEXT("HeaderName_GPU", "GPU (ms)"))
		)
	);

	RebuildDataProviderList();
}

void SDataProviderListView::RefreshMonitorSession(TWeakPtr<IStageMonitorSession> NewSession)
{
	AttachToMonitorSession(NewSession);
	bRebuildListRequested = true;
}

SDataProviderListView::~SDataProviderListView()
{
	if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
	{
		SessionPtr->OnStageDataProviderListChanged().RemoveAll(this);
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

	if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
	{
		const TConstArrayView<FStageSessionProviderEntry> Providers = SessionPtr->GetProviders();
		for (const FStageSessionProviderEntry& Provider : Providers)
		{
			TSharedRef<FDataProviderTableRowData> RowData = MakeShared<FDataProviderTableRowData>(Provider.Identifier, Provider.Descriptor, Session);
			ListItemsSource.Add(RowData);
		}

		for (FDataProviderTableRowDataPtr& TableRowData : ListItemsSource)
		{
			TableRowData->UpdateCachedValues();
		}
	}

	RequestListRefresh();
}

void SDataProviderListView::AttachToMonitorSession(const TWeakPtr<IStageMonitorSession>& NewSession)
{
	if (NewSession != Session)
	{
		if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
		{
			SessionPtr->OnStageDataProviderListChanged().RemoveAll(this);
		}

		Session = NewSession;

		if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
		{
			SessionPtr->OnStageDataProviderListChanged().AddSP(this, &SDataProviderListView::OnStageMonitoringMachineListChanged);
		}
	}
}

#undef LOCTEXT_NAMESPACE

