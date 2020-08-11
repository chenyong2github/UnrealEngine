// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataProviderActivities.h"

#include "EditorFontGlyphs.h"
#include "EditorStyleSet.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "IStageDataCollection.h"
#include "IStageMonitor.h"
#include "IStageMonitorModule.h"
#include "Misc/App.h"
#include "Misc/QualifiedFrameTime.h"
#include "Misc/Timecode.h"
#include "PropertyEditorModule.h"
#include "SDataProviderActivityFilter.h"
#include "StageMessages.h"
#include "StageMonitorEditorStyle.h"
#include "Widgets/Text/STextBlock.h"


PRAGMA_DISABLE_OPTIMIZATION


#define LOCTEXT_NAMESPACE "SDataProviderActivities"


namespace DataProviderActivitiesListView
{
	const FName HeaderIdName_Timecode = "Timecode";
	const FName HeaderIdName_StageName = "StageName";
	const FName HeaderIdName_Type = "Type";
	const FName HeaderIdName_Description = "Description";
}


/**
 * FDataProviderTableRowData
 */
struct FDataProviderActivity : TSharedFromThis<FDataProviderActivity>
{
	FDataProviderActivity(TSharedPtr<FStageDataEntry> InActivityPayload)
		: ActivityPayload(InActivityPayload)
	{

	}

public:
	TSharedPtr<FStageDataEntry> ActivityPayload;
};


/**
 * SDataProviderActivities
 */
void SDataProviderActivities::Construct(const FArguments& InArgs, TSharedPtr<SStageMonitorPanel> InOwnerPanel, const TWeakPtr<IStageDataCollection>& Collection)
{
	OwnerPanel = InOwnerPanel;
	DataCollection = Collection;

	if (TSharedPtr<IStageDataCollection> CollectionPtr = DataCollection.Pin())
	{
		CollectionPtr->OnStageDataCollectionNewDataReceived().AddSP(this, &SDataProviderActivities::OnNewStageActivity);
		CollectionPtr->OnStageDataCollectionCleared().AddSP(this, &SDataProviderActivities::OnStageDataCleared);
	}

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = true;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ObjectsUseNameArea;
	DetailsViewArgs.ViewIdentifier = NAME_None;
	DetailsViewArgs.bShowCustomFilterOption = false;
	DetailsViewArgs.bShowOptions = false;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FStructureDetailsViewArgs StructViewArgs;
	StructureDetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructViewArgs, TSharedPtr<FStructOnScope>());
	StructureDetailsView->GetDetailsView()->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([]() { return false; }));


	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		// List view
		+ SSplitter::Slot()
		.Value(0.75f)
		[
			SNew(SVerticalBox)
			// Filter
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(1.f, 1.f, 1.f, 1.f)
			[
				SAssignNew(ActivityFilter, SDataProviderActivityFilter)	
				.OnActivityFilterChanged(FSimpleDelegate::CreateSP(this, &SDataProviderActivities::OnActivityFilterChanged))
			]
			// Activities
			+ SVerticalBox::Slot()
			.FillHeight(.8f)
			.VAlign(VAlign_Fill)
			.Padding(10.f, 0.f, 10.f, 10.f)
			[
				SAssignNew(ActivityList, SListView<FDataProviderActivityPtr>)
				.ListItemsSource(&FilteredActivities)
				.OnGenerateRow(this, &SDataProviderActivities::OnGenerateActivityRowWidget)
				.SelectionMode(ESelectionMode::Single)
				.OnSelectionChanged(this, &SDataProviderActivities::OnListViewSelectionChanged)
				//.AllowOverscroll(EAllowOverscroll::No)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(DataProviderActivitiesListView::HeaderIdName_Timecode)
					.FillWidth(15.f)
					.DefaultLabel(LOCTEXT("HeaderName_Timecode", "Timecode"))
					+ SHeaderRow::Column(DataProviderActivitiesListView::HeaderIdName_StageName)
					.FillWidth(25.f)
					.DefaultLabel(LOCTEXT("HeaderName_StageName", "Stage Name"))
					+ SHeaderRow::Column(DataProviderActivitiesListView::HeaderIdName_Type)
					.FillWidth(25.f)
					.DefaultLabel(LOCTEXT("HeaderName_Type", "Type"))
					+ SHeaderRow::Column(DataProviderActivitiesListView::HeaderIdName_Description)
					.FillWidth(35.f)
					.DefaultLabel(LOCTEXT("HeaderName_Description", "Description"))
				)
			]
		]
		+ SSplitter::Slot()
		.Value(.25f)
		[
			SNew(SBorder)
			.VAlign(VAlign_Fill)
			[
				StructureDetailsView->GetWidget().ToSharedRef()
			]
		]
	];

	RequestRebuild();
}

SDataProviderActivities::~SDataProviderActivities()
{
}


void SDataProviderActivities::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRebuildRequested)
	{
		bRebuildRequested = false;
		Activities.Empty();
		FilteredActivities.Empty();
		ReloadActivityHistory();
		ActivityList->RebuildList();
	}
	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}


void SDataProviderActivities::RequestRebuild()
{
	bRebuildRequested = true;
}

TSharedRef<ITableRow> SDataProviderActivities::OnGenerateActivityRowWidget(FDataProviderActivityPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<SDataProviderActivitiesTableRow> Row = SNew(SDataProviderActivitiesTableRow, OwnerTable)
		.Item(InItem);
	return Row;
}

void SDataProviderActivities::OnListViewSelectionChanged(FDataProviderActivityPtr InActivity, ESelectInfo::Type SelectInfo)
{
	if (InActivity.IsValid())
	{
		StructureDetailsView->SetStructureData(InActivity->ActivityPayload->Data);
	}
	else
	{
		StructureDetailsView->SetStructureData(nullptr);
	}
}


#include "StageMessages.h"
void SDataProviderActivities::OnNewStageActivity(TSharedPtr<FStageDataEntry> NewActivity)
{
	//Make new row data
	TSharedPtr<FDataProviderActivity> RowData = MakeShared<FDataProviderActivity>(NewActivity);
	Activities.Add(RowData);
	if (ActivityFilter->GetActivityFilter().DoesItPass(NewActivity))
	{
		FilteredActivities.Insert(RowData, 0);
		ActivityList->RequestListRefresh();
	}
}

void SDataProviderActivities::OnActivityFilterChanged()
{
	//When filtering has changed, update the current filtered list based on the full list
	FilteredActivities.Reset(Activities.Num());

	for (int32 i = 0; i < Activities.Num(); ++i)
	{
		const FDataProviderActivityPtr& Activity = Activities.Last(i);
		if (ActivityFilter->GetActivityFilter().DoesItPass(Activity->ActivityPayload))
		{
			FilteredActivities.Add(Activity);
		}
	}

	ActivityList->ScrollToTop();

	// Request a refresh to update the view
	ActivityList->RequestListRefresh();
}

void SDataProviderActivities::ReloadActivityHistory()
{
	FilteredActivities.Reset();

	if (TSharedPtr<IStageDataCollection> CollectionPtr = DataCollection.Pin())
	{
		TArray<TSharedPtr<FStageDataEntry>> CurrentActivities;
		CollectionPtr->GetAllEntries(CurrentActivities);

		for (const TSharedPtr<FStageDataEntry>& Entry : CurrentActivities)
		{
			OnNewStageActivity(Entry);
		}
	}

	ActivityList->RequestListRefresh();
}

void SDataProviderActivities::OnStageDataCleared()
{
	RequestRebuild();
}

/**
 * SDataProviderActivitiesTableRow
 */
void SDataProviderActivitiesTableRow::Construct(const FArguments & InArgs, const TSharedRef<STableViewBase> & InOwerTableView)
{
	Item = InArgs._Item;
	check(Item.IsValid());

	Super::FArguments Arg;
	if (Item->ActivityPayload.IsValid())
	{
		FStageProviderMessage* Data = reinterpret_cast<FStageProviderMessage*>(Item->ActivityPayload->Data->GetStructMemory());

		FCollectionProviderEntry Provider;
		if (IStageMonitorModule::Get().GetStageMonitor().GetDataCollection()->GetProvider(Data->Identifier, Provider))
		{
			Descriptor = Provider.Descriptor;
			if (IStageMonitorModule::Get().GetStageMonitor().IsTimePartOfCriticalState(Data->FrameTime.AsSeconds()))
			{
				Arg.Style(FStageMonitorEditorStyle::Get(), "TableView.CriticalStateRow");
			}
		}
	}

	Super::Construct(Arg, InOwerTableView);
}

TSharedRef<SWidget> SDataProviderActivitiesTableRow::GenerateWidgetForColumn(const FName & ColumnName)
{
	if (DataProviderActivitiesListView::HeaderIdName_Timecode == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderActivitiesTableRow::GetTimecode)
			];
	}
	if (DataProviderActivitiesListView::HeaderIdName_StageName == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderActivitiesTableRow::GetStageName)
			];
	}
	if (DataProviderActivitiesListView::HeaderIdName_Type == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderActivitiesTableRow::GetMessageType)
			];
	}
	if (DataProviderActivitiesListView::HeaderIdName_Description == ColumnName)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SDataProviderActivitiesTableRow::GetDescription)
			];
	}

	return SNullWidget::NullWidget;
}

FText SDataProviderActivitiesTableRow::GetTimecode() const
{	
	if (Item->ActivityPayload.IsValid())
	{
		check(Item->ActivityPayload->Data->GetStruct()->IsChildOf(FStageProviderMessage::StaticStruct()));
		FStageProviderMessage* Data = reinterpret_cast<FStageProviderMessage*>(Item->ActivityPayload->Data->GetStructMemory());
		return FText::FromString(FTimecode::FromFrameNumber(Data->FrameTime.Time.GetFrame(), Data->FrameTime.Rate).ToString());
	}

	return FText::GetEmpty();
}

FText SDataProviderActivitiesTableRow::GetStageName() const
{
	if (Item->ActivityPayload.IsValid())
	{
		return FText::FromName(Descriptor.FriendlyName);
	}

	return FText::GetEmpty();
}

FText SDataProviderActivitiesTableRow::GetMessageType() const
{
	if (Item->ActivityPayload.IsValid())
	{
		check(Item->ActivityPayload->Data->GetStruct()->IsChildOf(FStageProviderMessage::StaticStruct()));
		return Item->ActivityPayload->Data->GetStruct()->GetDisplayNameText();
	}

	return FText::GetEmpty();
}

FText SDataProviderActivitiesTableRow::GetDescription() const
{
	if (Item->ActivityPayload.IsValid())
	{
		check(Item->ActivityPayload->Data->GetStruct()->IsChildOf(FStageProviderMessage::StaticStruct()));
		FStageProviderMessage* Data = reinterpret_cast<FStageProviderMessage*>(Item->ActivityPayload->Data->GetStructMemory());
		return FText::FromString(Data->ToString());
	}

	return FText::GetEmpty();
}


#undef LOCTEXT_NAMESPACE



PRAGMA_ENABLE_OPTIMIZATION