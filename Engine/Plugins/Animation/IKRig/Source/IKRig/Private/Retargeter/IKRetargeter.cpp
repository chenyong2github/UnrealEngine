// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargeter.h"

#include "Algo/LevenshteinDistance.h"
#include "Engine/SkeletalMesh.h"

const FName UIKRetargeter::DefaultPoseName = "Default Pose";

void FRetargetSkeleton::Initialize(
	USkeletalMesh* SkeletalMesh,
	FIKRetargetPose* RetargetPose)
{
	// record name for debug warnings
	SkeletalMeshName = SkeletalMesh->GetName();

	// get reference skeleton
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	
	// copy names and parent indices into local storage
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton.GetNum(); ++BoneIndex)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(BoneIndex));
		ParentIndices.Add(RefSkeleton.GetParentIndex(BoneIndex));	
	}

	// store copy of retarget pose
	RetargetLocalPose = RefSkeleton.GetRefBonePose();
	RetargetGlobalPose = RetargetLocalPose;

	// update global retarget pose
	UpdateGlobalTransformsBelowBone(0, RetargetLocalPose, RetargetGlobalPose);

	// apply bone rotation offsets to retarget pose
	if (RetargetPose != nullptr)
	{
		for (const TTuple<FName, FQuat>& BoneRotationOffset : RetargetPose->BoneRotationOffsets)
		{
			const int32 BoneIndex = FindBoneIndexByName(BoneRotationOffset.Key);
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}

			FTransform& ChainStartTransform = RetargetGlobalPose[BoneIndex];
			FQuat RotatedChain = BoneRotationOffset.Value * ChainStartTransform.GetRotation();
			ChainStartTransform.SetRotation(RotatedChain);
			UpdateLocalTransformOfSingleBone(BoneIndex, RetargetLocalPose, RetargetGlobalPose);
			UpdateGlobalTransformsBelowBone(BoneIndex, RetargetLocalPose, RetargetGlobalPose);
		}
	}
}

int32 FRetargetSkeleton::FindBoneIndexByName(const FName InName) const
{
	for (int32 Index=0; Index<BoneNames.Num(); ++Index)
	{
		if (InName == BoneNames[Index])
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void FRetargetSkeleton::UpdateGlobalTransformsBelowBone(
	const int32 StartBoneIndex,
	TArray<FTransform>& InLocalPose,
	TArray<FTransform>& OutGlobalPose) const
{
	check(BoneNames.IsValidIndex(StartBoneIndex));
	check(BoneNames.Num() == InLocalPose.Num());
	check(BoneNames.Num() == OutGlobalPose.Num());
	
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<OutGlobalPose.Num(); ++BoneIndex)
	{
		UpdateGlobalTransformOfSingleBone(BoneIndex,InLocalPose,OutGlobalPose);
	}
}

void FRetargetSkeleton::UpdateLocalTransformsBelowBone(
	const int32 StartBoneIndex,
	TArray<FTransform>& OutLocalPose,
	TArray<FTransform>& InGlobalPose) const
{
	check(BoneNames.IsValidIndex(StartBoneIndex));
	check(BoneNames.Num() == OutLocalPose.Num());
	check(BoneNames.Num() == InGlobalPose.Num());
	
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<InGlobalPose.Num(); ++BoneIndex)
	{
		UpdateLocalTransformOfSingleBone(BoneIndex, OutLocalPose, InGlobalPose);
	}
}

void FRetargetSkeleton::UpdateGlobalTransformOfSingleBone(
	const int32 BoneIndex,
	const TArray<FTransform>& InLocalPose,
	TArray<FTransform>& OutGlobalPose) const
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		return; // root always in global space
	}
	const FTransform ChildLocalTransform = InLocalPose[BoneIndex];
	const FTransform ParentGlobalTransform = OutGlobalPose[ParentIndex];
	OutGlobalPose[BoneIndex] = ChildLocalTransform * ParentGlobalTransform;
}

void FRetargetSkeleton::UpdateLocalTransformOfSingleBone(
	const int32 BoneIndex,
	TArray<FTransform>& OutLocalPose,
	TArray<FTransform>& InGlobalPose) const
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		return; // root always in global space
	}
	const FTransform ChildGlobalTransform = InGlobalPose[BoneIndex];
	const FTransform ParentGlobalTransform = InGlobalPose[ParentIndex];
	OutLocalPose[BoneIndex] = ChildGlobalTransform.GetRelativeTransform(ParentGlobalTransform);
}

int32 FRetargetSkeleton::GetParentIndex(const int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex>ParentIndices.Num() || BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return ParentIndices[BoneIndex];
}

void FTargetSkeleton::Initialize(
	USkeletalMesh* SkeletalMesh, 
	FIKRetargetPose* RetargetPose)
{
	FRetargetSkeleton::Initialize(SkeletalMesh, RetargetPose);

	// initialize storage for output pose (the result of the retargeting)
	OutputGlobalPose = RetargetGlobalPose;

	// make storage for per-bone "Is Retargeted" flag (used for hierarchy updates)
	IsBoneRetargeted.Init(false, OutputGlobalPose.Num());
}

void FTargetSkeleton::UpdateGlobalTransformsAllNonRetargetedBones(TArray<FTransform>& InOutGlobalPose)
{
	check(IsBoneRetargeted.Num() == InOutGlobalPose.Num());
	
	for (int32 BoneIndex=0; BoneIndex<InOutGlobalPose.Num(); ++BoneIndex)
	{
		if (!IsBoneRetargeted[BoneIndex])
		{
			UpdateGlobalTransformOfSingleBone(BoneIndex, RetargetLocalPose, InOutGlobalPose);
		}
	}
}

void FTargetSkeleton::SetBoneIsRetargeted(const int32 BoneIndex, const bool IsRetargeted)
{
	check(IsBoneRetargeted.IsValidIndex(BoneIndex));
	IsBoneRetargeted[BoneIndex] = IsRetargeted;
}

bool FChainFK::Initialize(const TArray<int32>& BoneIndices, const TArray<FTransform>& InitialGlobalPose)
{
	check(!BoneIndices.IsEmpty());

	// store all the initial bone transforms in the bone chain
	for (int32 Index=0; Index < BoneIndices.Num(); ++Index)
	{
		const int32 BoneIndex = BoneIndices[Index];
		check(BoneIndex < InitialGlobalPose.Num());
		InitialBoneTransforms.Emplace(InitialGlobalPose[BoneIndex]);
	}

	// initialize storage for current bones
	CurrentBoneTransforms = InitialBoneTransforms;

	// calculate parameter of each bone, normalized by the length of the bone chain
	return CalculateBoneParameters();
}

bool FChainFK::CalculateBoneParameters()
{
	// special case, a single-bone chain
	if (InitialBoneTransforms.Num() == 1)
	{
		Params.Add(1.0f);
		return true;
	}

	// calculate bone lengths in chain and accumulate total length
	TArray<float> BoneDistances;
	float TotalChainLength = 0.0f;
	BoneDistances.Add(0.0f);
	for (int32 i=1; i<InitialBoneTransforms.Num(); ++i)
	{
		TotalChainLength += (InitialBoneTransforms[i].GetTranslation() - InitialBoneTransforms[i-1].GetTranslation()).Size();
		BoneDistances.Add(TotalChainLength);
	}

	// cannot retarget chain if all the bones are sitting directly on each other
	if (TotalChainLength <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain length is too small to reliably retarget."));
		return false;
	}

	// calc each bone's param along length
	for (int32 i=0; i<InitialBoneTransforms.Num(); ++i)
	{
		Params.Add(BoneDistances[i] / TotalChainLength); 
	}

	return true;
}

void FChainEncoderFK::EncodePose(
	const TArray<int32>& SourceBoneIndices,
    const TArray<FTransform> &InputGlobalPose)
{
	check(SourceBoneIndices.Num() == CurrentBoneTransforms.Num());
	
	// copy the input pose for the chain
	for (int32 ChainIndex=0; ChainIndex<SourceBoneIndices.Num(); ++ChainIndex)
	{
		const int32 BoneIndex = SourceBoneIndices[ChainIndex];
		CurrentBoneTransforms[ChainIndex] = InputGlobalPose[BoneIndex];
	}
}

void FChainDecoderFK::DecodePose(
	const TArray<int32>& TargetBoneIndices,
    const FChainEncoderFK& SourceChain,
    const FTargetSkeleton& TargetSkeleton,
    TArray<FTransform> &InOutGlobalPose)
{
	check(TargetBoneIndices.Num() == CurrentBoneTransforms.Num());
	check(TargetBoneIndices.Num() == Params.Num());

	// Before setting this chain pose, we need to ensure that any
	// intermediate (between chains) NON-retargeted parent bones have had their
	// global transforms updated.
	// 
	// For example, if this chain is retargeting a single head bone, AND the spine was
	// retargeted in the prior step, then the neck bones will need updating first.
	// Otherwise the neck bones will remain at their location prior to the spine update.
	UpdateIntermediateParents(TargetSkeleton,InOutGlobalPose);

	// now retarget the pose of each bone in the chain, copying from source to target
	for (int32 ChainIndex=0; ChainIndex<TargetBoneIndices.Num(); ++ChainIndex)
	{
		// get the initial and current transform of source chain at param
		// this is the interpolated transform along the chain
		float Param = Params[ChainIndex];
		FTransform SourceCurrentTransform = GetTransformAtParam(
			SourceChain.CurrentBoneTransforms,
			SourceChain.Params,
			Param);
		FTransform SourceInitialTransform = GetTransformAtParam(
			SourceChain.InitialBoneTransforms,
			SourceChain.Params,
			Param);

		const int32 BoneIndex = TargetBoneIndices[ChainIndex];
		const int32 ParentIndex = TargetSkeleton.ParentIndices[BoneIndex];
		
		// the initial transform on the target chain
		const FTransform& TargetInitialTransform = InitialBoneTransforms[ChainIndex];

		// calculate output ROTATION
		const FQuat SourceCurrentRotation = SourceCurrentTransform.GetRotation();
		const FQuat SourceInitialRotation = SourceInitialTransform.GetRotation();
		const FQuat RotationDelta = SourceCurrentRotation * SourceInitialRotation.Inverse();
		const FQuat TargetInitialRotation = TargetInitialTransform.GetRotation();
		const FQuat OutRotation = RotationDelta * TargetInitialRotation;

		// calculate output POSITION (constant for now, maybe we support retargeting translation later?)
		const FTransform& ParentGlobalTransform = InOutGlobalPose[ParentIndex];
		const FVector InitialLocalOffset = TargetSkeleton.RetargetLocalPose[BoneIndex].GetTranslation();
		const FVector OutPosition = ParentGlobalTransform.TransformPosition(InitialLocalOffset);

		// calculate output SCALE
		const FVector SourceCurrentScale = SourceCurrentTransform.GetScale3D();
		const FVector SourceInitialScale = SourceInitialTransform.GetScale3D();
		const FVector TargetInitialScale = TargetInitialTransform.GetScale3D();
		const FVector OutScale = SourceCurrentScale + (TargetInitialScale - SourceInitialScale);
		
		// apply output transform
		FTransform OutTransform = FTransform(OutRotation, OutPosition, OutScale);
		InOutGlobalPose[BoneIndex] = OutTransform;
	}
}

void FChainDecoderFK::InitializeIntermediateParentIndices(
	const int32 RetargetRootBoneIndex,
	const int32 ChainRootBoneIndex,
	const FTargetSkeleton& TargetSkeleton)
{
	IntermediateParentIndices.Reset();
	int32 ParentBoneIndex = TargetSkeleton.ParentIndices[ChainRootBoneIndex];
	while (true)
	{
		if (ParentBoneIndex < 0 || ParentBoneIndex == RetargetRootBoneIndex)
		{
			break; // reached root of skeleton
		}

		if (TargetSkeleton.IsBoneRetargeted[ParentBoneIndex])
		{
			break; // reached the start of another retargeted chain
		}

		IntermediateParentIndices.Add(ParentBoneIndex);
		ParentBoneIndex = TargetSkeleton.ParentIndices[ParentBoneIndex];
	}

	Algo::Reverse(IntermediateParentIndices);
}

void FChainDecoderFK::UpdateIntermediateParents(
	const FTargetSkeleton& TargetSkeleton,
	TArray<FTransform>& InOutGlobalPose)
{
	for (const int32& ParentIndex : IntermediateParentIndices)
	{
		TargetSkeleton.UpdateGlobalTransformOfSingleBone(ParentIndex, TargetSkeleton.RetargetLocalPose, InOutGlobalPose);
	}
}

FTransform FChainDecoderFK::GetTransformAtParam(
	const TArray<FTransform>& Transforms,
	const TArray<float>& InParams,
	const float Param) const
{
	if (InParams.Num() == 1)
	{
		return Transforms[0];
	}
	
	if (Param < KINDA_SMALL_NUMBER)
	{
		return Transforms[0];
	}

	if (Param > 1.0f - KINDA_SMALL_NUMBER)
	{
		return Transforms.Last();
	}

	for (int32 ChainIndex=1; ChainIndex<InParams.Num(); ++ChainIndex)
	{
		const float CurrentParam = InParams[ChainIndex];
		if (CurrentParam <= Param)
		{
			continue;
		}
		
		const float PrevParam = InParams[ChainIndex-1];
		const float PercentBetweenParams = (Param - PrevParam) / (CurrentParam - PrevParam);
		const FTransform& Prev = Transforms[ChainIndex-1];
		const FTransform& Next = Transforms[ChainIndex];
		const FVector Position = FMath::Lerp(Prev.GetTranslation(), Next.GetTranslation(), PercentBetweenParams);
		const FQuat Rotation = FQuat::FastLerp(Prev.GetRotation(), Next.GetRotation(), PercentBetweenParams).GetNormalized();
		const FVector Scale = FMath::Lerp(Prev.GetScale3D(), Next.GetScale3D(), PercentBetweenParams);
		
		return FTransform(Rotation,Position, Scale);
	}

	checkNoEntry();
	return FTransform::Identity;
}

bool FChainEncoderIK::Initialize(
	const TArray<int32>& BoneIndices,
	const TArray<FTransform>& InitialGlobalPose)
{
	if (BoneIndices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget bone chain with IK but it has less than 3 joints."));
		return false;
	}
	
	BoneIndexA = BoneIndices[0];
	BoneIndexB = BoneIndices[1];
	BoneIndexC = BoneIndices.Last();
	
	const FTransform& End = InitialGlobalPose[BoneIndexC];
	EndPositionOrig = End.GetTranslation();
	EndRotationOrig = End.GetRotation();

	const FTransform& Start = InitialGlobalPose[BoneIndexA];
	const float Length = (Start.GetTranslation() - EndPositionOrig).Size();

	if (Length <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget bone chain with IK, but it is zero length!"));
    	return false;
	}

	InverseLength = 1.0f / Length;
	
	return true;
}

void FChainEncoderIK::EncodePose(const TArray<FTransform>& InputGlobalPose)
{
	const FVector A = InputGlobalPose[BoneIndexA].GetTranslation();
	//FVector B = InputGlobalPose[BoneIndexB].GetTranslation(); TODO use for pole vector 
	const FVector C = InputGlobalPose[BoneIndexC].GetTranslation();

    // get the normalized direction / length of the IK limb (how extended it is as percentage of original length)
    const FVector AC = C - A;
	float ACLength;
	FVector ACDirection;
	AC.ToDirectionAndLength(ACDirection, ACLength);
	const float NormalizedLimbLength = ACLength * InverseLength;

	EncodedResult.EndDirectionNormalized = ACDirection * NormalizedLimbLength;
	EncodedResult.EndRotation = InputGlobalPose[BoneIndexC].GetRotation();
	EncodedResult.EndRotationOrig = EndRotationOrig;
	EncodedResult.HeightFromGroundNormalized = (C.Z - EndPositionOrig.Z)  * InverseLength;
	EncodedResult.PoleVectorDirection = FVector::OneVector; // TBD
}

bool FChainDecoderIK::Initialize(
	const TArray<int32>& BoneIndices,
	const TArray<FTransform> &InitialGlobalPose)
{
	if (BoneIndices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget bone chain with IK but it has less than 3 joints."));
		return false;
	}
	
	BoneIndexA = BoneIndices[0];
	const FTransform& Last = InitialGlobalPose[BoneIndices.Last()];
	EndPositionOrig = Last.GetTranslation();
	EndRotationOrig = Last.GetRotation();
	Length = (InitialGlobalPose[BoneIndexA].GetTranslation() - Last.GetTranslation()).Size();

	if (Length <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget bone chain with IK, but it is zero length!"));
		return false;
	}
	
	return true;
}
	
void FChainDecoderIK::DecodePose(
	const FEncodedIKChain& EncodedChain,
    const TArray<FTransform>& OutGlobalPose,
    FDecodedIKChain& OutResults) const
{
	// calculate end effector position
	const FVector Start = OutGlobalPose[BoneIndexA].GetTranslation();
	OutResults.EndEffectorPosition = Start + EncodedChain.EndDirectionNormalized * Length; // * LimbScale (todo)
	// optionally factor in ground height
	//const bool IsGrounded = false;
	//if (IsGrounded) // todo expose this
	//{
	//	OutResults.EndEffectorPosition.Z = (EncodedChain.HeightFromGroundNormalized * Length) + EndPositionOrig.Z;
	//}

	// calculate end effector rotation
	const FQuat Delta = EndRotationOrig * EncodedChain.EndRotationOrig.Inverse();
	OutResults.EndEffectorRotation = Delta * EncodedChain.EndRotation;

	// TBD calc pole vector position
	OutResults.PoleVectorPosition = FVector::OneVector;
}

bool FRetargetChainPair::Initialize(
    const FBoneChain& SourceBoneChain,
    const FBoneChain& TargetBoneChain,
    const FRetargetSkeleton& SourceSkeleton,
    const FTargetSkeleton& TargetSkeleton)
{
	// validate source bone chain is compatible with source skeletal mesh
	const bool bIsSourceValid = ValidateBoneChainWithSkeletalMesh(true, SourceBoneChain, SourceSkeleton);
	if (!bIsSourceValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter source bone chain, '%s', is not compatible with Skeletal Mesh: '%s'"),
			*SourceBoneChain.ChainName.ToString(), *SourceSkeleton.SkeletalMeshName);
		return false;
	}

	// validate target bone chain is compatible with target skeletal mesh
	const bool bIsTargetValid = ValidateBoneChainWithSkeletalMesh(false, TargetBoneChain, TargetSkeleton);
	if (!bIsTargetValid)
    {
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter target bone chain, '%s', is not compatible with Skeletal Mesh: '%s'"),
            *TargetBoneChain.ChainName.ToString(), *TargetSkeleton.SkeletalMeshName);
		return false;
    }

	// store attributes of chain
	SourceBoneChainName = SourceBoneChain.ChainName;
	TargetBoneChainName = TargetBoneChain.ChainName;
	
	return true;
}

bool FRetargetChainPair::ValidateBoneChainWithSkeletalMesh(
    const bool IsSource,
    const FBoneChain& BoneChain,
    const FRetargetSkeleton& RetargetSkeleton)
{	
	// validate start bone exists
	const int32 StartIndex = RetargetSkeleton.FindBoneIndexByName(BoneChain.StartBone);
	if (StartIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain, %s, could not find start bone, %s in mesh %s"),
            *BoneChain.ChainName.ToString(), *BoneChain.StartBone.ToString(), *RetargetSkeleton.SkeletalMeshName);
		return false;
	}
	
	// validate end bone exists
	const int32 EndIndex = RetargetSkeleton.FindBoneIndexByName(BoneChain.EndBone);
	if (EndIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain, %s, could not find end bone, %s in mesh %s"),
            *BoneChain.ChainName.ToString(), *BoneChain.EndBone.ToString(), *RetargetSkeleton.SkeletalMeshName);
		return false;
	}
	
	// validate end bone is child of start bone
	// record all bones in chain while walking up the hierarchy (tip to root of chain)
	TArray<int>& BoneIndices = IsSource ? SourceBoneIndices : TargetBoneIndices;
	BoneIndices.Reset();
	int32 NextBoneIndex = EndIndex;
	while (true)
	{
		BoneIndices.Add(NextBoneIndex);
		
		if (NextBoneIndex == StartIndex)
		{
			break;
		}

		NextBoneIndex = RetargetSkeleton.GetParentIndex(NextBoneIndex);
		
		if (NextBoneIndex == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain, '%s' has end bone, '%s' that is not a child of start bone, '%s' in mesh '%s'"),
            *BoneChain.ChainName.ToString(), *BoneChain.EndBone.ToString(), *BoneChain.StartBone.ToString(), *RetargetSkeleton.SkeletalMeshName);
			return false;
		}
	}

	// reverse the indices (we want root to tip order)
	Algo::Reverse(BoneIndices);
	
	return true;
}

bool FRetargetChainPairFK::Initialize(
	const FBoneChain& SourceBoneChain,
	const FBoneChain& TargetBoneChain,
	const FRetargetSkeleton& SourceSkeleton,
	const FTargetSkeleton& TargetSkeleton)
{
	const bool bChainInitialized = FRetargetChainPair::Initialize(SourceBoneChain, TargetBoneChain, SourceSkeleton, TargetSkeleton);
	if (!bChainInitialized)
	{
		return false;
	}

	// initialize SOURCE FK chain encoder with retarget pose
	const bool bFKEncoderInitialized = FKEncoder.Initialize(SourceBoneIndices, SourceSkeleton.RetargetGlobalPose);
	if (!bFKEncoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize FK encoder, '%s', on Skeletal Mesh: '%s'"),
            *SourceBoneChainName.ToString(), *SourceSkeleton.SkeletalMeshName);
		return false;
	}

	// initialize TARGET FK chain decoder with retarget pose
	const bool bFKDecoderInitialized = FKDecoder.Initialize(TargetBoneIndices, TargetSkeleton.RetargetGlobalPose);
	if (!bFKDecoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize FK decoder, '%s', on Skeletal Mesh: '%s'"),
            *TargetBoneChainName.ToString(), *TargetSkeleton.SkeletalMeshName);
		return false;
	}

	return true;
}

bool FRetargetChainPairIK::Initialize(
	const FBoneChain& SourceBoneChain,
	const FBoneChain& TargetBoneChain,
	const FRetargetSkeleton& SourceSkeleton,
	const FTargetSkeleton& TargetSkeleton)
{
	// validate if this chain even uses an IK Goal
	const bool bUsingIKGoal = TargetBoneChain.IKGoalName != NAME_None && TargetBoneChain.IKGoalName != "- None -";
	if (!bUsingIKGoal)
	{
		return false;
	}

	// store target IK goal
	IKGoalName = TargetBoneChain.IKGoalName;

	// initialize bone chains
	const bool bChainInitialized = FRetargetChainPair::Initialize(SourceBoneChain, TargetBoneChain, SourceSkeleton, TargetSkeleton);
	if (!bChainInitialized)
	{
		return false;
	}

	// initialize SOURCE IK chain encoder with retarget pose
	const bool bIKEncoderInitialized = IKEncoder.Initialize(SourceBoneIndices, SourceSkeleton.RetargetGlobalPose);
	if (!bIKEncoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize IK encoder, '%s', on Skeletal Mesh: '%s'"),
        *SourceBoneChainName.ToString(), *SourceSkeleton.SkeletalMeshName);
		return false;
	}

	// initialize TARGET IK chain decoder with retarget pose
	const bool bIKDecoderInitialized = IKDecoder.Initialize(TargetBoneIndices, TargetSkeleton.RetargetGlobalPose);
	if (!bIKDecoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize IK decoder, '%s', on Skeletal Mesh: '%s'"),
        *TargetBoneChainName.ToString(), *TargetSkeleton.SkeletalMeshName);
		return false;
	}

	return true;
}

void FIKRetargetPose::AddRotationDeltaToBone(FName BoneName, FQuat RotationDelta)
{
	FQuat* RotOffset = BoneRotationOffsets.Find(BoneName);
	if (RotOffset == nullptr)
	{
		// first time this bone has been modified in this pose
		BoneRotationOffsets.Emplace(BoneName, RotationDelta);
		return;
	}

	// accumulate delta rotation
	*RotOffset = RotationDelta * (*RotOffset);
}

bool FRootEncoder::Initialize(const FName RootBoneName, const FRetargetSkeleton& SourceSkeleton)
{
	// validate root bone exists
	BoneIndex = SourceSkeleton.FindBoneIndexByName(RootBoneName);
	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter could not find source root bone, %s in mesh %s"),
			*RootBoneName.ToString(), *SourceSkeleton.SkeletalMeshName);
		return false;
	}
	
	// record initial root data
	const FTransform InitialTransform = SourceSkeleton.RetargetGlobalPose[BoneIndex]; 
	float InitialHeight = InitialTransform.GetTranslation().Z;
	EncodedResult.InitialRotation = InitialTransform.GetRotation();

	// ensure root height is not at origin, this happens if user sets root to ACTUAL skeleton root and not pelvis
	if (InitialHeight < KINDA_SMALL_NUMBER)
	{
		// warn user and push it up slightly to avoid divide by zero
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter root bone is very near the ground plane. This is probably not correct."));
		InitialHeight = 1.0f;
	}

	// invert height
	InvInitialHeight = 1.0f / InitialHeight;

	return true;
}

void FRootEncoder::EncodePose(const TArray<FTransform>& InputComponentSpaceBoneTransforms)
{
	const FTransform& Transform = InputComponentSpaceBoneTransforms[BoneIndex];
	EncodedResult.NormalizedPosition = Transform.GetTranslation() * InvInitialHeight;
	EncodedResult.Rotation = Transform.GetRotation();	
}

bool FRootDecoder::Initialize(const FName RootBoneName, const FTargetSkeleton& TargetSkeleton)
{
	// validate root bone exists
	BoneIndex = TargetSkeleton.FindBoneIndexByName(RootBoneName);
	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter could not find target root bone, %s in mesh %s"),
            *RootBoneName.ToString(), *TargetSkeleton.SkeletalMeshName);
		return false;
	}

	const FTransform InitialTransform = TargetSkeleton.RetargetGlobalPose[BoneIndex];
	InitialHeight = InitialTransform.GetTranslation().Z;
	InitialRotation = InitialTransform.GetRotation();

	return true;
}

void FRootDecoder::DecodePose(
	const FEncodedRoot& EncodedResult,
	TArray<FTransform>& InOutTargetBoneTransforms,
	const float StrideScale) const
{
	// scale normalized position by root height
	FVector Position = EncodedResult.NormalizedPosition * InitialHeight;
	// scale horizontal displacement by stride scale factor
	Position.X *= StrideScale;
	Position.Y *= StrideScale;
	// apply position to target
	InOutTargetBoneTransforms[BoneIndex].SetTranslation(Position);

	// calc offset between initial source/target root rotations
	const FQuat RotationOffset = InitialRotation * EncodedResult.InitialRotation.Inverse();
	// add offset to the current source rotation
	const FQuat Rotation = RotationOffset * EncodedResult.Rotation;
	// apply rotation to target
	InOutTargetBoneTransforms[BoneIndex].SetRotation(Rotation);	
}

UIKRetargeter::UIKRetargeter()
	: SourceIKRigAsset(nullptr),
    TargetIKRigAsset(nullptr),
    bIsLoadedAndValid(false)
{
}

void UIKRetargeter::Initialize(USkeletalMesh* SourceSkeletalMesh, USkeletalMesh* TargetSkeletalMesh, UObject* Outer)
{
	bIsLoadedAndValid = false;

	check(SourceSkeletalMesh && TargetSkeletalMesh);
	
	// initialize skeleton data for source and target
	const TArray<FRetargetChainMap> DummySourceChainMapping;
	SourceSkeleton.Initialize(SourceSkeletalMesh, nullptr);
	TargetSkeleton.Initialize(TargetSkeletalMesh, &RetargetPoses[CurrentRetargetPose]);

	// initialize roots
	const bool bRootsInitialized = InitializeRoots();
	if (!bRootsInitialized)
	{
		// couldn't match up any BoneChain pairs, no retargeting possible
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize one or more root bones on source, %s and target, %s"),
            *SourceSkeleton.SkeletalMeshName, *TargetSkeleton.SkeletalMeshName);
		return;
	}

	// initialize pairs of bone chains
	const bool bAtLeastOneValidBoneChainPair = InitializeBoneChainPairs();
	if (!bAtLeastOneValidBoneChainPair)
	{
		// couldn't match up any BoneChain pairs, no retargeting possible
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to create any Bone Chain pairs between source, %s and target, %s"),
            *SourceSkeleton.SkeletalMeshName, *TargetSkeleton.SkeletalMeshName);
		return;
	}

	// initialize the IKRigProcessor for doing IK decoding
	const bool bIKRigInitialized = InitializeIKRig(Outer, TargetSkeletalMesh->GetRefSkeleton());
	if (!bIKRigInitialized)
	{
		// couldn't initialize the IK Rig
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize IK Rig for %s. See output for details."),
			*TargetSkeleton.SkeletalMeshName);
		return;
	}

	bIsLoadedAndValid = true;
}

bool UIKRetargeter::InitializeRoots()
{
	check(SourceIKRigAsset && TargetIKRigAsset);

	// initialize root encoder
	const FName SourceRootBoneName = SourceIKRigAsset->RetargetDefinition.RootBone;
	const bool bRootEncoderInit = RootEncoder.Initialize(SourceRootBoneName, SourceSkeleton);
	if (!bRootEncoderInit)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize source root, '%s' on skeletal mesh: '%s'"),
            *SourceRootBoneName.ToString(), *SourceSkeleton.SkeletalMeshName);
		return false;
	}

	// initialize root decoder
	const FName TargetRootBoneName = TargetIKRigAsset->RetargetDefinition.RootBone;
	const bool bRootDecoderInit = RootDecoder.Initialize(TargetRootBoneName, TargetSkeleton);
	if (!bRootDecoderInit)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize target root, '%s' on skeletal mesh: '%s'"),
            *TargetRootBoneName.ToString(), *TargetSkeleton.SkeletalMeshName);
		return false;
	}

	return true;
}

bool UIKRetargeter::InitializeBoneChainPairs()
{
	check(SourceIKRigAsset && TargetIKRigAsset);
	
	FRetargetDefinition& SourceRetargetDef = SourceIKRigAsset->RetargetDefinition;
	FRetargetDefinition& TargetRetargetDef = TargetIKRigAsset->RetargetDefinition;

	for (const FRetargetChainMap& ChainSettings : ChainMapping)
	{
		FName SourceChain = ChainSettings.SourceChain;
		FName TargetChain = ChainSettings.TargetChain;

		// user opted to not map this to anything, we don't need to spam a warning about it
		if (SourceChain == NAME_None)
		{
			continue; 
		}
		
		// get source bone chain
		FBoneChain* SourceBoneChain = SourceRetargetDef.GetBoneChainByName(SourceChain);
		if (!SourceBoneChain)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Retargeter missing source bone chain: %s"), *SourceChain.ToString());
			continue;
		}

		// get target bone chain
		FBoneChain* TargetBoneChain = TargetRetargetDef.GetBoneChainByName(TargetChain);
		if (!TargetBoneChain)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Retargeter missing target bone chain: %s"), *TargetChain.ToString());
			continue;
		}

		// all chains are loaded as FK (giving IK better starting pose)
		FRetargetChainPairFK ChainPair;
		const bool bChainPairValid = ChainPair.Initialize(*SourceBoneChain, *TargetBoneChain, SourceSkeleton, TargetSkeleton);
		if (bChainPairValid)
		{
			ChainPairsFK.Add(ChainPair);
		}
		
		// load IK chain
		FRetargetChainPairIK ChainPairIK;
		const bool bChainPairIKValid = ChainPairIK.Initialize(*SourceBoneChain, *TargetBoneChain, SourceSkeleton, TargetSkeleton);
		if (bChainPairIKValid)
		{
			ChainPairsIK.Add(ChainPairIK);
		}
	}

	// record which bones in the target skeleton are being retargeted
	for (const FRetargetChainPairFK& FKChainPair : ChainPairsFK)
	{
		for (const int32 BoneIndex : FKChainPair.TargetBoneIndices)
		{
			TargetSkeleton.SetBoneIsRetargeted(BoneIndex, true);
		}
	}

	// record intermediate bones (non-retargeted bones located BETWEEN FK chains on the target skeleton)
	for (FRetargetChainPairFK& FKChainPair : ChainPairsFK)
	{
		FKChainPair.FKDecoder.InitializeIntermediateParentIndices(
			RootDecoder.BoneIndex,
			FKChainPair.TargetBoneIndices[0],
			TargetSkeleton);
	}

	// root is updated before IK as well
	TargetSkeleton.SetBoneIsRetargeted(RootDecoder.BoneIndex, true);

	// return true if at least 1 pair of bone chains were initialized
	return !(ChainPairsIK.IsEmpty() && ChainPairsFK.IsEmpty());
}

bool UIKRetargeter::InitializeIKRig(UObject* Outer, const FReferenceSkeleton& InRefSkeleton)
{	
	// initialize IK Rig runtime processor
	if (!IKRigProcessor)
	{
		IKRigProcessor = NewObject<UIKRigProcessor>(Outer);	
	}
	IKRigProcessor->Initialize(TargetIKRigAsset, InRefSkeleton);
	if (IKRigProcessor->NeedsInitialized(TargetIKRigAsset))
	{
		return false;
	}

	// validate that all IK bone chains have an associated Goal
	TArray<FName> GoalNames;
	IKRigProcessor->GetGoalContainer().Goals.GenerateKeyArray(GoalNames);
	for (FRetargetChainPairIK& ChainPair : ChainPairsIK)
	{
		// does the IK rig have the IK goal this bone chain requires?
		if (!GoalNames.Contains(ChainPair.IKGoalName))
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Retargeter has target bone chain, %s that references an IK Goal, %s that is not present in any of the solvers in the IK Rig asset."),
            *ChainPair.TargetBoneChainName.ToString(), *ChainPair.IKGoalName.ToString());
			return false;
		}
	}
	
	return true;
}

TArray<FTransform>&  UIKRetargeter::RunRetargeter(const TArray<FTransform>& InSourceGlobalPose)
{
	check(bIsLoadedAndValid);

	// start from retarget pose
	TargetSkeleton.OutputGlobalPose = TargetSkeleton.RetargetGlobalPose;
	
	// in edit mode we just want to see the edited reference pose, not actually run the retargeting
	// as long as the retargeter is reinitialized after every modification to the limb rotation offsets,
	// then the TargetSkeleton.RetargetGlobalPose will contain the updated retarget pose.
	if (bEditReferencePoseMode)
	{
		return TargetSkeleton.OutputGlobalPose; 
	}

	// ROOT retargeting
	if (bRetargetRoot)
	{
		RunRootRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
		// update global transforms below root
		TargetSkeleton.UpdateGlobalTransformsBelowBone(RootDecoder.BoneIndex, TargetSkeleton.RetargetLocalPose, TargetSkeleton.OutputGlobalPose);
	}	
	// FK CHAIN retargeting
	if (bRetargetFK)
	{
		RunFKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
		// update all the bones that are not controlled by FK chains or root
		TargetSkeleton.UpdateGlobalTransformsAllNonRetargetedBones(TargetSkeleton.OutputGlobalPose);
	}
	
	// IK CHAIN retargeting
	if (bRetargetIK)
	{
		RunIKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
	}

	return TargetSkeleton.OutputGlobalPose;
}

void UIKRetargeter::RunRootRetarget(
	const TArray<FTransform>& InGlobalTransforms,
    TArray<FTransform>& OutGlobalTransforms)
{
	RootEncoder.EncodePose(InGlobalTransforms);
	const float StrideScale = 1.0f;
	RootDecoder.DecodePose(RootEncoder.EncodedResult, OutGlobalTransforms, StrideScale);
}

void UIKRetargeter::RunFKRetarget(
	const TArray<FTransform>& InGlobalTransforms,
    TArray<FTransform>& OutGlobalTransforms)
{
	// spin through chains and encode/decode them all using the input pose
    for (FRetargetChainPairFK& ChainPair : ChainPairsFK)
    {
    	ChainPair.FKEncoder.EncodePose(ChainPair.SourceBoneIndices, InGlobalTransforms);
    	ChainPair.FKDecoder.DecodePose(ChainPair.TargetBoneIndices, ChainPair.FKEncoder, TargetSkeleton,OutGlobalTransforms);
    }
}

void UIKRetargeter::RunIKRetarget(
	const TArray<FTransform>& InGlobalPose,
    TArray<FTransform>& OutGlobalPose)
{
	if (ChainPairsIK.IsEmpty())
	{
		return; // skip IK
	}
	
	// spin through IK chains
	for (FRetargetChainPairIK& ChainPair : ChainPairsIK)
	{
		// encode them all using the input pose
		ChainPair.IKEncoder.EncodePose(InGlobalPose);
		// decode the IK goal and apply to IKRig
		FDecodedIKChain OutIKGoal;
		ChainPair.IKDecoder.DecodePose(ChainPair.IKEncoder.EncodedResult, OutGlobalPose, OutIKGoal);
		// set the goal transform on the IK Rig
		FIKRigGoal Goal = FIKRigGoal(
			ChainPair.IKGoalName,
			OutIKGoal.EndEffectorPosition,
			OutIKGoal.EndEffectorRotation,
			1.0f,
			1.0f);
		IKRigProcessor->SetIKGoal(Goal);
	}

	// copy input pose to start IK solve from
	IKRigProcessor->SetInputPoseGlobal(OutGlobalPose);
	// run IK solve
	IKRigProcessor->Solve();
	// copy results of solve
	IKRigProcessor->CopyOutputGlobalPoseToArray(OutGlobalPose);
}

#if WITH_EDITOR

FName UIKRetargeter::GetSourceRootBone()
{
	return SourceIKRigAsset ? SourceIKRigAsset->RetargetDefinition.RootBone : FName("None");
}

FName UIKRetargeter::GetTargetRootBone()
{
	return TargetIKRigAsset ? TargetIKRigAsset->RetargetDefinition.RootBone : FName("None");
}

void UIKRetargeter::GetTargetChainNames(TArray<FName>& OutNames) const
{
	if (TargetIKRigAsset)
	{
		for (const FBoneChain& Chain : TargetIKRigAsset->RetargetDefinition.BoneChains)
		{
			OutNames.Add(Chain.ChainName);
		}
	}
}

void UIKRetargeter::GetSourceChainNames(TArray<FName>& OutNames) const
{
	if (SourceIKRigAsset)
	{
		for (const FBoneChain& Chain : SourceIKRigAsset->RetargetDefinition.BoneChains)
		{
			OutNames.Add(Chain.ChainName);
		}
	}
}

void UIKRetargeter::CleanChainMapping()
{
	if (!TargetIKRigAsset)
	{
		// don't clean chain mappings, in case user is replacing with IK Rig asset that has some valid mappings
		return;
	}
	
	TArray<FName> TargetChainNames;
	GetTargetChainNames(TargetChainNames);

	// remove all target chains that are no longer in the target IK rig asset
	TArray<FName> TargetChainsToRemove;
	for (FRetargetChainMap& ChainMap : ChainMapping)
	{
		if (!TargetChainNames.Contains(ChainMap.TargetChain))
		{
			TargetChainsToRemove.Add(ChainMap.TargetChain);
		}
	}
	for (FName TargetChainToRemove : TargetChainsToRemove)
	{
		ChainMapping.RemoveAll([&TargetChainToRemove](FRetargetChainMap& Element)
		{
			return Element.TargetChain == TargetChainToRemove;
		});
	}

	// add a mapping for each chain that is in the target IK rig (if it doesn't have one already)
	for (FName TargetChainName : TargetChainNames)
	{
		const bool HasChain = ChainMapping.ContainsByPredicate([&TargetChainName](FRetargetChainMap& Element)
		{
			return Element.TargetChain == TargetChainName;
		});
		
		if (!HasChain)
		{
			ChainMapping.Add(FRetargetChainMap(TargetChainName));
		}
	}

	TArray<FName> SourceChainNames;
	GetSourceChainNames(SourceChainNames);
	
	// reset any sources that are no longer present to "None"
	for (FRetargetChainMap& ChainMap : ChainMapping)
	{
		if (!SourceChainNames.Contains(ChainMap.SourceChain))
		{
			ChainMap.SourceChain = NAME_None;
		}
	}

	// enforce the same chain order as the target IK rig
	ChainMapping.Sort([this](const FRetargetChainMap& A, const FRetargetChainMap& B)
	{
		const TArray<FBoneChain>& BoneChains = TargetIKRigAsset->RetargetDefinition.BoneChains;
		
		const int32 IndexA = BoneChains.IndexOfByPredicate([&A](const FBoneChain& Chain)
		{
			return A.TargetChain == Chain.ChainName;
		});

		const int32 IndexB = BoneChains.IndexOfByPredicate([&B](const FBoneChain& Chain)
		{
			return B.TargetChain == Chain.ChainName;
		});
 
		return IndexA < IndexB;
	});

	// force update with latest mapping
	Modify();
}

void UIKRetargeter::CleanPoseList()
{
	// enforce the existence of a default pose
	const bool HasDefaultPose = RetargetPoses.Contains(DefaultPoseName);
	if (!HasDefaultPose)
	{
		RetargetPoses.Emplace(DefaultPoseName);
	}
	
	// use default pose unless set to something else
	if (CurrentRetargetPose == NAME_None)
	{
		CurrentRetargetPose = DefaultPoseName;
	}

	// remove all bone offsets that are no longer part of the target skeleton
	if (TargetIKRigAsset)
	{
		const TArray<FName> AllowedBoneNames = TargetIKRigAsset->Skeleton.BoneNames;
		for (TTuple<FName, FIKRetargetPose>& Pose : RetargetPoses)
		{
			// find bone offsets no longer in target skeleton
			TArray<FName> BonesToRemove;
			for (TTuple<FName, FQuat>& BoneOffset : Pose.Value.BoneRotationOffsets)
			{
				if (!AllowedBoneNames.Contains(BoneOffset.Key))
				{
					BonesToRemove.Add(BoneOffset.Key);
				}
			}
			
			// remove bone offsets
			for (const FName& BoneToRemove : BonesToRemove)
			{
				Pose.Value.BoneRotationOffsets.Remove(BoneToRemove);
			}
		}
	}

	Modify();
}

void UIKRetargeter::AutoMapChains()
{
	TArray<FName> SourceChainNames;
	GetSourceChainNames(SourceChainNames);
	
	// auto-map any chains that have no value using a fuzzy string search
	for (FRetargetChainMap& ChainMap : ChainMapping)
	{
		if (ChainMap.SourceChain != NAME_None)
		{
			continue; // already set by user
		}

		// find "best match" automatically as a convenience for the user
		FString TargetNameLowerCase = ChainMap.TargetChain.ToString().ToLower();
		float HighestScore = 0.2f;
		int32 HighestScoreIndex = -1;
		for (int32 ChainIndex=0; ChainIndex<SourceChainNames.Num(); ++ChainIndex)
		{
			FString SourceNameLowerCase = SourceChainNames[ChainIndex].ToString().ToLower();
			float WorstCase = TargetNameLowerCase.Len() + SourceNameLowerCase.Len();
			WorstCase = WorstCase < 1.0f ? 1.0f : WorstCase;
			const float Score = 1.0f - (Algo::LevenshteinDistance(TargetNameLowerCase, SourceNameLowerCase) / WorstCase);
			if (Score > HighestScore)
			{
				HighestScore = Score;
				HighestScoreIndex = ChainIndex;
			}
		}

		// apply source if any decent matches were found
		if (SourceChainNames.IsValidIndex(HighestScoreIndex))
		{
			ChainMap.SourceChain = SourceChainNames[HighestScoreIndex];
		}
	}

	// force update with latest mapping
	Modify();
}

USkeleton* UIKRetargeter::GetSourceSkeletonAsset() const
{
	if (!SourceIKRigAsset)
	{
		return nullptr;
	}

	if (!SourceIKRigAsset->PreviewSkeletalMesh)
	{
		return nullptr;
	}

	return SourceIKRigAsset->PreviewSkeletalMesh->GetSkeleton();
}

void UIKRetargeter::AddRetargetPose(FName NewPoseName)
{
	if (RetargetPoses.Contains(NewPoseName))
	{
		return;
	}

	RetargetPoses.Add(NewPoseName);
	CurrentRetargetPose = NewPoseName;

	Modify();
}

void UIKRetargeter::RemoveRetargetPose(FName PoseToRemove)
{
	if (PoseToRemove == DefaultPoseName)
	{
		return; // cannot remove default pose
	}

	if (!RetargetPoses.Contains(PoseToRemove))
	{
		return; // cannot remove pose that doesn't exist
	}

	RetargetPoses.Remove(PoseToRemove);

	// did we remove the currently used pose?
	if (CurrentRetargetPose == PoseToRemove)
	{
		CurrentRetargetPose = UIKRetargeter::DefaultPoseName;
	}

	Modify();
}

void UIKRetargeter::ResetRetargetPose(FName PoseToReset)
{
	if (!RetargetPoses.Contains(PoseToReset))
	{
		return; // cannot reset pose that doesn't exist
	}

	RetargetPoses[PoseToReset].BoneRotationOffsets.Reset();
	RetargetPoses[PoseToReset].RootTranslationOffset = FVector::ZeroVector;
	RetargetPoses[PoseToReset].RootRotationOffset = FQuat::Identity;
	
	Modify();
}

void UIKRetargeter::SetCurrentRetargetPose(FName CurrentPose)
{
	check(RetargetPoses.Contains(CurrentPose));
	CurrentRetargetPose = CurrentPose;
	Modify();
}

void UIKRetargeter::AddRotationOffsetToRetargetPoseBone(FName BoneName, FQuat RotationOffset)
{
	RetargetPoses[CurrentRetargetPose].AddRotationDeltaToBone(BoneName, RotationOffset);
	Modify();
}

bool UIKRetargeter::Modify(bool bAlwaysMarkDirty)
{
	const bool bSavedToTransactionBuffer = Super::Modify(bAlwaysMarkDirty);
	AssetVersion++; // inform any runtime/editor systems they should copy-up modifications	
	return bSavedToTransactionBuffer;
}

void UIKRetargeter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == "TargetIKRigAsset"
		|| PropertyChangedEvent.GetPropertyName() == "SourceIKRigAsset")
	{
		AutoMapChains();
		CleanChainMapping();
	}
}

void UIKRetargeter::PostLoad()
{
	Super::PostLoad();
	CleanChainMapping();
	CleanPoseList();
}
#endif

FTransform UIKRetargeter::GetTargetBoneRetargetPoseGlobalTransform(const FName& TargetBoneName) const
{
	check(bIsLoadedAndValid);

	// makes sure is actually in skeleton
	const int32 BoneIndex = TargetSkeleton.FindBoneIndexByName(TargetBoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	// get the current retarget pose
	return TargetSkeleton.RetargetGlobalPose[BoneIndex];
}

void UIKRetargeter::GetTargetBoneStartAndEnd(const FName& TargetBoneName, FVector& OutStart, FVector& OutEnd) const
{
	check(bIsLoadedAndValid);

	// find the bone
	const int32 BoneIndex = TargetSkeleton.FindBoneIndexByName(TargetBoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}

	// find the end bone of the bone chain
	// todo, return list of lines to each child
	int32 FirstChildIndex = INDEX_NONE;
	for (int32 I=0; I<TargetSkeleton.ParentIndices.Num(); ++I)
	{
		const int32 ParentIndex = TargetSkeleton.ParentIndices[I];
		if (ParentIndex == BoneIndex)
		{
			FirstChildIndex = I;
			break;
		}
	}

	if (FirstChildIndex == INDEX_NONE)
	{
		return;
	}

	// get the origin of the bone chain
	OutStart = TargetSkeleton.RetargetGlobalPose[BoneIndex].GetTranslation();
	OutEnd = TargetSkeleton.RetargetGlobalPose[FirstChildIndex].GetTranslation();
}

FRetargetChainMap* UIKRetargeter::GetChainMap(const FName& TargetChainName)
{
	for (FRetargetChainMap& ChainMap : ChainMapping)
	{
		if (ChainMap.TargetChain == TargetChainName)
		{
			return &ChainMap;
		}
	}

	return nullptr;
}
