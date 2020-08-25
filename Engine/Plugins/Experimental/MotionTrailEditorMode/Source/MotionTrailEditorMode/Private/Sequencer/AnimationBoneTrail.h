// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trail.h"
#include "TrajectoryCache.h"
#include "TrajectoryDrawInfo.h"

#include "Sequencer/MovieSceneControlRigParameterSection.h"

namespace UE 
{
namespace MotionTrailEditor	
{

class FAnimTrajectoryCache : public FGCObject
{
public:
	FAnimTrajectoryCache(USkeletalMeshComponent* InSkeletalMeshComponent, TWeakPtr<class ISequencer> InWeakSequencer)
		: WeakSequencer(InWeakSequencer)
		, CachedAnimSequence(NewObject<UAnimSequence>())
		, GlobalBoneTransforms()
		, ComponentBoneTransforms()
		, SkelToTrackIdx()
		, AnimRange()
		, Spacing()
		, bDirty(true)
	{
		CachedAnimSequence->SetSkeleton(InSkeletalMeshComponent->SkeletalMesh->Skeleton);
	}

	void Evaluate(FTrajectoryCache* ParentTrajectoryCache, USkeletalMeshComponent* SkeletalMeshComponent);
	void UpdateRange(const TRange<double>& EvalRange, FTrajectoryCache* ParentTrajectoryCache, const int32 BoneIdx);
	const TRange<double>& GetRange() const { return AnimRange; }

	void MarkAsDirty() { bDirty = true; }
	bool IsDirty() const { return bDirty; }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:

	void GetSpaceBasedAnimationData(TArray<TArray<FTransform>>& OutAnimationDataInComponentSpace);

	TWeakPtr<ISequencer> WeakSequencer;
	UAnimSequence* CachedAnimSequence;
	TArray<TArray<FTransform>> GlobalBoneTransforms;
	TArray<TArray<FTransform>> ComponentBoneTransforms;
	TArray<int32> SkelToTrackIdx;
	TRange<double> AnimRange;
	double Spacing;
	bool bDirty;

	friend class FAnimBoneTrajectoryCache;
};

class FAnimBoneTrajectoryCache : public FTrajectoryCache
{
public:
	FAnimBoneTrajectoryCache(const FName& BoneName, TSharedPtr<FAnimTrajectoryCache> InAnimTrajectoryCache)
		: AnimTrajectoryCache(InAnimTrajectoryCache)
		, BoneIdx(InAnimTrajectoryCache->CachedAnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName))
	{}

	// begin FTrajectoryCache interface
	virtual const FTransform& Get(const double InTime) const override { return GetInterp(InTime); }
	virtual FTransform GetInterp(const double InTime) const override;

	// This cache is read-only
	virtual void Set(const double InTime, const FTransform& InValue) override {};

	virtual TArray<double> GetAllTimesInRange(const TRange<double>& InRange) const override;
	// end FTrajectoryCache interface

	// TODO: true for now
	bool IsValid() const { return true; }
	TSharedPtr<FAnimTrajectoryCache> GetAnimCache() const { return AnimTrajectoryCache; }
	int32 GetBoneIndex() const { return BoneIdx; }

private:

	TSharedPtr<FAnimTrajectoryCache> AnimTrajectoryCache;
	int32 BoneIdx;
};

class FAnimationBoneTrail : public FTrail 
{
public:
	FAnimationBoneTrail(const FLinearColor& InColor, const bool bInIsVisible, TSharedPtr<FAnimTrajectoryCache> InAnimTrajectoryCache, const FName& InBoneName, const bool bInIsRootBone)
		: FTrail()
		, TrajectoryCache(MakeUnique<FAnimBoneTrajectoryCache>(InBoneName, InAnimTrajectoryCache))
		, DrawInfo()
		, CachedEffectiveRange(TRange<double>::Empty())
		, bIsRootBone(bInIsRootBone)
	{
		DrawInfo = MakeUnique<FCachedTrajectoryDrawInfo>(InColor, bInIsVisible, TrajectoryCache.Get());
	}

	// FTrail interface
	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) override;
	virtual FTrajectoryCache* GetTrajectoryTransforms() override { return TrajectoryCache.Get(); }
	virtual FTrajectoryDrawInfo* GetDrawInfo() override { return DrawInfo.Get(); }
	virtual TRange<double> GetEffectiveRange() const override { return CachedEffectiveRange; }
	// End FTrail interface

private:
	TUniquePtr<FAnimBoneTrajectoryCache> TrajectoryCache;
	TUniquePtr<FCachedTrajectoryDrawInfo> DrawInfo;

	TRange<double> CachedEffectiveRange;
	bool bIsRootBone;
};

} // namespace MovieScene
} // namespace UE
