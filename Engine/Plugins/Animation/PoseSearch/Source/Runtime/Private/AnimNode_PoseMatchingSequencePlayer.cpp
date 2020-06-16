// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/AnimNode_PoseMatchingSequencePlayer.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Animation/AnimInstanceProxy.h"
#include "PoseSearch/PoseSearch.h"

#define LOCTEXT_NAMESPACE "AnimNode_PoseMatchingSequencePlayer"

TAutoConsoleVariable<int32> CVarAnimPoseMatchingSequencePlayerEnable(TEXT("a.AnimNode.PoseMatchingSequencePlayer.Enable"), 1, TEXT("Enable / Disable Pose Matching"));
TAutoConsoleVariable<int32> CVarAnimPoseMatchingSequencePlayerDebugVis(TEXT("a.AnimNode.PoseMatchingSequencePlayer.DebugVis"), 0, TEXT("Enable / Disable Pose Matching Debug Visualization"));

const UPoseSearchIndex* GetPoseSearchDataIndex(const UAnimSequenceBase& Sequence)
{
	UPoseSearchIndex* PoseSearchIndex = nullptr;

	const TArray<UAnimMetaData*>& MetaData = Sequence.GetMetaData();

	for (UAnimMetaData* MetaDataInstance : MetaData)
	{
		if (MetaDataInstance->GetClass() == UPoseSearchIndex::StaticClass())
		{
			PoseSearchIndex = Cast<UPoseSearchIndex>(MetaDataInstance);
			break;
		}
	}

	return PoseSearchIndex;
}

static float FindStartPosition(FAnimInstanceProxy* AnimProxy, const UAnimSequenceBase& Sequence, const FPoseSearchPoseHistory& History, FPoseSearchBuildQueryScratch* Scratch, TArray<float>* Query, bool bEnableDebugVis)
{
	const UPoseSearchIndex* PoseSearchIndex = GetPoseSearchDataIndex(Sequence);

	float Offset = -1.0f;
	if (PoseSearchIndex && PoseSearchIndex->Schema)
	{
		bool bQueryBuilt = PoseSearchBuildQuery(
		    *PoseSearchIndex->Schema,
		    PoseSearchIndex->SequenceSampleRate,
			History,
		    Scratch,
		    Query);

		if (bQueryBuilt)
		{
			USkeletalMeshComponent* SkeletalMeshComponent = AnimProxy->GetSkelMeshComponent();
			check(SkeletalMeshComponent);

			FPoseSearchDebugDrawParams DebugDraw;
			DebugDraw.World = SkeletalMeshComponent->GetWorld();
			DebugDraw.Flags = bEnableDebugVis ? EPoseSearchDebugDrawFlags::DrawAll : EPoseSearchDebugDrawFlags::None;
			DebugDraw.DefaultLifeTime = 2.0f;
			DebugDraw.ComponentTransform = SkeletalMeshComponent->GetComponentTransform();
			FPoseSearchResult Result = PoseSearch(*PoseSearchIndex, *Query, DebugDraw);
			Offset = Result.TimeOffsetSeconds;
		}
	}

	return Offset;
}

/////////////////////////////////////////////////////
// FAnimNode_PoseMatchingSequencePlayer

void FAnimNode_PoseMatchingSequencePlayer::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	const FAnimNode_PoseSearchHistoryCollector* HistoryNode = Context.GetAncestor<FAnimNode_PoseSearchHistoryCollector>();
	const FPoseSearchPoseHistory* History = HistoryNode ? &HistoryNode->GetPoseHistory() : nullptr;
	
	if (Sequence && StartFromNearestPose && History && CVarAnimPoseMatchingSequencePlayerEnable.GetValueOnAnyThread())
	{
		StartPosition = FindStartPosition(Context.AnimInstanceProxy, *Sequence, *History, &Scratch, &SearchQuery, (bool)CVarAnimPoseMatchingSequencePlayerDebugVis.GetValueOnAnyThread());
		if (StartPosition < 0.0f)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Pose search history buffer too small. Increase history time or decrease fragment offsets in search index."));
			StartPosition = 0.0f;
		}
	}
	else
	{
		StartPosition = 0.0f;
	}

	Super::Initialize_AnyThread(Context);
}

void FAnimNode_PoseMatchingSequencePlayer::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(UpdateAssetPlayer);

	Super::UpdateAssetPlayer(Context);
}

void FAnimNode_PoseMatchingSequencePlayer::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	Super::Evaluate_AnyThread(Output);
}

void FAnimNode_PoseMatchingSequencePlayer::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	Super::OnInitializeAnimInstance(InProxy, InAnimInstance);
}

#undef LOCTEXT_NAMESPACE