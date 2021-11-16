// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_StrideWarping.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimRootMotionProvider.h"

DECLARE_CYCLE_STAT(TEXT("StrideWarping Eval"), STAT_StrideWarping_Eval, STATGROUP_Anim);

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarAnimNodeStrideWarpingDebug(TEXT("a.AnimNode.StrideWarping.Debug"), 0, TEXT("Turn on visualization debugging for Stride Warping"));
TAutoConsoleVariable<int32> CVarAnimNodeStrideWarpingVerbose(TEXT("a.AnimNode.StrideWarping.Verbose"), 0, TEXT("Turn on verbose graph debugging for Stride Warping"));
TAutoConsoleVariable<int32> CVarAnimNodeStrideWarpingEnable(TEXT("a.AnimNode.StrideWarping.Enable"), 1, TEXT("Toggle Stride Warping"));
#endif

void FAnimNode_StrideWarping::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeStrideWarpingVerbose.GetValueOnAnyThread() == 1)
	{
		DebugLine += FString::Printf(TEXT("\n - Evaluation Mode: (%s)"), Mode == EWarpingEvaluationMode::Graph ? TEXT("Graph") : TEXT("Manual"));
		DebugLine += FString::Printf(TEXT("\n - Stride Scale: (%.3fd)"), StrideScale);
		DebugLine += FString::Printf(TEXT("\n - Stride Direction: (%s)"), *(StrideDirection.ToCompactString()));
		if (Mode == EWarpingEvaluationMode::Graph)
		{
			DebugLine += FString::Printf(TEXT("\n - Locomotion Speed: (%.3fd)"), LocomotionSpeed);
			DebugLine += FString::Printf(TEXT("\n - Min Locomotion Speed Threshold: (%.3fd)"), MinLocomotionSpeedThreshold);
		}
		DebugLine += FString::Printf(TEXT("\n - Floor Normal: (%s)"), *(FloorNormalDirection.Value.ToCompactString()));
		DebugLine += FString::Printf(TEXT("\n - Gravity Direction: (%s)"), *(GravityDirection.Value.ToCompactString()));
	}
	else
#endif
	{
		DebugLine += FString::Printf(TEXT("(Stride Scale: %.3fd, Direction: %s)"), StrideScale, *(StrideDirection.ToCompactString()));
	}
	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_StrideWarping::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	AnimInstanceProxy = Context.AnimInstanceProxy;
	StrideScaleModifierState.Reinitialize();
}

void FAnimNode_StrideWarping::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	CachedDeltaTime = Context.GetDeltaTime();
}

void FAnimNode_StrideWarping::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_StrideWarping_Eval);
	check(OutBoneTransforms.Num() == 0);

	FTransform RootMotionTransformDelta;
	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();
	bool bGraphDrivenWarping = RootMotionProvider && Mode == EWarpingEvaluationMode::Graph;

	if (bGraphDrivenWarping)
	{
		// Graph driven stride warping will override the manual stride direction with the intent of the current animation sub-graph's accumulated root motion
		bGraphDrivenWarping = RootMotionProvider->ExtractRootMotion(Output.CustomAttributes, RootMotionTransformDelta);
		if (ensure(bGraphDrivenWarping))
		{
			StrideDirection = RootMotionTransformDelta.GetTranslation().GetSafeNormal();
		}
	}

	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
	const FTransform IKFootRootTransform = Output.Pose.GetComponentSpaceTransform(IKFootRootBone.GetCompactPoseIndex(RequiredBones));
	const FVector ResolvedFloorNormal = FloorNormalDirection.AsComponentSpaceDirection(AnimInstanceProxy, IKFootRootTransform);
	const FVector ResolvedGravityDirection = GravityDirection.AsComponentSpaceDirection(AnimInstanceProxy, IKFootRootTransform);

	if (bOrientStrideDirectionUsingFloorNormal)
	{
		const FVector StrideWarpingAxis = ResolvedFloorNormal ^ StrideDirection;
		StrideDirection = StrideWarpingAxis ^ ResolvedFloorNormal;
	}

#if ENABLE_ANIM_DEBUG
	bool bDebugging = false;
#if WITH_EDITORONLY_DATA
	bDebugging = bDebugging || bEnableDebugDraw;
#endif
	bDebugging = bDebugging || CVarAnimNodeStrideWarpingDebug.GetValueOnAnyThread() == 1;
	if (bDebugging)
	{
		// Draw Floor Normal
		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			AnimInstanceProxy->GetComponentTransform().TransformPosition(IKFootRootTransform.GetLocation()),
			AnimInstanceProxy->GetComponentTransform().TransformPosition(IKFootRootTransform.GetLocation() + ResolvedFloorNormal * 25.f),
			40.f, FColor::Blue, false, 0.f, 2.f);
	}
#endif

	// Get all foot IK Transforms
	for (auto& Foot : FootData)
	{
		Foot.IKFootBoneTransform = Output.Pose.GetComponentSpaceTransform(Foot.IKFootBoneIndex);
#if ENABLE_ANIM_DEBUG
		if (bDebugging
#if WITH_EDITORONLY_DATA
			&& bDebugDrawIKFootOrigin
#endif
		)
		{
			const FVector FootWorldLocation = AnimInstanceProxy->GetComponentTransform().TransformPosition(Foot.IKFootBoneTransform.GetLocation());
			AnimInstanceProxy->AnimDrawDebugSphere(FootWorldLocation, 8.f, 16, FColor::Red);
		}
#endif
	}

	if (bGraphDrivenWarping)
	{
		// Early out when stride warping will have minimal pose contribution
		if (FMath::IsNearlyZero(CachedDeltaTime, KINDA_SMALL_NUMBER))
		{
			return;
		}

		const float RootMotionSpeed = RootMotionTransformDelta.GetTranslation().Size() / CachedDeltaTime;

		if (FMath::IsNearlyZero(RootMotionSpeed, MinLocomotionSpeedThreshold) || FMath::IsNearlyZero(LocomotionSpeed, MinLocomotionSpeedThreshold))
		{
			StrideScale = 1.f;
		}
		else
		{
			// Graph driven stride scale factor will be determined by the ratio of the
			// locomotion (capsule/physics) speed against the animation root motion speed
			StrideScale = LocomotionSpeed / RootMotionSpeed;
		}
	}

	// Allow the opportunity for stride scale clamping and biasing regardless of evaluation mode
	StrideScale = StrideScaleModifierState.ApplyTo(StrideScaleModifier, StrideScale, CachedDeltaTime);

	if (bGraphDrivenWarping)
	{
		// Forward the side effects of stride warping on the root motion contribution for this sub-graph
		RootMotionTransformDelta.ScaleTranslation(StrideScale);
		const bool bRootMotionOverridden = RootMotionProvider->OverrideRootMotion(RootMotionTransformDelta, Output.CustomAttributes);
		ensure(bRootMotionOverridden);
	}

	// Scale IK feet bones along Stride Warping Axis, from the Thigh bone location.
	for (auto& Foot : FootData)
	{
		// Stride Warping along Stride Warping Axis
		const FVector IKFootLocation = Foot.IKFootBoneTransform.GetLocation();
		const FVector ThighBoneLocation = Output.Pose.GetComponentSpaceTransform(Foot.ThighBoneIndex).GetLocation();

		// Project Thigh Bone Location on plane made of FootIKLocation and FloorPlaneNormal, along Gravity Dir.
		// This will be the StrideWarpingPlaneOrigin
		const FVector StrideWarpingPlaneOrigin = (FMath::Abs(ResolvedGravityDirection | ResolvedFloorNormal) > DELTA) ? FMath::LinePlaneIntersection(ThighBoneLocation, ThighBoneLocation + ResolvedGravityDirection, IKFootLocation, ResolvedFloorNormal) : IKFootLocation;

		// Project FK Foot along StrideWarping Plane, this will be our Scale Origin
		const FVector ScaleOrigin = FVector::PointPlaneProject(IKFootLocation, StrideWarpingPlaneOrigin, StrideDirection);

		// Now the ScaleOrigin and IKFootLocation are forming a line parallel to the floor, and we can scale the IK foot.
		const FVector WarpedLocation = ScaleOrigin + (IKFootLocation - ScaleOrigin) * StrideScale;
		Foot.IKFootBoneTransform.SetLocation(WarpedLocation);

#if ENABLE_ANIM_DEBUG
		if (bDebugging
#if WITH_EDITORONLY_DATA
			&& bDebugDrawIKFootAdjustment
#endif
		)
		{
			const FVector FootWorldLocation = AnimInstanceProxy->GetComponentTransform().TransformPosition(Foot.IKFootBoneTransform.GetLocation());
			AnimInstanceProxy->AnimDrawDebugSphere(FootWorldLocation, 8.f, 16, FColor::Green);

			const FVector ScaleOriginWorldLoc = AnimInstanceProxy->GetComponentTransform().TransformPosition(ScaleOrigin);
			AnimInstanceProxy->AnimDrawDebugSphere(ScaleOriginWorldLoc, 8.f, 16, FColor::Yellow);
		}
#endif
	}

	FVector PelvisOffset = FVector::ZeroVector;
	const FCompactPoseBoneIndex PelvisBoneIndex = PelvisBone.GetCompactPoseIndex(RequiredBones);
	check(PelvisBoneIndex != INDEX_NONE);

	FTransform PelvisTransform = Output.Pose.GetComponentSpaceTransform(PelvisBoneIndex);
	const FVector InitialPelvisLocation = PelvisTransform.GetLocation();

	TArray<float, TInlineAllocator<10>> FKFootDistancesToPelvis;
	FKFootDistancesToPelvis.Reserve(FootData.Num());

	TArray<FVector, TInlineAllocator<10>> IKFootLocations;
	IKFootLocations.Reserve(FootData.Num());
	
	for (auto& Foot : FootData)
	{
		const FVector FKFootLocation = Output.Pose.GetComponentSpaceTransform(Foot.FKFootBoneIndex).GetLocation();
		FKFootDistancesToPelvis.Add(FVector::Dist(FKFootLocation, InitialPelvisLocation));

		const FVector IKFootLocation = Foot.IKFootBoneTransform.GetLocation();
		IKFootLocations.Add(IKFootLocation);
	}

	// Adjust Pelvis down if needed to keep foot contact with the ground and prevent over-extension
	PelvisTransform = PelvisIKFootSolver.Solve(PelvisTransform, FKFootDistancesToPelvis, IKFootLocations, CachedDeltaTime);

#if ENABLE_ANIM_DEBUG
	if (bDebugging
#if WITH_EDITORONLY_DATA
		&& bDebugDrawPelvisAdjustment
#endif
	)
	{
		// Draw Adjustments in Pelvis location
		AnimInstanceProxy->AnimDrawDebugSphere(AnimInstanceProxy->GetComponentTransform().TransformPosition(InitialPelvisLocation), 8.f, 16, FColor::Red);
		AnimInstanceProxy->AnimDrawDebugSphere(AnimInstanceProxy->GetComponentTransform().TransformPosition(PelvisTransform.GetLocation()), 8.f, 16, FColor::Blue);
	}

	if (bDebugging)
	{
		// Draw Stride Direction
		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			AnimInstanceProxy->GetComponentTransform().TransformPosition(InitialPelvisLocation),
			AnimInstanceProxy->GetComponentTransform().TransformPosition(InitialPelvisLocation + StrideDirection * StrideScale * 100.f),
			40.f, FColor::Red, false, 0.f, 2.f);
	}
#endif
	// Add adjusted pelvis transform
	check(!PelvisTransform.ContainsNaN());
	OutBoneTransforms.Add(FBoneTransform(PelvisBoneIndex, PelvisTransform));

	// Compute final offset to use below
	PelvisOffset = (PelvisTransform.GetLocation() - InitialPelvisLocation);

	// Rotate Thigh bones to help IK, and maintain leg shape.
	if (bCompensateIKUsingFKThighRotation)
	{
		for (auto& Foot : FootData)
		{
			const FTransform ThighTransform = Output.Pose.GetComponentSpaceTransform(Foot.ThighBoneIndex);
			const FTransform FKFootTransform = Output.Pose.GetComponentSpaceTransform(Foot.FKFootBoneIndex);
			
			FTransform AdjustedThighTransform = ThighTransform;
			AdjustedThighTransform.AddToTranslation(PelvisOffset);

			const FVector InitialDir = (FKFootTransform.GetLocation() - ThighTransform.GetLocation()).GetSafeNormal();
			const FVector TargetDir = (Foot.IKFootBoneTransform.GetLocation() - AdjustedThighTransform.GetLocation()).GetSafeNormal();
			
#if ENABLE_ANIM_DEBUG
			if (bDebugging
#if WITH_EDITORONLY_DATA
				&& bDebugDrawThighAdjustment
#endif
			)
			{
				AnimInstanceProxy->AnimDrawDebugLine(
					AnimInstanceProxy->GetComponentTransform().TransformPosition(ThighTransform.GetLocation()),
					AnimInstanceProxy->GetComponentTransform().TransformPosition(FKFootTransform.GetLocation()),
					FColor::Red, false, 0.f, 2.f);

				AnimInstanceProxy->AnimDrawDebugLine(
					AnimInstanceProxy->GetComponentTransform().TransformPosition(AdjustedThighTransform.GetLocation()),
					AnimInstanceProxy->GetComponentTransform().TransformPosition(Foot.IKFootBoneTransform.GetLocation()),
					FColor::Green, false, 0.f, 2.f);
			}
#endif
			// Find Delta Rotation take takes us from Old to New dir
			const FQuat DeltaRotation = FQuat::FindBetweenNormals(InitialDir, TargetDir);

			// Rotate our Joint quaternion by this delta rotation
			AdjustedThighTransform.SetRotation(DeltaRotation * AdjustedThighTransform.GetRotation());

			// Add adjusted thigh transform
			check(!AdjustedThighTransform.ContainsNaN());
			OutBoneTransforms.Add(FBoneTransform(Foot.ThighBoneIndex, AdjustedThighTransform));

			// Clamp IK Feet bone based on FK leg. To prevent over-extension and preserve animated motion.
			if (bClampIKUsingFKLimits)
			{
				const float FKLength = FVector::Dist(FKFootTransform.GetLocation(), ThighTransform.GetLocation());
				const float IKLength = FVector::Dist(Foot.IKFootBoneTransform.GetLocation(), AdjustedThighTransform.GetLocation());
				if (IKLength > FKLength)
				{
					const FVector ClampedFootLocation = AdjustedThighTransform.GetLocation() + TargetDir * FKLength;
					Foot.IKFootBoneTransform.SetLocation(ClampedFootLocation);
				}
			}
		}
	}

	// Add final IK feet transforms
	for (auto& Foot : FootData)
	{
#if ENABLE_ANIM_DEBUG
		if (bDebugging
#if WITH_EDITORONLY_DATA
			&& bDebugDrawIKFootFinal
#endif
		)
		{
			AnimInstanceProxy->AnimDrawDebugSphere(AnimInstanceProxy->GetComponentTransform().TransformPosition(Foot.IKFootBoneTransform.GetLocation()), 8.f, 16, FColor::Blue);
		}
#endif
		check(!Foot.IKFootBoneTransform.ContainsNaN());
		OutBoneTransforms.Add(FBoneTransform(Foot.IKFootBoneIndex, Foot.IKFootBoneTransform));
	}

	// Sort OutBoneTransforms so indices are in increasing order.
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

bool FAnimNode_StrideWarping::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeStrideWarpingEnable.GetValueOnAnyThread() == 0)
	{
		return false;
	}
#endif
	if (PelvisBone.GetCompactPoseIndex(RequiredBones) == INDEX_NONE)
	{
		return false;
	}
	
	if (IKFootRootBone.GetCompactPoseIndex(RequiredBones) == INDEX_NONE)
	{
		return false;
	}

	if (FootData.IsEmpty())
	{
		return false;
	}
	else
	{
		for (const auto& Foot : FootData)
		{
			if (Foot.IKFootBoneIndex == INDEX_NONE || Foot.FKFootBoneIndex == INDEX_NONE || Foot.ThighBoneIndex == INDEX_NONE)
			{
				return false;
			}
		}
	}
	
	if (Mode == EWarpingEvaluationMode::Manual)
	{
		if (FMath::IsNearlyEqual(StrideScaleModifierState.ApplyTo(StrideScaleModifier, StrideScale, CachedDeltaTime), 1.f, KINDA_SMALL_NUMBER))
		{
			return false;
		}
	}
	else if (LocomotionSpeed <= MinLocomotionSpeedThreshold)
	{
		return false;
	}
	return true;
}

void FAnimNode_StrideWarping::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	IKFootRootBone.Initialize(RequiredBones);
	PelvisBone.Initialize(RequiredBones);
	FootData.Empty();

	for (auto& Foot : FootDefinitions)
	{
		Foot.IKFootBone.Initialize(RequiredBones);
		Foot.FKFootBone.Initialize(RequiredBones);
		Foot.ThighBone.Initialize(RequiredBones);

		FStrideWarpingFootData StrideFootData;
		StrideFootData.IKFootBoneIndex = Foot.IKFootBone.GetCompactPoseIndex(RequiredBones);
		StrideFootData.FKFootBoneIndex = Foot.FKFootBone.GetCompactPoseIndex(RequiredBones);
		StrideFootData.ThighBoneIndex = Foot.ThighBone.GetCompactPoseIndex(RequiredBones);

		if ((StrideFootData.IKFootBoneIndex != INDEX_NONE) && (StrideFootData.FKFootBoneIndex != INDEX_NONE) && (StrideFootData.ThighBoneIndex != INDEX_NONE))
		{
			FootData.Add(StrideFootData);
		}
	}
}