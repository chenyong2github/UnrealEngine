// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DrawDebugHelpers.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchResult.h"

struct FTrajectorySampleRange;
class UPoseSearchDatabase;
class UPoseSearchFeatureChannel_Position;

namespace UE::PoseSearch
{

struct FPoseIndicesHistory;
struct IPoseHistory;

enum class EDebugDrawFlags : uint32
{
	None = 0,

	// Draw using Query colors form the schema / config
	DrawQuery = 1 << 1,
};
ENUM_CLASS_FLAGS(EDebugDrawFlags);

enum class EPoseCandidateFlags : uint8
{
	None = 0,

	Valid_Pose = 1 << 0,
	Valid_ContinuingPose = 1 << 1,
	Valid_CurrentPose = 1 << 2,

	AnyValidMask = Valid_Pose | Valid_ContinuingPose | Valid_CurrentPose,

	DiscardedBy_PoseJumpThresholdTime = 1 << 3,
	DiscardedBy_PoseReselectHistory = 1 << 4,
	DiscardedBy_BlockTransition = 1 << 5,
	DiscardedBy_PoseFilter = 1 << 6,

	AnyDiscardedMask = DiscardedBy_PoseJumpThresholdTime | DiscardedBy_PoseReselectHistory | DiscardedBy_BlockTransition | DiscardedBy_PoseFilter,
};
ENUM_CLASS_FLAGS(EPoseCandidateFlags);

template<typename FTransformType>
struct POSESEARCH_API FCachedTransform
{
	FCachedTransform()
		: SampleTime(0.f)
		, BoneIndexType(RootBoneIndexType)
		, Transform(FTransformType::Identity)
	{
	}

	FCachedTransform(float InSampleTime, FBoneIndexType InBoneIndexType, const FTransformType& InTransform)
		: SampleTime(InSampleTime)
		, BoneIndexType(InBoneIndexType)
		, Transform(InTransform)
	{
	}

	float SampleTime = 0.f;
	
	FBoneIndexType BoneIndexType = RootBoneIndexType;

	// associated transform to BoneIndexType in ComponentSpace (except for the root bone stored in global space)
	FTransformType Transform = FTransformType::Identity;
};

template<typename FTransformType>
struct FCachedTransforms
{
	const FCachedTransform<FTransformType>* Find(float SampleTime, FBoneIndexType BoneIndexType) const
	{
		// @todo: use an hashmap if we end up having too many entries
		return CachedTransforms.FindByPredicate([SampleTime, BoneIndexType](const FCachedTransform<FTransformType>& CachedTransform)
			{
				return CachedTransform.SampleTime == SampleTime && CachedTransform.BoneIndexType == BoneIndexType;
			});
	}

	void Add(float SampleTime, FBoneIndexType BoneIndexType, const FTransformType& Transform)
	{
		CachedTransforms.Emplace(SampleTime, BoneIndexType, Transform);
	}

	void Reset()
	{
		CachedTransforms.Reset();
	}

	bool IsEmpty() const
	{
		return CachedTransforms.IsEmpty();
	}

private:
	TArray<FCachedTransform<FTransformType>, TInlineAllocator<64>> CachedTransforms;
};

#if ENABLE_DRAW_DEBUG
struct POSESEARCH_API FDebugDrawParams
{
	FDebugDrawParams(FAnimInstanceProxy* InAnimInstanceProxy, const UPoseSearchDatabase* InDatabase, EDebugDrawFlags InFlags = EDebugDrawFlags::None);
	FDebugDrawParams(const UWorld* InWorld, const USkinnedMeshComponent* InMesh, const UPoseSearchDatabase* InDatabase, EDebugDrawFlags InFlags = EDebugDrawFlags::None);

	const FPoseSearchIndex* GetSearchIndex() const;
	const UPoseSearchSchema* GetSchema() const;

	FVector ExtractPosition(TConstArrayView<float> PoseVector, const UPoseSearchFeatureChannel_Position* Position) const;
	FVector ExtractPosition(TConstArrayView<float> PoseVector, float SampleTimeOffset, int8 SchemaBoneIdx = RootSchemaBoneIdx, EPermutationTimeType PermutationTimeType = EPermutationTimeType::UseSampleTime) const;
	const FTransform& GetRootTransform() const;

	void DrawLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, float Thickness = 0.f) const;
	void DrawPoint(const FVector& Position, const FColor& Color, float Thickness = 6.f) const;
	void DrawCircle(const FMatrix& TransformMatrix, float Radius, int32 Segments, const FColor& Color, float Thickness = 1.f) const;
	void DrawCentripetalCatmullRomSpline(TConstArrayView<FVector> Points, TConstArrayView<FColor> Colors, float Alpha, int32 NumSamplesPerSegment, float Thickness = 1.f) const;
	
	void DrawFeatureVector(TConstArrayView<float> PoseVector);
	void DrawFeatureVector(int32 PoseIdx);

private:
	bool CanDraw() const;

	FAnimInstanceProxy* AnimInstanceProxy = nullptr;
	const UWorld* World = nullptr;
	const USkinnedMeshComponent* Mesh = nullptr;
	const UPoseSearchDatabase* Database = nullptr;
	EDebugDrawFlags Flags = EDebugDrawFlags::None;
};

#endif // ENABLE_DRAW_DEBUG

struct POSESEARCH_API FSearchContext
{
	EPoseSearchBooleanRequest QueryMirrorRequest = EPoseSearchBooleanRequest::Indifferent;
	const IPoseHistory* History = nullptr;
	const FTrajectorySampleRange* Trajectory = nullptr;
	FSearchResult CurrentResult;
	float PoseJumpThresholdTime = 0.f;
	bool bForceInterrupt = false;
	// can the continuing pose advance? (if not we skip evaluating it)
	bool bCanAdvance = true;

	FQuat GetSampleRotation(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx, bool bUseHistoryRoot = false, EPermutationTimeType PermutationTimeType = EPermutationTimeType::UseSampleTime);
	FVector GetSamplePosition(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx, bool bUseHistoryRoot = false, EPermutationTimeType PermutationTimeType = EPermutationTimeType::UseSampleTime);
	FVector GetSampleVelocity(float SampleTimeOffset, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx, bool bUseCharacterSpaceVelocities = true, bool bUseHistoryRoot = false, EPermutationTimeType PermutationTimeType = EPermutationTimeType::UseSampleTime);

	void ClearCachedEntries();

	void ResetCurrentBestCost();
	void UpdateCurrentBestCost(const FPoseSearchCost& PoseSearchCost);
	float GetCurrentBestTotalCost() const { return CurrentBestTotalCost; }

	void GetOrBuildQuery(const UPoseSearchSchema* Schema, FPoseSearchFeatureVectorBuilder& FeatureVectorBuilder);
	const FPoseSearchFeatureVectorBuilder* GetCachedQuery(const UPoseSearchSchema* Schema) const;

	bool IsCurrentResultFromDatabase(const UPoseSearchDatabase* Database) const;

	TConstArrayView<float> GetCurrentResultPrevPoseVector() const;
	TConstArrayView<float> GetCurrentResultPoseVector() const;
	TConstArrayView<float> GetCurrentResultNextPoseVector() const;

	float DesiredPermutationTimeOffset = 0.f;

	const FPoseIndicesHistory* PoseIndicesHistory = nullptr;

private:
	FTransform GetTransform(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaBoneIdx = RootSchemaBoneIdx, bool bUseHistoryRoot = false);
	FTransform GetComponentSpaceTransform(float SampleTime, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx);
	FVector GetSamplePositionInternal(float SampleTime, float OriginTime, const UPoseSearchSchema* Schema, int8 SchemaSampleBoneIdx = RootSchemaBoneIdx, int8 SchemaOriginBoneIdx = RootSchemaBoneIdx, bool bUseHistoryRoot = false);

	// transforms cached in component space
	FCachedTransforms<FTransform> CachedTransforms;
	TArray<FPoseSearchFeatureVectorBuilder, TInlineAllocator<8>> CachedQueries;

	float CurrentBestTotalCost = MAX_flt;
	
#if UE_POSE_SEARCH_TRACE_ENABLED

public:
	struct FPoseCandidate
	{
		FPoseSearchCost Cost;
		int32 PoseIdx = 0;
		const UPoseSearchDatabase* Database = nullptr;
		EPoseCandidateFlags PoseCandidateFlags = EPoseCandidateFlags::None;

		bool operator<(const FPoseCandidate& Other) const { return Other.Cost < Cost; } // Reverse compare because BestCandidates is a max heap
		bool operator==(const FSearchResult& SearchResult) const { return (PoseIdx == SearchResult.PoseIdx) && (Database == SearchResult.Database.Get()); }
	};

	struct FBestPoseCandidates : private TArray<FPoseCandidate>
	{
		typedef TArray<FPoseCandidate> Super;
		using Super::IsEmpty;

		int32 MaxPoseCandidates = 100;

		void Add(const FPoseSearchCost& Cost, int32 PoseIdx, const UPoseSearchDatabase* Database, EPoseCandidateFlags PoseCandidateFlags)
		{
			if (Num() < MaxPoseCandidates || Cost < HeapTop().Cost)
			{
				while (Num() >= MaxPoseCandidates)
				{
					ElementType Unused;
					Pop(Unused);
				}

				FSearchContext::FPoseCandidate PoseCandidate;
				PoseCandidate.Cost = Cost;
				PoseCandidate.PoseIdx = PoseIdx;
				PoseCandidate.Database = Database;
				PoseCandidate.PoseCandidateFlags = PoseCandidateFlags;
				HeapPush(PoseCandidate);
			}
		}

		void Pop(FPoseCandidate& OutItem)
		{
			HeapPop(OutItem, false);
		}
	};
	
	FBestPoseCandidates BestCandidates;
#endif
};

} // namespace UE::PoseSearch
