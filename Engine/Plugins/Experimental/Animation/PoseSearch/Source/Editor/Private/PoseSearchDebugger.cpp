// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDebugger.h"
#include "IAnimationProvider.h"
#include "IGameplayProvider.h"
#include "PoseSearch/PoseSearch.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "Animation/MirrorDataTable.h"
#include "RewindDebuggerInterface/Public/IRewindDebugger.h"
#include "ObjectTrace.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Trace/PoseSearchTraceProvider.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "TraceServices/Model/Frames.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
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

void UPoseSearchMeshComponent::Initialize(const FTransform& InComponentToWorld)
{
	SetComponentToWorld(InComponentToWorld);
	const FReferenceSkeleton& SkeletalMeshRefSkeleton = SkeletalMesh->GetRefSkeleton();

	// set up bone visibility states as this gets skipped since we allocate the component array before registration
	for (int32 BaseIndex = 0; BaseIndex < 2; ++BaseIndex)
	{
		BoneVisibilityStates[BaseIndex].SetNum(SkeletalMeshRefSkeleton.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < SkeletalMeshRefSkeleton.GetNum(); BoneIndex++)
		{
			BoneVisibilityStates[BaseIndex][BoneIndex] = BVS_ExplicitlyHidden;
		}
	}

	StartingTransform = InComponentToWorld;
	Refresh();
}

void UPoseSearchMeshComponent::Refresh()
{
	// Flip buffers once to copy the directly-written component space transforms
	bNeedToFlipSpaceBaseBuffers = true;
	bHasValidBoneTransform = false;
	FlipEditableSpaceBases();
	bHasValidBoneTransform = true;

	InvalidateCachedBounds();
	UpdateBounds();
	MarkRenderTransformDirty();
	MarkRenderDynamicDataDirty();
	MarkRenderStateDirty();
}

void UPoseSearchMeshComponent::ResetToStart()
{
	SetComponentToWorld(StartingTransform);
	Refresh();
}

void UPoseSearchMeshComponent::UpdatePose(const FUpdateContext& UpdateContext)
{
	FMemMark Mark(FMemStack::Get());

	FCompactPose CompactPose;
	CompactPose.SetBoneContainer(&RequiredBones);
	FBlendedCurve Curve;
	Curve.InitFrom(RequiredBones);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData PoseData(CompactPose, Curve, Attributes);

	float AdvancedTime = UpdateContext.SequenceStartTime;
	FAnimationRuntime::AdvanceTime(
		UpdateContext.bLoop,
		UpdateContext.SequenceTime - UpdateContext.SequenceStartTime,
		AdvancedTime,
		UpdateContext.Sequence->GetPlayLength());

	FAnimExtractContext ExtractionCtx;
	ExtractionCtx.CurrentTime = AdvancedTime;

	UpdateContext.Sequence->GetAnimationPose(PoseData, ExtractionCtx);
	if (UpdateContext.bMirrored)
	{
		FAnimationRuntime::MirrorPose(
			CompactPose, 
			UpdateContext.MirrorDataTable->MirrorAxis, 
			*UpdateContext.CompactPoseMirrorBones, 
			*UpdateContext.ComponentSpaceRefRotations);
	}

	FCSPose<FCompactPose> ComponentSpacePose;
	ComponentSpacePose.InitPose(CompactPose);

	for (const FBoneIndexType BoneIndex : RequiredBones.GetBoneIndicesArray())
	{
		const FTransform BoneTransform = 
			ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(BoneIndex));

		FSkeletonPoseBoneIndex SkeletonBoneIndex =
			RequiredBones.GetSkeletonPoseIndexFromCompactPoseIndex(FCompactPoseBoneIndex(BoneIndex));
		FName BoneName = 
			RequiredBones.GetSkeletonAsset()->GetReferenceSkeleton().GetBoneName(SkeletonBoneIndex.GetInt());
		SetBoneTransformByName(BoneName, BoneTransform, EBoneSpaces::ComponentSpace);
	}

	LastRootMotionDelta = UpdateContext.Sequence->ExtractRootMotion(
		UpdateContext.SequenceStartTime, 
		UpdateContext.SequenceTime - UpdateContext.SequenceStartTime,
		UpdateContext.bLoop);

	if (UpdateContext.bMirrored)
	{
		const EAxis::Type MirrorAxis = UpdateContext.MirrorDataTable->MirrorAxis;
		FVector T = LastRootMotionDelta.GetTranslation();
		T = FAnimationRuntime::MirrorVector(T, MirrorAxis);
		const FQuat ReferenceRotation = (*UpdateContext.ComponentSpaceRefRotations)[FCompactPoseBoneIndex(0)];
		FQuat Q = LastRootMotionDelta.GetRotation();
		Q = FAnimationRuntime::MirrorQuat(Q, MirrorAxis);
		Q *= FAnimationRuntime::MirrorQuat(ReferenceRotation, MirrorAxis).Inverse() * ReferenceRotation;
		LastRootMotionDelta = FTransform(Q, T, LastRootMotionDelta.GetScale3D());
	}

	const FTransform ComponentTransform = LastRootMotionDelta * StartingTransform;

	SetComponentToWorld(ComponentTransform);
	FillComponentSpaceTransforms();
	Refresh();
}


namespace UE::PoseSearch {

class FDebuggerDatabaseRowData : public TSharedFromThis<FDebuggerDatabaseRowData>
{
public:
	FDebuggerDatabaseRowData() = default;
	
	float GetPoseCost() const { return PoseCostDetails.ChannelCosts.Num() ? PoseCostDetails.ChannelCosts[0] : 0.0f; }
	float GetTrajectoryCost() const { return PoseCostDetails.ChannelCosts.Num() >= 3 ? PoseCostDetails.ChannelCosts[1] + PoseCostDetails.ChannelCosts[2] : 0.0f; }
	float GetAddendsCost() const { return PoseCostDetails.NotifyCostAddend + PoseCostDetails.MirrorMismatchCostAddend; }

	int32 PoseIdx = 0;
	FString AnimSequenceName = "";
	FString AnimSequencePath = "";
	int32 DbSequenceIdx = 0;
	int32 AnimFrame = 0;
	float Time = 0.0f;
	bool bMirrored = false;
	FPoseCostDetails PoseCostDetails;
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

		virtual FSortPredicate GetSortPredicate() const = 0;

		virtual TSharedRef<SWidget> GenerateWidget(const FRowDataRef& RowData) const = 0;
	};

	/** Column struct to represent each column in the debugger database */
	struct ITextColumn : IColumn
	{
		explicit ITextColumn(int32 InSortIndex, bool InEnabled = true)
			: IColumn(InSortIndex, InEnabled)
		{
		}

		virtual ~ITextColumn() = default;
		
		virtual TSharedRef<SWidget> GenerateWidget(const FRowDataRef& RowData) const override
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

	struct FPoseIdx : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseIdx < Row1->PoseIdx; };
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
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->AnimSequenceName < Row1->AnimSequenceName; };
			return Predicate;
		}

		virtual TSharedRef<SWidget> GenerateWidget(const FRowDataRef& RowData) const override
		{
			return SNew(SHyperlink)
				.Text_Lambda([RowData]() -> FText { return FText::FromString(RowData->AnimSequenceName); })
				.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
				.ToolTipText_Lambda([RowData]() -> FText 
					{ 
						return FText::Format(
							LOCTEXT("AssetHyperlinkTooltipFormat", "Open asset '{0}'"), 
							FText::FromString(RowData->AnimSequencePath)); 
					})
				.OnNavigate_Lambda([RowData]()
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(RowData->AnimSequencePath);
					});
		}
	};
	const FName FAnimSequenceName::Name = "Sequence";

	struct FFrame : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			static FSortPredicate Predicate = [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->Time < Row1->Time; };
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

	struct FCost : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseCostDetails.PoseCost < Row1->PoseCostDetails.PoseCost; };
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
        {
        	return FText::AsNumber(Row->PoseCostDetails.PoseCost.TotalCost);
        }
	};
	const FName FCost::Name = "Cost";


	struct FPoseCostColumn : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetPoseCost() < Row1->GetPoseCost(); };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetPoseCost());
		}
	};
	const FName FPoseCostColumn::Name = "Pose Cost";


	struct FTrajectoryCost : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetTrajectoryCost() < Row1->GetTrajectoryCost(); };
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetTrajectoryCost());
        }
	};
	const FName FTrajectoryCost::Name = "Trajectory Cost";

	struct FCostModifier : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool 
			{ 
				return Row0->GetAddendsCost() < Row1->GetAddendsCost();
			};
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetAddendsCost());
		}
	};
	const FName FCostModifier::Name = "Cost Modifier";

	struct FMirrored : ITextColumn
	{
		using ITextColumn::ITextColumn;
		static const FName Name;
		virtual FName GetName() const override { return Name; }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool 
			{ 
				return Row0->bMirrored < Row1->bMirrored; 
			};
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
		const TSharedRef<SWidget> Widget = Column->GenerateWidget(Row.ToSharedRef());
		
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

const TSharedRef<FDebuggerDatabaseRowData>& SDebuggerDatabaseView::GetPoseIdxDatabaseRow(int32 PoseIdx) const
{
	const TSharedRef<FDebuggerDatabaseRowData>* RowPtr = UnfilteredDatabaseRows.FindByPredicate(
		[=](TSharedRef<FDebuggerDatabaseRowData>& Row)
	{
		return Row->PoseIdx == PoseIdx;
	});

	check(RowPtr != nullptr);

	return *RowPtr;
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


void SDebuggerDatabaseView::OnDatabaseRowSelectionChanged(
	TSharedPtr<FDebuggerDatabaseRowData> Row, 
	ESelectInfo::Type SelectInfo)
{
	if (Row.IsValid())
	{
		OnPoseSelectionChanged.ExecuteIfBound(Row->PoseIdx, Row->Time);
	}
}

ECheckBoxState SDebuggerDatabaseView::IsSequenceFilterEnabled() const
{
	return bSequenceFilterEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SDebuggerDatabaseView::OnSequenceFilterEnabledChanged(ECheckBoxState NewState)
{
	bSequenceFilterEnabled = NewState == ECheckBoxState::Checked;
	FilterDatabaseRows();
}

void SDebuggerDatabaseView::SortDatabaseRows()
{
	if (SortMode == EColumnSortMode::Ascending)
	{
		UnfilteredDatabaseRows.Sort(Columns[SortColumn]->GetSortPredicate());
	}
	else if (SortMode == EColumnSortMode::Descending)
	{
		auto DescendingPredicate = [this](const auto& Lhs, const auto& Rhs) -> bool
		{
			return !Columns[SortColumn]->GetSortPredicate()(Lhs, Rhs);
		};

		UnfilteredDatabaseRows.Sort(DescendingPredicate);
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
			if (!bSequenceFilterEnabled || DatabaseSequenceFilter[UnfilteredRow->DbSequenceIdx])
			{
				FilteredDatabaseView.Rows.Add(UnfilteredRow);
			}
		}
	}
	else
	{
		for (const auto& UnfilteredRow : UnfilteredDatabaseRows)
		{
			if (!bSequenceFilterEnabled || DatabaseSequenceFilter[UnfilteredRow->DbSequenceIdx])
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
	}

	FilteredDatabaseView.ListView->RequestListRefresh();
}

void SDebuggerDatabaseView::CreateRows(const UPoseSearchDatabase& Database)
{
	const int32 NumPoses = Database.SearchIndex.NumPoses;
	UnfilteredDatabaseRows.Reset(NumPoses);

	RowsSourceDatabase = &Database;

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
			Row->AnimSequencePath = DbSequence.Sequence->GetPathName();
			Row->DbSequenceIdx = SearchIndexAsset.SourceAssetIdx;
			Row->Time = Time;
			Row->AnimFrame = DbSequence.Sequence->GetFrameAtTime(Time);
			Row->bMirrored = SearchIndexAsset.bMirrored;
		}
	}

	ActiveView.Rows.Reset();
	ActiveView.Rows.Add(MakeShared<FDebuggerDatabaseRowData>());
}

void SDebuggerDatabaseView::UpdateRows(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database)
{
	const bool bNewDatabase = RowsSourceDatabase != &Database;

	if (bNewDatabase || UnfilteredDatabaseRows.IsEmpty())
	{
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

		ComparePoses(PoseIdx, SearchContext, Row->PoseCostDetails);

		// If we are on the active pose for the frame
		if (PoseIdx == State.DbPoseIdx)
		{
			*ActiveView.Rows[0] = Row.Get();
		}
	}

	DatabaseSequenceFilter = State.DatabaseSequenceFilter;

	SortDatabaseRows();
	FilterDatabaseRows();

	if (bNewDatabase)
	{
		FilteredDatabaseView.ListView->ClearSelection();
	}
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
	using namespace DebuggerDatabaseColumns;

	ParentDebuggerViewPtr = InArgs._Parent;
	OnPoseSelectionChanged = InArgs._OnPoseSelectionChanged;
	check(OnPoseSelectionChanged.IsBound());

	// @TODO: Support runtime reordering of these indices
	// Construct all column types
	AddColumn(MakeShared<FAnimSequenceName>(0));
	AddColumn(MakeShared<FCost>(1));
	AddColumn(MakeShared<FPoseCostColumn>(2));
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
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(10, 5, 10, 5)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SDebuggerDatabaseView::IsSequenceFilterEnabled)
					.OnCheckStateChanged(this, &SDebuggerDatabaseView::OnSequenceFilterEnabledChanged)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PoseSearchDebuggerGroupFiltering", "Apply Group Filtering"))
					]
				]
				+ SHorizontalBox::Slot()
				[
					SAssignNew(FilterBox, SSearchBox)
					.OnTextChanged(this, &SDebuggerDatabaseView::OnFilterTextChanged)
				]
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

	Reflection->AssetPlayerSequenceName = FString();
	if (const FPoseSearchIndexAsset* IndexAsset = Database.SearchIndex.FindAssetForPose(State.DbPoseIdx))
	{
		Reflection->AssetPlayerSequenceName = Database.GetSourceAsset(IndexAsset).Sequence->GetName();
	}

	Reflection->AssetPlayerTime = State.AssetPlayerTime;
	Reflection->LastDeltaTime = State.DeltaTime;
	Reflection->SimLinearVelocity = State.SimLinearVelocity;
	Reflection->SimAngularVelocity = State.SimAngularVelocity;
	Reflection->AnimLinearVelocity = State.AnimLinearVelocity;
	Reflection->AnimAngularVelocity = State.AnimAngularVelocity;

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

			Pose = Selected->PoseCostDetails.CostVector;
			//Database.SearchIndex.InverseNormalize(Pose);
			Reader.SetValues(Pose);
			Reflection->CostVector.ExtractFeatures(Reader);

			const TSharedRef<FDebuggerDatabaseRowData>& ActiveRow = 
				DebuggerView->GetPoseIdxDatabaseRow(State.DbPoseIdx);

			TArray<float> ActiveCostDifference(Pose);
			for (int i = 0; i < ActiveCostDifference.Num(); ++i)
			{
				ActiveCostDifference[i] -= ActiveRow->PoseCostDetails.CostVector[i];
			}

			Reader.SetValues(ActiveCostDifference);
			Reflection->CostVectorDifference.ExtractFeatures(Reader);
		}
	}
}

void SDebuggerView::Construct(const FArguments& InArgs, uint64 InAnimInstanceId)
{
	ViewModel = InArgs._ViewModel;
	OnViewClosed = InArgs._OnViewClosed;

	

	// Validate the existence of the passed getters
	check(ViewModel.IsBound())
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
	if (FDebugger::IsPIESimulating())
	{
		return;
	}

	TimeMarker = InTimeMarker;
}

void SDebuggerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (FDebugger::IsPIESimulating())
	{
		return;
	}

	const UWorld* DebuggerWorld = FDebugger::GetWorld();
    check(DebuggerWorld);
	
	// @TODO: Handle editor world when those features are enabled for the Rewind Debugger
	// Currently prevents debug draw remnants from stopped world
	if (DebuggerWorld->WorldType != EWorldType::PIE)
	{
		return;
	}
	
	const bool bSameTime = FMath::Abs(TimeMarker - PreviousTimeMarker) < DOUBLE_SMALL_NUMBER;
	PreviousTimeMarker = TimeMarker;

	TSharedPtr<FDebuggerViewModel> Model = ViewModel.Get();

	bool bNeedUpdate = Model->NeedsUpdate();

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
		Model->OnUpdate();
		if (UpdateSelection())
		{
			Model->OnUpdateNodeSelection(SelectedNodeId);
			UpdateViews();
		}
		bUpdated = true;
	}

	Model->UpdateAnimSequence();
	
	// Draw visualization every tick
	DrawVisualization();
}

bool SDebuggerView::UpdateSelection()
{
	TSharedPtr<FDebuggerViewModel> Model = ViewModel.Get();

	// Update selection view if no node selected
	bool bNodeSelected = SelectedNodeId != INDEX_NONE;
	if (!bNodeSelected)
	{
		const TArray<int32>& NodeIds = *Model->GetNodeIds();
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
				Model->OnUpdateNodeSelection(NodeId);
				SelectionView->AddSlot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(10.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(Model->GetPoseSearchDatabase()->GetName()))
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
	const FTraceMotionMatchingStateMessage* State = ViewModel.Get()->GetMotionMatchingState();
	const UPoseSearchDatabase* Database = ViewModel.Get()->GetPoseSearchDatabase();
	if (State && Database)
	{
		DatabaseView->Update(*State, *Database);
		DetailsView->Update(*State, *Database);
	}
}

void SDebuggerView::DrawVisualization() const
{
	const UWorld* DebuggerWorld = FDebugger::GetWorld();
	check(DebuggerWorld);

	const FTraceMotionMatchingStateMessage* State = ViewModel.Get()->GetMotionMatchingState();
	const UPoseSearchDatabase* Database = ViewModel.Get()->GetPoseSearchDatabase();
	const FTransform* Transform = ViewModel.Get()->GetRootTransform();
	if (State && Database && Transform)
	{
		DrawFeatures(*DebuggerWorld, *State, *Database, *Transform);
	}
}

TArray<TSharedRef<FDebuggerDatabaseRowData>> SDebuggerView::GetSelectedDatabaseRows() const
{
	return DatabaseView->GetDatabaseRows()->GetSelectedItems();
}

const TSharedRef<FDebuggerDatabaseRowData>& SDebuggerView::GetPoseIdxDatabaseRow(int32 PoseIdx) const
{
	return DatabaseView->GetPoseIdxDatabaseRow(PoseIdx);
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

	FSkeletonDrawParams SkeletonDrawParams;
	const bool bDrawSelectedPose = Reflection->bDrawSelectedSkeleton;
	if (bDrawSelectedPose)
	{
		SkeletonDrawParams.Flags |= ESkeletonDrawFlags::SelectedPose;
	}

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

	if (Reflection->bDrawActiveSkeleton)
	{
		SkeletonDrawParams.Flags |= ESkeletonDrawFlags::ActivePose;
	}

	SkeletonDrawParams.Flags |= ESkeletonDrawFlags::AnimSequence;

	ViewModel.Get()->OnDraw(SkeletonDrawParams);
}

int32 SDebuggerView::SelectView() const
{
	// Currently recording
	if (FDebugger::IsPIESimulating() && FDebugger::IsRecording())
	{
		return RecordingMsg;
	}

	// Data has not been recorded yet
	if (FDebugger::GetRecordingDuration() < DOUBLE_SMALL_NUMBER)
	{
		return StoppedMsg;
	}

	const TSharedPtr<FDebuggerViewModel> Model = ViewModel.Get();

	const bool bNoActiveNodes = Model->GetNodesNum() == 0;
	const bool bNodeSelectedWithoutData = SelectedNodeId != INDEX_NONE && Model->GetMotionMatchingState() == nullptr;

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

void SDebuggerView::OnPoseSelectionChanged(int32 PoseIdx, float Time)
{
	const TSharedPtr<FDebuggerViewModel> Model = ViewModel.Get();
	const FTraceMotionMatchingStateMessage* State = Model->GetMotionMatchingState();
	const UPoseSearchDatabase* Database = Model->GetPoseSearchDatabase();

	if (State && Database)
	{
		DetailsView->Update(*State, *Database);
	}
	
	if (PoseIdx == INDEX_NONE)
	{
		Model->ClearSelectedSkeleton();
	}
	else
	{
		Model->ShowSelectedSkeleton(PoseIdx, Time);
		// Stop sequence player when switching selections
		Model->StopSelection();
	}
}

FReply SDebuggerView::OnUpdateNodeSelection(int32 InSelectedNodeId)
{
	// -1 will backtrack to selection view
	SelectedNodeId = InSelectedNodeId;
	bUpdated = false;
	return FReply::Handled();
}

FReply SDebuggerView::TogglePlaySelectedSequences() const
{
	const TSharedPtr<SListView<TSharedRef<FDebuggerDatabaseRowData>>>& DatabaseRows = DatabaseView->GetDatabaseRows();
	TArray<TSharedRef<FDebuggerDatabaseRowData>> Selected = DatabaseRows->GetSelectedItems();
	const bool bPlaying = ViewModel.Get()->IsPlayingSelections();
	if (!bPlaying)
	{
		if (!Selected.IsEmpty())
		{
			// @TODO: Make functional with multiple poses being selected
			ViewModel.Get()->PlaySelection(Selected[0]->PoseIdx, Selected[0]->Time);
		}
	}
	else
	{
		ViewModel.Get()->StopSelection();
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SDebuggerView::GenerateNoDataMessageView()
{
	TSharedRef<SWidget> ReturnButtonView = GenerateReturnButtonView();
	ReturnButtonView->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() -> EVisibility
	{
		// Hide the return button for the no data message if we have no nodes at all
		return ViewModel.Get()->GetNodesNum() > 0 ? EVisibility::Visible : EVisibility::Hidden;
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

TSharedRef<SHorizontalBox> SDebuggerView::GenerateReturnButtonView()
{
	return
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.Padding(10, 5, 0, 0)
		.AutoWidth()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.Visibility_Lambda([this] { return ViewModel.Get()->GetNodesNum() > 1 ? EVisibility::Visible : EVisibility::Hidden; })
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
	TSharedRef<SHorizontalBox> ReturnButtonView = GenerateReturnButtonView();
	ReturnButtonView->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Fill)
	.Padding(32, 5, 0, 0)
	.AutoWidth()
	[
		SNew(SButton)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Fill)
		.ButtonStyle(FEditorStyle::Get(), "Button")
		.ContentPadding( FMargin(5, 0) )
		.OnClicked(this, &SDebuggerView::TogglePlaySelectedSequences)
		[
			SNew(SHorizontalBox)
			// Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SImage)
				.Image_Lambda([this]
				{
					const bool bPlayingSelections = ViewModel.Get()->IsPlayingSelections();
					return FSlateIcon("FEditorStyle", bPlayingSelections ? "PlayWorld.StopPlaySession.Small" : "PlayWorld.PlayInViewport.Small").GetSmallIcon();
				})
			]
			// Text
			+ SHorizontalBox::Slot()
			.Padding(FMargin(8, 0, 0, 0))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return ViewModel.Get()->IsPlayingSelections() ? FText::FromString("Stop Selected Sequence") : FText::FromString("Play Selected Sequence"); })
				.Justification(ETextJustify::Center)
			]
		]
	];
	
	ReturnButtonView->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	.Padding(64, 5, 0, 0)
	.AutoWidth()
	[
		SNew(SHorizontalBox)
		
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 5, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString("Sequence Play Rate: "))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8, 0, 0, 0)
		[
			SNew(SNumericEntryBox<float>)
			.MinValue(0.0f)
			.MaxValue(5.0f)
			.MinSliderValue(0.0f)
			.MaxSliderValue(5.0f)
			.Delta(0.01f)
			.AllowSpin(true)
			// Lambda to accomodate the TOptional this requires (for now)
			.Value_Lambda([this] { return ViewModel.Get()->GetPlayRate(); })
			.OnValueChanged(ViewModel.Get().ToSharedRef(), &FDebuggerViewModel::ChangePlayRate)	
		]
	];

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

FDebuggerViewModel::FDebuggerViewModel(uint64 InAnimInstanceId)
	: AnimInstanceId(InAnimInstanceId)
{
	Skeletons.AddDefaulted(ESkeletonIndex::Num);
}

FDebuggerViewModel::~FDebuggerViewModel()
{
	for (FSkeleton& Skeleton : Skeletons)
	{
		if (Skeleton.Actor.IsValid())
		{
			Skeleton.Actor->Destroy();
		}
	}

	Skeletons.Empty();
}

const FTraceMotionMatchingStateMessage* FDebuggerViewModel::GetMotionMatchingState() const
{
	return ActiveMotionMatchingState;
}

const UPoseSearchDatabase* FDebuggerViewModel::GetPoseSearchDatabase() const
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

const FPoseSearchDatabaseSequence* FDebuggerViewModel::GetAnimSequence(int32 SequenceIdx) const
{
	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (Database && Database->Sequences.IsValidIndex(SequenceIdx))
	{
		const FPoseSearchDatabaseSequence* DatabaseSequence = &Database->Sequences[SequenceIdx];
		if (DatabaseSequence)
		{
			return DatabaseSequence;
		}
	}
	return nullptr;
}

void FDebuggerViewModel::ShowSelectedSkeleton(int32 PoseIdx, float Time)
{
	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (!Database)
	{
		return;
	}
	UPoseSearchMeshComponent* Component = Skeletons[SelectedPose].Component;
	if (!Component)
	{
		return;
	}

	Component->ResetToStart();
	bSelecting = true;
	const FPoseSearchIndexAsset* Asset = Database->SearchIndex.FindAssetForPose(PoseIdx);
	Skeletons[SelectedPose].SequenceIdx = Asset->SourceAssetIdx;
	Skeletons[SelectedPose].Time = Time;
	Skeletons[SelectedPose].bMirrored = Asset->bMirrored;
}

void FDebuggerViewModel::ClearSelectedSkeleton()
{
	bSelecting = false;
}

const TArray<int32>* FDebuggerViewModel::GetNodeIds() const
{
	return &NodeIds;
}

int32 FDebuggerViewModel::GetNodesNum() const
{
	return MotionMatchingStates.Num();
}

const FTransform* FDebuggerViewModel::GetRootTransform() const
{
	return RootTransform;
}

bool FDebuggerViewModel::NeedsUpdate() const
{
	const UPoseSearchDatabase* NewDatabase = GetPoseSearchDatabase();
	const bool bDatabaseChanged = NewDatabase != CurrentDatabase;
	return bDatabaseChanged;
}

void FDebuggerViewModel::OnUpdate()
{
	if (!bSkeletonsInitialized)
	{
		UWorld* World = RewindDebugger.Get()->GetWorldToVisualize();
		for (FSkeleton& Skeleton : Skeletons)
		{
			FActorSpawnParameters ActorSpawnParameters;
			ActorSpawnParameters.bHideFromSceneOutliner = false;
			ActorSpawnParameters.ObjectFlags |= RF_Transient;
			Skeleton.Actor = World->SpawnActor<AActor>(ActorSpawnParameters);
			Skeleton.Actor->SetActorLabel(TEXT("PoseSearch"));
			Skeleton.Component = NewObject<UPoseSearchMeshComponent>(Skeleton.Actor.Get());
			Skeleton.Actor->AddInstanceComponent(Skeleton.Component);
			Skeleton.Component->RegisterComponentWithWorld(World);
		}
		FWorldDelegates::OnWorldCleanup.AddRaw(this, &FDebuggerViewModel::OnWorldCleanup);
		bSkeletonsInitialized = true;
	}

	UpdateFromTimeline();
}

void FDebuggerViewModel::OnUpdateNodeSelection(int32 InNodeId)
{
	if (InNodeId == INDEX_NONE)
	{
		return;
	}

	ActiveMotionMatchingState = nullptr;

	// Find node in all motion matching states this frame
	const int32 NodesNum = NodeIds.Num();
	for (int i = 0; i < NodesNum; ++i)
	{
		if (NodeIds[i] == InNodeId)
		{
			ActiveMotionMatchingState = MotionMatchingStates[i];
			break;
		}
	}

	const UPoseSearchDatabase* NewDatabase = GetPoseSearchDatabase();

	if (ActiveMotionMatchingState)
	{
		//todo: this seems wrong, sequenceidx == poseidx? let's test doing it right
		//Skeletons[ActivePose].SequenceIdx = ActiveMotionMatchingState->DbPoseIdx;

		const FPoseSearchIndexAsset* Asset = 
			NewDatabase->SearchIndex.FindAssetForPose(ActiveMotionMatchingState->DbPoseIdx);
		Skeletons[ActivePose].SequenceIdx = Asset->SourceAssetIdx;
	}

	if (NewDatabase != CurrentDatabase)
	{
		ClearSelectedSkeleton();
		CurrentDatabase = NewDatabase;
	}
}

void FDebuggerViewModel::OnDraw(FSkeletonDrawParams& DrawParams)
{
	// Returns if it is to be drawn this frame
	auto SetDrawSkeleton = [this](UPoseSearchMeshComponent* InComponent, bool bDraw)
	{
		const bool bIsDrawingSkeleton = InComponent->ShouldDrawDebugSkeleton();
		if (bIsDrawingSkeleton != bDraw)
		{
			InComponent->SetDrawDebugSkeleton(bDraw);
		}
		InComponent->MarkRenderStateDirty();
	};
	const bool bDrawActivePose = EnumHasAnyFlags(DrawParams.Flags, ESkeletonDrawFlags::ActivePose);
	SetDrawSkeleton(Skeletons[ActivePose].Component, bDrawActivePose);
	// If flag is set and we are currently in a valid drawing state
	const bool bDrawSelectedPose = EnumHasAnyFlags(DrawParams.Flags, ESkeletonDrawFlags::SelectedPose) && bSelecting;
	SetDrawSkeleton(Skeletons[SelectedPose].Component, bDrawSelectedPose);

	FillCompactPoseAndComponentRefRotations();

	UPoseSearchMeshComponent::FUpdateContext UpdateContext;
	UpdateContext.MirrorDataTable = GetPoseSearchDatabase()->Schema->MirrorDataTable;
	UpdateContext.CompactPoseMirrorBones = &CompactPoseMirrorBones;
	UpdateContext.ComponentSpaceRefRotations = &ComponentSpaceRefRotations;

	if (bDrawSelectedPose)
	{
		UPoseSearchMeshComponent* Component = Skeletons[SelectedPose].Component;
		const FPoseSearchDatabaseSequence* DatabaseSequence = GetAnimSequence(Skeletons[SelectedPose].SequenceIdx);
		UAnimSequence* Sequence = DatabaseSequence->Sequence;

		UpdateContext.Sequence = Sequence;
		UpdateContext.SequenceStartTime = Skeletons[SelectedPose].Time;
		UpdateContext.SequenceTime = Skeletons[SelectedPose].Time;
		UpdateContext.bMirrored = Skeletons[SelectedPose].bMirrored;
		UpdateContext.bLoop = DatabaseSequence->bLoopAnimation;

		Component->UpdatePose(UpdateContext);
	}

	const bool bDrawSequence = EnumHasAnyFlags(DrawParams.Flags, ESkeletonDrawFlags::AnimSequence);
	if (bDrawSequence && SequenceData.bActive)
	{
		UPoseSearchMeshComponent* Component = Skeletons[AnimSequence].Component;
		SetDrawSkeleton(Component, true);

		const FPoseSearchDatabaseSequence* DatabaseSequence = GetAnimSequence(Skeletons[AnimSequence].SequenceIdx);
		UAnimSequence* Sequence = DatabaseSequence->Sequence;

		UpdateContext.Sequence = Sequence;
		UpdateContext.SequenceStartTime = SequenceData.StartTime;
		UpdateContext.SequenceTime = Skeletons[AnimSequence].Time;
		UpdateContext.bMirrored = Skeletons[SelectedPose].bMirrored;
		UpdateContext.bLoop = DatabaseSequence->bLoopAnimation;


		Component->UpdatePose(UpdateContext);
	}
}

void FDebuggerViewModel::UpdateFromTimeline()
{
	NodeIds.Empty();
	MotionMatchingStates.Empty();
	SkeletalMeshComponentId = 0;

	// Get provider and validate
	const TraceServices::IAnalysisSession* Session = RewindDebugger.Get()->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

	const FTraceProvider* PoseSearchProvider = Session->ReadProvider<FTraceProvider>(FTraceProvider::ProviderName);
	const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider");
	const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
	if (!(PoseSearchProvider && AnimationProvider && GameplayProvider))
	{
		return;
	}
	const double TraceTime = RewindDebugger.Get()->CurrentTraceTime();
	TraceServices::FFrame Frame;
	ReadFrameProvider(*Session).GetFrameFromTime(TraceFrameType_Game, TraceTime, Frame);
	PoseSearchProvider->EnumerateMotionMatchingStateTimelines(AnimInstanceId, [&](const FTraceProvider::FMotionMatchingStateTimeline& InTimeline)
	{
		const FTraceMotionMatchingStateMessage* Message = nullptr;

		InTimeline.EnumerateEvents(Frame.StartTime, Frame.EndTime, [&Message](double InStartTime, double InEndTime, const FTraceMotionMatchingStateMessage& InMessage)
		{
			Message = &InMessage;
			return TraceServices::EEventEnumerate::Stop;
		});
		if (Message)
		{
			NodeIds.Add(Message->NodeId);
			MotionMatchingStates.Add(Message);
			SkeletalMeshComponentId = Message->SkeletalMeshComponentId;
		}
	});
	/** No active motion matching state as no messages were read */
	if (SkeletalMeshComponentId == 0)
	{
		return;
	}
	AnimationProvider->ReadSkeletalMeshPoseTimeline(SkeletalMeshComponentId, [&](const IAnimationProvider::SkeletalMeshPoseTimeline& TimelineData, bool bHasCurves)
	{
		TimelineData.EnumerateEvents(Frame.StartTime, Frame.EndTime, [&](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& PoseMessage) -> TraceServices::EEventEnumerate
		{
			// Update root transform
			RootTransform = &PoseMessage.ComponentToWorld;
			const FSkeletalMeshInfo* SkeletalMeshInfo = AnimationProvider->FindSkeletalMeshInfo(PoseMessage.MeshId);
			const FObjectInfo* SkeletalMeshObjectInfo = GameplayProvider->FindObjectInfo(PoseMessage.MeshId);
			if (!SkeletalMeshInfo || !SkeletalMeshObjectInfo)
			{

				return TraceServices::EEventEnumerate::Stop;
			}
			UPoseSearchMeshComponent* ActiveComponent = Skeletons[ActivePose].Component;
			UPoseSearchMeshComponent* SelectedComponent = Skeletons[SelectedPose].Component;
			UPoseSearchMeshComponent* SequenceComponent = Skeletons[AnimSequence].Component;
			USkeletalMesh* SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(SkeletalMeshObjectInfo->PathName)).LoadSynchronous();
			if (SkeletalMesh)
			{
				ActiveComponent->SetSkeletalMesh(SkeletalMesh);
				SelectedComponent->SetSkeletalMesh(SkeletalMesh);
				SequenceComponent->SetSkeletalMesh(SkeletalMesh);
			}
			FTransform ComponentWorldTransform;
			// Active skeleton is simply the traced bone transforms
			AnimationProvider->GetSkeletalMeshComponentSpacePose(PoseMessage, *SkeletalMeshInfo, ComponentWorldTransform, ActiveComponent->GetEditableComponentSpaceTransforms());
			ActiveComponent->Initialize(ComponentWorldTransform);
			ActiveComponent->SetDebugDrawColor(FLinearColor::Green);
			SelectedComponent->SetDebugDrawColor(FLinearColor::Blue);
			SelectedComponent->Initialize(ComponentWorldTransform);
			SequenceComponent->SetDebugDrawColor(FLinearColor::Red);
			SequenceComponent->Initialize(ComponentWorldTransform);

			return TraceServices::EEventEnumerate::Stop;
		});
	});
}

void FDebuggerViewModel::UpdateAnimSequence()
{
	if (!IsPlayingSelections())
	{
		return;
	}

	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	const FPoseSearchDatabaseSequence* DatabaseSequence = GetAnimSequence(Skeletons[AnimSequence].SequenceIdx);
	UAnimSequence* Sequence = DatabaseSequence->Sequence;
	FSkeleton& SequenceSkeleton = Skeletons[AnimSequence];
	UPoseSearchMeshComponent* Component = SequenceSkeleton.Component;
	auto RestartSequence = [&]()
	{
		Component->ResetToStart();
		SequenceData.AccumulatedTime = 0.0;
		SequenceSkeleton.Time = SequenceData.StartTime;
	};

	if (Sequence)


	{
		const float DT = static_cast<float>(FApp::GetDeltaTime()) * SequencePlayRate;
		const float PlayLength = Sequence->GetPlayLength();

		const float DistanceHorizon = Database->Schema->GetTrajectoryFutureDistanceHorizon();
		const bool bExceededDistanceHorizon = Component->LastRootMotionDelta.GetTranslation().Size() > DistanceHorizon;
		const float TimeHorizon = Database->Schema->GetTrajectoryFutureTimeHorizon();
		const bool bExceededTimeHorizon = (SequenceSkeleton.Time - SequenceData.StartTime) > TimeHorizon;
		const bool bExceededHorizon = bExceededDistanceHorizon && bExceededTimeHorizon;
		if (DatabaseSequence->bLoopAnimation)
		{
			if (bExceededHorizon)
			{
				// Delay before restarting the sequence to give the user some idea of where it would land
				if (SequenceData.AccumulatedTime > SequenceData.StopDuration)
				{
					RestartSequence();
				}
				else
				{
					SequenceData.AccumulatedTime += DT;
				}
				return;
			}

			SequenceSkeleton.Time += DT;
			SequenceData.AccumulatedTime += DT;
		}
		else
		{
			// Used to cap the sequence, but avoid modding when updating the pose
			static constexpr float LengthOffset = 0.001f;
			const bool bFinishedSequence = SequenceSkeleton.Time >= PlayLength - LengthOffset;

			// Sequence player reached end of clip or reached distance horizon of trajectory vector
			if (bFinishedSequence || bExceededHorizon)
			{
				// Delay before restarting the sequence to give the user some idea of where it would land
				if (SequenceData.AccumulatedTime > SequenceData.StopDuration)
				{
					RestartSequence();
				}
				else
				{
					SequenceData.AccumulatedTime += DT;
				}
			}
			else
			{
				// If we haven't finished, update the play time capped by the anim sequence (not looping)
				SequenceSkeleton.Time += DT;
			}
		}
	}
}

void FDebuggerViewModel::FillCompactPoseAndComponentRefRotations()
{
	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (Database != nullptr)
	{
		UMirrorDataTable* MirrorDataTable = Database->Schema->MirrorDataTable;
		if (MirrorDataTable != nullptr)
		{
			if (CompactPoseMirrorBones.Num() == 0 || ComponentSpaceRefRotations.Num() == 0)
			{
				MirrorDataTable->FillCompactPoseAndComponentRefRotations(
					Skeletons[ActivePose].Component->RequiredBones,
					CompactPoseMirrorBones,
					ComponentSpaceRefRotations);
			}

			return;
		}
	}

	CompactPoseMirrorBones.Reset();
	ComponentSpaceRefRotations.Reset();
}

void FDebuggerViewModel::PlaySelection(int32 PoseIdx, float Time)
{
	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (!Database)
	{
		return;
	}
	UPoseSearchMeshComponent* Component = Skeletons[AnimSequence].Component;
	if (!Component)
	{
		return;
	}

	const FPoseSearchIndexAsset* Asset = Database->SearchIndex.FindAssetForPose(PoseIdx);

	Component->ResetToStart();
	Skeletons[AnimSequence].SequenceIdx = Asset->SourceAssetIdx;
	Skeletons[AnimSequence].Time = SequenceData.StartTime = Time;
	Skeletons[AnimSequence].bMirrored = Asset->bMirrored;
	SequenceData.AccumulatedTime = 0.0f;
	SequenceData.bActive = true;
}
void FDebuggerViewModel::StopSelection()
{
	UPoseSearchMeshComponent* Component = Skeletons[AnimSequence].Component;
	if (!Component)
	{
		return;
	}

	SequenceData = {};
	// @TODO: Make more functionality rely on checking if it should draw the sequence
	Component->SetDrawDebugSkeleton(false);
}
void FDebuggerViewModel::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	bSkeletonsInitialized = false;
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

bool FDebugger::IsPIESimulating()
{
	return Debugger->RewindDebugger->IsPIESimulating();
}

bool FDebugger::IsRecording()
{
	return Debugger->RewindDebugger->IsRecording();

}

double FDebugger::GetRecordingDuration()
{
	return Debugger->RewindDebugger->GetRecordingDuration();
}

UWorld* FDebugger::GetWorld()
{
	return Debugger->RewindDebugger->GetWorldToVisualize();
}

const IRewindDebugger* FDebugger::GetRewindDebugger()
{
	return Debugger->RewindDebugger;
}

void FDebugger::Update(float DeltaTime, IRewindDebugger* InRewindDebugger)
{
	// Update active rewind debugger in use
	RewindDebugger = InRewindDebugger;
}

void FDebugger::OnViewClosed(uint64 InAnimInstanceId)
{
	TArray<TSharedRef<FDebuggerViewModel>>& Models = Debugger->ViewModels;
	for (int i = 0; i < Models.Num(); ++i)
	{
		if (Models[i]->AnimInstanceId == InAnimInstanceId)
		{
			Models.RemoveAtSwap(i);
			return;
		}
	}
	// Should always be a valid remove
	checkNoEntry();
}

TSharedPtr<FDebuggerViewModel> FDebugger::GetViewModel(uint64 InAnimInstanceId)
{
	TArray<TSharedRef<FDebuggerViewModel>>& Models = Debugger->ViewModels;
	for (int i = 0; i < Models.Num(); ++i)
	{
		if (Models[i]->AnimInstanceId == InAnimInstanceId)
		{
			return Models[i];
		}
	}
	return nullptr;
}

TSharedPtr<SDebuggerView> FDebugger::GenerateInstance(uint64 InAnimInstanceId)
{
	ViewModels.Add_GetRef(MakeShared<FDebuggerViewModel>(InAnimInstanceId))->RewindDebugger.BindStatic(&FDebugger::GetRewindDebugger);

	TSharedPtr<SDebuggerView> DebuggerView;

	SAssignNew(DebuggerView, SDebuggerView, InAnimInstanceId)
		.ViewModel_Static(&FDebugger::GetViewModel, InAnimInstanceId)
		.OnViewClosed_Static(&FDebugger::OnViewClosed);

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

#undef LOCTEXT_NAMESPACE