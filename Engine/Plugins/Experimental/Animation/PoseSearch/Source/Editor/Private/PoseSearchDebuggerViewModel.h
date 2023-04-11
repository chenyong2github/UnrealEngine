// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "PoseSearchMeshComponent.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FInstancedStruct;
class IRewindDebugger;
class UPoseSearchDatabase;

namespace UE::PoseSearch
{

struct FTraceMotionMatchingStateMessage;

class FDebuggerViewModel : public TSharedFromThis<FDebuggerViewModel>
{
public:
	explicit FDebuggerViewModel(uint64 InAnimInstanceId);
	virtual ~FDebuggerViewModel();

	// Used for view callbacks
    const FTraceMotionMatchingStateMessage* GetMotionMatchingState() const;
	const UPoseSearchDatabase* GetCurrentDatabase() const;
	const TArray<int32>* GetNodeIds() const;
	int32 GetNodesNum() const;
	const FTransform* GetRootTransform() const;

	/** Checks if Update must be called */
	bool HasSearchableAssetChanged() const;

	/** Update motion matching states for frame */
	void OnUpdate();
	
	/** Updates active motion matching state based on node selection */
	void OnUpdateNodeSelection(int32 InNodeId);

	/** Sets the selected pose skeleton*/
	void ShowSelectedSkeleton(const UPoseSearchDatabase* Database, int32 DbPoseIdx, float Time);
	
	/** Clears the selected pose skeleton */
	void ClearSelectedSkeleton();
	
	void SetVerbose(bool bVerbose) { bIsVerbose = bVerbose; }

	bool IsVerbose() const { return bIsVerbose; }

	/** Callback to reset debug skeletons for the active world */
	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);

	const USkinnedMeshComponent* GetMeshComponent() const;

private:
	/** Debug visualization skeleton actor */
	struct FSkeleton
	{
		/** Actor object for the skeleton */
		TWeakObjectPtr<AActor> Actor = nullptr;

		/** Derived skeletal mesh for setting the skeleton in the scene */
		TWeakObjectPtr<UPoseSearchMeshComponent> Component = nullptr;

		/** Source database for this skeleton  */
		TWeakObjectPtr<const UPoseSearchDatabase> SourceDatabase = nullptr;

		/** Source asset for this skeleton */
		int32 AssetIdx = 0;

		/** Time in the sequence this skeleton is accessing */
		float Time = 0.0f;

		/** If this asset should be mirrored */
		bool bMirrored = false;

		/** Blend Parameters if asset is a BlendSpace */
		FVector BlendParameters = FVector::Zero();

		const FInstancedStruct* GetAnimationAsset() const;
	};

	/** Update the list of states for this frame */
	void UpdateFromTimeline();

	/** Populates arrays used for mirroring the animation pose */
	void FillCompactPoseAndComponentRefRotations();

	void UpdatePoseSearchContext(UPoseSearchMeshComponent::FUpdateContext& InOutContext, const FSkeleton& Skeleton) const;
	
	/** List of all Node IDs associated with motion matching states */
	TArray<int32> NodeIds;
	
	/** List of all updated motion matching states per node */
	TArray<const FTraceMotionMatchingStateMessage*> MotionMatchingStates;
	
	/** Currently active motion matching state based on node selection in the view */
	const FTraceMotionMatchingStateMessage* ActiveMotionMatchingState = nullptr;

	/** Active motion matching state's searchable asset */
	uint64 SearchableAssetId = 0;

	/** Current Skeletal Mesh Component Id for the AnimInstance */
	uint64 SkeletalMeshComponentId = 0;

	/** Currently active root transform on the skeletal mesh */
	const FTransform* RootTransform = nullptr;

	/** Pointer to the active rewind debugger in the scene */
	TAttribute<const IRewindDebugger*> RewindDebugger;

	/** Anim Instance associated with this debugger instance */
	uint64 AnimInstanceId = 0;

	/** Compact pose format of Mirror Bone Map */
	TCustomBoneIndexArray<FCompactPoseBoneIndex, FCompactPoseBoneIndex> CompactPoseMirrorBones;

	/** Pre-calculated component space rotations of reference pose */
	TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> ComponentSpaceRefRotations;


	/** Index for each type of skeleton we store for debug visualization */
	enum ESkeletonIndex
	{
		ActivePose = 0,
		SelectedPose,
		Asset,

		Num
	};

	/** Skeleton container for each type */
	TArray<FSkeleton, TFixedAllocator<ESkeletonIndex::Num>> Skeletons;

	/** Whether the skeletons have been initialized for this world */
	bool bSkeletonsInitialized = false;
	
	/** If we currently have a selection active in the view */
	bool bSelecting = false;
	
	bool bIsVerbose = false;

	/** Limits some public API */
	friend class FDebugger;
};

} // namespace UE::PoseSearch
