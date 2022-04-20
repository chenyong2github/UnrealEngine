// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Math/UnrealMathUtility.h"

DECLARE_CYCLE_STAT(TEXT("OffsetRootBone Eval"), STAT_OffsetRootBone_Eval, STATGROUP_Anim);

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<int32> CVarAnimNodeOffsetRootBoneDebug(TEXT("a.AnimNode.OffsetRootBone.Debug"), 0, TEXT("Turn on visualization debugging for Offset Root Bone"));
TAutoConsoleVariable<int32> CVarAnimNodeOffsetRootBoneEnable(TEXT("a.AnimNode.OffsetRootBone.Enable"), 1, TEXT("Toggle Offset Root Bone"));
TAutoConsoleVariable<int32> CVarAnimNodeOffsetRootBoneModifyBone(TEXT("a.AnimNode.OffsetRootBone.ModifyBone"), 1, TEXT("Toggle whether the transform is applied to the bone"));
#endif


namespace UE::Anim::OffsetRootBone
{
	// Taken from https://theorangeduck.com/page/spring-roll-call#implicitspringdamper
	static float DamperImplicit(float Halflife, float DeltaTime, float Epsilon = 1e-8f)
	{
		return (1.0f - FMath::InvExpApprox((0.69314718056f * DeltaTime) / (Halflife + Epsilon)));
	}
}

// TODO: implement 
void FAnimNode_OffsetRootBone::GatherDebugData(FNodeDebugData& DebugData)
{
}

void FAnimNode_OffsetRootBone::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	AnimInstanceProxy = Context.AnimInstanceProxy;
	ComponentTransform = Context.AnimInstanceProxy->GetComponentTransform();
	SimulatedTranslation = ComponentTransform.GetLocation();
	SimulatedRotation = ComponentTransform.GetRotation();
}

void FAnimNode_OffsetRootBone::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	CachedDeltaTime = Context.GetDeltaTime();
}

void FAnimNode_OffsetRootBone::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_OffsetRootBone_Eval);
	check(OutBoneTransforms.Num() == 0);

	bool bGraphDriven = false;
	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();

	if (GetEvaluationMode() == EWarpingEvaluationMode::Graph)
	{
		bGraphDriven = RootMotionProvider != nullptr;
		ensureMsgf(bGraphDriven, TEXT("Graph driven Offset Root Bone expected a valid root motion delta provider interface."));
	}

	const FCompactPoseBoneIndex TargetBoneIndex(0);
	const FTransform InputBoneTransform = Output.Pose.GetComponentSpaceTransform(TargetBoneIndex);

	const FTransform LastComponentTransform = ComponentTransform;
	ComponentTransform = AnimInstanceProxy->GetComponentTransform();

	const bool bShouldConsumeTranslationOffset = GetOffsetSettings().TranslationMode == EOffsetRootBoneMode::Accumulate ||
										GetOffsetSettings().TranslationMode == EOffsetRootBoneMode::Interpolate;
	const bool bShouldConsumeRotationOffset = GetOffsetSettings().RotationMode == EOffsetRootBoneMode::Accumulate ||
										GetOffsetSettings().RotationMode == EOffsetRootBoneMode::Interpolate;

	FTransform RootMotionTransformDelta = FTransform::Identity;
	if (bGraphDriven)
	{
		// Graph driven mode will override translation and rotation delta
		// with the intent of the current animation sub-graph's accumulated root motion
		bGraphDriven = RootMotionProvider->ExtractRootMotion(Output.CustomAttributes, RootMotionTransformDelta);
	}
	else
	{
		// Apply the offset as is (component space) in manual mode
		RootMotionTransformDelta = FTransform(GetRotationDelta(), GetTranslationDelta());
	}

	FTransform ConsumedRootMotionDelta;

	if (bShouldConsumeTranslationOffset)
	{
		// Grab root motion translation from the root motion attribute
		ConsumedRootMotionDelta.SetTranslation(RootMotionTransformDelta.GetTranslation());
	}
	else
	{
		// Accumulate the translation frame delta into the simulated translation, to keep it in place
		const FVector ComponentTranslationDelta = ComponentTransform.GetLocation() - LastComponentTransform.GetLocation();
		SimulatedTranslation += ComponentTranslationDelta;
	}

	if (bShouldConsumeRotationOffset)
	{
		// Grab root motion rotation from the root motion attribute
		ConsumedRootMotionDelta.SetRotation(RootMotionTransformDelta.GetRotation());
	}
	else
	{
		// Accumulate the rotation frame delta into the simulated translation, to keep it in place
		const FQuat ComponentRotationDelta = LastComponentTransform.GetRotation().Inverse() * ComponentTransform.GetRotation();
		SimulatedRotation = ComponentRotationDelta * SimulatedRotation;
	}

	FTransform SimulatedTransform(SimulatedRotation, SimulatedTranslation);
	// Apply the root motion delta
	SimulatedTransform = ConsumedRootMotionDelta * SimulatedTransform;

	SimulatedTranslation = SimulatedTransform.GetLocation();
	SimulatedRotation = SimulatedTransform.GetRotation();
	
	// TODO: Make this a parameter
	const FVector GravityDirCS = -FVector::UpVector;
	// Simulated translation should stay the same along the approach direction
	SimulatedTranslation = FVector::PointPlaneProject(SimulatedTranslation, ComponentTransform.GetLocation(), GravityDirCS);

	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	bool bModifyBone = true;
#if ENABLE_ANIM_DEBUG
	bModifyBone = CVarAnimNodeOffsetRootBoneModifyBone.GetValueOnAnyThread() == 1;
#endif

	if (GetOffsetSettings().TranslationMode == EOffsetRootBoneMode::Release ||
		GetOffsetSettings().TranslationMode == EOffsetRootBoneMode::Interpolate)
	{
		FVector TranslationOffset = ComponentTransform.GetLocation() - SimulatedTranslation;

		const float DampenAlpha = UE::Anim::OffsetRootBone::DamperImplicit(GetOffsetSettings().TranslationHalflife, CachedDeltaTime);
		FVector TranslationOffsetDelta = FMath::Lerp(FVector::ZeroVector, TranslationOffset, DampenAlpha);

		if (GetOffsetSettings().bClampToTranslationVelocity)
		{
			const float RootMotionDelta = RootMotionTransformDelta.GetLocation().Size();
			const float MaxDelta = 	GetOffsetSettings().TranslationSpeedRatio * RootMotionDelta;

			const float AdjustmentDelta = TranslationOffsetDelta.Size();
			if (AdjustmentDelta > MaxDelta)
			{
				TranslationOffsetDelta = MaxDelta * TranslationOffsetDelta.GetSafeNormal2D();
			}
		}

		SimulatedTranslation = SimulatedTranslation + TranslationOffsetDelta;
	}

	if (GetOffsetSettings().RotationMode == EOffsetRootBoneMode::Release ||
		GetOffsetSettings().RotationMode == EOffsetRootBoneMode::Interpolate)
	{
		FQuat RotationOffset = ComponentTransform.GetRotation() * SimulatedRotation.Inverse();
		RotationOffset.Normalize();
		RotationOffset = RotationOffset.W < 0.0 ? -RotationOffset : RotationOffset;

		const float DampenAlpha = UE::Anim::OffsetRootBone::DamperImplicit(GetOffsetSettings().RotationHalfLife, CachedDeltaTime);
		FQuat RotationOffsetDelta = FQuat::Slerp(FQuat::Identity, RotationOffset, DampenAlpha);

		if (GetOffsetSettings().bClampToRotationVelocity)
		{
			float RotationMotionAngleDelta;
			FVector RootMotionRotationAxis;
			RootMotionTransformDelta.GetRotation().ToAxisAndAngle(RootMotionRotationAxis, RotationMotionAngleDelta);

			float MaxRotationAngle = GetOffsetSettings().RotationSpeedRatio * RotationMotionAngleDelta;

			FVector DeltaAxis;
			float DeltaAngle;
			RotationOffsetDelta.ToAxisAndAngle(DeltaAxis, DeltaAngle);

			if (DeltaAngle > MaxRotationAngle)
			{
				RotationOffsetDelta = FQuat(DeltaAxis, MaxRotationAngle);
			}
		}

		SimulatedRotation = RotationOffsetDelta * SimulatedRotation;
	}

	if (GetOffsetSettings().MaxTranslationError >= 0.0f)
	{
		FVector TranslationOffset = ComponentTransform.GetLocation() - SimulatedTranslation;
		const float TranslationOffsetSize = TranslationOffset.Size();
		if (TranslationOffsetSize > GetOffsetSettings().MaxTranslationError)
		{
			TranslationOffset = TranslationOffset.GetClampedToMaxSize(GetOffsetSettings().MaxTranslationError);
			SimulatedTranslation = ComponentTransform.GetLocation() - TranslationOffset;
		}
	}

	const float MaxAngleRadians = FMath::DegreesToRadians(GetOffsetSettings().MaxRotationError);
	if (GetOffsetSettings().MaxRotationError >= 0.0f)
	{
		FQuat RotationOffset = ComponentTransform.GetRotation().Inverse() * SimulatedRotation;
		RotationOffset.Normalize();
		RotationOffset = RotationOffset.W < 0.0 ? -RotationOffset : RotationOffset;

		FVector OffsetAxis;
		float OffsetAngle;
		RotationOffset.ToAxisAndAngle(OffsetAxis, OffsetAngle);

		if (FMath::Abs(OffsetAngle) > MaxAngleRadians)
		{
			RotationOffset = FQuat(OffsetAxis, MaxAngleRadians);
			SimulatedRotation = RotationOffset * ComponentTransform.GetRotation();
		}
	}

	// Apply the offset adjustments to the simulated transform
	SimulatedTransform.SetLocation(SimulatedTranslation);
	SimulatedTransform.SetRotation(SimulatedRotation);

	// Combine with the input pose's bone transform, to preserve any adjustments done before this node in the graph
	FTransform TargetBoneTransform = SimulatedTransform * ComponentTransform.Inverse();
	// Accumulate the input bone transform to keep the offset independent from any previous adjustments to the root
	TargetBoneTransform.Accumulate(InputBoneTransform);
	if (bModifyBone)
	{
		OutBoneTransforms.Add(FBoneTransform(TargetBoneIndex, TargetBoneTransform));
	}

#if ENABLE_ANIM_DEBUG
	bool bDebugging = CVarAnimNodeOffsetRootBoneDebug.GetValueOnAnyThread() == 1;
	if (bDebugging)
	{
		const float InnerCircleRadius = 40.0f;
		const float CircleThickness = 0.5f;
		const float ConeThickness = 0.2f;

		const FTransform TargetBoneInitialTransformWorld = InputBoneTransform * ComponentTransform;
		const FTransform TargetBoneTransformWorld = TargetBoneTransform * ComponentTransform;

		if (GetOffsetSettings().MaxTranslationError >= 0.0f)
		{
			const float OuterCircleRadius = GetOffsetSettings().MaxTranslationError + InnerCircleRadius;
			AnimInstanceProxy->AnimDrawDebugCircle(ComponentTransform.GetLocation(), OuterCircleRadius, 36, FColor::Red,
				FVector::UpVector, false, -1.0f, SDPG_World, CircleThickness);
		}

		AnimInstanceProxy->AnimDrawDebugCircle(ComponentTransform.GetLocation(), InnerCircleRadius, 36, FColor::Blue,
			FVector::UpVector, false, -1.0f, SDPG_World, CircleThickness);
			
		AnimInstanceProxy->AnimDrawDebugCircle(TargetBoneTransformWorld.GetLocation(), InnerCircleRadius, 36, FColor::Green,
			FVector::UpVector, false, -1.0f, SDPG_World, CircleThickness);

		const int32 ConeSegments =  FMath::RoundUpToPowerOfTwo((GetOffsetSettings().MaxRotationError / 180.0f) * 12 );
		const FVector ArcDirection = ComponentTransform.GetRotation().GetRightVector();
		AnimInstanceProxy->AnimDrawDebugCone(TargetBoneTransformWorld.GetLocation(), 0.9f * InnerCircleRadius, ArcDirection,
			MaxAngleRadians, 0.0f, ConeSegments, FColor::Red, false, -1.0f, SDPG_World, ConeThickness);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			TargetBoneTransformWorld.GetLocation() + InnerCircleRadius * TargetBoneInitialTransformWorld.GetRotation().GetRightVector(),
			TargetBoneTransformWorld.GetLocation() + 1.5f * InnerCircleRadius * TargetBoneInitialTransformWorld.GetRotation().GetRightVector(),
			40.f, FColor::Red, false, 0.f, CircleThickness);

		AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
			TargetBoneTransformWorld.GetLocation() + InnerCircleRadius * TargetBoneTransformWorld.GetRotation().GetRightVector(),
			TargetBoneTransformWorld.GetLocation() + 1.3f * InnerCircleRadius * TargetBoneTransformWorld.GetRotation().GetRightVector(),
			40.f, FColor::Blue, false, 0.f, CircleThickness);
	}
#endif


	if (bGraphDriven && bModifyBone)
	{
		const FTransform RemainingRootMotionDelta = ConsumedRootMotionDelta * RootMotionTransformDelta.Inverse();
		FTransform TargetRootMotionTransformDelta(RemainingRootMotionDelta.GetRotation(), RemainingRootMotionDelta.GetTranslation(), RootMotionTransformDelta.GetScale3D());
		const bool bRootMotionOverridden = RootMotionProvider->OverrideRootMotion(RootMotionTransformDelta, Output.CustomAttributes);
		ensure(bRootMotionOverridden);
	}
}

bool FAnimNode_OffsetRootBone::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
#if ENABLE_ANIM_DEBUG
	if (CVarAnimNodeOffsetRootBoneEnable.GetValueOnAnyThread() == 0)
	{
		return false;
	}
#endif

	return true;
}

EWarpingEvaluationMode FAnimNode_OffsetRootBone::GetEvaluationMode() const
{
	return GET_ANIM_NODE_DATA(EWarpingEvaluationMode, EvaluationMode);
}

const FOffsetRootBoneSettings& FAnimNode_OffsetRootBone::GetOffsetSettings() const
{
	return GET_ANIM_NODE_DATA(FOffsetRootBoneSettings, OffsetSettings);
}

const FVector& FAnimNode_OffsetRootBone::GetTranslationDelta() const
{
	return GET_ANIM_NODE_DATA(FVector, TranslationDelta);
}

const FRotator& FAnimNode_OffsetRootBone::GetRotationDelta() const
{
	return GET_ANIM_NODE_DATA(FRotator, RotationDelta);
}
