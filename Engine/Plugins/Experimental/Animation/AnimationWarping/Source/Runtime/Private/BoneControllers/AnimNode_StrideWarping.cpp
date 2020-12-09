// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_StrideWarping.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "DrawDebugHelpers.h"
#include "Animation/InputScaleBias.h"

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarFortAnimNodeStrideWarpingDebug(TEXT("a.AnimNode.StrideWarping.Debug"), 0, TEXT("Turn on debug for AnimNode_StrideWarping"));
#endif

TAutoConsoleVariable<int32> CVarFortStrideWarpingEnable(TEXT("a.AnimNode.StrideWarping.Enable"), 1, TEXT("Toggle Stride Warping"));

DECLARE_CYCLE_STAT(TEXT("StrideWarping Eval"), STAT_StrideWarping_Eval, STATGROUP_Anim);

FAnimNode_StrideWarping::FAnimNode_StrideWarping()
	: StrideWarpingAxisMode(EStrideWarpingAxisMode::ActorSpaceVectorInput)
	, FloorNormalAxisMode(EStrideWarpingAxisMode::IKFootRootLocalZ)
	, GravityDirAxisMode(EStrideWarpingAxisMode::ComponentSpaceVectorInput)
	, StrideScaling(1.f)
	, ManualStrideWarpingDir(ForceInitToZero)
	, ManualFloorNormalInput(ForceInitToZero)
	, ManualGravityDirInput(-FVector::UpVector)
	, PelvisPostAdjustmentAlpha(0.4f)
	, PelvisAdjustmentMaxIter(3)
	, bAdjustThighBonesRotation(true)
	, bClampIKUsingFKLeg(true)
	, bOrientStrideWarpingAxisBasedOnFloorNormal(true)
	, CachedDeltaTime(0.f)
{
}

void FAnimNode_StrideWarping::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);

	MyAnimInstanceProxy = Context.AnimInstanceProxy;
	StrideScalingScaleBiasClamp.Reinitialize();
}

void FAnimNode_StrideWarping::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);

// 	DebugLine += "(";
// 	AddDebugNodeData(DebugLine);
// 	DebugLine += FString::Printf(TEXT(" Target: %s)"), *BoneToModify.BoneName.ToString());

	DebugData.AddDebugItem(DebugLine);

	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_StrideWarping::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);

	CachedDeltaTime += Context.GetDeltaTime();
}

FVector FAnimNode_StrideWarping::GetAxisModeValue(const EStrideWarpingAxisMode& AxisMode, const FTransform& IKFootRootCSTransform, const FVector& UserSuppliedVector) const
{
	switch (AxisMode)
	{
	case EStrideWarpingAxisMode::IKFootRootLocalX: return IKFootRootCSTransform.GetUnitAxis(EAxis::X);
	case EStrideWarpingAxisMode::IKFootRootLocalY: return IKFootRootCSTransform.GetUnitAxis(EAxis::Y);
	case EStrideWarpingAxisMode::IKFootRootLocalZ: return IKFootRootCSTransform.GetUnitAxis(EAxis::Z);
	case EStrideWarpingAxisMode::ComponentSpaceVectorInput: return UserSuppliedVector.GetSafeNormal();
	case EStrideWarpingAxisMode::ActorSpaceVectorInput : 
		{
			const FVector WorldSpaceDir = MyAnimInstanceProxy->GetActorTransform().TransformVectorNoScale(UserSuppliedVector);
			const FVector ComponentSpaceDir = MyAnimInstanceProxy->GetComponentTransform().InverseTransformVectorNoScale(WorldSpaceDir);
			return ComponentSpaceDir.GetSafeNormal();
		}
	case EStrideWarpingAxisMode::WorldSpaceVectorInput: 
		return MyAnimInstanceProxy->GetComponentTransform().InverseTransformVectorNoScale(UserSuppliedVector).GetSafeNormal();
	}

	return IKFootRootCSTransform.GetUnitAxis(EAxis::X);
}

void FAnimNode_StrideWarping::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_StrideWarping_Eval);

	check(OutBoneTransforms.Num() == 0);
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	FTransform IKFootRootTransform = Output.Pose.GetComponentSpaceTransform(IKFootRootBone.GetCompactPoseIndex(RequiredBones));
	FVector StrideWarpingPlaneNormal = GetAxisModeValue(StrideWarpingAxisMode, IKFootRootTransform, ManualStrideWarpingDir);
	const FVector FloorPlaneNormal = GetAxisModeValue(FloorNormalAxisMode, IKFootRootTransform, ManualFloorNormalInput);
	const FVector GravityDir = GetAxisModeValue(GravityDirAxisMode, IKFootRootTransform, ManualGravityDirInput);

	if (bOrientStrideWarpingAxisBasedOnFloorNormal)
	{
		const FVector StrideWarpingAxisY = FloorPlaneNormal ^ StrideWarpingPlaneNormal;
		StrideWarpingPlaneNormal = StrideWarpingAxisY ^ FloorPlaneNormal;
	}

#if ENABLE_ANIM_DEBUG
	const bool bShowDebug = (CVarFortAnimNodeStrideWarpingDebug.GetValueOnAnyThread() == 1);
	UWorld* DebugWorld = MyAnimInstanceProxy->GetSkelMeshComponent()->GetWorld();
	if (bShowDebug)
	{
		// Draw Floor Normal
		DrawDebugDirectionalArrow(DebugWorld
			, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(IKFootRootTransform.GetLocation())
			, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(IKFootRootTransform.GetLocation() + FloorPlaneNormal * 500.f), 50.f, FColor::Blue);
	}
#endif

	// Get all foot IK Transforms
	for (auto& FootData : FeetData)
	{
		FootData.IKBoneTransform = Output.Pose.GetComponentSpaceTransform(FootData.IKFootBoneIndex);
#if ENABLE_ANIM_DEBUG
		if (bShowDebug)
		{
			FVector FootWorldLocation = MyAnimInstanceProxy->GetComponentTransform().TransformPosition(FootData.IKBoneTransform.GetLocation());
			DrawDebugSphere(DebugWorld, FootWorldLocation, 8.f, 16, FColor::Red);
		}
#endif
	}

	// Scale IK feet bones along Stride Warping Axis, from the hip bone location.
	const float ActualStrideScaling = StrideScalingScaleBiasClamp.ApplyTo(StrideScaling, CachedDeltaTime);
	for (auto& FootData : FeetData)
	{
		// Stride Warping along Stride Warping Axis
		const FVector IKFootLocation = FootData.IKBoneTransform.GetLocation();
		const FVector HipBoneLocation = Output.Pose.GetComponentSpaceTransform(FootData.HipBoneIndex).GetLocation();

		// Project Hip Bone Location on plane made of FootIKLocation and FloorPlaneNormal, along Gravity Dir.
		// This will be the StrideWarpingPlaneOrigin
		const FVector StrideWarpingPlaneOrigin = (FMath::Abs(GravityDir | FloorPlaneNormal) > DELTA) ? FMath::LinePlaneIntersection(HipBoneLocation, HipBoneLocation + GravityDir, IKFootLocation, FloorPlaneNormal) : IKFootLocation;

		// Project FK Foot along StrideWarping Plane, this will be our Scale Origin
		const FVector ScaleOrigin = FVector::PointPlaneProject(IKFootLocation, StrideWarpingPlaneOrigin, StrideWarpingPlaneNormal);

		// Now the ScaleOrigin and IKFootLocation are forming a line parallel to the floor, and we can scale the IK foot.
		const FVector WarpedLocation = ScaleOrigin + (IKFootLocation - ScaleOrigin) * ActualStrideScaling;
		FootData.IKBoneTransform.SetLocation(WarpedLocation);

#if ENABLE_ANIM_DEBUG
		if (bShowDebug)
		{
			FVector FootWorldLocation = MyAnimInstanceProxy->GetComponentTransform().TransformPosition(FootData.IKBoneTransform.GetLocation());
			DrawDebugSphere(DebugWorld, FootWorldLocation, 8.f, 16, FColor::Green);

			FVector ScaleOriginWorldLoc = MyAnimInstanceProxy->GetComponentTransform().TransformPosition(ScaleOrigin);
			DrawDebugSphere(DebugWorld, ScaleOriginWorldLoc, 8.f, 16, FColor::Yellow);
		}
#endif
	}

	// Adjust Pelvis down if needed to keep foot contact with the ground and prevent over-extension
	FVector PelvisOffset = FVector::ZeroVector;
	const FCompactPoseBoneIndex PelvisBoneIndex = PelvisBone.GetCompactPoseIndex(RequiredBones);

	if ((PelvisBoneIndex != INDEX_NONE) && (PelvisPostAdjustmentAlpha > 0.f))
	{
		FTransform PelvisTransform = Output.Pose.GetComponentSpaceTransform(PelvisBoneIndex);
		const FVector InitialPelvisLocation = PelvisTransform.GetLocation();

		TArray<float> FKFeetDistToPelvis;
		TArray<FVector> IKFeetLocation;
	
		for (auto& FootData : FeetData)
		{
			const FVector FKFootLocation = Output.Pose.GetComponentSpaceTransform(FootData.FKFootBoneIndex).GetLocation();
			FKFeetDistToPelvis.Add(FVector::Dist(FKFootLocation, InitialPelvisLocation));

			const FVector IKFootLocation = FootData.IKBoneTransform.GetLocation();
			IKFeetLocation.Add(IKFootLocation);
		}

		// Pull Pelvis closer to feet iteratively
		FVector AdjustedPelvisLocation = InitialPelvisLocation;
		{
			check(IKFeetLocation.Num() > 0);
			const float PerFootWeight = (1.f / float(IKFeetLocation.Num()));

			int32 Iterations = FMath::Clamp(PelvisAdjustmentMaxIter, 1, 10);
			while (Iterations-- > 0)
			{
				const FVector PreAdjustmentLocation = AdjustedPelvisLocation;
				AdjustedPelvisLocation = FVector::ZeroVector;

				for (int32 Index = 0; Index < IKFeetLocation.Num(); Index++)
				{
					const FVector IdealPelvisLoc = IKFeetLocation[Index] + (PreAdjustmentLocation - IKFeetLocation[Index]).GetSafeNormal() * FKFeetDistToPelvis[Index];
					AdjustedPelvisLocation += IdealPelvisLoc * PerFootWeight;
				}
			}
		}
		
		// Apply spring between initial and adjusted spring location to smooth out change over time.
		const FVector TargetAdjustment = (AdjustedPelvisLocation - InitialPelvisLocation);
		PelvisAdjustmentInterp.Update(TargetAdjustment, CachedDeltaTime);

		// Apply an alpha with the initial pelvis location, to retain some of the original motion. 
		const FVector SmoothAdjustedPelvisLocation = InitialPelvisLocation + FMath::Lerp(FVector::ZeroVector, PelvisAdjustmentInterp.GetPosition(), PelvisPostAdjustmentAlpha);
		PelvisTransform.SetLocation(SmoothAdjustedPelvisLocation);

#if ENABLE_ANIM_DEBUG
		if (bShowDebug)
		{
			// Draw Adjustments in Pelvis location
			DrawDebugSphere(DebugWorld, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(InitialPelvisLocation), 8.f, 16, FColor::Red);
			DrawDebugSphere(DebugWorld, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(AdjustedPelvisLocation), 8.f, 16, FColor::Green);
			DrawDebugSphere(DebugWorld, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(SmoothAdjustedPelvisLocation), 8.f, 16, FColor::Blue);

			// Draw Stride Direction
			DrawDebugDirectionalArrow(DebugWorld
				, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(InitialPelvisLocation)
				, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(InitialPelvisLocation + StrideWarpingPlaneNormal * 500.f), 50.f, FColor::Red);
		}
#endif

		// Add adjusted pelvis transform
		check(!PelvisTransform.ContainsNaN());
		OutBoneTransforms.Add(FBoneTransform(PelvisBoneIndex, PelvisTransform));

		// Compute final offset to use below
		PelvisOffset = (PelvisTransform.GetLocation() - InitialPelvisLocation);
	}

	// Rotate Thigh bones to help IK, and maintain leg shape.
	if (bAdjustThighBonesRotation)
	{
		for (auto& FootData : FeetData)
		{
			const FTransform HipTransform = Output.Pose.GetComponentSpaceTransform(FootData.HipBoneIndex);
			const FTransform FKFootTransform = Output.Pose.GetComponentSpaceTransform(FootData.FKFootBoneIndex);
			FTransform AdjustedHipTransform = HipTransform;

			AdjustedHipTransform.AddToTranslation(PelvisOffset);

			const FVector InitialDir = (FKFootTransform.GetLocation() - HipTransform.GetLocation()).GetSafeNormal();
			const FVector TargetDir = (FootData.IKBoneTransform.GetLocation() - AdjustedHipTransform.GetLocation()).GetSafeNormal();
			
#if ENABLE_ANIM_DEBUG
			if (bShowDebug)
			{
				DrawDebugLine(DebugWorld
					, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(HipTransform.GetLocation())
					, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(FKFootTransform.GetLocation())
					, FColor::Red);

				DrawDebugLine(DebugWorld
					, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(AdjustedHipTransform.GetLocation())
					, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(FootData.IKBoneTransform.GetLocation())
					, FColor::Green);
			}
#endif

			// Find Delta Rotation take takes us from Old to New dir
			const FQuat DeltaRotation = FQuat::FindBetweenNormals(InitialDir, TargetDir);

			// Rotate our Joint quaternion by this delta rotation
			AdjustedHipTransform.SetRotation(DeltaRotation * AdjustedHipTransform.GetRotation());

			// Add adjusted hip transform
			check(!AdjustedHipTransform.ContainsNaN());
			OutBoneTransforms.Add(FBoneTransform(FootData.HipBoneIndex, AdjustedHipTransform));

			// Clamp IK Feet bone based on FK leg. To prevent over-extension and preserve animated motion.
			if (bClampIKUsingFKLeg)
			{
				const float FKLength = FVector::Dist(FKFootTransform.GetLocation(), HipTransform.GetLocation());
				const float IKLength = FVector::Dist(FootData.IKBoneTransform.GetLocation(), AdjustedHipTransform.GetLocation());
				if (IKLength > FKLength)
				{
					const FVector ClampedFootLocation = AdjustedHipTransform.GetLocation() + TargetDir * FKLength;
					FootData.IKBoneTransform.SetLocation(ClampedFootLocation);
				}
			}
		}
	}

	// Add final IK feet transforms
	for (auto& FootData : FeetData)
	{
#if ENABLE_ANIM_DEBUG
		if (bShowDebug)
		{
			DrawDebugSphere(DebugWorld, MyAnimInstanceProxy->GetComponentTransform().TransformPosition(FootData.IKBoneTransform.GetLocation()), 8.f, 16, FColor::Blue);
		}
#endif
		check(!FootData.IKBoneTransform.ContainsNaN());
		OutBoneTransforms.Add(FBoneTransform(FootData.IKFootBoneIndex, FootData.IKBoneTransform));
	}

	// Sort OutBoneTransforms so indices are in increasing order.
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());

	// Clear time accumulator, to be filled during next update.
	CachedDeltaTime = 0.f;
}

bool FAnimNode_StrideWarping::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	const bool bIsEnabled = (CVarFortStrideWarpingEnable.GetValueOnAnyThread() == 1);
	return bIsEnabled && (FeetData.Num() > 0) 
		&& (PelvisBone.GetCompactPoseIndex(RequiredBones) != INDEX_NONE) 
		&& (IKFootRootBone.GetCompactPoseIndex(RequiredBones) != INDEX_NONE) 
		&& (!FMath::IsNearlyEqual(StrideScalingScaleBiasClamp.ApplyTo(StrideScaling, 0.f), 1.f, 0.001f) || PelvisAdjustmentInterp.IsInMotion());
}

FCompactPoseBoneIndex FAnimNode_StrideWarping::FindHipBoneIndex(const FCompactPoseBoneIndex& InFootBoneIndex, const int32& NumBonesInLimb, const FBoneContainer& RequiredBones) const
{
	FCompactPoseBoneIndex BoneIndex = InFootBoneIndex;
	if (BoneIndex != INDEX_NONE)
	{
		FCompactPoseBoneIndex ParentBoneIndex = RequiredBones.GetParentBoneIndex(BoneIndex);

		int32 NumIterations = NumBonesInLimb;
		while ((NumIterations-- > 0) && (ParentBoneIndex != INDEX_NONE))
		{
			BoneIndex = ParentBoneIndex;
			ParentBoneIndex = RequiredBones.GetParentBoneIndex(BoneIndex);
		};
	}

	return BoneIndex;
}

void FAnimNode_StrideWarping::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	IKFootRootBone.Initialize(RequiredBones);
	PelvisBone.Initialize(RequiredBones);

	FeetData.Empty();
	for (auto& FootDef : FeetDefinitions)
	{
		FootDef.IKFootBone.Initialize(RequiredBones);
		FootDef.FKFootBone.Initialize(RequiredBones);

		FStrideWarpingFootData FootData;
		FootData.IKFootBoneIndex = FootDef.IKFootBone.GetCompactPoseIndex(RequiredBones);
		FootData.FKFootBoneIndex = FootDef.FKFootBone.GetCompactPoseIndex(RequiredBones);
		FootData.HipBoneIndex = FindHipBoneIndex(FootData.FKFootBoneIndex, FMath::Max(FootDef.NumBonesInLimb, 1), RequiredBones);

		if ((FootData.IKFootBoneIndex != INDEX_NONE) && (FootData.FKFootBoneIndex != INDEX_NONE) && (FootData.HipBoneIndex != INDEX_NONE))
		{
			FeetData.Add(FootData);
		}
	}
}