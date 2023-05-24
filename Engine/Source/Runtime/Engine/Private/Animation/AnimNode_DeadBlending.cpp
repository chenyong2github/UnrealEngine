// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_DeadBlending.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SaveCachedPose.h"
#include "Animation/BlendProfile.h"
#include "Algo/MaxElement.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Logging/TokenizedMessage.h"
#include "Animation/AnimCurveUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_DeadBlending)

LLM_DEFINE_TAG(Animation_DeadBlending);

#define LOCTEXT_NAMESPACE "AnimNode_DeadBlending"

namespace UE::Anim {

	// Inertialization request event bound to a node
	class FDeadBlendingRequester : public IInertializationRequester
	{
	public:
		FDeadBlendingRequester(const FAnimationBaseContext& InContext, FAnimNode_DeadBlending* InNode)
			: Node(*InNode)
			, NodeId(InContext.GetCurrentNodeId())
			, Proxy(*InContext.AnimInstanceProxy)
		{}

	private:
		// IInertializationRequester interface
		virtual void RequestInertialization(
			float InRequestedDuration,
			const UBlendProfile* InBlendProfile) override
		{
			Node.RequestInertialization(InRequestedDuration, InBlendProfile, false, EAlphaBlendOption::Linear, nullptr);
		}

		virtual void RequestInertializationWithBlendMode(
			float InRequestedDuration,
			const UBlendProfile* InBlendProfile,
			const bool bUseBlendMode,
			const EAlphaBlendOption InBlendMode,
			UCurveFloat* InCustomBlendCurve)
		{
			Node.RequestInertialization(InRequestedDuration, InBlendProfile, bUseBlendMode, InBlendMode, InCustomBlendCurve);
		}

		virtual void AddDebugRecord(const FAnimInstanceProxy& InSourceProxy, int32 InSourceNodeId)
		{
#if WITH_EDITORONLY_DATA
			Proxy.RecordNodeAttribute(InSourceProxy, NodeId, InSourceNodeId, IInertializationRequester::Attribute);
#endif
			TRACE_ANIM_NODE_ATTRIBUTE(Proxy, InSourceProxy, NodeId, InSourceNodeId, IInertializationRequester::Attribute);
		}

		// Node to target
		FAnimNode_DeadBlending& Node;

		// Node index
		int32 NodeId;

		// Proxy currently executing
		FAnimInstanceProxy& Proxy;
	};

}	// namespace UE::Anim

namespace UE::Anim::DeadBlending::Private
{
	static constexpr int32 MaxPoseSnapShotNum = 2;

	static constexpr float Ln2 = 0.69314718056f;

	static int32 GetNumSkeletonBones(const FBoneContainer& BoneContainer)
	{
		const USkeleton* SkeletonAsset = BoneContainer.GetSkeletonAsset();
		check(SkeletonAsset);

		const FReferenceSkeleton& RefSkeleton = SkeletonAsset->GetReferenceSkeleton();
		return RefSkeleton.GetNum();
	}

	static inline FVector VectorDivMax(const float V, const FVector W, const float Epsilon = UE_SMALL_NUMBER)
	{
		return FVector(
			V / FMath::Max(W.X, Epsilon),
			V / FMath::Max(W.Y, Epsilon),
			V / FMath::Max(W.Z, Epsilon));
	}

	static inline FVector VectorDivMax(const FVector V, const FVector W, const float Epsilon = UE_SMALL_NUMBER)
	{
		return FVector(
			V.X / FMath::Max(W.X, Epsilon),
			V.Y / FMath::Max(W.Y, Epsilon),
			V.Z / FMath::Max(W.Z, Epsilon));
	}

	static inline FVector VectorInvExpApprox(const FVector V)
	{
		return FVector(
			FMath::InvExpApprox(V.X),
			FMath::InvExpApprox(V.Y),
			FMath::InvExpApprox(V.Z));
	}

	static inline FVector VectorEerp(const FVector V, const FVector W, const float Alpha)
	{
		return FVector(
			FMath::Pow(V.X, (1.0f - Alpha)) * FMath::Pow(W.X, Alpha),
			FMath::Pow(V.Y, (1.0f - Alpha)) * FMath::Pow(W.Y, Alpha),
			FMath::Pow(V.Z, (1.0f - Alpha)) * FMath::Pow(W.Z, Alpha));
	}

	static inline FVector VectorExp(const FVector V)
	{
		return FVector(
			FMath::Exp(V.X),
			FMath::Exp(V.Y),
			FMath::Exp(V.Z));
	}

	static inline FVector VectorLogSafe(const FVector V, const float Epsilon = UE_SMALL_NUMBER)
	{
		return FVector(
			FMath::Loge(FMath::Max(V.X, Epsilon)),
			FMath::Loge(FMath::Max(V.Y, Epsilon)),
			FMath::Loge(FMath::Max(V.Z, Epsilon)));
	}

	static inline FVector ExtrapolateTranslation(
		const FVector Translation,
		const FVector Velocity,
		const float Time,
		const FVector DecayHalflife,
		const float Epsilon = UE_SMALL_NUMBER)
	{
		const FVector C = VectorDivMax(Ln2, DecayHalflife, Epsilon);
		return Translation + VectorDivMax(Velocity, C, Epsilon) * (FVector::OneVector - VectorInvExpApprox(C * Time));
	}

	static inline FQuat ExtrapolateRotation(
		const FQuat Rotation,
		const FVector Velocity,
		const float Time,
		const FVector DecayHalflife,
		const float Epsilon = UE_SMALL_NUMBER)
	{
		const FVector C = VectorDivMax(Ln2, DecayHalflife, Epsilon);
		return FQuat::MakeFromRotationVector(VectorDivMax(Velocity, C, Epsilon) * (FVector::OneVector - VectorInvExpApprox(C * Time))) * Rotation;
	}

	static inline FVector ExtrapolateScale(
		const FVector Scale,
		const FVector Velocity,
		const float Time,
		const FVector DecayHalflife,
		const float Epsilon = UE_SMALL_NUMBER)
	{
		const FVector C = VectorDivMax(Ln2, DecayHalflife, Epsilon);
		return VectorExp(VectorDivMax(Velocity, C, Epsilon) * (FVector::OneVector - VectorInvExpApprox(C * Time))) * Scale;
	}

	static inline float ExtrapolateCurve(
		const float Curve,
		const float Velocity,
		const float Time,
		const float DecayHalflife,
		const float Epsilon = UE_SMALL_NUMBER)
	{
		const float C = Ln2 / FMath::Max(DecayHalflife, Epsilon);
		return Curve + FMath::Max(Velocity / C, Epsilon) * (1.0f - FMath::InvExpApprox(C * Time));
	}

	static inline float ClipMagnitudeToGreaterThanEpsilon(const float X, const float Epsilon = UE_KINDA_SMALL_NUMBER)
	{
		return
			X >= 0.0f && X <  Epsilon ?  Epsilon :
			X <  0.0f && X > -Epsilon ? -Epsilon : X;
	}

	static inline float ComputeDecayHalfLifeFromDiffAndVelocity(
		const float SrcDstDiff,
		const float SrcVelocity,
		const float HalfLife,
		const float HalfLifeMin,
		const float HalfLifeMax,
		const float Epsilon = UE_KINDA_SMALL_NUMBER)
	{
		// Essentially what this function does is compute a half-life based on the ratio between the velocity vector and
		// the vector from the source to the destination. This is then clamped to some min and max. If the signs are
		// different (i.e. the velocity and the vector from source to destination are in opposite directions) this will
		// produce a negative number that will get clamped to HalfLifeMin. If the signs match, this will produce a large
		// number when the velocity is small and the vector from source to destination is large, and a small number when
		// the velocity is large and the vector from source to destination is small. This will be clamped either way to 
		// be in the range given by HalfLifeMin and HalfLifeMax. Finally, since the velocity can be close to zero we 
		// have to clamp it to always be greater than some given magnitude (preserving the sign).

		return FMath::Clamp(HalfLife * (SrcDstDiff / ClipMagnitudeToGreaterThanEpsilon(SrcVelocity, Epsilon)), HalfLifeMin, HalfLifeMax);
	}

	static inline FVector ComputeDecayHalfLifeFromDiffAndVelocity(
		const FVector SrcDstDiff,
		const FVector SrcVelocity,
		const float HalfLife,
		const float HalfLifeMin,
		const float HalfLifeMax,
		const float Epsilon = UE_KINDA_SMALL_NUMBER)
	{
		return FVector(
			ComputeDecayHalfLifeFromDiffAndVelocity(SrcDstDiff.X, SrcVelocity.X, HalfLife, HalfLifeMin, HalfLifeMax, Epsilon),
			ComputeDecayHalfLifeFromDiffAndVelocity(SrcDstDiff.Y, SrcVelocity.Y, HalfLife, HalfLifeMin, HalfLifeMax, Epsilon),
			ComputeDecayHalfLifeFromDiffAndVelocity(SrcDstDiff.Z, SrcVelocity.Z, HalfLife, HalfLifeMin, HalfLifeMax, Epsilon));
	}
}

void FAnimNode_DeadBlending::InitFrom(
	const FCompactPose& InPose,
	const FBlendedCurve& InCurves,
	const FInertializationPose& SrcPosePrev,
	const FInertializationPose& SrcPoseCurr)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FAnimNode_DeadBlending::InitFrom);

	const FBoneContainer& BoneContainer = InPose.GetBoneContainer();

	const int32 NumSkeletonBones = UE::Anim::DeadBlending::Private::GetNumSkeletonBones(BoneContainer);

	BoneValid.Init(false, NumSkeletonBones);
	BoneTranslations.Init(FVector::ZeroVector, NumSkeletonBones);
	BoneRotations.Init(FQuat::Identity, NumSkeletonBones);
	BoneRotationDirections.Init(FQuat::Identity, NumSkeletonBones);
	BoneScales.Init(FVector::OneVector, NumSkeletonBones);

	BoneTranslationVelocities.Init(FVector::ZeroVector, NumSkeletonBones);
	BoneRotationVelocities.Init(FVector::ZeroVector, NumSkeletonBones);
	BoneScaleVelocities.Init(FVector::ZeroVector, NumSkeletonBones);

	BoneTranslationDecayHalfLives.Init(ExtrapolationHalfLifeMin * FVector::OneVector, NumSkeletonBones);
	BoneRotationDecayHalfLives.Init(ExtrapolationHalfLifeMin * FVector::OneVector, NumSkeletonBones);
	BoneScaleDecayHalfLives.Init(ExtrapolationHalfLifeMin * FVector::OneVector, NumSkeletonBones);

	// Record bone state

	for (FCompactPoseBoneIndex BoneIndex : InPose.ForEachBoneIndex())
	{
		const int32 SkeletonPoseBoneIndex = BoneContainer.GetSkeletonIndex(BoneIndex);

		if (SkeletonPoseBoneIndex == INDEX_NONE ||
			SrcPosePrev.BoneStates[SkeletonPoseBoneIndex] != EInertializationBoneState::Valid ||
			SrcPoseCurr.BoneStates[SkeletonPoseBoneIndex] != EInertializationBoneState::Valid)
		{
			continue;
		}

		// Mark bone as valid

		BoneValid[SkeletonPoseBoneIndex] = true;

		// Get Source Animation Transform

		const FTransform SrcTransformCurr = SrcPoseCurr.BoneTransforms[SkeletonPoseBoneIndex];

		BoneTranslations[SkeletonPoseBoneIndex] = SrcTransformCurr.GetTranslation();
		BoneRotations[SkeletonPoseBoneIndex] = SrcTransformCurr.GetRotation();
		BoneScales[SkeletonPoseBoneIndex] = SrcTransformCurr.GetScale3D();

		if (SrcPoseCurr.DeltaTime > UE_SMALL_NUMBER)
		{
			// Get Source Animation Velocity

			const FTransform SrcTransformPrev = SrcPosePrev.BoneTransforms[SkeletonPoseBoneIndex];

			const FVector TranslationDiff = SrcTransformCurr.GetTranslation() - SrcTransformPrev.GetTranslation();

			FQuat RotationDiff = SrcTransformCurr.GetRotation() * SrcTransformPrev.GetRotation().Inverse();
			RotationDiff.EnforceShortestArcWith(FQuat::Identity);

			const FVector ScaleDiff = UE::Anim::DeadBlending::Private::VectorDivMax(SrcTransformCurr.GetScale3D(), SrcTransformPrev.GetScale3D());

			BoneTranslationVelocities[SkeletonPoseBoneIndex] = TranslationDiff / SrcPoseCurr.DeltaTime;
			BoneRotationVelocities[SkeletonPoseBoneIndex] = RotationDiff.ToRotationVector() / SrcPoseCurr.DeltaTime;
			BoneScaleVelocities[SkeletonPoseBoneIndex] = UE::Anim::DeadBlending::Private::VectorLogSafe(ScaleDiff) / SrcPoseCurr.DeltaTime;

			// Compute Decay HalfLives

			const FTransform DstTransform = InPose[BoneIndex];

			const FVector TranslationSrcDstDiff = DstTransform.GetTranslation() - SrcTransformCurr.GetTranslation();

			FQuat RotationSrcDstDiff = DstTransform.GetRotation() * SrcTransformCurr.GetRotation().Inverse();
			RotationSrcDstDiff.EnforceShortestArcWith(FQuat::Identity);

			const FVector ScaleSrcDstDiff = UE::Anim::DeadBlending::Private::VectorDivMax(DstTransform.GetScale3D(), SrcTransformCurr.GetScale3D());

			BoneTranslationDecayHalfLives[SkeletonPoseBoneIndex] = UE::Anim::DeadBlending::Private::ComputeDecayHalfLifeFromDiffAndVelocity(
				TranslationSrcDstDiff,
				BoneTranslationVelocities[SkeletonPoseBoneIndex],
				ExtrapolationHalfLife,
				ExtrapolationHalfLifeMin,
				ExtrapolationHalfLifeMax);

			BoneRotationDecayHalfLives[SkeletonPoseBoneIndex] = UE::Anim::DeadBlending::Private::ComputeDecayHalfLifeFromDiffAndVelocity(
				RotationSrcDstDiff.ToRotationVector(),
				BoneRotationVelocities[SkeletonPoseBoneIndex],
				ExtrapolationHalfLife,
				ExtrapolationHalfLifeMin,
				ExtrapolationHalfLifeMax);

			BoneScaleDecayHalfLives[SkeletonPoseBoneIndex] = UE::Anim::DeadBlending::Private::ComputeDecayHalfLifeFromDiffAndVelocity(
				ScaleSrcDstDiff,
				BoneScaleVelocities[SkeletonPoseBoneIndex],
				ExtrapolationHalfLife,
				ExtrapolationHalfLifeMin,
				ExtrapolationHalfLifeMax);
		}
	}

	CurveData.CopyFrom(SrcPoseCurr.Curves.BlendedCurve);

	// Record curve state

	UE::Anim::FNamedValueArrayUtils::Union(CurveData, SrcPoseCurr.Curves.BlendedCurve,
		[this](FDeadBlendingCurveElement& OutResultElement, const UE::Anim::FCurveElement& InElement1, UE::Anim::ENamedValueUnionFlags InFlags)
		{
			// Here we need to store an additional `Valid` value because the `Union` operation used later on 
			// can add curves which are in `InCurves` but which are not in `SrcPoseCurr` or `SrcPosePrev`.
			// 
			// Since we only want to extrapolate curves which are in `SrcPoseCurr` we therefore we need a `Valid` 
			// value to indicate this. Probably it would be better to not use the `Union` operation and instead something 
			// like `Intersection`, but right now no version of this is provided which modifies the curves in place 
			// in the same way `Union` does.

			OutResultElement.Valid = true;
			OutResultElement.Value = InElement1.Value;
			OutResultElement.Velocity = 0.0f;
			OutResultElement.HalfLife = ExtrapolationHalfLifeMin;
		});

	if (SrcPoseCurr.DeltaTime > UE_SMALL_NUMBER)
	{
		// Record Curve Velocity

		UE::Anim::FNamedValueArrayUtils::Union(CurveData, SrcPosePrev.Curves.BlendedCurve,
			[DeltaTime = SrcPoseCurr.DeltaTime](FDeadBlendingCurveElement& OutResultElement, const UE::Anim::FCurveElement& InElement1, UE::Anim::ENamedValueUnionFlags InFlags)
			{
				if (OutResultElement.Valid)
				{
					OutResultElement.Velocity = (OutResultElement.Value - InElement1.Value) / DeltaTime;
				}
			});

		// Record Half-life

		UE::Anim::FNamedValueArrayUtils::Union(CurveData, InCurves,
			[this](FDeadBlendingCurveElement& OutResultElement, const UE::Anim::FCurveElement& InElement1, UE::Anim::ENamedValueUnionFlags InFlags)
			{
				if (OutResultElement.Valid)
				{
					OutResultElement.HalfLife = UE::Anim::DeadBlending::Private::ComputeDecayHalfLifeFromDiffAndVelocity(
						InElement1.Value - OutResultElement.Value,
						OutResultElement.Velocity,
						ExtrapolationHalfLife,
						ExtrapolationHalfLifeMin,
						ExtrapolationHalfLifeMax);
				}
			});
	}

	// Apply filtering to remove anything we don't want to inertialize

	if (CurveFilter.Num() > 0)
	{
		UE::Anim::FCurveUtils::Filter(CurveData, CurveFilter);
	}
}

void FAnimNode_DeadBlending::ApplyTo(FCompactPose& InOutPose, FBlendedCurve& InOutCurves)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FAnimNode_DeadBlending::ApplyTo);

	const FBoneContainer& BoneContainer = InOutPose.GetBoneContainer();

	for (FCompactPoseBoneIndex BoneIndex : InOutPose.ForEachBoneIndex())
	{
		const int32 SkeletonPoseBoneIndex = BoneContainer.GetSkeletonIndex(BoneIndex);

		if (SkeletonPoseBoneIndex == INDEX_NONE || !BoneValid[SkeletonPoseBoneIndex])
		{
			continue;
		}

		// Compute Extrapolated Bone State

		const FVector ExtrapolatedTranslation = UE::Anim::DeadBlending::Private::ExtrapolateTranslation(
			BoneTranslations[SkeletonPoseBoneIndex],
			BoneTranslationVelocities[SkeletonPoseBoneIndex],
			InertializationTime,
			BoneTranslationDecayHalfLives[SkeletonPoseBoneIndex]);

		const FQuat ExtrapolatedRotation = UE::Anim::DeadBlending::Private::ExtrapolateRotation(
			BoneRotations[SkeletonPoseBoneIndex],
			BoneRotationVelocities[SkeletonPoseBoneIndex],
			InertializationTime,
			BoneRotationDecayHalfLives[SkeletonPoseBoneIndex]);

		const FVector ExtrapolatedScale = UE::Anim::DeadBlending::Private::ExtrapolateScale(
			BoneScales[SkeletonPoseBoneIndex],
			BoneScaleVelocities[SkeletonPoseBoneIndex],
			InertializationTime,
			BoneScaleDecayHalfLives[SkeletonPoseBoneIndex]);

#if WITH_EDITORONLY_DATA
		if (bShowExtrapolations)
		{
			InOutPose[BoneIndex].SetTranslation(ExtrapolatedTranslation);
			InOutPose[BoneIndex].SetRotation(ExtrapolatedRotation);
			InOutPose[BoneIndex].SetScale3D(ExtrapolatedScale);
			continue;
		}
#endif
		// We need to enforce that the blend of the rotation doesn't suddenly "switch sides"
		// given that the extrapolated rotation can become quite far from the destination
		// animation. To do this we keep track of the blend "direction" and ensure that the
		// delta we are applying to the destination animation always remains on the same
		// side of this rotation.

		FQuat RotationDiff = ExtrapolatedRotation * InOutPose[BoneIndex].GetRotation().Inverse();
		RotationDiff.EnforceShortestArcWith(BoneRotationDirections[SkeletonPoseBoneIndex]);

		// Update BoneRotationDirections to match our current path
		BoneRotationDirections[SkeletonPoseBoneIndex] = RotationDiff;

		// Compute Blend Alpha

		const float Alpha = 1.0f - FAlphaBlend::AlphaToBlendOption(
			InertializationTime / FMath::Max(InertializationDurationPerBone[SkeletonPoseBoneIndex], UE_SMALL_NUMBER),
			InertializationBlendMode, InertializationCustomBlendCurve);

		// Perform Blend

		if (Alpha != 0.0f)
		{
			InOutPose[BoneIndex].SetTranslation(FMath::Lerp(InOutPose[BoneIndex].GetTranslation(), ExtrapolatedTranslation, Alpha));
			InOutPose[BoneIndex].SetRotation(FQuat::MakeFromRotationVector(RotationDiff.ToRotationVector() * Alpha) * InOutPose[BoneIndex].GetRotation());

			// Here we use `Eerp` rather than `Lerp` to interpolate scales correctly (see: https://theorangeduck.com/page/scalar-velocity).
			// This is inconsistent with the rest of Unreal which (mostly) uses `Lerp` on scales. The decision to use `Eerp` here is partially 
			// due to the fact we are also dealing properly with scalar velocities in this node, and partially to try and not to lock this node 
			// into having the same incorrect behavior by default. We can add an option to interpolate scales with `Lerp` later down the line if 
			// users want this, but we will not be able to change the default behavior again, so the decision is to opt for the most correct by 
			// default this time even if it is somewhat less performant and inconsistent with other parts of Unreal.
			InOutPose[BoneIndex].SetScale3D(UE::Anim::DeadBlending::Private::VectorEerp(InOutPose[BoneIndex].GetScale3D(), ExtrapolatedScale, Alpha));
		}
	}

	if (InOutCurves.Num() > 0)
	{
		// Compute Blend Alpha

		const float CurveAlpha = 1.0f - FAlphaBlend::AlphaToBlendOption(
			InertializationTime / FMath::Max(InertializationDuration, UE_SMALL_NUMBER),
			InertializationBlendMode, InertializationCustomBlendCurve);

		// Blend Curves

		if (CurveAlpha != 0.0f)
		{
			UE::Anim::FNamedValueArrayUtils::Union(InOutCurves, CurveData,
				[CurveAlpha, this](UE::Anim::FCurveElement& OutResultElement, const FDeadBlendingCurveElement& InElement1, UE::Anim::ENamedValueUnionFlags InFlags)
				{
					// Compute Extrapolated Curve Value

					if (InElement1.Valid)
					{
						const float ExtrapolatedCurve = UE::Anim::DeadBlending::Private::ExtrapolateCurve(
							InElement1.Value,
							InElement1.Velocity,
							InertializationTime,
							InElement1.HalfLife);

#if WITH_EDITORONLY_DATA
						if (bShowExtrapolations)
						{
							OutResultElement.Value = ExtrapolatedCurve;
							OutResultElement.Flags |= InElement1.Flags;
							return;
						}
#endif
						OutResultElement.Value = FMath::Lerp(OutResultElement.Value, ExtrapolatedCurve, CurveAlpha);
						OutResultElement.Flags |= InElement1.Flags;
					}
				});
		}
	}
}

FAnimNode_DeadBlending::FAnimNode_DeadBlending() {}

void FAnimNode_DeadBlending::RequestInertialization(
	float Duration,
	const UBlendProfile* BlendProfile,
	const bool bInUseBlendMode,
	const EAlphaBlendOption InBlendMode,
	UCurveFloat* InCustomBlendCurve)
{
	if (Duration >= 0.0f)
	{
		RequestQueue.AddUnique(FInertializationRequest(Duration, BlendProfile, bInUseBlendMode, InBlendMode, InCustomBlendCurve));
	}
}

void FAnimNode_DeadBlending::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/DeadBlending"));
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);

	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);

	const USkeleton* Skeleton = Context.AnimInstanceProxy->GetSkeleton();
	check(Skeleton);

	CurveFilter.Empty();
	CurveFilter.SetFilterMode(UE::Anim::ECurveFilterMode::DisallowFiltered);
	CurveFilter.AppendNames(FilteredCurves);

	PoseSnapshots.Empty(UE::Anim::DeadBlending::Private::MaxPoseSnapShotNum);

	RequestQueue.Reserve(8);

	const int32 NumSkeletonBones = UE::Anim::DeadBlending::Private::GetNumSkeletonBones(Context.AnimInstanceProxy->GetRequiredBones());

	BoneValid.Empty(NumSkeletonBones);
	BoneTranslations.Empty(NumSkeletonBones);
	BoneRotations.Empty(NumSkeletonBones);
	BoneRotationDirections.Empty(NumSkeletonBones);
	BoneScales.Empty(NumSkeletonBones);

	BoneTranslationVelocities.Empty(NumSkeletonBones);
	BoneRotationVelocities.Empty(NumSkeletonBones);
	BoneScaleVelocities.Empty(NumSkeletonBones);

	BoneTranslationDecayHalfLives.Empty(NumSkeletonBones);
	BoneRotationDecayHalfLives.Empty(NumSkeletonBones);
	BoneScaleDecayHalfLives.Empty(NumSkeletonBones);

	CurveData.Empty();

	DeltaTime = 0.0f;

	InertializationState = EInertializationState::Inactive;
	InertializationTime = 0.0f;

	InertializationDuration = 0.0f;
	InertializationDurationPerBone.Empty(NumSkeletonBones);
	InertializationMaxDuration = 0.0f;

	InertializationBlendMode = DefaultBlendMode;
	InertializationCustomBlendCurve = DefaultCustomBlendCurve;
}


void FAnimNode_DeadBlending::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread);

	FAnimNode_Base::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);
}


void FAnimNode_DeadBlending::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/DeadBlending"));
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);

	const int32 NodeId = Context.GetCurrentNodeId();
	const FAnimInstanceProxy& Proxy = *Context.AnimInstanceProxy;

	// Allow nodes further towards the leaves to inertialize using this node
	UE::Anim::TScopedGraphMessage<UE::Anim::FDeadBlendingRequester> Inertialization(Context, Context, this);

	// Handle skipped updates for cached poses by forwarding to inertialization nodes in those residual stacks
	UE::Anim::TScopedGraphMessage<UE::Anim::FCachedPoseSkippedUpdateHandler> CachedPoseSkippedUpdate(Context, [this, NodeId, &Proxy](TArrayView<const UE::Anim::FMessageStack> InSkippedUpdates)
	{
		// If we have a pending request forward the request to other Inertialization nodes
		// that were skipped due to pose caching.
		if (RequestQueue.Num() > 0)
		{
			// Cached poses have their Update function called once even though there may be multiple UseCachedPose nodes for the same pose.
			// Because of this, there may be Inertialization ancestors of the UseCachedPose nodes that missed out on requests.
			// So here we forward 'this' node's requests to the ancestors of those skipped UseCachedPose nodes.
			// Note that in some cases, we may be forwarding the requests back to this same node.  Those duplicate requests will ultimately
			// be ignored by the 'AddUnique' in the body of FAnimNode_DeadBlending::RequestInertialization.
			for (const UE::Anim::FMessageStack& Stack : InSkippedUpdates)
			{
				Stack.ForEachMessage<UE::Anim::IInertializationRequester>([this, NodeId, &Proxy](UE::Anim::IInertializationRequester& InMessage)
				{
					for (const FInertializationRequest& Request : RequestQueue)
					{
						InMessage.RequestInertializationWithBlendMode(Request.Duration, Request.BlendProfile, Request.bUseBlendMode, Request.BlendMode, Request.CustomBlendCurve);
					}
					InMessage.AddDebugRecord(Proxy, NodeId);

					return UE::Anim::FMessageStack::EEnumerate::Stop;
				});
			}
		}
	});

	Source.Update(Context);

	// Accumulate delta time between calls to Evaluate_AnyThread
	DeltaTime += Context.GetDeltaTime();
}

void FAnimNode_DeadBlending::Evaluate_AnyThread(FPoseContext& Output)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/DeadBlending"));
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);

	// Evaluate the Input and write it to the Output

	Source.Evaluate(Output);

	// Automatically detect teleports... note that we do the teleport distance check against the root bone's location (world space) rather
	// than the mesh component's location because we still want to inertialize instances where the skeletal mesh component has been moved
	// while simultaneously counter-moving the root bone (as is the case when mounting and dismounting vehicles for example)

	const FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

	bool bTeleported = false;

	const float TeleportDistanceThreshold = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetTeleportDistanceThreshold();

	if (PoseSnapshots.Num() > 0 && TeleportDistanceThreshold > 0.0f)
	{
		const FVector RootWorldSpaceLocation = ComponentTransform.TransformPosition(Output.Pose[FCompactPoseBoneIndex(0)].GetTranslation());
		const FVector PrevRootWorldSpaceLocation = PoseSnapshots.Last().ComponentTransform.TransformPosition(PoseSnapshots.Last().BoneTransforms[0].GetTranslation());

		if (FVector::DistSquared(RootWorldSpaceLocation, PrevRootWorldSpaceLocation) > FMath::Square(TeleportDistanceThreshold))
		{
			bTeleported = true;
		}
	}

	// If teleported we simply reset the inertialization

	if (bTeleported)
	{
		InertializationState = EInertializationState::Inactive;
	}
	
	// If we don't have any Pose Snapshots recorded it means this is the first time this node has been evaluated in 
	// which case there shouldn't be any discontinuity to remove, so no inertialization needs to be done, and we can 
	// discard any requests.

	if (PoseSnapshots.IsEmpty())
	{
		RequestQueue.Reset();
	}

	// Process Inertialization Requests

	if (!RequestQueue.IsEmpty())
	{
		const int32 NumSkeletonBones = UE::Anim::DeadBlending::Private::GetNumSkeletonBones(Output.AnimInstanceProxy->GetRequiredBones());

		InertializationTime = 0.0f;

		if (bAlwaysUseDefaultBlendSettings)
		{
			InertializationDuration = BlendTimeMultiplier * DefaultBlendDuration;
			InertializationDurationPerBone.Init(BlendTimeMultiplier * DefaultBlendDuration, NumSkeletonBones);
			InertializationMaxDuration = BlendTimeMultiplier * DefaultBlendDuration;
			InertializationBlendMode = DefaultBlendMode;
			InertializationCustomBlendCurve = DefaultCustomBlendCurve;
		}
		else
		{
			// Process Request Durations by taking min of all requests
			// For Blend Mode and Custom Curve we will just take whichever
			// request is last in the stack (if they are provided).

			InertializationDuration = UE_MAX_FLT;
			InertializationDurationPerBone.Init(UE_MAX_FLT, NumSkeletonBones);
			InertializationMaxDuration = UE_MAX_FLT;
			InertializationBlendMode = DefaultBlendMode;
			InertializationCustomBlendCurve = DefaultCustomBlendCurve;

			UE::Anim::TTypedIndexArray<FSkeletonPoseBoneIndex, float, FAnimStackAllocator> RequestDurationPerBone;

			for (const FInertializationRequest Request : RequestQueue)
			{
				// Duration is min of requests

				InertializationDuration = BlendTimeMultiplier * FMath::Min(InertializationDuration, Request.Duration);

				// Per-bone durations as min of requests accounting for blend profile

				if (Request.BlendProfile)
				{
					Request.BlendProfile->FillSkeletonBoneDurationsArray(RequestDurationPerBone, Request.Duration);
				}
				else if (DefaultBlendProfile)
				{
					DefaultBlendProfile->FillSkeletonBoneDurationsArray(RequestDurationPerBone, Request.Duration);
				}
				else
				{
					RequestDurationPerBone.Init(Request.Duration, NumSkeletonBones);
				}

				check(RequestDurationPerBone.Num() == InertializationDurationPerBone.Num());

				for (int32 BoneIndex = 0; BoneIndex < NumSkeletonBones; BoneIndex++)
				{
					InertializationDurationPerBone[BoneIndex] = BlendTimeMultiplier * FMath::Min(InertializationDurationPerBone[BoneIndex], RequestDurationPerBone[BoneIndex]);
				}

				// Process blend mode - will take the last one given

				if (Request.bUseBlendMode)
				{
					InertializationBlendMode = Request.BlendMode;
					InertializationCustomBlendCurve = Request.CustomBlendCurve;
				}
			}
		}

		InertializationMaxDuration = FMath::Max(InertializationDuration, *Algo::MaxElement(InertializationDurationPerBone));

		check(InertializationDuration != UE_MAX_FLT);
		check(InertializationMaxDuration != UE_MAX_FLT);

		// Reset Request Queue

		RequestQueue.Reset();

		// Initialize the recorded pose state at the point of transition

		if (PoseSnapshots.Num() > 1)
		{
			// We have two previous poses and so can initialize as normal.

			InitFrom(
				Output.Pose,
				Output.Curve,
				PoseSnapshots[PoseSnapshots.Num() - 2],
				PoseSnapshots[PoseSnapshots.Num() - 1]);
		}
		else if (PoseSnapshots.Num() > 0)
		{
			// We only have a single previous pose. Repeat this pose assuming zero velocity.

			InitFrom(
				Output.Pose,
				Output.Curve,
				PoseSnapshots.Last(),
				PoseSnapshots.Last());
		}
		else
		{
			// This should never happen because we are not able to issue an inertialization 
			// requested until we have at least one pose recorded in the snapshots.
			check(false);
		}

		// Set state to active

		InertializationState = EInertializationState::Active;
	}

	// Update Time Since Transition and deactivate if blend is over

	if (InertializationState == EInertializationState::Active)
	{
		InertializationTime += DeltaTime;

		if (InertializationTime >= InertializationMaxDuration)
		{
			InertializationState = EInertializationState::Inactive;
		}
	}

	// Apply inertialization

	if (InertializationState == EInertializationState::Active)
	{
		ApplyTo(Output.Pose, Output.Curve);
	}

	// Find AttachParentName

	FName AttachParentName = NAME_None;
	if (AActor* Owner = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetOwner())
	{
		if (AActor* AttachParentActor = Owner->GetAttachParentActor())
		{
			AttachParentName = AttachParentActor->GetFName();
		}
	}

	// Record Pose Snapshot

	if (PoseSnapshots.Num() < UE::Anim::DeadBlending::Private::MaxPoseSnapShotNum)
	{
		// Add the pose to the end of the buffer
		PoseSnapshots.AddDefaulted_GetRef().InitFrom(Output.Pose, Output.Curve, ComponentTransform, AttachParentName, DeltaTime);
	}
	else
	{
		// Bubble the old poses forward in the buffer (using swaps to avoid allocations and copies)
		for (int32 SnapshotIndex = 0; SnapshotIndex < UE::Anim::DeadBlending::Private::MaxPoseSnapShotNum - 1; ++SnapshotIndex)
		{
			Swap(PoseSnapshots[SnapshotIndex], PoseSnapshots[SnapshotIndex + 1]);
		}

		// Overwrite the (now irrelevant) pose in the last slot with the new post snapshot
		// (thereby avoiding the reallocation costs we would have incurred had we simply added a new pose at the end)
		PoseSnapshots.Last().InitFrom(Output.Pose, Output.Curve, ComponentTransform, AttachParentName, DeltaTime);
	}

	// Reset Delta Time

	DeltaTime = 0.0f;
}

bool FAnimNode_DeadBlending::NeedsDynamicReset() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
