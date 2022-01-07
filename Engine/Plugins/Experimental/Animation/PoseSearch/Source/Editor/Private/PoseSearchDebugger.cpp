// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDebugger.h"
#include "IAnimationProvider.h"
#include "Editor/EditorEngine.h"
#include "PoseSearch/PoseSearch.h"
#include "Animation/AnimSequence.h"
#include "RewindDebuggerInterface/Public/IRewindDebugger.h"
#include "ObjectTrace.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Trace/PoseSearchTraceProvider.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "TraceServices/Model/Frames.h"
#include "Algo/AllOf.h"

#define LOCTEXT_NAMESPACE "PoseSearchDebugger"


void FPoseSearchDebuggerPoseVectorChannel::Reset()
{
	Positions.Reset();
	LinearVelocities.Reset();
	FacingDirections.Reset();
}

void FPoseSearchDebuggerPoseVector::Reset()
{
	Pose.Reset();
	TrajectoryTimeBased.Reset();
	TrajectoryDistanceBased.Reset();
}

void FPoseSearchDebuggerPoseVector::ExtractFeatures(const UE::PoseSearch::FFeatureVectorReader& Reader)
{
	using namespace UE::PoseSearch;

	Reset();

	FPoseSearchDebuggerPoseVectorChannel* Channels[] = 
	{
		&Pose,
		&TrajectoryTimeBased,
		&TrajectoryDistanceBased
	};

	for (const FPoseSearchFeatureDesc& Feature: Reader.GetLayout()->Features)
	{
		if (Feature.ChannelIdx >= UE_ARRAY_COUNT(Channels))
		{
			continue;
		}

		FVector Vector;
		if (Reader.GetVector(Feature, &Vector))
		{
			if (Feature.Type == EPoseSearchFeatureType::Position)
			{
				Channels[Feature.ChannelIdx]->Positions.Add(Vector);
			}
			else if (Feature.Type == EPoseSearchFeatureType::LinearVelocity)
			{
				Channels[Feature.ChannelIdx]->LinearVelocities.Add(Vector);
			}
			else if (Feature.Type == EPoseSearchFeatureType::ForwardVector)
			{
				Channels[Feature.ChannelIdx]->FacingDirections.Add(Vector);
			}
		}
		
	}

	for (FPoseSearchDebuggerPoseVectorChannel* Channel : Channels)
	{
		Channel->bShowPositions = !Channel->Positions.IsEmpty();
		Channel->bShowLinearVelocities = !Channel->LinearVelocities.IsEmpty();
		Channel->bShowFacingDirections = !Channel->FacingDirections.IsEmpty();
	}

	bShowPose = !Pose.IsEmpty();
	bShowTrajectoryTimeBased = !TrajectoryTimeBased.IsEmpty();
	bShowTrajectoryDistanceBased = !TrajectoryDistanceBased.IsEmpty();
}


namespace UE::PoseSearch {

class FDebuggerDatabaseRowData : public TSharedFromThis<FDebuggerDatabaseRowData>
{
public:
	FDebuggerDatabaseRowData() = default;
	
	float GetPoseCost() const { return PoseCostInfo.ChannelCosts.Num() ? PoseCostInfo.ChannelCosts[0] : 0.0f; }
	float GetTrajectoryCost() const { return PoseCostInfo.ChannelCosts.Num() >= 3 ? PoseCostInfo.ChannelCosts[1] + PoseCostInfo.ChannelCosts[2] : 0.0f; }
	float GetAddendsCost() const { return PoseCostInfo.NotifyCostAddend + PoseCostInfo.MirrorMismatchCostAddend; }

	int32 PoseIdx = 0;
	FString AnimSequenceName = "";
	int32 DbSequenceIdx = 0;
	int32 AnimFrame = 0;
	float Time = 0.0f;
	bool bMirrored = false;
	FPoseCostInfo PoseCostInfo;
};

namespace DebuggerDatabaseColumns
{
	/** Column struct to represent each column in the debugger database */
	struct IColumn : TSharedFromThis<IColumn>
	{
		explicit IColumn(int32 InSortIndex, bool InEnabled = true)
			: SortIndex(InSortIndex)
			, bEnabled(InEnabled)
		{
		}
		
		virtual ~IColumn() = default;
		
		/** Sorted left to right based on this index */
		int32 SortIndex = 0;
		/** Current width, starts at 1 to be evenly spaced between all columns */
		float Width = 1.0f;
		/** Disabled selectively with view options */
		bool bEnabled = false;

		virtual FName GetName() const = 0;
		
		using FRowDataRef = TSharedRef<FDebuggerDatabaseRowData>;
		using FSortPredicate = TFunctionRef<bool(const FRowDataRef&, const FRowDataRef&)>;
		
		/** Sort predicate to sort list in ascending order by this column */
		virtual const FSortPredicate& GetSortPredicateAscending() const = 0;
		/** Sort predicate to sort list in descending order by this column */
		virtual const FSortPredicate& GetSortPredicateDescending() const = 0;
		
		TSharedRef<STextBlock> GenerateTextWidget(const FRowDataRef& RowData) const
		{
			static FSlateFontInfo RowFont = FEditorStyle::Get().GetFontStyle("DetailsView.CategoryTextStyle");
			
			return SNew(STextBlock)
            	.Font(RowFont)
				.Text_Lambda([this, RowData]() -> FText { return GetRowText(RowData); })
            	.Justification(ETextJustify::Center);
		}

	protected:
		virtual FText GetRowText(const FRowDataRef& Row) const = 0;
	};

	struct FPoseIdx : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }

		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseIdx < Row1->PoseIdx; };
			return Predicate;
		}
		
		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseIdx >= Row1->PoseIdx; };
			return Predicate;
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->PoseIdx, &FNumberFormattingOptions::DefaultNoGrouping());
		}
	};
	const FName FPoseIdx::Name = "Pose Index";

	struct FAnimSequenceName : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->AnimSequenceName < Row1->AnimSequenceName; };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->AnimSequenceName >= Row1->AnimSequenceName; };
			return Predicate;
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::FromString(Row->AnimSequenceName);
		}
	};
	const FName FAnimSequenceName::Name = "Sequence";

	struct FFrame : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Time < Row1->Time; };
			return Predicate;
		}
		
		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Time >= Row1->Time; };
			return Predicate;
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			FNumberFormattingOptions TimeFormattingOptions = FNumberFormattingOptions()
				.SetUseGrouping(false)
				.SetMaximumFractionalDigits(2);

			return FText::Format(
				FText::FromString("{0} ({1})"),
				FText::AsNumber(Row->AnimFrame, &FNumberFormattingOptions::DefaultNoGrouping()),
				FText::AsNumber(Row->Time, &TimeFormattingOptions)
			);
		}
	};
	const FName FFrame::Name = "Frame";

	struct FCost : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseCostInfo.TotalCost < Row1->PoseCostInfo.TotalCost; };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseCostInfo.TotalCost >= Row1->PoseCostInfo.TotalCost; };
			return Predicate;
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
        {
        	return FText::AsNumber(Row->PoseCostInfo.TotalCost);
        }
	};
	const FName FCost::Name = "Cost";


	struct FPoseCost : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetPoseCost() < Row1->GetPoseCost(); };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetPoseCost() >= Row1->GetPoseCost(); };
			return Predicate;
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetPoseCost());
		}
	};
	const FName FPoseCost::Name = "Pose Cost";


	struct FTrajectoryCost : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetTrajectoryCost() < Row1->GetTrajectoryCost(); };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetTrajectoryCost() >= Row1->GetTrajectoryCost(); };
			return Predicate;
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetTrajectoryCost());
        }
	};
	const FName FTrajectoryCost::Name = "Trajectory Cost";

	struct FCostModifier : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }

		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool 
			{ 
				return Row0->GetAddendsCost() < Row1->GetAddendsCost();
			};
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool 
			{ 
				return Row0->GetAddendsCost() >= Row1->GetAddendsCost();
			};
			return Predicate;
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetAddendsCost());
		}
	};
	const FName FCostModifier::Name = "Cost Modifier";

	struct FMirrored : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }

		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool 
			{ 
				return Row0->bMirrored < Row1->bMirrored; 
			};
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool 
			{ 
				return Row0->bMirrored >= Row1->bMirrored; 
			};
			return Predicate;
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::Format(LOCTEXT("Mirrored", "{0}"), { Row->bMirrored } );
		}
	};
	const FName FMirrored::Name = "Mirrored";
}


/**
 * Widget representing a single row of the database view
 */
class SDebuggerDatabaseRow : public SMultiColumnTableRow<TSharedRef<FDebuggerDatabaseRowData>>
{
	SLATE_BEGIN_ARGS(SDebuggerDatabaseRow) {}
		SLATE_ATTRIBUTE(const SDebuggerDatabaseView::FColumnMap*, ColumnMap)
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FDebuggerDatabaseRowData> InRow,
		const FTableRowStyle& InRowStyle,
		const FSlateBrush* InRowBrush,
		FMargin InPaddingMargin
	)
	{
		ColumnMap = InArgs._ColumnMap;
		check(ColumnMap.IsBound());
		
		Row = InRow;
		
		RowBrush = InRowBrush;
		check(RowBrush);

		SMultiColumnTableRow<TSharedRef<FDebuggerDatabaseRowData>>::Construct(
			FSuperRowType::FArguments()
			.Padding(InPaddingMargin)
			.Style(&InRowStyle),
			InOwnerTable
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		// Get column
		const TSharedRef<DebuggerDatabaseColumns::IColumn>& Column = (*ColumnMap.Get())[InColumnName];
		
		static FSlateFontInfo NormalFont = FEditorStyle::Get().GetFontStyle("DetailsView.CategoryTextStyle");
		const TSharedRef<SWidget> Widget = Column->GenerateTextWidget(Row.ToSharedRef());
		
		return
			SNew(SBorder)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.BorderImage(RowBrush)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Padding(0.0f, 3.0f)
				[
					Widget
				]
			];
	}

	/** Row data associated with this widget */
	TSharedPtr<FDebuggerDatabaseRowData> Row;

	/** Used for cell styles (active vs database row) */
	const FSlateBrush* RowBrush = nullptr;

	/** Used to grab the column struct given a column name */
	TAttribute<const SDebuggerDatabaseView::FColumnMap*> ColumnMap;
};

class SDebuggerMessageBox : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDebuggerMessageBox) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FString& Message)
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Message))
				.Font(FEditorStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
			]
		];
	}
};

void SDebuggerDatabaseView::Update(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database)
{
	UpdateRows(State, Database);
}

void SDebuggerDatabaseView::RefreshColumns()
{
	using namespace DebuggerDatabaseColumns;
	
	ActiveView.HeaderRow->ClearColumns();
	FilteredDatabaseView.HeaderRow->ClearColumns();
	
	// Sort columns by index
	Columns.ValueSort([](const TSharedRef<IColumn> Column0, const TSharedRef<IColumn> Column1)
	{
		return Column0->SortIndex < Column1->SortIndex;
	});

	// Add columns from map to header row
	for(TPair<FName, TSharedRef<IColumn>>& ColumnPair : Columns)
	{
		IColumn& Column = ColumnPair.Value.Get();
		const FName& ColumnName = Column.GetName();
		if(ColumnPair.Value->bEnabled)
		{
			SHeaderRow::FColumn::FArguments ColumnArgs = SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnName)
				.DefaultLabel(FText::FromName(ColumnName))
				.SortMode(this, &SDebuggerDatabaseView::GetColumnSortMode, ColumnName)
				.OnSort(this, &SDebuggerDatabaseView::OnColumnSortModeChanged)
				.FillWidth(this, &SDebuggerDatabaseView::GetColumnWidth, ColumnName)
				.VAlignCell(VAlign_Center)
				.VAlignHeader(VAlign_Center)
				.HAlignHeader(HAlign_Center)
				.HAlignCell(HAlign_Fill);

			FilteredDatabaseView.HeaderRow->AddColumn(ColumnArgs);
			
			// Every time the active column is changed, update the database column
			ActiveView.HeaderRow->AddColumn(ColumnArgs.OnWidthChanged(this, &SDebuggerDatabaseView::OnColumnWidthChanged, ColumnName));
		}
	}
}

void SDebuggerDatabaseView::AddColumn(TSharedRef<DebuggerDatabaseColumns::IColumn>&& Column)
{
	Columns.Add(Column->GetName(), Column);
}

EColumnSortMode::Type SDebuggerDatabaseView::GetColumnSortMode(const FName ColumnId) const
{
	if (ColumnId == SortColumn)
	{
		return SortMode;
	}
	
	return EColumnSortMode::None;
}

float SDebuggerDatabaseView::GetColumnWidth(const FName ColumnId) const
{
	check(Columns.Find(ColumnId));

	return Columns[ColumnId]->Width;
}


void SDebuggerDatabaseView::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName & ColumnId, const EColumnSortMode::Type InSortMode)
{
	check(Columns.Find(ColumnId));
	SortColumn = ColumnId;
	SortMode = InSortMode;
	SortDatabaseRows();
	FilterDatabaseRows();
}

void SDebuggerDatabaseView::OnColumnWidthChanged(const float NewWidth, FName ColumnId) const
{
	check(Columns.Find(ColumnId));
	
	Columns[ColumnId]->Width = NewWidth;
}

void SDebuggerDatabaseView::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;
	FilterDatabaseRows();
}

void SDebuggerDatabaseView::OnDatabaseRowSelectionChanged(TSharedPtr<FDebuggerDatabaseRowData> Row, ESelectInfo::Type SelectInfo)
{
	OnPoseSelectionChanged.ExecuteIfBound();
}

void SDebuggerDatabaseView::SortDatabaseRows()
{
	if (SortMode == EColumnSortMode::Ascending)
	{
		UnfilteredDatabaseRows.Sort(Columns[SortColumn]->GetSortPredicateAscending());
	}
	else if (SortMode == EColumnSortMode::Descending)
	{
		UnfilteredDatabaseRows.Sort(Columns[SortColumn]->GetSortPredicateDescending());
	}
}

void SDebuggerDatabaseView::FilterDatabaseRows()
{
	FilteredDatabaseView.Rows.Empty();

	FString FilterString = FilterText.ToString();
	TArray<FString> Tokens;
	FilterString.ParseIntoArrayWS(Tokens);

	if (Tokens.Num() == 0)
	{
		for (const auto& UnfilteredRow : UnfilteredDatabaseRows)
		{
			FilteredDatabaseView.Rows.Add(UnfilteredRow);
		}
	}
	else
	{
		for (const auto& UnfilteredRow : UnfilteredDatabaseRows)
		{
			bool bMatchesAllTokens = Algo::AllOf(
				Tokens,
				[&](FString Token)
			{
				return UnfilteredRow->AnimSequenceName.Contains(Token);
			});

			if (bMatchesAllTokens)
			{
				FilteredDatabaseView.Rows.Add(UnfilteredRow);
			}
		}
	}

	FilteredDatabaseView.ListView->RequestListRefresh();
}

void SDebuggerDatabaseView::CreateRows(const UPoseSearchDatabase& Database)
{
	const int32 NumPoses = Database.SearchIndex.NumPoses;
	UnfilteredDatabaseRows.Reserve(NumPoses);

	// Build database rows
	for(const FPoseSearchIndexAsset& SearchIndexAsset : Database.SearchIndex.Assets)
	{
		const FPoseSearchDatabaseSequence& DbSequence = Database.GetSourceAsset(&SearchIndexAsset);
		const int32 LastPoseIdx = SearchIndexAsset.FirstPoseIdx + SearchIndexAsset.NumPoses;
		for (int32 PoseIdx = SearchIndexAsset.FirstPoseIdx; PoseIdx != LastPoseIdx; ++PoseIdx)
		{
			float Time = Database.GetTimeOffset(PoseIdx);

			TSharedRef<FDebuggerDatabaseRowData> Row = UnfilteredDatabaseRows.Add_GetRef(MakeShared<FDebuggerDatabaseRowData>());
			Row->PoseIdx = PoseIdx;
			Row->AnimSequenceName = DbSequence.Sequence->GetName();
			Row->Time = Time;
			Row->AnimFrame = DbSequence.Sequence->GetFrameAtTime(Time);
			Row->bMirrored = SearchIndexAsset.bMirrored;
		}
	}

	ActiveView.Rows.Add(MakeShared<FDebuggerDatabaseRowData>());
}

void SDebuggerDatabaseView::UpdateRows(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database)
{
	if (UnfilteredDatabaseRows.IsEmpty())
	{
		check(ActiveView.Rows.IsEmpty());
		CreateRows(Database);
	}
	check(ActiveView.Rows.Num() == 1);

	FPoseSearchWeightsContext StateWeights;
	StateWeights.Update(State.Weights, &Database);

	UE::PoseSearch::FSearchContext SearchContext;
	SearchContext.SetSource(&Database);
	SearchContext.QueryValues = State.QueryVectorNormalized;
	SearchContext.WeightsContext = &StateWeights;
	if (const FPoseSearchIndexAsset* CurrentIndexAsset = Database.SearchIndex.FindAssetForPose(State.DbPoseIdx))
	{
		SearchContext.QueryMirrorRequest = CurrentIndexAsset->bMirrored ? 
			EPoseSearchBooleanRequest::TrueValue : EPoseSearchBooleanRequest::FalseValue;
	}
	
	for(const TSharedRef<FDebuggerDatabaseRowData>& Row : UnfilteredDatabaseRows)
	{
		const int32 PoseIdx = Row->PoseIdx;

		ComparePoses(PoseIdx, SearchContext, Row->PoseCostInfo);

		// If we are on the active pose for the frame
		if (PoseIdx == State.DbPoseIdx)
		{
			*ActiveView.Rows[0] = Row.Get();
		}
	}

	SortDatabaseRows();
	FilterDatabaseRows();
}

TSharedRef<ITableRow> SDebuggerDatabaseView::HandleGenerateDatabaseRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return
		SNew(SDebuggerDatabaseRow, OwnerTable, Item, FilteredDatabaseView.RowStyle, &FilteredDatabaseView.RowBrush, FMargin(0.0f, 2.0f, 6.0f, 2.0f))
		.ColumnMap(this, &SDebuggerDatabaseView::GetColumnMap);
	
}

TSharedRef<ITableRow> SDebuggerDatabaseView::HandleGenerateActiveRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return
		SNew(SDebuggerDatabaseRow, OwnerTable, Item, ActiveView.RowStyle, &ActiveView.RowBrush, FMargin(0.0f, 2.0f, 6.0f, 4.0f))
		.ColumnMap(this, &SDebuggerDatabaseView::GetColumnMap);
}

void SDebuggerDatabaseView::Construct(const FArguments& InArgs)
{
	ParentDebuggerViewPtr = InArgs._Parent;
	OnPoseSelectionChanged = InArgs._OnPoseSelectionChanged;

	using namespace DebuggerDatabaseColumns;

	// @TODO: Support runtime reordering of these indices
	// Construct all column types
	AddColumn(MakeShared<FAnimSequenceName>(0));
	AddColumn(MakeShared<FCost>(1));
	AddColumn(MakeShared<FPoseCost>(2));
	AddColumn(MakeShared<FTrajectoryCost>(3));
	AddColumn(MakeShared<FCostModifier>(4));
	AddColumn(MakeShared<FFrame>(5));
	AddColumn(MakeShared<FMirrored>(6));
	AddColumn(MakeShared<FPoseIdx>(7));

	// Active Row
	ActiveView.HeaderRow = SNew(SHeaderRow);

	// Used for spacing
	ActiveView.ScrollBar =
		SNew(SScrollBar)
		.Orientation(Orient_Vertical)
		.HideWhenNotInUse(false)
		.AlwaysShowScrollbar(true)
		.AlwaysShowScrollbarTrack(true);

	ActiveView.ListView = SNew(SListView<TSharedRef<FDebuggerDatabaseRowData>>)
		.ListItemsSource(&ActiveView.Rows)
		.HeaderRow(ActiveView.HeaderRow.ToSharedRef())
		.OnGenerateRow(this, &SDebuggerDatabaseView::HandleGenerateActiveRow)
		.ExternalScrollbar(ActiveView.ScrollBar)
		.SelectionMode(ESelectionMode::SingleToggle)
		.ConsumeMouseWheel(EConsumeMouseWheel::Never);

	ActiveView.RowStyle = FEditorStyle::GetWidgetStyle<FTableRowStyle>("TableView.Row");
	ActiveView.RowBrush = *FEditorStyle::GetBrush("DetailsView.CategoryTop");


	// Filtered Database
	FilteredDatabaseView.ScrollBar =
		SNew(SScrollBar)
		.Orientation(Orient_Vertical)
		.HideWhenNotInUse(false)
		.AlwaysShowScrollbar(true)
		.AlwaysShowScrollbarTrack(true);
	FilteredDatabaseView.HeaderRow = SNew(SHeaderRow).Visibility(EVisibility::Collapsed);

	FilteredDatabaseView.ListView = SNew(SListView<TSharedRef<FDebuggerDatabaseRowData>>)
		.ListItemsSource(&FilteredDatabaseView.Rows)
		.HeaderRow(FilteredDatabaseView.HeaderRow.ToSharedRef())
		.OnGenerateRow(this, &SDebuggerDatabaseView::HandleGenerateDatabaseRow)
		.ExternalScrollbar(FilteredDatabaseView.ScrollBar)
		.SelectionMode(ESelectionMode::SingleToggle)
		.ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
		.OnSelectionChanged(this, &SDebuggerDatabaseView::OnDatabaseRowSelectionChanged);

	FilteredDatabaseView.RowStyle = FEditorStyle::GetWidgetStyle<FTableRowStyle>("TableView.Row");
	// Set selected color to white to retain visibility when multi-selecting
	FilteredDatabaseView.RowStyle.SetSelectedTextColor(FLinearColor(FVector3f(0.8f)));
	FilteredDatabaseView.RowBrush = *FEditorStyle::GetBrush("ToolPanel.GroupBorder");

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		// Side and top margins, ignore bottom handled by the color border below
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		.AutoHeight()
		[
			// Active Row text tab
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				.Padding(0.0f)
				.AutoWidth()
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
					.Padding(FMargin(30.0f, 3.0f, 30.0f, 0.0f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						SNew(STextBlock)
						.Text(FText::FromString("Active Pose"))	
					]
				]
			]

			// Active row list view with scroll bar
			+ SVerticalBox::Slot()
			
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(0.0f)
				[
					SNew(SBorder)
					
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					.Padding(0.0f)
					[
						ActiveView.ListView.ToSharedRef()
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ActiveView.ScrollBar.ToSharedRef()
				]
			]	
		]

		+ SVerticalBox::Slot()
		.Padding(0.0f, 0.0f, 0.0f, 5.0f)
		[
			// Database view text tab
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				.Padding(0.0f)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
					.Padding(FMargin(30.0f, 3.0f, 30.0f, 0.0f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						SNew(STextBlock)
						.Text(FText::FromString("Pose Database"))	
					]
				]
				.AutoWidth()
				
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SNew(SBorder)
					.BorderImage(&FilteredDatabaseView.RowStyle.EvenRowBackgroundBrush)
				]
			]
			.AutoHeight()

			// Gray line below the tab 
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
				.Padding(FMargin(0.0f, 3.0f, 0.0f, 3.0f))
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
			]

			+ SVerticalBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			.AutoHeight()
			[
				SAssignNew(FilterBox, SSearchBox)
				.OnTextChanged(this, &SDebuggerDatabaseView::OnFilterTextChanged)
			]
		
			+ SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0.0f)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("NoBorder"))
					.Padding(0.0f)
					[
						FilteredDatabaseView.ListView.ToSharedRef()
					]
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					FilteredDatabaseView.ScrollBar.ToSharedRef()
				]
			]
		]
	];
	
	SortColumn = FCost::Name;
	SortMode = EColumnSortMode::Ascending;

	// Active view scroll bar only for indenting the columns to align w/ database
	ActiveView.ScrollBar->SetVisibility(EVisibility::Hidden);
	
	RefreshColumns();
}

void SDebuggerDetailsView::Construct(const FArguments& InArgs)
{
	ParentDebuggerViewPtr = InArgs._Parent;

	// Add property editor (detail view) UObject to world root so that it persists when PIE is stopped
	Reflection = NewObject<UPoseSearchDebuggerReflection>();
	Reflection->AddToRoot();
	check(IsValid(Reflection));

	// @TODO: Convert this to a custom builder instead of of a standard details view
	// Load property module and create details view with our reflection UObject
	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;

	Details = PropPlugin.CreateDetailView(DetailsViewArgs);
	Details->SetObject(Reflection);
	
	ChildSlot
	[
		Details.ToSharedRef()
	];
}

void SDebuggerDetailsView::Update(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database) const
{
	UpdateReflection(State, Database);
}

SDebuggerDetailsView::~SDebuggerDetailsView()
{
	// Our previously instantiated object attached to root may be cleaned up at this point
	if (UObjectInitialized())
	{
		Reflection->RemoveFromRoot();
	}
}

void SDebuggerDetailsView::UpdateReflection(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database) const
{
	check(Reflection);
	const UPoseSearchSchema* Schema = Database.Schema;

	Reflection->CurrentDatabaseName = Database.GetName();
	Reflection->ElapsedPoseJumpTime = State.ElapsedPoseJumpTime;
	Reflection->bFollowUpAnimation = EnumHasAnyFlags(State.Flags, FTraceMotionMatchingState::EFlags::FollowupAnimation);
	
	FFeatureVectorReader Reader;
	Reader.Init(&Schema->Layout);

	// Query pose
	Reader.SetValues(State.QueryVector);
	Reflection->QueryPoseVector.ExtractFeatures(Reader);

	// Active pose
	TArray<float> Pose(Database.SearchIndex.GetPoseValues(State.DbPoseIdx));
	Database.SearchIndex.InverseNormalize(Pose);
	Reader.SetValues(Pose);
	Reflection->ActivePoseVector.ExtractFeatures(Reader);

	auto DebuggerView = ParentDebuggerViewPtr.Pin();
	if (DebuggerView.IsValid()) 
	{
		TArray<TSharedRef<FDebuggerDatabaseRowData>> SelectedRows = DebuggerView->GetSelectedDatabaseRows();
		if (!SelectedRows.IsEmpty())
		{
			const TSharedRef<FDebuggerDatabaseRowData>& Selected = SelectedRows[0];
			Pose = Database.SearchIndex.GetPoseValues(Selected->PoseIdx);
			Database.SearchIndex.InverseNormalize(Pose);
			Reader.SetValues(Pose);
			Reflection->SelectedPoseVector.ExtractFeatures(Reader);

			Pose = Selected->PoseCostInfo.CostVector;
			//Database.SearchIndex.InverseNormalize(Pose);
			Reader.SetValues(Pose);
			Reflection->CostVector.ExtractFeatures(Reader);
		}
	}
}

void SDebuggerView::Construct(const FArguments& InArgs, uint64 InAnimInstanceId)
{
	MotionMatchingNodeIds = InArgs._MotionMatchingNodeIds;
	MotionMatchingState = InArgs._MotionMatchingState;
	PoseSearchDatabase = InArgs._PoseSearchDatabase;
	OnUpdateSelection = InArgs._OnUpdateSelection;
	IsPIESimulating = InArgs._IsPIESimulating;
	IsRecording = InArgs._IsRecording;
	RecordingDuration = InArgs._RecordingDuration;
	NodesNum = InArgs._NodesNum;
	World = InArgs._World;
	RootTransform = InArgs._RootTransform;
	OnUpdate = InArgs._OnUpdate;
	OnViewClosed = InArgs._OnViewClosed;

	

	// Validate the existence of the passed getters
	check(MotionMatchingNodeIds.IsBound());
    check(MotionMatchingState.IsBound());
	check(PoseSearchDatabase.IsBound());
	check(OnUpdateSelection.IsBound());
	check(IsPIESimulating.IsBound())
    check(IsRecording.IsBound());
    check(RecordingDuration.IsBound());
	check(NodesNum.IsBound());
	check(World.IsBound());
	check(RootTransform.IsBound());
	check(OnUpdate.IsBound());
	check(OnViewClosed.IsBound());
	
	AnimInstanceId = InAnimInstanceId;
	SelectedNodeId = -1;

	ChildSlot
	[
		SAssignNew(DebuggerView, SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			.WidgetIndex(this, &SDebuggerView::SelectView)

			// [0] Selection view before node selection is made
			+ SWidgetSwitcher::Slot()
			.Padding(40.0f)
			.HAlign(HAlign_Fill)
            .VAlign(VAlign_Center)
			[
				SAssignNew(SelectionView, SVerticalBox)
			]

			// [1] Node selected; node debugger view
			+ SWidgetSwitcher::Slot()
			[
				GenerateNodeDebuggerView()
			]

			// [2] Occluding message box when stopped (no recording)
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDebuggerMessageBox, "Record gameplay to begin debugging")
			]

			// [3] Occluding message box when recording
			+ SWidgetSwitcher::Slot()
			[
				SNew(SDebuggerMessageBox, "Recording...")
			]
			
			// [4] Occluding message box when there is no data for the selected MM node
			+ SWidgetSwitcher::Slot()
			[
				GenerateNoDataMessageView()
			]
		]
	];
}

void SDebuggerView::SetTimeMarker(double InTimeMarker)
{
	if (IsPIESimulating.Get())
	{
		return;
	}

	TimeMarker = InTimeMarker;
}

void SDebuggerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (IsPIESimulating.Get())
	{
		return;
	}

	const UWorld* DebuggerWorld = World.Get();
    check(DebuggerWorld);
	
	// @TODO: Handle editor world when those features are enabled for the Rewind Debugger
	// Currently prevents debug draw remnants from stopped world
	if (DebuggerWorld->WorldType != EWorldType::PIE)
	{
		return;
	}
	
	const bool bSameTime = FMath::Abs(TimeMarker - PreviousTimeMarker) < DOUBLE_SMALL_NUMBER;
	PreviousTimeMarker = TimeMarker;

	bool bNeedUpdate = false;

	// We haven't reached the update point yet
	if (CurrentConsecutiveFrames < ConsecutiveFramesUpdateThreshold)
	{
		// If we're on the same time marker, it is consecutive
		if (bSameTime)
		{
			++CurrentConsecutiveFrames;
		}
	}
	else
	{
		// New frame after having updated, reset consecutive frames count and start counting again
		if (!bSameTime)
		{
			CurrentConsecutiveFrames = 0;
			bUpdated = false;
		}
		// Haven't updated since passing through frame gate, update once
		else if (!bUpdated)
		{
			bNeedUpdate = true;
		}
	}

	if (bNeedUpdate)
	{
		OnUpdate.Execute(AnimInstanceId);
		if (UpdateSelection())
		{
			OnUpdateSelection.Execute(SelectedNodeId);
			UpdateViews();
		}
		bUpdated = true;
	}
	
	// Draw visualization every tick
	DrawVisualization();
}

bool SDebuggerView::UpdateSelection()
{
	// Update selection view if no node selected
	bool bNodeSelected = SelectedNodeId != INDEX_NONE;
	if (!bNodeSelected)
	{
		const TArray<int32>& NodeIds = *MotionMatchingNodeIds.Get();
		// Only one node active, bypass selection view
		if (NodeIds.Num() == 1)
		{
			SelectedNodeId = *NodeIds.begin();
			bNodeSelected = true;
		}
		// Create selection view with buttons for each node, displaying the database name
		else
		{
			SelectionView->ClearChildren();
			for (int32 NodeId : NodeIds)
			{
				OnUpdateSelection.Execute(NodeId);
				SelectionView->AddSlot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(10.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(PoseSearchDatabase.Get()->GetName()))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.ContentPadding(10.0f)
					.OnClicked(this, &SDebuggerView::OnUpdateNodeSelection, NodeId)
				];
			}
		}
	}

	return bNodeSelected;
}

void SDebuggerView::UpdateViews() const
{
	const FTraceMotionMatchingStateMessage* State = MotionMatchingState.Get();
	const UPoseSearchDatabase* Database = PoseSearchDatabase.Get();
	if (State && Database)
	{
		DatabaseView->Update(*State, *Database);
		DetailsView->Update(*State, *Database);
	}
}

void SDebuggerView::DrawVisualization() const
{
	const UWorld* DebuggerWorld = World.Get();
	check(DebuggerWorld);

	const FTraceMotionMatchingStateMessage* State = MotionMatchingState.Get();
	const UPoseSearchDatabase* Database = PoseSearchDatabase.Get();
	const FTransform* Transform = RootTransform.Get();
	if (State && Database && Transform)
	{
		DrawFeatures(*DebuggerWorld, *State, *Database, *Transform);
	}
}

TArray<TSharedRef<FDebuggerDatabaseRowData>> SDebuggerView::GetSelectedDatabaseRows() const
{
	return DatabaseView->GetDatabaseRows()->GetSelectedItems();
}

void SDebuggerView::DrawFeatures(
	const UWorld& DebuggerWorld,
	const FTraceMotionMatchingStateMessage& State,
	const UPoseSearchDatabase& Database,
	const FTransform& Transform
) const
{
	// Set shared state
	FDebugDrawParams DrawParams;
	DrawParams.Database = &Database;
	DrawParams.PoseVector = State.QueryVector;
	DrawParams.World = &DebuggerWorld;
	DrawParams.RootTransform = Transform;
	// Single frame render
	DrawParams.DefaultLifeTime = 0.0f;

	const TObjectPtr<UPoseSearchDebuggerReflection>& Reflection = DetailsView->GetReflection();

	auto SetDrawFlags = [](FDebugDrawParams& InDrawParams, const FPoseSearchDebuggerFeatureDrawOptions& Options)
	{
		InDrawParams.Flags = EDebugDrawFlags::None;
		if (Options.bDisable)
		{
			return;
		}

		if (Options.bDrawPoseFeatures)
		{
			InDrawParams.Flags |= EDebugDrawFlags::IncludePose;
		}

		if (Options.bDrawTrajectoryFeatures)
		{
			InDrawParams.Flags |= EDebugDrawFlags::IncludeTrajectory;
		}

		if (Options.bDrawSampleLabels)
		{
			InDrawParams.Flags |= EDebugDrawFlags::DrawSampleLabels;
		}

		if (Options.bDrawSamplesWithColorGradient)
		{
			InDrawParams.Flags |= EDebugDrawFlags::DrawSamplesWithColorGradient;
		}
	};

	// Draw query vector
	DrawParams.Color = &FLinearColor::Blue;
	SetDrawFlags(DrawParams, Reflection->QueryDrawOptions);
	DrawParams.LabelPrefix = TEXT("Q");
	Draw(DrawParams);
	DrawParams.PoseVector = {};

	const TSharedPtr<SListView<TSharedRef<FDebuggerDatabaseRowData>>>& DatabaseRows = DatabaseView->GetDatabaseRows();
	TArray<TSharedRef<FDebuggerDatabaseRowData>> Selected = DatabaseRows->GetSelectedItems();

	// Red for non-active database view
	DrawParams.Color = &FLinearColor::Red;
	DrawParams.LabelPrefix = TEXT("S");
	SetDrawFlags(DrawParams, Reflection->SelectedPoseDrawOptions);

	// Draw any selected database vectors
	for (const TSharedRef<FDebuggerDatabaseRowData>& Row : Selected)
	{
		DrawParams.PoseIdx = Row->PoseIdx;
		Draw(DrawParams);
	}

	Selected = DatabaseView->GetActiveRow()->GetSelectedItems();
	
	// Active row should only have 0 or 1
	check(Selected.Num() < 2);

	if (!Selected.IsEmpty())
	{
		// Green for the active view
		DrawParams.Color = &FLinearColor::Green;
		
		// Use the motion-matching state's pose idx, as the active row may be update-throttled at this point
		DrawParams.PoseIdx = State.DbPoseIdx;
		DrawParams.LabelPrefix = TEXT("A");
		Draw(DrawParams);
	}
}

int32 SDebuggerView::SelectView() const
{
	// Currently recording
	if (IsPIESimulating.Get())
	{
		if (IsRecording.Get())
		{
			return RecordingMsg;
		}
	}

	// Data has not been recorded yet
	if (RecordingDuration.Get() < DOUBLE_SMALL_NUMBER)
	{
		return StoppedMsg;
	}

	const bool bNoActiveNodes = NodesNum.Get() == 0;
	const bool bNodeSelectedWithoutData = SelectedNodeId != INDEX_NONE && MotionMatchingState.Get() == nullptr;

	// No active nodes, or node selected has no data
	if (bNoActiveNodes || bNodeSelectedWithoutData)
    {
    	return NoDataMsg;
    }

	// Node not selected yet, showcase selection view
	if (SelectedNodeId == INDEX_NONE)
	{
		return Selection;
	}

	// Standard debugger view
	return Debugger;
}

void SDebuggerView::OnPoseSelectionChanged()
{
	const FTraceMotionMatchingStateMessage* State = MotionMatchingState.Get();
	const UPoseSearchDatabase* Database = PoseSearchDatabase.Get();
	if (State && Database)
	{
		DetailsView->Update(*State, *Database);
	}
}

FReply SDebuggerView::OnUpdateNodeSelection(int32 InSelectedNodeId)
{
	// -1 will backtrack to selection view
	SelectedNodeId = InSelectedNodeId;
	bUpdated = false;
	return FReply::Handled();
}

TSharedRef<SWidget> SDebuggerView::GenerateNoDataMessageView()
{
	TSharedRef<SWidget> ReturnButtonView = GenerateReturnButtonView();
	ReturnButtonView->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() -> EVisibility
	{
		// Hide the return button for the no data message if we have no nodes at all
		return NodesNum.Get() > 0 ? EVisibility::Visible : EVisibility::Hidden;
	}));
	
	return 
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		[
			SNew(SDebuggerMessageBox, "No recorded data available for the selected frame")
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			ReturnButtonView
		];
}

TSharedRef<SWidget> SDebuggerView::GenerateReturnButtonView()
{
	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.Padding(10, 5, 0, 0)
		.AutoWidth()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.ButtonStyle(FEditorStyle::Get(), "SimpleButton")
			.ContentPadding( FMargin(1, 0) )
			.OnClicked(this, &SDebuggerView::OnUpdateNodeSelection, static_cast<int32>(INDEX_NONE))
			// Contents of button, icon then text
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.CircleArrowLeft"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(8.0f, 0.0f, 0.0f, 0.0f))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString("Return to Database Selection"))
					.Justification(ETextJustify::Center)
				]
			]
		];
}

TSharedRef<SWidget> SDebuggerView::GenerateNodeDebuggerView()
{
	TSharedRef<SWidget> ReturnButtonView = GenerateReturnButtonView();
	ReturnButtonView->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() -> EVisibility
		{
			// Collapse this view if we have don't have more than 1 node
			return NodesNum.Get() > 1 ? EVisibility::Visible : EVisibility::Collapsed;
		}
	));
	
	return 
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		.ResizeMode(ESplitterResizeMode::Fill)
	
		// Database view
		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ReturnButtonView
			]
			
			+ SVerticalBox::Slot()
			[
				SAssignNew(DatabaseView, SDebuggerDatabaseView)
				.Parent(SharedThis(this))
				.OnPoseSelectionChanged(this, &SDebuggerView::OnPoseSelectionChanged)
			]
		]

		// Details panel view
		+ SSplitter::Slot()
		.Value(0.35f)
		[
			SAssignNew(DetailsView, SDebuggerDetailsView)
			.Parent(SharedThis(this))
		];
}

FName SDebuggerView::GetName() const
{
	static const FName DebuggerName("PoseSearchDebugger");
	return DebuggerName;
}

uint64 SDebuggerView::GetObjectId() const
{
	return AnimInstanceId;
}

SDebuggerView::~SDebuggerView()
{
	OnViewClosed.Execute(AnimInstanceId);
}

void FDebuggerInstance::UpdateMotionMatchingStates(uint64 InAnimInstanceId)
{
	NodeIds.Empty();
	MotionMatchingStates.Empty();
	if (!RewindDebugger)
	{
		return;
	}
	
	// Get provider and validate
	const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
	
	const FTraceProvider* TraceProvider = Session->ReadProvider<FTraceProvider>(FTraceProvider::ProviderName);
	if (!TraceProvider)
	{
		return;
	}

	const double TraceTime = RewindDebugger->CurrentTraceTime();
	TraceServices::FFrame Frame;
	ReadFrameProvider(*Session).GetFrameFromTime(TraceFrameType_Game, TraceTime, Frame);
	

	TraceProvider->EnumerateMotionMatchingStateTimelines(InAnimInstanceId, [this, &Frame](const FTraceProvider::FMotionMatchingStateTimeline& InTimeline)
	{
		const FTraceMotionMatchingStateMessage* Message = nullptr;
		
		InTimeline.EnumerateEvents(Frame.StartTime, Frame.EndTime, [this, &Message, &Frame](double InStartTime, double InEndTime, const FTraceMotionMatchingStateMessage& InMessage)
		{
			Message = &InMessage;
			return TraceServices::EEventEnumerate::Stop;
		});

		if (Message)
		{
			NodeIds.Add(Message->NodeId);
			MotionMatchingStates.Add(Message);
		}
	});
}


FDebuggerInstance::FDebuggerInstance(uint64 InAnimInstanceId)
	: AnimInstanceId(InAnimInstanceId)
{
}

const FTraceMotionMatchingStateMessage* FDebuggerInstance::GetMotionMatchingState() const
{
	return ActiveMotionMatchingState;
}

const UPoseSearchDatabase* FDebuggerInstance::GetPoseSearchDatabase() const
{
	if (!ActiveMotionMatchingState)
	{
		return nullptr;
	}

	const uint64 DatabaseId = ActiveMotionMatchingState->DatabaseId;
	if (DatabaseId == 0)
	{
	    return nullptr;
	}
	
	UObject* DatabaseObject = FObjectTrace::GetObjectFromId(DatabaseId);
	// @TODO: Load the object if unloaded
	if (DatabaseObject == nullptr)
	{
		return nullptr;
	}
	check(DatabaseObject->IsA<UPoseSearchDatabase>());

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(DatabaseObject);
	const UPoseSearchSchema* Schema = Database->Schema;
	if (Schema == nullptr || !Schema->IsValid())
	{
		return nullptr;
	}
	return Database;
}

const TArray<int32>* FDebuggerInstance::GetNodeIds() const
{
	return &NodeIds;
}


int32 FDebuggerInstance::GetNodesNum() const
{
	return MotionMatchingStates.Num();
}

const FTransform* FDebuggerInstance::GetRootTransform() const
{
	if (!RewindDebugger || !ActiveMotionMatchingState)
	{
		return nullptr;
	}
	
	// Get provider and validate
	const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
	
	const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider");
	if (!AnimationProvider)
	{
		return nullptr;
	}

	const double TraceTime = RewindDebugger->CurrentTraceTime();
	TraceServices::FFrame Frame;
	ReadFrameProvider(*Session).GetFrameFromTime(TraceFrameType_Game, TraceTime, Frame);

	const FTransform* RootTransform = nullptr;

	AnimationProvider->ReadSkeletalMeshPoseTimeline(ActiveMotionMatchingState->SkeletalMeshComponentId, [&RootTransform, &Frame](const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData, bool bHasCurves)
	{
		TimelineData.EnumerateEvents(Frame.StartTime, Frame.EndTime,
			[&RootTransform, &Frame](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& PoseMessage)
			{
				RootTransform = &PoseMessage.ComponentToWorld;
				return TraceServices::EEventEnumerate::Stop;
			});
	});

	return RootTransform;
}

void FDebuggerInstance::OnUpdate(uint64 InAnimInstanceId)
{
	UpdateMotionMatchingStates(InAnimInstanceId);
}

void FDebuggerInstance::OnUpdateSelection(int32 InNodeId)
{
	if (InNodeId == INDEX_NONE)
	{
		return;
	}
	
	// Find node in all motion matching states this frame
	const int32 NodesNum = NodeIds.Num();
	for(int i = 0; i < NodesNum; ++i)
	{
		if (NodeIds[i] == InNodeId)
		{
			ActiveMotionMatchingState = MotionMatchingStates[i];
			return;
		}
	}

	ActiveMotionMatchingState = nullptr;
}

FDebugger* FDebugger::Debugger;
void FDebugger::Initialize()
{
	Debugger = new FDebugger;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, Debugger);

}

void FDebugger::Shutdown()
{
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, Debugger);
	delete Debugger;
}

void FDebugger::Update(float DeltaTime, IRewindDebugger* InRewindDebugger)
{
	// Update active rewind debugger in use
	RewindDebugger = InRewindDebugger;

	for (TSharedRef<FDebuggerInstance>& DebuggerInstance : DebuggerInstances)
	{
		DebuggerInstance->RewindDebugger = RewindDebugger;
	}
}

bool FDebugger::IsPIESimulating() const
{
	return RewindDebugger->IsPIESimulating();
}

bool FDebugger::IsRecording() const
{
	return RewindDebugger->IsRecording();
}

double FDebugger::GetRecordingDuration() const
{
	return RewindDebugger->GetRecordingDuration();
}

const UWorld* FDebugger::GetWorld() const
{
	return RewindDebugger->GetWorldToVisualize();
}

void FDebugger::OnViewClosed(uint64 InAnimInstanceId)
{
	for (int i = 0; i < DebuggerInstances.Num(); ++i)
	{
		if (DebuggerInstances[i]->AnimInstanceId == InAnimInstanceId)
		{
			DebuggerInstances.RemoveAtSwap(i);
			return;
		}
	}

	// Should always be a valid remove
	checkNoEntry();
}

TSharedPtr<SDebuggerView> FDebugger::GenerateInstance(uint64 InAnimInstanceId)
{
	TSharedRef<FDebuggerInstance>& DebuggerInstance = DebuggerInstances.Add_GetRef(MakeShared<FDebuggerInstance>(InAnimInstanceId));
	
	TSharedPtr<SDebuggerView> DebuggerView;

	SAssignNew(DebuggerView, SDebuggerView, InAnimInstanceId)
	.MotionMatchingNodeIds(DebuggerInstance, &FDebuggerInstance::GetNodeIds)
	.MotionMatchingState(DebuggerInstance, &FDebuggerInstance::GetMotionMatchingState)
	.PoseSearchDatabase(DebuggerInstance, &FDebuggerInstance::GetPoseSearchDatabase)
	.NodesNum(DebuggerInstance, &FDebuggerInstance::GetNodesNum)
	.RootTransform(DebuggerInstance, &FDebuggerInstance::GetRootTransform)
	.OnUpdateSelection(DebuggerInstance, &FDebuggerInstance::OnUpdateSelection)
	.OnUpdate(DebuggerInstance, &FDebuggerInstance::OnUpdate)
	// Rewind debugger callbacks gathered from shared object
	.World_Raw(this, &FDebugger::GetWorld)
	.IsRecording_Raw(this, &FDebugger::IsRecording)
	.IsPIESimulating_Raw(this, &FDebugger::IsPIESimulating)
	.RecordingDuration_Raw(this, &FDebugger::GetRecordingDuration)
	.OnViewClosed_Raw(this, &FDebugger::OnViewClosed);

	return DebuggerView;
}


FText FDebuggerViewCreator::GetTitle() const
{
	return LOCTEXT("PoseSearchDebuggerTabTitle", "Pose Search");
}

FSlateIcon FDebuggerViewCreator::GetIcon() const
{
#if WITH_EDITOR
	return FSlateIconFinder::FindIconForClass(UAnimInstance::StaticClass());
#else
	return FSlateIcon();
#endif
}

FName FDebuggerViewCreator::GetTargetTypeName() const
{
	static FName TargetTypeName = "AnimInstance";
	return TargetTypeName;
}

TSharedPtr<IRewindDebuggerView> FDebuggerViewCreator::CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession) const
{
	return FDebugger::Get()->GenerateInstance(ObjectId);
}

FName FDebuggerViewCreator::GetName() const
{
	static const FName Name("PoseSearchDebugger");
	return Name;
}

} // namespace UE::PoseSearch