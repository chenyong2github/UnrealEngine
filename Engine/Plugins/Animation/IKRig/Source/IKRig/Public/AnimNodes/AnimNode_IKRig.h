// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IKRigDataTypes.h"
#include "IKRigProcessor.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_IKRig.generated.h"

class IIKGoalCreatorInterface;
class UIKRigDefinition;
class UIKRigProcessor;

USTRUCT(BlueprintInternalUseOnly)
struct IKRIG_API FAnimNode_IKRig : public FAnimNode_Base
{
	GENERATED_BODY()

	/** The input pose to start the IK solve relative to. */
	UPROPERTY(EditAnywhere, Category = Links)
	FPoseLink Source;

	/** The IK rig to use to modify the incoming source pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = RigDefinition, meta = (NeverAsPin))
	UIKRigDefinition* RigDefinitionAsset;

	/** The input goal transforms used by the IK Rig solvers.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, EditFixedSize, Category = Goal, meta = (PinShownByDefault))
	TArray<FIKRigGoal> Goals;

	/** optionally ignore the input pose and start from the reference pose each solve */
	UPROPERTY(EditAnywhere, Category = Solver)
	bool bStartFromRefPose = false;

	/** when true, goals will use the current transforms stored in the IK Rig Definition asset itself */
	bool bDriveWithSourceAsset = false;

#if WITH_EDITORONLY_DATA
	/** Toggle drawing of axes to debug joint rotation*/
	UPROPERTY(EditAnywhere, Category = Solver)
	bool bEnableDebugDraw;
#endif

private:

	/** IK Rig runtime processor */
	UPROPERTY(Transient)
	FIKRigProcessor IKRigProcessor;

	/** a cached list of components on the owning actor that implement the goal creator interface */
	TArray<IIKGoalCreatorInterface*> GoalCreators;
	TMap<FName, FIKRigGoal> GoalsFromGoalCreators;
	
	TMap<FCompactPoseBoneIndex, int32, FDefaultSetAllocator, TCompactPoseBoneIndexMapKeyFuncs<int32>> CompactPoseToRigIndices;

public:
	
	FAnimNode_IKRig();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)  override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

private:
	void CopyInputPoseToSolver(FCompactPose& InputPose);
	void AssignGoalTargets();
	void CopyOutputPoseToAnimGraph(FCompactPose& OutputPose);
	
	bool RebuildGoalList();
	FName GetGoalName(int32 Index) const;
	void QueueDrawInterface(FAnimInstanceProxy* AnimProxy, const FTransform& ComponentToWorld) const;
	friend class UAnimGraphNode_IKRig;
};
