// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDebugger.h"
#include "IAnimationProvider.h"
#include "IGameplayProvider.h"
#include "PoseSearch/PoseSearch.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/AnimRootMotionProvider.h"
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
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PoseSearchDebugger"

static FLinearColor LinearColorBlend(FLinearColor LinearColorA, FLinearColor LinearColorB, float BlendParam)
{
	return LinearColorA + (LinearColorB - LinearColorA) * BlendParam;
}

void UPoseSearchMeshComponent::Initialize(const FTransform& InComponentToWorld)
{
	SetComponentToWorld(InComponentToWorld);
	const FReferenceSkeleton& SkeletalMeshRefSkeleton = GetSkeletalMesh()->GetRefSkeleton();

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

	if (UpdateContext.Type == ESearchIndexAssetType::Sequence)
	{
		float AdvancedTime = UpdateContext.StartTime;

		FAnimationRuntime::AdvanceTime(
			UpdateContext.bLoop,
			UpdateContext.Time - UpdateContext.StartTime,
			AdvancedTime,
			UpdateContext.Sequence->GetPlayLength());

		FAnimExtractContext ExtractionCtx;
		ExtractionCtx.CurrentTime = AdvancedTime;

		UpdateContext.Sequence->GetAnimationPose(PoseData, ExtractionCtx);
	}
	else if (UpdateContext.Type == ESearchIndexAssetType::BlendSpace)
	{
		TArray<FBlendSampleData> BlendSamples;
		int32 TriangulationIndex = 0;
		UpdateContext.BlendSpace->GetSamplesFromBlendInput(UpdateContext.BlendParameters, BlendSamples, TriangulationIndex, true);
		
		float PlayLength = UpdateContext.BlendSpace->GetAnimationLengthFromSampleData(BlendSamples);
		
		float PreviousTime = UpdateContext.StartTime * PlayLength;
		float CurrentTime = UpdateContext.Time * PlayLength;

		float AdvancedTime = PreviousTime;
		FAnimationRuntime::AdvanceTime(
			UpdateContext.bLoop,
			CurrentTime - PreviousTime,
			CurrentTime,
			PlayLength);
		
		FDeltaTimeRecord DeltaTimeRecord;
		DeltaTimeRecord.Set(PreviousTime, AdvancedTime - PreviousTime);
		FAnimExtractContext ExtractionCtx(AdvancedTime, true, DeltaTimeRecord, UpdateContext.bLoop);

		for (int32 BlendSampleIdex = 0; BlendSampleIdex < BlendSamples.Num(); BlendSampleIdex++)
		{
			float Scale = BlendSamples[BlendSampleIdex].Animation->GetPlayLength() / PlayLength;

			FDeltaTimeRecord BlendSampleDeltaTimeRecord;
			BlendSampleDeltaTimeRecord.Set(DeltaTimeRecord.GetPrevious() * Scale, DeltaTimeRecord.Delta * Scale);

			BlendSamples[BlendSampleIdex].DeltaTimeRecord = BlendSampleDeltaTimeRecord;
			BlendSamples[BlendSampleIdex].PreviousTime = PreviousTime * Scale;
			BlendSamples[BlendSampleIdex].Time = AdvancedTime * Scale;
		}

		UpdateContext.BlendSpace->GetAnimationPose(BlendSamples, ExtractionCtx, PoseData);
	}
	else
	{
		checkNoEntry();
	}

	LastRootMotionDelta = FTransform::Identity;

	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

	if (ensureMsgf(RootMotionProvider, TEXT("Could not get Root Motion Provider.")))
	{
		if (ensureMsgf(RootMotionProvider->HasRootMotion(Attributes), TEXT("Blend Space had no Root Motion Attribute.")))
		{
			RootMotionProvider->ExtractRootMotion(Attributes, LastRootMotionDelta);
		}
	}

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

struct FChannelCostRange
{
	float Min = MAX_flt;
	float Max = -MAX_flt;
	float Delta = 1.0f;
};

class FDebuggerDatabaseRowData : public TSharedFromThis<FDebuggerDatabaseRowData>
{
public:
	FDebuggerDatabaseRowData() = default;
	
	float GetChannelCost(int32 ChannelIdx) const
	{
		return PoseCostDetails.ChannelCosts.IsValidIndex(ChannelIdx)
			? PoseCostDetails.ChannelCosts[ChannelIdx]
			: 0.0f;
	}
	
	float GetAddendsCost() const { return PoseCostDetails.NotifyCostAddend + PoseCostDetails.MirrorMismatchCostAddend; }

	void CalculateColors(FChannelCostRange TotalCostRange, TArrayView<const FChannelCostRange> ChannelCostRanges)
	{
		const float CostColorBlend = (PoseCostDetails.PoseCost.GetTotalCost() - TotalCostRange.Min) / TotalCostRange.Delta;
		CostColor = LinearColorBlend(FLinearColor::Green, FLinearColor::Red, CostColorBlend);

		ChannelCostColors.SetNum(ChannelCostRanges.Num());
		for (int32 ChannelIdx = 0; ChannelIdx != ChannelCostRanges.Num(); ++ChannelIdx)
		{
			const FChannelCostRange& CostRange = ChannelCostRanges[ChannelIdx];
			const float ColorBlend = (GetChannelCost(ChannelIdx) - CostRange.Min) / CostRange.Delta;
			ChannelCostColors[ChannelIdx] = LinearColorBlend(FLinearColor::Green, FLinearColor::Red, ColorBlend);
		}
	}

	FLinearColor GetChannelCostColor (int32 ChannelIdx) const
	{
		return ChannelCostColors.IsValidIndex(ChannelIdx) ? ChannelCostColors[ChannelIdx] : FLinearColor::White;
	}

	ESearchIndexAssetType AssetType = ESearchIndexAssetType::Invalid;
	int32 PoseIdx = 0;
	FString AssetName = "";
	FString AssetPath = "";
	int32 DbAssetIdx = 0;
	int32 AnimFrame = 0;
	float AssetTime = 0.0f;
	bool bMirrored = false;
	bool bLooping = false;
	FVector BlendParameters = FVector::Zero();
	FPoseCostDetails PoseCostDetails;
	FLinearColor CostColor = FLinearColor::White;
	TArray<FLinearColor> ChannelCostColors;
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
			ColumnId = FName(FString::Printf(TEXT("Column %d"), SortIndex));
		}

		virtual ~IColumn() = default;

		FName ColumnId;

		/** Sorted left to right based on this index */
		int32 SortIndex = 0;
		/** Current width, starts at 1 to be evenly spaced between all columns */
		float Width = 1.0f;
		/** Disabled selectively with view options */
		bool bEnabled = false;

		virtual FText GetLabel() const = 0;

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
			static FSlateFontInfo RowFont = FAppStyle::Get().GetFontStyle("DetailsView.CategoryTextStyle");
			
			return SNew(STextBlock)
            	.Font(RowFont)
				.Text_Lambda([this, RowData]() -> FText { return GetRowText(RowData); })
            	.Justification(ETextJustify::Center)
				.ColorAndOpacity_Lambda([this, RowData] { return GetColorAndOpacity(RowData); });
		}

	protected:
		virtual FText GetRowText(const FRowDataRef& Row) const = 0;
		virtual FSlateColor GetColorAndOpacity(const FRowDataRef& Row) const
		{
			return FSlateColor(FLinearColor::White);
		}
	};

	struct FPoseIdx : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelPoseIndex", "Pose Index"); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseIdx < Row1->PoseIdx; };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->PoseIdx, &FNumberFormattingOptions::DefaultNoGrouping());
		}
	};

	struct FAssetName : IColumn
	{
		using IColumn::IColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelAssetName", "Asset"); }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->AssetName < Row1->AssetName; };
		}

		virtual TSharedRef<SWidget> GenerateWidget(const FRowDataRef& RowData) const override
		{
			return SNew(SHyperlink)
				.Text_Lambda([RowData]() -> FText { return FText::FromString(RowData->AssetName); })
				.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
				.ToolTipText_Lambda([RowData]() -> FText 
					{ 
						return FText::Format(
							LOCTEXT("AssetHyperlinkTooltipFormat", "Open asset '{0}'"), 
							FText::FromString(RowData->AssetPath)); 
					})
				.OnNavigate_Lambda([RowData]()
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(RowData->AssetPath);
					});
		}
	};

	struct FAssetType : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelAssetType", "Type"); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->AssetType < Row1->AssetType; };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			if (Row->AssetType == ESearchIndexAssetType::Sequence)
			{
				return FText::FromString(TEXT("Sequence"));
			}
			else if (Row->AssetType == ESearchIndexAssetType::BlendSpace)
			{
				return FText::FromString(TEXT("BlendSpace"));
			}
			else
			{
				checkNoEntry();
				return FText::FromString(TEXT(""));
			}
		}
	};

	struct FFrame : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelFrame", "Frame"); }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->AssetTime < Row1->AssetTime; };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			FNumberFormattingOptions TimeFormattingOptions = FNumberFormattingOptions()
				.SetUseGrouping(false)
				.SetMaximumFractionalDigits(2);

			if (Row->AssetType == ESearchIndexAssetType::Sequence)
			{
				return FText::Format(
					FText::FromString("{0} ({1})"),
					FText::AsNumber(Row->AnimFrame, &FNumberFormattingOptions::DefaultNoGrouping()),
					FText::AsNumber(Row->AssetTime, &TimeFormattingOptions));
			}
			else if (Row->AssetType == ESearchIndexAssetType::BlendSpace)
			{
				// There is no frame index associated with a blendspace
				return FText::Format(
					FText::FromString("({0})"),
					FText::AsNumber(Row->AssetTime, &TimeFormattingOptions));
			}
			else
			{
				checkNoEntry();
				return FText();
			}
		}
	};

	struct FCost : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelCost", "Cost"); }
		
		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->PoseCostDetails.PoseCost < Row1->PoseCostDetails.PoseCost; };
		}
		
		virtual FText GetRowText(const FRowDataRef& Row) const override
        {
        	return FText::AsNumber(Row->PoseCostDetails.PoseCost.GetTotalCost());
        }

		virtual FSlateColor GetColorAndOpacity(const FRowDataRef& Row) const override
		{
			return Row->CostColor;
		}
	};

	struct FChannelCostColumn : ITextColumn
	{
		struct FParams
		{
			int32 SortIndex;
			bool bEnabled = true;
			int32 ChannelIdx;
		};

		FChannelCostColumn(const FParams& Params)
			: ITextColumn(Params.SortIndex, Params.bEnabled)
			, ChannelIdx(Params.ChannelIdx)
		{}

		virtual FText GetLabel() const override { return FText::Format(LOCTEXT("ColumnLabelChannelCost", "Cost[{0}]"), ChannelIdx); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [this](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetChannelCost(ChannelIdx) < Row1->GetChannelCost(ChannelIdx); };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetChannelCost(ChannelIdx));
		}

		virtual FSlateColor GetColorAndOpacity(const FRowDataRef& Row) const override
		{
			return Row->GetChannelCostColor(ChannelIdx);
		}

		int32 ChannelIdx = -1;
	};

	struct FCostModifier : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelCostModifier", "Cost Modifier"); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->GetAddendsCost() < Row1->GetAddendsCost(); };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::AsNumber(Row->GetAddendsCost());
		}
	};

	struct FMirrored : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelMirrored", "Mirrored"); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->bMirrored < Row1->bMirrored; };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::Format(LOCTEXT("Mirrored", "{0}"), { Row->bMirrored } );
		}
	};

	struct FLooping : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelLooping", "Looping"); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool { return Row0->bLooping < Row1->bLooping; };
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			return FText::Format(LOCTEXT("Looping", "{0}"), { Row->bLooping });
		}
	};

	struct FBlendParameters : ITextColumn
	{
		using ITextColumn::ITextColumn;

		virtual FText GetLabel() const override { return LOCTEXT("ColumnLabelBlendParams", "Blend Parameters"); }

		virtual FSortPredicate GetSortPredicate() const override
		{
			return [](const FRowDataRef& Row0, const FRowDataRef& Row1) -> bool
			{
				return (
					Row0->BlendParameters[0] < Row1->BlendParameters[0] ||
					Row0->BlendParameters[1] < Row1->BlendParameters[1]);
			};
		}

		virtual FText GetRowText(const FRowDataRef& Row) const override
		{
			if (Row->AssetType == ESearchIndexAssetType::Sequence)
			{
				return FText::FromString(TEXT("-"));
			}
			else if (Row->AssetType == ESearchIndexAssetType::BlendSpace)
			{
				return FText::Format(
					LOCTEXT("Blend Parameters", "({0}, {1})"),
					FText::AsNumber(Row->BlendParameters[0]),
					FText::AsNumber(Row->BlendParameters[1]));
			}
			else
			{
				checkNoEntry();
				return FText();
			}
		}
	};
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
		
		static FSlateFontInfo NormalFont = FAppStyle::Get().GetFontStyle("DetailsView.CategoryTextStyle");
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
				.Font(FAppStyle::Get().GetFontStyle("DetailsView.CategoryFontStyle"))
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
	ContinuingPoseView.HeaderRow->ClearColumns();
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
		if(ColumnPair.Value->bEnabled)
		{
			SHeaderRow::FColumn::FArguments ColumnArgs = SHeaderRow::FColumn::FArguments()
				.ColumnId(Column.ColumnId)
				.DefaultLabel(Column.GetLabel())
				.SortMode(this, &SDebuggerDatabaseView::GetColumnSortMode, Column.ColumnId)
				.OnSort(this, &SDebuggerDatabaseView::OnColumnSortModeChanged)
				.FillWidth(this, &SDebuggerDatabaseView::GetColumnWidth, Column.ColumnId)
				.VAlignCell(VAlign_Center)
				.VAlignHeader(VAlign_Center)
				.HAlignHeader(HAlign_Center)
				.HAlignCell(HAlign_Fill);

			FilteredDatabaseView.HeaderRow->AddColumn(ColumnArgs);
			
			// Every time the active column is changed, update the database column
			ActiveView.HeaderRow->AddColumn(ColumnArgs.OnWidthChanged(this, &SDebuggerDatabaseView::OnColumnWidthChanged, Column.ColumnId));
			
			ContinuingPoseView.HeaderRow->AddColumn(ColumnArgs.OnWidthChanged(this, &SDebuggerDatabaseView::OnColumnWidthChanged, Column.ColumnId));
		}
	}
}

void SDebuggerDatabaseView::AddColumn(TSharedRef<DebuggerDatabaseColumns::IColumn>&& Column)
{
	Columns.Add(Column->ColumnId, Column);
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
		OnPoseSelectionChanged.ExecuteIfBound(Row->PoseIdx, Row->AssetTime);
	}
}

ECheckBoxState SDebuggerDatabaseView::IsAssetFilterEnabled() const
{
	return bAssetFilterEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SDebuggerDatabaseView::OnAssetFilterEnabledChanged(ECheckBoxState NewState)
{
	bAssetFilterEnabled = NewState == ECheckBoxState::Checked;
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
			if (!bAssetFilterEnabled || 
				(UnfilteredRow->AssetType == ESearchIndexAssetType::Sequence && DatabaseSequenceFilter[UnfilteredRow->DbAssetIdx]) ||
				(UnfilteredRow->AssetType == ESearchIndexAssetType::BlendSpace && DatabaseBlendSpaceFilter[UnfilteredRow->DbAssetIdx]))
			{
				FilteredDatabaseView.Rows.Add(UnfilteredRow);
			}
		}
	}
	else
	{
		for (const auto& UnfilteredRow : UnfilteredDatabaseRows)
		{
			if (!bAssetFilterEnabled ||
				(UnfilteredRow->AssetType == ESearchIndexAssetType::Sequence && DatabaseSequenceFilter[UnfilteredRow->DbAssetIdx]) ||
				(UnfilteredRow->AssetType == ESearchIndexAssetType::BlendSpace && DatabaseBlendSpaceFilter[UnfilteredRow->DbAssetIdx]))
			{
				bool bMatchesAllTokens = Algo::AllOf(
					Tokens,
					[&](FString Token)
				{
					return UnfilteredRow->AssetName.Contains(Token);
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
	const bool bValidIndex = Database.GetSearchIndex() && Database.GetSearchIndex()->IsValid();
	if (bValidIndex)
	{
		const int32 NumPoses = Database.GetSearchIndex()->NumPoses;
		UnfilteredDatabaseRows.Reset(NumPoses);

		RowsSourceDatabase = &Database;

		// Build database rows
		for (const FPoseSearchIndexAsset& SearchIndexAsset : Database.GetSearchIndex()->Assets)
		{
			if (SearchIndexAsset.Type == ESearchIndexAssetType::Sequence)
			{
				const FPoseSearchDatabaseSequence& DbSequence = Database.GetSequenceSourceAsset(&SearchIndexAsset);
				const int32 LastPoseIdx = SearchIndexAsset.FirstPoseIdx + SearchIndexAsset.NumPoses;
				for (int32 PoseIdx = SearchIndexAsset.FirstPoseIdx; PoseIdx != LastPoseIdx; ++PoseIdx)
				{
					float Time = Database.GetAssetTime(PoseIdx);

					TSharedRef<FDebuggerDatabaseRowData> Row = UnfilteredDatabaseRows.Add_GetRef(MakeShared<FDebuggerDatabaseRowData>());
					Row->PoseIdx = PoseIdx;
					Row->AssetType = ESearchIndexAssetType::Sequence;
					Row->AssetName = DbSequence.Sequence->GetName();
					Row->AssetPath = DbSequence.Sequence->GetPathName();
					Row->DbAssetIdx = SearchIndexAsset.SourceAssetIdx;
					Row->AssetTime = Time;
					Row->AnimFrame = DbSequence.Sequence->GetFrameAtTime(Time);
					Row->bMirrored = SearchIndexAsset.bMirrored;
					Row->bLooping = DbSequence.Sequence->bLoop;
					Row->BlendParameters = FVector::Zero();
				}
			}
			else if (SearchIndexAsset.Type == ESearchIndexAssetType::BlendSpace)
			{
				const FPoseSearchDatabaseBlendSpace& DbBlendSpace = Database.GetBlendSpaceSourceAsset(&SearchIndexAsset);
				const int32 LastPoseIdx = SearchIndexAsset.FirstPoseIdx + SearchIndexAsset.NumPoses;
				for (int32 PoseIdx = SearchIndexAsset.FirstPoseIdx; PoseIdx != LastPoseIdx; ++PoseIdx)
				{
					float Time = Database.GetAssetTime(PoseIdx);

					TSharedRef<FDebuggerDatabaseRowData> Row = UnfilteredDatabaseRows.Add_GetRef(MakeShared<FDebuggerDatabaseRowData>());
					Row->PoseIdx = PoseIdx;
					Row->AssetType = ESearchIndexAssetType::BlendSpace;
					Row->AssetName = DbBlendSpace.BlendSpace->GetName();
					Row->AssetPath = DbBlendSpace.BlendSpace->GetPathName();
					Row->DbAssetIdx = SearchIndexAsset.SourceAssetIdx;
					Row->AssetTime = Time;
					Row->AnimFrame = 0; // There is no frame index associated with a blendspace
					Row->bMirrored = SearchIndexAsset.bMirrored;
					Row->bLooping = DbBlendSpace.BlendSpace->bLoop;
					Row->BlendParameters = SearchIndexAsset.BlendParameters;
				}
			}
			else
			{
				checkNoEntry();
			}
		}
	}
	else
	{
		UnfilteredDatabaseRows.Reset();
	}

	ActiveView.Rows.Reset();
	ActiveView.Rows.Add(MakeShared<FDebuggerDatabaseRowData>());

	ContinuingPoseView.Rows.Reset();
	ContinuingPoseView.Rows.Add(MakeShared<FDebuggerDatabaseRowData>());
}

void SDebuggerDatabaseView::UpdateRows(const FTraceMotionMatchingStateMessage& State, const UPoseSearchDatabase& Database)
{
	const bool bValidIndex = Database.GetSearchIndex() && Database.GetSearchIndex()->IsValid();
	const bool bNewDatabase = RowsSourceDatabase != &Database || !bValidIndex;
	if (bNewDatabase || UnfilteredDatabaseRows.IsEmpty())
	{
		CreateRows(Database);
	}

	check(ActiveView.Rows.Num() == 1);
	check(ContinuingPoseView.Rows.Num() == 1);

	FPoseSearchContext SearchContext;
	if (const FPoseSearchIndexAsset* CurrentIndexAsset = Database.GetSearchIndex()->FindAssetForPose(State.DbPoseIdx))
	{
		SearchContext.QueryMirrorRequest = CurrentIndexAsset->bMirrored ? 
			EPoseSearchBooleanRequest::TrueValue : EPoseSearchBooleanRequest::FalseValue;
	}
	
	if (!UnfilteredDatabaseRows.IsEmpty())
	{
		bool IsActiveViewRowInitialized = false;
		bool IsContinuingPoseViewRowInitialized = false;
		for (const TSharedRef<FDebuggerDatabaseRowData>& Row : UnfilteredDatabaseRows)
		{
			const int32 PoseIdx = Row->PoseIdx;

			// @todo: invalidate rows in case Database changed while PIE is active with pose debugger. for now we just avoid crashes
			if (PoseIdx < Database.GetSearchIndex()->NumPoses)
			{
				Database.ComparePoses(
					SearchContext,
					PoseIdx,
					State.QueryVectorNormalized,
					Row->PoseCostDetails);

				// If we are on the active pose for the frame
				if (PoseIdx == State.DbPoseIdx)
				{
					*ActiveView.Rows[0] = Row.Get();
					IsActiveViewRowInitialized = true;
				}

				if (PoseIdx == State.ContinuingPoseIdx)
				{
					*ContinuingPoseView.Rows[0] = Row.Get();
					IsContinuingPoseViewRowInitialized = true;
				}
			}
		}

		if (!IsActiveViewRowInitialized)
		{
			*ActiveView.Rows[0] = UnfilteredDatabaseRows[0].Get();
		}

		if (!IsContinuingPoseViewRowInitialized)
		{
			*ContinuingPoseView.Rows[0] = *ActiveView.Rows[0];
		}
	}

	DatabaseSequenceFilter = State.DatabaseSequenceFilter;
	DatabaseBlendSpaceFilter = State.DatabaseBlendSpaceFilter;

	SortDatabaseRows();
	FilterDatabaseRows();

	if (bNewDatabase)
	{
		FilteredDatabaseView.ListView->ClearSelection();
	}

	ComputeFilteredDatabaseRowsColors();
}

void SDebuggerDatabaseView::ComputeFilteredDatabaseRowsColors()
{
	FChannelCostRange CostRange;
	TArray<FChannelCostRange> ChannelCostRanges;
	for (const TSharedRef<FDebuggerDatabaseRowData>& Row : FilteredDatabaseView.Rows)
	{
		const float Cost = Row->PoseCostDetails.PoseCost.GetTotalCost();
		CostRange.Min = FMath::Min(CostRange.Min, Cost);
		CostRange.Max = FMath::Max(CostRange.Max, Cost);

		const int32 NumChannels = Row->PoseCostDetails.ChannelCosts.Num();
		if (ChannelCostRanges.Num() < NumChannels)
		{
			ChannelCostRanges.SetNum(NumChannels);
		}

		for (int32 ChannelIdx = 0; ChannelIdx != NumChannels; ++ChannelIdx)
		{
			ChannelCostRanges[ChannelIdx].Min = FMath::Min(ChannelCostRanges[ChannelIdx].Min, Row->PoseCostDetails.ChannelCosts[ChannelIdx]);
			ChannelCostRanges[ChannelIdx].Max = FMath::Max(ChannelCostRanges[ChannelIdx].Max, Row->PoseCostDetails.ChannelCosts[ChannelIdx]);
		}
	}

	CostRange.Delta = CostRange.Max - CostRange.Min;
	if (FMath::IsNearlyZero(CostRange.Delta))
	{
		CostRange.Delta = 1.f;
	}

	for (int32 ChannelIdx = 0; ChannelIdx != ChannelCostRanges.Num(); ++ChannelIdx)
	{
		FChannelCostRange ChannelCostRange = ChannelCostRanges[ChannelIdx];
		ChannelCostRange.Delta = ChannelCostRange.Max - ChannelCostRange.Min;
		if (FMath::IsNearlyZero(ChannelCostRange.Delta))
		{
			ChannelCostRange.Delta = 1.f;
		}
	}

	for (const TSharedRef<FDebuggerDatabaseRowData>& Row : FilteredDatabaseView.Rows)
	{
		Row->CalculateColors(CostRange, ChannelCostRanges);
	}

	if (!ActiveView.Rows.IsEmpty())
	{
		ActiveView.Rows[0]->CalculateColors(CostRange, ChannelCostRanges);
	}

	if (!ContinuingPoseView.Rows.IsEmpty())
	{
		ContinuingPoseView.Rows[0]->CalculateColors(CostRange, ChannelCostRanges);
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

TSharedRef<ITableRow> SDebuggerDatabaseView::HandleGenerateContinuingPoseRow(TSharedRef<FDebuggerDatabaseRowData> Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return
		SNew(SDebuggerDatabaseRow, OwnerTable, Item, ContinuingPoseView.RowStyle, &ContinuingPoseView.RowBrush, FMargin(0.0f, 2.0f, 6.0f, 4.0f))
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
	int32 ColumnIdx = 0;
	AddColumn(MakeShared<FAssetName>(ColumnIdx++));
	AddColumn(MakeShared<FAssetType>(ColumnIdx++));

	auto CostColumn = MakeShared<FCost>(ColumnIdx++);
	AddColumn(CostColumn);

	int32 ChannelIdx = 0;
	FChannelCostColumn::FParams ChannelCostParams;
	ChannelCostParams.ChannelIdx = ChannelIdx++;
	ChannelCostParams.SortIndex = ColumnIdx++;
	AddColumn(MakeShared<FChannelCostColumn>(ChannelCostParams));

	ChannelCostParams.ChannelIdx = ChannelIdx++;
	ChannelCostParams.SortIndex = ColumnIdx++;
	AddColumn(MakeShared<FChannelCostColumn>(ChannelCostParams));

	ChannelCostParams.ChannelIdx = ChannelIdx++;
	ChannelCostParams.SortIndex = ColumnIdx++;
	AddColumn(MakeShared<FChannelCostColumn>(ChannelCostParams));

	ChannelCostParams.ChannelIdx = ChannelIdx++;
	ChannelCostParams.SortIndex = ColumnIdx++;
	AddColumn(MakeShared<FChannelCostColumn>(ChannelCostParams));


	AddColumn(MakeShared<FCostModifier>(ColumnIdx++));
	AddColumn(MakeShared<FFrame>(ColumnIdx++));
	AddColumn(MakeShared<FMirrored>(ColumnIdx++));
	AddColumn(MakeShared<FLooping>(ColumnIdx++));
	AddColumn(MakeShared<FBlendParameters>(ColumnIdx++));
	AddColumn(MakeShared<FPoseIdx>(ColumnIdx++));

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

	ActiveView.RowStyle = FAppStyle::GetWidgetStyle<FTableRowStyle>("TableView.Row");
	ActiveView.RowBrush = *FAppStyle::GetBrush("DetailsView.CategoryTop");

	// ContinuingPose Row
	ContinuingPoseView.HeaderRow = SNew(SHeaderRow).Visibility(EVisibility::Collapsed);

	// Used for spacing
	ContinuingPoseView.ScrollBar =
		SNew(SScrollBar)
		.Orientation(Orient_Vertical)
		.HideWhenNotInUse(false)
		.AlwaysShowScrollbar(true)
		.AlwaysShowScrollbarTrack(true);

	ContinuingPoseView.ListView = SNew(SListView<TSharedRef<FDebuggerDatabaseRowData>>)
		.ListItemsSource(&ContinuingPoseView.Rows)
		.HeaderRow(ContinuingPoseView.HeaderRow.ToSharedRef())
		.OnGenerateRow(this, &SDebuggerDatabaseView::HandleGenerateContinuingPoseRow)
		.ExternalScrollbar(ContinuingPoseView.ScrollBar)
		.SelectionMode(ESelectionMode::SingleToggle)
		.ConsumeMouseWheel(EConsumeMouseWheel::Never);

	ContinuingPoseView.RowStyle = FAppStyle::GetWidgetStyle<FTableRowStyle>("TableView.Row");
	ContinuingPoseView.RowBrush = *FAppStyle::GetBrush("DetailsView.CategoryTop");

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

	FilteredDatabaseView.RowStyle = FAppStyle::GetWidgetStyle<FTableRowStyle>("TableView.Row");
	// Set selected color to white to retain visibility when multi-selecting
	FilteredDatabaseView.RowStyle.SetSelectedTextColor(FLinearColor(FVector3f(0.8f)));
	FilteredDatabaseView.RowBrush = *FAppStyle::GetBrush("ToolPanel.GroupBorder");

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
					.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
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
					
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
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
		// Side and top margins, ignore bottom handled by the color border below
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		.AutoHeight()
		[
			// ContinuingPose Row text tab
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
					.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
					.Padding(FMargin(30.0f, 3.0f, 30.0f, 0.0f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						SNew(STextBlock)
						.Text(FText::FromString("Continuing Pose"))	
					]
				]
			]

			// ContinuingPose row list view with scroll bar
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
					
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Padding(0.0f)
					[
						ContinuingPoseView.ListView.ToSharedRef()
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ContinuingPoseView.ScrollBar.ToSharedRef()
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
					.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
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
				.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
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
					.IsChecked(this, &SDebuggerDatabaseView::IsAssetFilterEnabled)
					.OnCheckStateChanged(this, &SDebuggerDatabaseView::OnAssetFilterEnabledChanged)
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
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
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
	
	SortColumn = CostColumn->ColumnId;
	SortMode = EColumnSortMode::Ascending;

	// Active and Continuing Pose view scroll bars only for indenting the columns to align w/ database
	ActiveView.ScrollBar->SetVisibility(EVisibility::Hidden);
	ContinuingPoseView.ScrollBar->SetVisibility(EVisibility::Hidden);

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

	const bool bValidIndex = Database.GetSearchIndex() && Database.GetSearchIndex()->IsValid();
	const bool bValidState = bValidIndex && State.DbPoseIdx < Database.GetSearchIndex()->NumPoses;
	// @todo: cache the derived data key in the state to check its validity
	if (bValidState)
	{
		Reflection->AssetPlayerAssetName = FString();
		if (const FPoseSearchIndexAsset* IndexAsset = Database.GetSearchIndex()->FindAssetForPose(State.DbPoseIdx))
		{
			Reflection->AssetPlayerAssetName = Database.GetSourceAssetName(IndexAsset);
		}

		Reflection->AssetPlayerTime = State.AssetPlayerTime;
		Reflection->LastDeltaTime = State.DeltaTime;
		Reflection->SimLinearVelocity = State.SimLinearVelocity;
		Reflection->SimAngularVelocity = State.SimAngularVelocity;
		Reflection->AnimLinearVelocity = State.AnimLinearVelocity;
		Reflection->AnimAngularVelocity = State.AnimAngularVelocity;

		// Query pose
		Reflection->QueryPoseVector = State.QueryVector;

		// Active pose
		Reflection->ActivePoseVector = Database.GetSearchIndex()->GetPoseValues(State.DbPoseIdx);
		Database.GetSearchIndex()->InverseNormalize(Reflection->ActivePoseVector);

		auto DebuggerView = ParentDebuggerViewPtr.Pin();
		if (DebuggerView.IsValid())
		{
			TArray<TSharedRef<FDebuggerDatabaseRowData>> SelectedRows = DebuggerView->GetSelectedDatabaseRows();
			if (!SelectedRows.IsEmpty())
			{
				const TSharedRef<FDebuggerDatabaseRowData>& Selected = SelectedRows[0];
				Reflection->SelectedPoseVector = Database.GetSearchIndex()->GetPoseValues(Selected->PoseIdx);
				Database.GetSearchIndex()->InverseNormalize(Reflection->SelectedPoseVector);

				Reflection->CostVector = Selected->PoseCostDetails.CostVector;
				//Database.SearchIndex.InverseNormalize(Reflection->CostVector);

				const TSharedRef<FDebuggerDatabaseRowData>& ActiveRow = DebuggerView->GetPoseIdxDatabaseRow(State.DbPoseIdx);

				Reflection->CostVectorDifference = Reflection->CostVector;
				for (int i = 0; i < Reflection->CostVectorDifference.Num(); ++i)
				{
					Reflection->CostVectorDifference[i] -= ActiveRow->PoseCostDetails.CostVector[i];
				}
			}
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

	Model->UpdateAsset();
	
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
		DrawFeatures(*DebuggerWorld, *State, *Database, *Transform, ViewModel.Get()->GetMeshComponent());
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
	const FTransform& Transform,
	const USkinnedMeshComponent* Mesh
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
	DrawParams.Mesh = Mesh;
	const TObjectPtr<UPoseSearchDebuggerReflection>& Reflection = DetailsView->GetReflection();

	auto SetDrawFlags = [](FDebugDrawParams& InDrawParams, const FPoseSearchDebuggerFeatureDrawOptions& Options)
	{
		InDrawParams.Flags = EDebugDrawFlags::None;
		if (Options.bDisable)
		{
			return;
		}

		if (Options.bDrawBoneNames)
		{
			InDrawParams.Flags |= EDebugDrawFlags::DrawBoneNames;
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

	Selected = DatabaseView->GetContinuingPoseRow()->GetSelectedItems();

	// ContinuingPose row should only have 0 or 1
	check(Selected.Num() < 2);

	if (!Selected.IsEmpty())
	{
		// Green for the ContinuingPose view
		DrawParams.Color = &FLinearColor::Gray;

		// Use the motion-matching state's pose idx, as the ContinuingPose row may be update-throttled at this point
		DrawParams.PoseIdx = (*Selected.begin())->PoseIdx;
		DrawParams.LabelPrefix = TEXT("C");
		Draw(DrawParams);
	}

	FSkeletonDrawParams SkeletonDrawParams;
	if (Reflection->bDrawSelectedSkeleton)
	{
		SkeletonDrawParams.Flags |= ESkeletonDrawFlags::SelectedPose;
	}
	if (Reflection->bDrawActiveSkeleton)
	{
		SkeletonDrawParams.Flags |= ESkeletonDrawFlags::ActivePose;
	}

	SkeletonDrawParams.Flags |= ESkeletonDrawFlags::Asset;

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
		// Stop asset player when switching selections
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

FReply SDebuggerView::TogglePlaySelectedAssets() const
{
	const TSharedPtr<SListView<TSharedRef<FDebuggerDatabaseRowData>>>& DatabaseRows = DatabaseView->GetDatabaseRows();
	TArray<TSharedRef<FDebuggerDatabaseRowData>> Selected = DatabaseRows->GetSelectedItems();
	const bool bPlaying = ViewModel.Get()->IsPlayingSelections();
	if (!bPlaying)
	{
		if (!Selected.IsEmpty())
		{
			// @TODO: Make functional with multiple poses being selected
			ViewModel.Get()->PlaySelection(Selected[0]->PoseIdx, Selected[0]->AssetTime);
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
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
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
		.ButtonStyle(FAppStyle::Get(), "Button")
		.ContentPadding( FMargin(5, 0) )
		.OnClicked(this, &SDebuggerView::TogglePlaySelectedAssets)
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
					return FSlateIcon("FAppStyle", bPlayingSelections ? "PlayWorld.StopPlaySession.Small" : "PlayWorld.PlayInViewport.Small").GetSmallIcon();
				})
			]
			// Text
			+ SHorizontalBox::Slot()
			.Padding(FMargin(8, 0, 0, 0))
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return ViewModel.Get()->IsPlayingSelections() ? FText::FromString("Stop Selected Asset") : FText::FromString("Play Selected Asset"); })
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
			.Text(FText::FromString("Asset Play Rate: "))
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

const FPoseSearchDatabaseBlendSpace* FDebuggerViewModel::GetBlendSpace(int32 BlendSpaceIdx) const
{
	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (Database && Database->BlendSpaces.IsValidIndex(BlendSpaceIdx))
	{
		const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = &Database->BlendSpaces[BlendSpaceIdx];
		if (DatabaseBlendSpace)
		{
			return DatabaseBlendSpace;
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
	const FPoseSearchIndexAsset* IndexAsset = Database->GetSearchIndex()->FindAssetForPose(PoseIdx);
	if (!IndexAsset)
	{
		return;
	}

	Component->ResetToStart(); 
	bSelecting = true;
	
	Skeletons[SelectedPose].Type = IndexAsset->Type;
	Skeletons[SelectedPose].Time = Time;
	Skeletons[SelectedPose].bMirrored = IndexAsset->bMirrored;
	Skeletons[SelectedPose].AssetIdx = IndexAsset->SourceAssetIdx;
	Skeletons[SelectedPose].BlendParameters = IndexAsset->BlendParameters;
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

	if (ActiveMotionMatchingState && NewDatabase)
	{
		const FPoseSearchIndexAsset* IndexAsset = NewDatabase->GetSearchIndex()->FindAssetForPose(ActiveMotionMatchingState->DbPoseIdx);

		if (IndexAsset)
		{
			Skeletons[Asset].Type = IndexAsset->Type;
			Skeletons[Asset].bMirrored = IndexAsset->bMirrored;
			Skeletons[Asset].AssetIdx = IndexAsset->SourceAssetIdx;
			Skeletons[Asset].BlendParameters = IndexAsset->BlendParameters;
		}
	}

	if (NewDatabase != CurrentDatabase)
	{
		ClearSelectedSkeleton();
		CurrentDatabase = NewDatabase;
	}
}

void FDebuggerViewModel::OnDraw(FSkeletonDrawParams& DrawParams)
{
	const UPoseSearchDatabase* PoseSearchDatabase = GetPoseSearchDatabase();
	if (!PoseSearchDatabase)
		return;

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

	UpdateContext.MirrorDataTable = PoseSearchDatabase->Schema->MirrorDataTable;
	UpdateContext.CompactPoseMirrorBones = &CompactPoseMirrorBones;
	UpdateContext.ComponentSpaceRefRotations = &ComponentSpaceRefRotations;

	if (bDrawSelectedPose)
	{
		UPoseSearchMeshComponent* Component = Skeletons[SelectedPose].Component;

		if (Skeletons[SelectedPose].Type == ESearchIndexAssetType::Sequence)
		{
			const FPoseSearchDatabaseSequence* DatabaseSequence = GetAnimSequence(Skeletons[SelectedPose].AssetIdx);
			if (DatabaseSequence)
			{
				UpdateContext.Type = ESearchIndexAssetType::Sequence;
				UpdateContext.Sequence = DatabaseSequence->Sequence;
				UpdateContext.StartTime = Skeletons[SelectedPose].Time;
				UpdateContext.Time = Skeletons[SelectedPose].Time;
				UpdateContext.bMirrored = Skeletons[SelectedPose].bMirrored;
				UpdateContext.bLoop = DatabaseSequence->Sequence->bLoop;
			}
		}
		else if (Skeletons[SelectedPose].Type == ESearchIndexAssetType::BlendSpace)
		{
			const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = GetBlendSpace(Skeletons[SelectedPose].AssetIdx);
			if (DatabaseBlendSpace)
			{
				UpdateContext.Type = ESearchIndexAssetType::BlendSpace;
				UpdateContext.BlendSpace = DatabaseBlendSpace->BlendSpace;
				UpdateContext.StartTime = Skeletons[SelectedPose].Time;
				UpdateContext.Time = Skeletons[SelectedPose].Time;
				UpdateContext.bMirrored = Skeletons[SelectedPose].bMirrored;
				UpdateContext.bLoop = DatabaseBlendSpace->BlendSpace->bLoop;
				UpdateContext.BlendParameters = Skeletons[SelectedPose].BlendParameters;
			}
		}
		else
		{
			checkNoEntry();
		}

		if (UpdateContext.Type != ESearchIndexAssetType::Invalid)
		{
			Component->UpdatePose(UpdateContext);
		}
	}

	const bool bDrawAsset = EnumHasAnyFlags(DrawParams.Flags, ESkeletonDrawFlags::Asset);
	if (bDrawAsset && AssetData.bActive)
	{
		UPoseSearchMeshComponent* Component = Skeletons[Asset].Component;
		SetDrawSkeleton(Component, true);

		if (Skeletons[SelectedPose].Type == ESearchIndexAssetType::Sequence)
		{
			const FPoseSearchDatabaseSequence* DatabaseSequence = GetAnimSequence(Skeletons[Asset].AssetIdx);
			if (DatabaseSequence)
			{
				UpdateContext.Type = ESearchIndexAssetType::Sequence;
				UpdateContext.Sequence = DatabaseSequence->Sequence;
				UpdateContext.StartTime = Skeletons[Asset].Time;
				UpdateContext.Time = Skeletons[Asset].Time;
				UpdateContext.bMirrored = Skeletons[Asset].bMirrored;
				UpdateContext.bLoop = DatabaseSequence->Sequence->bLoop;
			}
		}
		else if (Skeletons[SelectedPose].Type == ESearchIndexAssetType::BlendSpace)
		{
			const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = GetBlendSpace(Skeletons[Asset].AssetIdx);
			if (DatabaseBlendSpace)
			{
				UpdateContext.Type = ESearchIndexAssetType::BlendSpace;
				UpdateContext.BlendSpace = DatabaseBlendSpace->BlendSpace;
				UpdateContext.StartTime = Skeletons[Asset].Time;
				UpdateContext.Time = Skeletons[Asset].Time;
				UpdateContext.bMirrored = Skeletons[Asset].bMirrored;
				UpdateContext.bLoop = DatabaseBlendSpace->BlendSpace->bLoop;
				UpdateContext.BlendParameters = Skeletons[Asset].BlendParameters;
			}
		}
		else
		{
			checkNoEntry();
		}

		if (UpdateContext.Type != ESearchIndexAssetType::Invalid)
		{
			Component->UpdatePose(UpdateContext);
		}
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
			UPoseSearchMeshComponent* AssetComponent = Skeletons[Asset].Component;
			USkeletalMesh* SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(SkeletalMeshObjectInfo->PathName)).LoadSynchronous();
			if (SkeletalMesh)
			{
				ActiveComponent->SetSkeletalMesh(SkeletalMesh);
				SelectedComponent->SetSkeletalMesh(SkeletalMesh);
				AssetComponent->SetSkeletalMesh(SkeletalMesh);
			}
			FTransform ComponentWorldTransform;
			// Active skeleton is simply the traced bone transforms
			AnimationProvider->GetSkeletalMeshComponentSpacePose(PoseMessage, *SkeletalMeshInfo, ComponentWorldTransform, ActiveComponent->GetEditableComponentSpaceTransforms());
			ActiveComponent->Initialize(ComponentWorldTransform);
			ActiveComponent->SetDebugDrawColor(FLinearColor::Green);
			SelectedComponent->SetDebugDrawColor(FLinearColor::Blue);
			SelectedComponent->Initialize(ComponentWorldTransform);
			AssetComponent->SetDebugDrawColor(FLinearColor::Red);
			AssetComponent->Initialize(ComponentWorldTransform);

			return TraceServices::EEventEnumerate::Stop;
		});
	});
}

void FDebuggerViewModel::UpdateAsset()
{
	// @todo: expose those parameters
	static float MAX_DISTANCE_RANGE = 200.f;
	static float MAX_TIME_RANGE = 2.f;

	const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
	if (!Database || !IsPlayingSelections())
	{
		return;
	}

	FSkeleton& AssetSkeleton = Skeletons[Asset];
	UPoseSearchMeshComponent* Component = AssetSkeleton.Component;

	auto RestartAsset = [&]()
	{
		Component->ResetToStart();
		AssetData.AccumulatedTime = 0.0;
		AssetSkeleton.Time = AssetData.StartTime;
	};

	UAnimationAsset* AnimAsset = nullptr;
	bool bAssetLooping = false;

	if (AssetSkeleton.Type == ESearchIndexAssetType::Sequence)
	{
		const FPoseSearchDatabaseSequence* DatabaseSequence = GetAnimSequence(AssetSkeleton.AssetIdx);
		AnimAsset = DatabaseSequence->Sequence;
		bAssetLooping = DatabaseSequence->Sequence->bLoop;
	}
	else if (AssetSkeleton.Type == ESearchIndexAssetType::BlendSpace)
	{
		const FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = GetBlendSpace(AssetSkeleton.AssetIdx);
		AnimAsset = DatabaseBlendSpace->BlendSpace;
		bAssetLooping = DatabaseBlendSpace->BlendSpace->bLoop;
	}
	else
	{
		checkNoEntry();
	}

	if (AnimAsset)
	{
		const float DT = static_cast<float>(FApp::GetDeltaTime()) * AssetPlayRate;
		const float PlayLength = AnimAsset->GetPlayLength();
		const bool bExceededDistanceHorizon = Component->LastRootMotionDelta.GetTranslation().Size() > MAX_DISTANCE_RANGE;
		const bool bExceededTimeHorizon = (AssetSkeleton.Time - AssetData.StartTime) > MAX_TIME_RANGE;
		const bool bExceededHorizon = bExceededDistanceHorizon && bExceededTimeHorizon;
		if (bAssetLooping)
		{
			if (bExceededHorizon)
			{
				// Delay before restarting the asset to give the user some idea of where it would land
				if (AssetData.AccumulatedTime > AssetData.StopDuration)
				{
					RestartAsset();
				}
				else
				{
					AssetData.AccumulatedTime += DT;
				}
				return;
			}

			AssetSkeleton.Time += DT;
			AssetData.AccumulatedTime += DT;
		}
		else
		{
			// Used to cap the asset, but avoid modding when updating the pose
			static constexpr float LengthOffset = 0.001f;
			const bool bFinishedAsset = AssetSkeleton.Time >= PlayLength - LengthOffset;

			// Asset player reached end of clip or reached distance horizon of trajectory vector
			if (bFinishedAsset || bExceededHorizon)
			{
				// Delay before restarting the asset to give the user some idea of where it would land
				if (AssetData.AccumulatedTime > AssetData.StopDuration)
				{
					RestartAsset();
				}
				else
				{
					AssetData.AccumulatedTime += DT;
				}
			}
			else
			{
				// If we haven't finished, update the play time capped by the anim asset (not looping)
				AssetSkeleton.Time += DT;
			}
		}
	}
}

const USkinnedMeshComponent* FDebuggerViewModel::GetMeshComponent() const
{
	if (Skeletons.Num() > FDebuggerViewModel::Asset)
	{
		return Skeletons[FDebuggerViewModel::Asset].Component;
	}
	return nullptr;
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
	UPoseSearchMeshComponent* Component = Skeletons[Asset].Component;
	if (!Component)
	{
		return;
	}
	const FPoseSearchIndexAsset* IndexAsset = Database->GetSearchIndex()->FindAssetForPose(PoseIdx);
	if (!IndexAsset)
	{
		return;
	}

	Component->ResetToStart();
	
	Skeletons[Asset].Type = IndexAsset->Type;
	Skeletons[Asset].AssetIdx = IndexAsset->SourceAssetIdx;
	Skeletons[Asset].Time = Time;
	Skeletons[Asset].bMirrored = IndexAsset->bMirrored;
	Skeletons[Asset].BlendParameters = IndexAsset->BlendParameters;

	AssetData.StartTime = Time;
	AssetData.AccumulatedTime = 0.0f;
	AssetData.bActive = true;
}
void FDebuggerViewModel::StopSelection()
{
	UPoseSearchMeshComponent* Component = Skeletons[Asset].Component;
	if (!Component)
	{
		return;
	}

	AssetData = {};
	// @TODO: Make more functionality rely on checking if it should draw the asset
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

bool FDebuggerViewCreator::HasDebugInfo(uint64 AnimInstanceId) const
{
	// Get provider and validate
	const TraceServices::IAnalysisSession* Session = IRewindDebugger::Instance()->GetAnalysisSession();
	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

	const FTraceProvider* PoseSearchProvider = Session->ReadProvider<FTraceProvider>(FTraceProvider::ProviderName);
	const IAnimationProvider* AnimationProvider = Session->ReadProvider<IAnimationProvider>("AnimationProvider");
	const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
	if (!(PoseSearchProvider && AnimationProvider && GameplayProvider))
	{
		return false;
	}
	
	bool bHasData = false;
	
	PoseSearchProvider->EnumerateMotionMatchingStateTimelines(AnimInstanceId, [&bHasData](const FTraceProvider::FMotionMatchingStateTimeline& InTimeline)
	{
		bHasData = true;
	});
	
	return bHasData;
}

FName FDebuggerViewCreator::GetName() const
{
	static const FName Name("PoseSearchDebugger");
	return Name;
}

} // namespace UE::PoseSearch

#undef LOCTEXT_NAMESPACE