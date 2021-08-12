// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDebugger.h"
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
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "TraceServices/Model/Frames.h"

#define LOCTEXT_NAMESPACE "PoseSearchDebugger"


UPoseSearchDebuggerReflection::UPoseSearchDebuggerReflection(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void FPoseSearchDebuggerFeatureReflection::EmptyAll()
{
	Positions.Empty();
	LinearVelocities.Empty();
	AngularVelocities.Empty();
}

namespace UE { namespace PoseSearch {

class FDebuggerDatabaseRowData : public TSharedFromThis<FDebuggerDatabaseRowData>
{
public:
	FDebuggerDatabaseRowData() = default;
	FDebuggerDatabaseRowData(
	    int32 InPoseIdx, 
	    const FString& InAnimSequenceName,
	    float InTime,
	    float InLength,
	    float InScore,
	    float InPoseScore,
	    float InTrajectoryScore
	)
		: PoseIdx(InPoseIdx)
		, AnimSequenceName(InAnimSequenceName)
		, Time(InTime)
		, Length(InLength)
		, Score(InScore)
		, PoseScore(InPoseScore)
		, TrajectoryScore(InTrajectoryScore)
	{
	}

	int32 PoseIdx = 0;
	FString AnimSequenceName = "";
	float Time = 0.0f;
	float Length = 0.0f;
	float Score = 0.0f;
	float PoseScore = 0.0f;
	float TrajectoryScore = 0.0f;
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
				.Text_Lambda([this, RowData] { return GetRowText(RowData); })
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
			return FText::AsNumber(Row->PoseIdx);
		}
	};
	const FName FPoseIdx::Name = "PoseIdx";

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
	const FName FAnimSequenceName::Name = "AnimSequence";

	struct FTime : IColumn
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
			return FText::AsNumber(Row->Time);
		}
	};
	const FName FTime::Name = "Time";

	struct FLength : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Length < Row1->Length; };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Length >= Row1->Length; };
			return Predicate;
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->Length);
		}
	};
	const FName FLength::Name = "Length";

	struct FScore : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Score < Row1->Score; };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Score >= Row1->Score; };
			return Predicate;
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
        {
        	return FText::AsNumber(Row->Score);
        }
	};
	const FName FScore::Name = "Score";


	struct FPoseScore : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseScore < Row1->PoseScore; };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseScore >= Row1->PoseScore; };
			return Predicate;
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->PoseScore);
		}
	};
	const FName FPoseScore::Name = "Pose Score";


	struct FTrajectoryScore : IColumn
	{
		using IColumn::IColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual const FSortPredicate& GetSortPredicateAscending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->TrajectoryScore < Row1->TrajectoryScore; };
			return Predicate;
		}

		virtual const FSortPredicate& GetSortPredicateDescending() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->TrajectoryScore >= Row1->TrajectoryScore; };
			return Predicate;
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->TrajectoryScore);
        }
	};
	const FName FTrajectoryScore::Name = "Trajectory Score";
}


DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<DebuggerDatabaseColumns::IColumn>&, FGetColumnDelegate, FName);

/**
 * Widget representing a single row of the database view
 */
class SDebuggerDatabaseRow : public SMultiColumnTableRow<TSharedRef<FDebuggerDatabaseRowData>>
{
	SLATE_BEGIN_ARGS(SDebuggerDatabaseRow) {}
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
		Row = InRow;
		RowBrush = InRowBrush;

		SMultiColumnTableRow<TSharedRef<FDebuggerDatabaseRowData>>::Construct(
			FSuperRowType::FArguments()
			.Padding(InPaddingMargin)
			.Style(&InRowStyle),
			InOwnerTable
		);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		check(GetColumn.IsBound());
		check(RowBrush);
		
		// Get column
		TSharedRef<DebuggerDatabaseColumns::IColumn>& Column = GetColumn.Execute(InColumnName);
		
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
	static FGetColumnDelegate GetColumn;
};
FGetColumnDelegate SDebuggerDatabaseRow::GetColumn;

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

void SDebuggerDatabaseView::Update(const FTraceMotionMatchingStateMessage* State, const UPoseSearchDatabase* Database)
{
	UpdateRows(State, Database);
}

void SDebuggerDatabaseView::RefreshColumns()
{
	using namespace DebuggerDatabaseColumns;
	
	ActiveView.HeaderRow->ClearColumns();
	DatabaseView.HeaderRow->ClearColumns();
	
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

			DatabaseView.HeaderRow->AddColumn(ColumnArgs);
			
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
}

void SDebuggerDatabaseView::OnColumnWidthChanged(const float NewWidth, FName ColumnId) const
{
	check(Columns.Find(ColumnId));
	
	Columns[ColumnId]->Width = NewWidth;
}


void SDebuggerDatabaseView::SortDatabaseRows()
{
	if (SortMode == EColumnSortMode::Ascending)
	{
		DatabaseView.Rows.Sort(Columns[SortColumn]->GetSortPredicateAscending());
	}
	else if (SortMode == EColumnSortMode::Descending)
	{
		DatabaseView.Rows.Sort(Columns[SortColumn]->GetSortPredicateDescending());
	}

	DatabaseView.ListView->RequestListRefresh();
	DatabaseView.HeaderRow->RefreshColumns();
}

void SDebuggerDatabaseView::CreateRows(const UPoseSearchDatabase* Database)
{
	const int32 NumPoses = Database->SearchIndex.NumPoses;
	DatabaseView.Rows.Reserve(NumPoses);

	// Build database rows
	for(const FPoseSearchDatabaseSequence& DbSequence : Database->Sequences)
	{
		const int32 LastPoseIdx = DbSequence.FirstPoseIdx + DbSequence.NumPoses;
		for (int32 PoseIdx = DbSequence.FirstPoseIdx; PoseIdx != LastPoseIdx; ++PoseIdx)
		{
			TSharedRef<FDebuggerDatabaseRowData> Row = DatabaseView.Rows.Add_GetRef(MakeShared<FDebuggerDatabaseRowData>());
			Row->PoseIdx = PoseIdx;

			const FString SequenceName = DbSequence.Sequence->GetName();
			const float SequenceLength = DbSequence.Sequence->GetPlayLength();
			FFloatInterval Range = DbSequence.SamplingRange;

			// @TODO: Update this when range is computed natively as part of the sequence class
			const bool bSampleAll = (Range.Min == 0.0f) && (Range.Max == 0.0f);
			const float SequencePlayLength = DbSequence.Sequence->GetPlayLength();
			Range.Min = bSampleAll ? 0.0f : Range.Min;
			Range.Max = bSampleAll ? SequencePlayLength : FMath::Min(SequencePlayLength, Range.Max);
			// ---

			Row->AnimSequenceName = SequenceName;
			// Cap time in sequence to end of range
			Row->Time = FGenericPlatformMath::Min(Range.Min + (PoseIdx - DbSequence.FirstPoseIdx) * Database->Schema->SamplingInterval, Range.Max);
			Row->Length = SequenceLength;
		}
	}

	ActiveView.Rows.Add(MakeShared<FDebuggerDatabaseRowData>());
}

void SDebuggerDatabaseView::UpdateRows(const FTraceMotionMatchingStateMessage* State, const UPoseSearchDatabase* Database)
{
	if (DatabaseView.Rows.IsEmpty())
	{
		check(ActiveView.Rows.IsEmpty());
		CreateRows(Database);
	}
	check(ActiveView.Rows.Num() == 1);

	// Active bias weights pulled from the MM node
	FPoseSearchBiasWeights BiasWeights;
    BiasWeights.Weights = State->BiasWeights;

	const FPoseSearchFeatureVectorLayout& Layout = Database->Schema->Layout;
	const FPoseSearchIndex& SearchIndex = Database->SearchIndex;

	// Reverse engineer weight params from weights array
	auto ExtractWeight = [](
		const FPoseSearchFeatureVectorLayout& InLayout,
		const FPoseSearchBiasWeights& InBiasWeights,
		const EPoseSearchFeatureType InFeatureType,
		const bool bTrajectory
		) -> float
		{
			int32 FeatureIdx = INDEX_NONE;
			if (InLayout.EnumerateFeature(InFeatureType, bTrajectory, FeatureIdx))
			{
				const FPoseSearchFeatureDesc& Feature = InLayout.Features[FeatureIdx];
            	// Return first weight found associated with the feature
				// as the same weight is applied to all features in the buffer (for now?)
            	return InBiasWeights.Weights[Feature.ValueOffset];	
			}
			return 0.0f;
		};

	// @TODO: Compute alternate scores based on column visibility in view options
	// Zeroed trajectory for pose score exclusively
	FPoseSearchBiasWeightParams Params;
	Params.PosePositionWeight = ExtractWeight(Layout, BiasWeights, EPoseSearchFeatureType::Position, false);
	Params.PoseLinearVelocityWeight = ExtractWeight(Layout, BiasWeights, EPoseSearchFeatureType::LinearVelocity, false);
	Params.TrajectoryPositionWeight = 0.0f;
	Params.TrajectoryLinearVelocityWeight = 0.0f;
	
	FPoseSearchBiasWeights PoseBiasWeights;
	PoseBiasWeights.Init(Params, Layout);

	// Zeroed pose for trajectory score
	Params.PosePositionWeight = 0.0f;
	Params.PoseLinearVelocityWeight = 0.0f;
	Params.TrajectoryPositionWeight = ExtractWeight(Layout, BiasWeights, EPoseSearchFeatureType::Position, true);
	Params.TrajectoryLinearVelocityWeight = ExtractWeight(Layout, BiasWeights, EPoseSearchFeatureType::LinearVelocity, true);
	
	FPoseSearchBiasWeights TrajectoryBiasWeights;
	TrajectoryBiasWeights.Init(Params, Layout);
	
	const FPoseSearchBiasWeightsContext BiasWeightsContext { &BiasWeights, Database };
	const FPoseSearchBiasWeightsContext PoseBiasWeightsContext { &PoseBiasWeights, Database };
	const FPoseSearchBiasWeightsContext TrajectoryBiasWeightsContext { &TrajectoryBiasWeights, Database };
	
	for(const TSharedRef<FDebuggerDatabaseRowData>& Row : DatabaseView.Rows)
	{
		const int32 PoseIdx = Row->PoseIdx;
		Row->Score = ComparePoses(SearchIndex, PoseIdx, State->QueryVectorNormalized, &BiasWeightsContext);
		Row->PoseScore = ComparePoses(SearchIndex, PoseIdx, State->QueryVectorNormalized, &PoseBiasWeightsContext);
		Row->TrajectoryScore = ComparePoses(SearchIndex, PoseIdx, State->QueryVectorNormalized, &TrajectoryBiasWeightsContext);

		// If we are on the active pose for the frame
		if (PoseIdx == State->DbPoseIdx)
		{
			*ActiveView.Rows[0] = Row.Get();
		}
	}

	SortDatabaseRows();
}

TSharedRef<ITableRow> SDebuggerDatabaseView::HandleGenerateDatabaseRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SDebuggerDatabaseRow, OwnerTable, Item, DatabaseView.RowStyle, &DatabaseView.RowBrush, FMargin(0.0f, 2.0f, 6.0f, 2.0f));
}

TSharedRef<ITableRow> SDebuggerDatabaseView::HandleGenerateActiveRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SDebuggerDatabaseRow, OwnerTable, Item, ActiveView.RowStyle, &ActiveView.RowBrush, FMargin(0.0f, 2.0f, 6.0f, 4.0f));
}

void SDebuggerDatabaseView::Construct(const FArguments& InArgs)
{
	using namespace DebuggerDatabaseColumns;
	
	// @TODO: Support runtime reordering of these indices
	// Construct all column types
	AddColumn(MakeShared<FAnimSequenceName>(0));
	AddColumn(MakeShared<FPoseIdx>(1));
	AddColumn(MakeShared<FTime>(2));
	AddColumn(MakeShared<FLength>(3));
	AddColumn(MakeShared<FScore>(4));
	AddColumn(MakeShared<FPoseScore>(5));
	AddColumn(MakeShared<FTrajectoryScore>(6));

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

	// Database 
	DatabaseView.ScrollBar =
		SNew(SScrollBar)
		.Orientation(Orient_Vertical)
		.HideWhenNotInUse(false)
		.AlwaysShowScrollbar(true)
		.AlwaysShowScrollbarTrack(true);
	DatabaseView.HeaderRow = SNew(SHeaderRow).Visibility(EVisibility::Collapsed);
	
	DatabaseView.ListView = SNew(SListView<TSharedRef<FDebuggerDatabaseRowData>>)
			.ListItemsSource(&DatabaseView.Rows)
			.HeaderRow(DatabaseView.HeaderRow.ToSharedRef())
			.OnGenerateRow(this, &SDebuggerDatabaseView::HandleGenerateDatabaseRow)
			.ExternalScrollbar(DatabaseView.ScrollBar)
			.SelectionMode(ESelectionMode::Multi)
			.ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);

	DatabaseView.RowStyle = FEditorStyle::GetWidgetStyle<FTableRowStyle>("TableView.Row");
	// Set selected color to white to retain visibility when multi-selecting
	DatabaseView.RowStyle.SetSelectedTextColor(FLinearColor(FVector3f(0.8f)));
	DatabaseView.RowBrush = *FEditorStyle::GetBrush("ToolPanel.GroupBorder");
	
	ChildSlot
	[
		SNew(SWidgetSwitcher)
		+ SWidgetSwitcher::Slot()
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
							.Text(FText::FromString("Selected Poses"))	
						]
					]
					.AutoWidth()
					
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					[
						SNew(SBorder)
						.BorderImage(&DatabaseView.RowStyle.EvenRowBackgroundBrush)
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
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(0.0f)
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("NoBorder"))
						.Padding(0.0f)
						[
							DatabaseView.ListView.ToSharedRef()
						]
					]
					
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						DatabaseView.ScrollBar.ToSharedRef()
					]
				]
			]
		]
	];
	
	SortColumn = FPoseIdx::Name;
	SortMode = EColumnSortMode::Ascending;

	ActiveView.ScrollBar->SetVisibility(EVisibility::Hidden);

	// Assign the get column function on the database rows
	SDebuggerDatabaseRow::GetColumn = FGetColumnDelegate::CreateLambda([this](const FName& Name) -> TSharedRef<IColumn>&
	{
		return Columns[Name];
	});
	
	RefreshColumns();
}

void SDebuggerDetailsView::Construct(const FArguments& InArgs, UPoseSearchDebuggerReflection* Reflection)
{
	/** Construct this panel using the property editor */
	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	// @TODO: Hide arrays with zero elements in the detail view, if possible
	Details = PropPlugin.CreateDetailView(DetailsViewArgs);

	Details->SetObject(Reflection);
	
	ChildSlot
	[
		Details.ToSharedRef()
	];
}


void SDebuggerView::Construct(const FArguments& InArgs, uint64 InAnimInstanceId)
{
	MotionMatchingNodeIds = InArgs._MotionMatchingNodeIds;
	MotionMatchingState = InArgs._MotionMatchingState;
	Reflection = InArgs._Reflection;
	PoseSearchDatabase = InArgs._PoseSearchDatabase;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	IsPIESimulating = InArgs._IsPIESimulating;
	IsRecording = InArgs._IsRecording;
	RecordingDuration = InArgs._RecordingDuration;
	ActiveNodesNum = InArgs._ActiveNodesNum;
	OnUpdate = InArgs._OnUpdate;

	// Validate the existence of the passed getters
	check(MotionMatchingNodeIds.IsBound());
    check(MotionMatchingState.IsBound());
	check(Reflection.IsBound());
	check(PoseSearchDatabase.IsBound());
	check(OnSelectionChanged.IsBound());
	check(IsPIESimulating.IsBound())
    check(IsRecording.IsBound());
    check(RecordingDuration.IsBound());
	check(ActiveNodesNum.IsBound());
	check(OnUpdate.IsBound());
	
	AnimInstanceId = InAnimInstanceId;
	SelectedNode = -1;

	ChildSlot
	[
		SAssignNew(DebuggerView, SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			.WidgetIndex_Raw(this, &SDebuggerView::SelectView)

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

	const bool SameTime = FMath::Abs(InTimeMarker - TimeMarker) < DOUBLE_SMALL_NUMBER;
	TimeMarker = InTimeMarker;

	// We haven't reached the update point yet
	if (CurrentConsecutiveFrames < ConsecutiveFramesUpdateThreshold)
	{
		// If we're on the same time marker, it is consecutive
		if (SameTime)
		{
			++CurrentConsecutiveFrames;
		}
		return;
	}
	// New frame after having updated, reset consecutive frames count and start counting again
	if (!SameTime)
	{
		CurrentConsecutiveFrames = 0;
		bUpdated = false;
		return;
	}

	// Haven't updated since passing through frame gate, update once
	if (!bUpdated)
	{
		UpdateViews();
    	bUpdated = true;	
	}
}

void SDebuggerView::UpdateViews()
{
	OnUpdate.Execute(AnimInstanceId);
	
	// Update selection view if no node selected
	if (SelectedNode == -1)
	{
		TSet<int32> NodeIds = TSet<int32>(*MotionMatchingNodeIds.Get());

		// If we have a new set of nodes
		if (!NodeIds.Difference(StoredNodes).IsEmpty())
		{
			// Only one node active, bypass selection view
			if (NodeIds.Num() == 1)
			{
				SelectedNode = *NodeIds.begin();
				UpdateViews();
			}
			// Create selection view with buttons for each node, displaying the database name
			else
			{
				SelectionView->ClearChildren();
				for (int32 NodeId : NodeIds)
				{
					OnSelectionChanged.Execute(NodeId);
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
						.OnClicked_Raw(this, &SDebuggerView::UpdateNodeSelection, NodeId)
					];
				}
			}
		}
		
		// Update active node list
		StoredNodes = MoveTemp(NodeIds);
	}
	else
	{
		check(Reflection.Get());
    	OnSelectionChanged.Execute(SelectedNode);
		
    	const FTraceMotionMatchingStateMessage* State = MotionMatchingState.Get();
    	const UPoseSearchDatabase* Database = PoseSearchDatabase.Get();
    	if (!State || !Database)
    	{
    		return;
    	}
    
    	SwitcherViewType = Debugger;
    	DatabaseView->Update(State, Database);	
	}
}

int32 SDebuggerView::SelectView() const
{
	if (IsPIESimulating.Get())
	{
		if (IsRecording.Get())
		{
			return RecordingMsg;
		}
	}

	if (RecordingDuration.Get() < DOUBLE_SMALL_NUMBER)
	{
		return StoppedMsg;
	}

	const bool bNoActiveNodes = ActiveNodesNum.Get() == 0;
	const bool bNodeSelectedWithoutData = SelectedNode != INDEX_NONE && MotionMatchingState.Get() == nullptr;
	
	if (bNoActiveNodes || bNodeSelectedWithoutData)
    {
    	return NoDataMsg;
    }
	
	if (SelectedNode == INDEX_NONE)
	{
		return Selection;
	}

	return Debugger;
}

FReply SDebuggerView::UpdateNodeSelection(int32 InSelectedNode)
{
	// -1 will backtrack to selection view
	SelectedNode = InSelectedNode;
	UpdateViews();
	return FReply::Handled();
}

TSharedRef<SWidget> SDebuggerView::GenerateNoDataMessageView()
{
	TSharedRef<SWidget> ReturnButtonView = GenerateReturnButtonView();
	ReturnButtonView->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() -> EVisibility
	{
		// Hide the return button for the no data message if we have no nodes at all
		return ActiveNodesNum.Get() > 0 ? EVisibility::Visible : EVisibility::Hidden;
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
			.OnClicked_Raw(this, &SDebuggerView::UpdateNodeSelection, static_cast<int32>(INDEX_NONE))
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
			return ActiveNodesNum.Get() > 1 ? EVisibility::Visible : EVisibility::Collapsed;
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
			]
		]

		// Details panel view
		+ SSplitter::Slot()
		.Value(0.35f)
		[
			SAssignNew(DetailsView, SDebuggerDetailsView, Reflection.Get())
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


FDebugger* FDebugger::InternalInstance;
void FDebugger::Initialize()
{
	InternalInstance = new FDebugger;
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, InternalInstance);

	// Add property editor (detail view) UObject to world root so that it persists when PIE is stopped
	InternalInstance->Reflection = NewObject<UPoseSearchDebuggerReflection>();
	InternalInstance->Reflection->AddToRoot();
	
	check(IsValid(InternalInstance->Reflection));
}

void FDebugger::Shutdown()
{
	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropPlugin.UnregisterCustomClassLayout(UPoseSearchDebuggerReflection::StaticClass()->GetFName());

	// Our previously instantiated object attached to root may be cleaned up at this point
	if (UObjectInitialized())
	{
		InternalInstance->Reflection->RemoveFromRoot();
	}
	
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, InternalInstance);
	delete InternalInstance;
}

void FDebugger::Update(float DeltaTime, IRewindDebugger* InRewindDebugger)
{
	// Update active rewind debugger in use
	RewindDebugger = InRewindDebugger;
}

void FDebugger::UpdateReflection() const
{
	check(Reflection);
	if (!ActiveMotionMatchingState)
	{
		return;
	}

	Reflection->ElapsedPoseJumpTime = ActiveMotionMatchingState->ElapsedPoseJumpTime;

	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (Database == nullptr)
	{
		return;
	}
	// Validated by the get above
	const UPoseSearchSchema* Schema = Database->Schema;
	
	Reflection->PoseFeatures.EmptyAll();
	Reflection->DistanceTrajectoryFeatures.EmptyAll();
	Reflection->TimeTrajectoryFeatures.EmptyAll();

	FFeatureVectorReader Reader;
	// Ensure parity between Layout and QueryVector
	Reader.Init(&Schema->Layout);
	Reader.SetValues(ActiveMotionMatchingState->QueryVector);

	int32 NumSubsamples = Schema->PoseSampleTimes.Num();
	const int32 NumBones = Schema->Bones.Num();

	FPoseSearchFeatureDesc Feature;
	
	// Aggregate all features and place into the reflection struct
	auto Extract = [](
		const FFeatureVectorReader& InReader,
		const FPoseSearchFeatureDesc& InFeature,
		FPoseSearchDebuggerFeatureReflection& ReflectionRef
	)
	{
		FVector OutputVec;
		if (InReader.GetPosition(InFeature, &OutputVec))
		{
			ReflectionRef.Positions.Add(OutputVec);
		}
		if (InReader.GetLinearVelocity(InFeature, &OutputVec))
		{
			ReflectionRef.LinearVelocities.Add(OutputVec);
		}
		if (InReader.GetAngularVelocity(InFeature, &OutputVec))
        {
        	ReflectionRef.AngularVelocities.Add(OutputVec);
        }
	};

	// Pose samples
	Feature.Domain = EPoseSearchFeatureDomain::Time;
	for (int32 SchemaSubsampleIndex = 0; SchemaSubsampleIndex < NumSubsamples; ++SchemaSubsampleIndex)
	{
		Feature.SubsampleIdx = SchemaSubsampleIndex;

		for(int32 SchemaBoneIdx = 0; SchemaBoneIdx < NumBones; ++SchemaBoneIdx)
		{
			Feature.SchemaBoneIdx = SchemaBoneIdx;
			Extract(Reader, Feature, Reflection->PoseFeatures);
		}
	}

	// Used for classifying trajectories instead of bones, special index
	Feature.SchemaBoneIdx = FPoseSearchFeatureDesc::TrajectoryBoneIndex;
	
	// Trajectory time samples
	NumSubsamples = Schema->TrajectorySampleTimes.Num();
	for (int32 SchemaSubsampleIndex = 0; SchemaSubsampleIndex < NumSubsamples; ++SchemaSubsampleIndex)
	{
		Feature.SubsampleIdx = SchemaSubsampleIndex;
		Extract(Reader, Feature, Reflection->TimeTrajectoryFeatures);
	}
	// Trajectory distance samples
	NumSubsamples = Schema->TrajectorySampleDistances.Num();
	Feature.Domain = EPoseSearchFeatureDomain::Distance;
	for (int32 SchemaSubsampleIndex = 0; SchemaSubsampleIndex < NumSubsamples; ++SchemaSubsampleIndex)
	{
		Feature.SubsampleIdx = SchemaSubsampleIndex;
		Extract(Reader, Feature, Reflection->DistanceTrajectoryFeatures);
	}
}

void FDebugger::UpdateMotionMatchingStates(uint64 InAnimInstanceId)
{
	NodeIds.Empty();
	MotionMatchingStates.Empty();
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


const FTraceMotionMatchingStateMessage* FDebugger::GetMotionMatchingState() const
{
	return ActiveMotionMatchingState;
}

const UPoseSearchDatabase* FDebugger::GetPoseSearchDatabase() const
{
	if (!InternalInstance->ActiveMotionMatchingState)
	{
		return nullptr;
	}

	const uint64 DatabaseId = InternalInstance->ActiveMotionMatchingState->DatabaseId;
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

UPoseSearchDebuggerReflection* FDebugger::GetReflection() const
{
	return Reflection;
}

bool FDebugger::IsPIESimulating() const
{
	return RewindDebugger->IsPIESimulating();
}

const TArray<int32>* FDebugger::GetNodeIds() const
{
	return &NodeIds;
}

bool FDebugger::IsRecording() const
{
	return RewindDebugger->IsRecording();
}

double FDebugger::GetRecordingDuration() const
{
	return RewindDebugger->GetRecordingDuration();
}

int32 FDebugger::GetActiveNodesNum() const
{
	return MotionMatchingStates.Num();
}

void FDebugger::OnUpdate(uint64 InAnimInstanceId)
{
	UpdateMotionMatchingStates(InAnimInstanceId);
}

void FDebugger::OnSelectionChanged(int32 InNodeId)
{
	bool bFound = false;
	// Find node in all motion matching states this frame
	const int32 NodesNum = NodeIds.Num();
	for(int i = 0; i < NodesNum; ++i)
	{
		if (NodeIds[i] == InNodeId)
		{
			ActiveMotionMatchingState = MotionMatchingStates[i];
			bFound = true;
			break;
		}
	}

	if (bFound)
	{
		UpdateReflection();
	}
	else
	{
		ActiveMotionMatchingState = nullptr;
	}
}

TSharedPtr<SDebuggerView> FDebugger::GenerateView(uint64 InAnimInstanceId)
{
	return
		SNew(SDebuggerView, InAnimInstanceId)
		.MotionMatchingNodeIds_Raw(Instance(), &FDebugger::GetNodeIds)
		.MotionMatchingState_Raw(Instance(), &FDebugger::GetMotionMatchingState)
		.Reflection_Raw(Instance(), &FDebugger::GetReflection)
		.PoseSearchDatabase_Raw(Instance(), &FDebugger::GetPoseSearchDatabase)
		.IsPIESimulating_Raw(Instance(), &FDebugger::IsPIESimulating)
		.IsRecording_Raw(Instance(), &FDebugger::IsRecording)
		.RecordingDuration_Raw(Instance(), &FDebugger::GetRecordingDuration)
		.ActiveNodesNum_Raw(Instance(), &FDebugger::GetActiveNodesNum)
		.OnSelectionChanged_Raw(Instance(), &FDebugger::OnSelectionChanged)
		.OnUpdate_Raw(Instance(), &FDebugger::OnUpdate);
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
	return FDebugger::GenerateView(ObjectId);
}

FName FDebuggerViewCreator::GetName() const
{
	static const FName Name("PoseSearchDebugger");
	return Name;
}

}}
