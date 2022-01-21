// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargetProcessor.h"

#include "IKRigDefinition.h"
#include "IKRigProcessor.h"
#include "Engine/SkeletalMesh.h"
#include "Retargeter/IKRetargeter.h"


void FRetargetSkeleton::Initialize(USkeletalMesh* InSkeletalMesh)
{	
	// record which skeletal mesh this is running on
	SkeletalMesh = InSkeletalMesh;
	
	// copy names and parent indices into local storage
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton.GetNum(); ++BoneIndex)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(BoneIndex));
		ParentIndices.Add(RefSkeleton.GetParentIndex(BoneIndex));	
	}

	// update retarget pose to reflect custom offsets
	GenerateRetargetPose();
}

void FRetargetSkeleton::Reset()
{
	BoneNames.Reset();
	ParentIndices.Reset();
	RetargetLocalPose.Reset();
	RetargetGlobalPose.Reset();
	SkeletalMesh = nullptr;
}

void FRetargetSkeleton::GenerateRetargetPose()
{
	// initialize retarget pose to the skeletal mesh reference pose
	RetargetLocalPose = SkeletalMesh->GetRefSkeleton().GetRefBonePose();
	// copy local pose to global
	RetargetGlobalPose = RetargetLocalPose;
	// convert to global space
	UpdateGlobalTransformsBelowBone(0, RetargetLocalPose, RetargetGlobalPose);
}

int32 FRetargetSkeleton::FindBoneIndexByName(const FName InName) const
{
	return BoneNames.IndexOfByPredicate([&InName](const FName& BoneName)
	{
			return BoneName == InName;
	});
}

void FRetargetSkeleton::UpdateGlobalTransformsBelowBone(
	const int32 StartBoneIndex,
	const TArray<FTransform>& InLocalPose,
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
	const TArray<FTransform>& InGlobalPose) const
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
	const FTransform& ChildLocalTransform = InLocalPose[BoneIndex];
	const FTransform& ParentGlobalTransform = OutGlobalPose[ParentIndex];
	OutGlobalPose[BoneIndex] = ChildLocalTransform * ParentGlobalTransform;
}

void FRetargetSkeleton::UpdateLocalTransformOfSingleBone(
	const int32 BoneIndex,
	TArray<FTransform>& OutLocalPose,
	const TArray<FTransform>& InGlobalPose) const
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		return; // root always in global space
	}
	const FTransform& ChildGlobalTransform = InGlobalPose[BoneIndex];
	const FTransform& ParentGlobalTransform = InGlobalPose[ParentIndex];
	OutLocalPose[BoneIndex] = ChildGlobalTransform.GetRelativeTransform(ParentGlobalTransform);
}

void FRetargetSkeleton::GetChildrenIndices(const int32 BoneIndex, TArray<int32>& OutChildren) const
{
	for (int32 ChildBoneIndex=0; ChildBoneIndex<ParentIndices.Num(); ++ChildBoneIndex)
	{
		if (ParentIndices[ChildBoneIndex] == BoneIndex)
		{
			OutChildren.Add(ChildBoneIndex);
		}
	}
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
	USkeletalMesh* InSkeletalMesh, 
	const FIKRetargetPose* RetargetPose,
	const FName& RetargetRootBone,
	const TArray<FBoneChain>& TargetChains)
{
	FRetargetSkeleton::Initialize(InSkeletalMesh);

	// initialize storage for output pose (the result of the retargeting)
	OutputGlobalPose = RetargetGlobalPose;

	// make storage for per-bone "Is Retargeted" flag (used for hierarchy updates)
	// these are bones that are in a target chain that is mapped to a source chain (ie, will actually be retargeted)
	// these flags are actually set later in init phase when bone chains are mapped together
	IsBoneRetargeted.Init(false, OutputGlobalPose.Num());

	// determine set of bones referenced by one of the target bone chains to be retargeted
	// this is the set of bones that will be affected by the retarget pose
	IsBoneInAnyTargetChain.Init(false, OutputGlobalPose.Num());
	for (const FBoneChain& TargetChain : TargetChains)
	{
		TArray<int32> BonesInChain;
		if (FResolvedBoneChain(TargetChain, *this, BonesInChain).IsValid())
		{
			for (int32 BoneInChain : BonesInChain)
			{
				IsBoneInAnyTargetChain[BoneInChain] = true;
			}
		}
	}

	// generate the retarget pose (applies stored offsets)
	// NOTE: this must be done AFTER generating IsBoneInAnyTargetChain array above
	GenerateRetargetPose(RetargetPose, RetargetRootBone);
}

void FTargetSkeleton::GenerateRetargetPose(const FIKRetargetPose* InRetargetPose, const FName& RetargetRootBone)
{
	// create a retarget pose by copying the ref pose
	FRetargetSkeleton::GenerateRetargetPose();
	
	// no retarget pose specified (will use default pose from skeletal mesh with no offsets)
	if (InRetargetPose==nullptr  || RetargetRootBone == NAME_None)
	{
		return;
	}

	// apply retarget pose offsets (retarget pose is stored as offset relative to reference pose)
	const TArray<FTransform>& RefPoseLocal = SkeletalMesh->GetRefSkeleton().GetRefBonePose();
	
	// apply root translation offset
	const int32 RootBoneIndex = FindBoneIndexByName(RetargetRootBone);
	if (RootBoneIndex != INDEX_NONE)
	{
		FTransform& RootTransform = RetargetGlobalPose[RootBoneIndex];
		RootTransform.AddToTranslation(InRetargetPose->RootTranslationOffset);
		UpdateLocalTransformOfSingleBone(RootBoneIndex, RetargetLocalPose, RetargetGlobalPose);
	}

	// apply bone rotation offsets
	for (const TTuple<FName, FQuat>& BoneRotationOffset : InRetargetPose->BoneRotationOffsets)
	{
		const int32 BoneIndex = FindBoneIndexByName(BoneRotationOffset.Key);
		if (BoneIndex == INDEX_NONE)
		{
			// this can happen if a retarget pose recorded a bone offset for a bone that is not present in the
			// target skeleton; ie, the retarget pose was generated from a different Skeletal Mesh with extra bones
			continue;
		}

		if (!IsBoneInAnyTargetChain[BoneIndex] && BoneIndex!=RootBoneIndex)
		{
			// this can happen if a retarget pose includes bone edits from a bone chain that was subsequently removed,
			// and the asset has not run through the "CleanChainMapping" operation yet (happens on load)
			continue;
		}

		const FQuat LocalBoneRotation = BoneRotationOffset.Value * RefPoseLocal[BoneIndex].GetRotation();
		RetargetLocalPose[BoneIndex].SetRotation(LocalBoneRotation);
	}

	UpdateGlobalTransformsBelowBone(0, RetargetLocalPose, RetargetGlobalPose);
}

void FTargetSkeleton::Reset()
{
	FRetargetSkeleton::Reset();
	OutputGlobalPose.Reset();
	IsBoneRetargeted.Reset();
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

FResolvedBoneChain::FResolvedBoneChain(
	const FBoneChain& BoneChain,
	const FRetargetSkeleton& Skeleton,
	TArray<int32> &OutBoneIndices)
{
	// validate start and end bones exist
	const int32 StartIndex = Skeleton.FindBoneIndexByName(BoneChain.StartBone.BoneName);
	const int32 EndIndex = Skeleton.FindBoneIndexByName(BoneChain.EndBone.BoneName);
	bFoundStartBone = StartIndex != INDEX_NONE;
	bFoundEndBone = EndIndex != INDEX_NONE;

	if (bFoundStartBone && bFoundEndBone)
	{
		// validate end bone is child of start bone
		bEndIsChildOfStart = true;
		// record all bones in chain while walking up the hierarchy (tip to root of chain)
		OutBoneIndices.Reset();
		int32 NextBoneIndex = EndIndex;
		while (true)
		{
			OutBoneIndices.Add(NextBoneIndex);
		
			if (NextBoneIndex == StartIndex)
			{
				break;
			}

			NextBoneIndex = Skeleton.GetParentIndex(NextBoneIndex);
		
			if (NextBoneIndex == 0)
			{
				// oops, we walked all the way up to the root without finding the start bone
				bEndIsChildOfStart = false;
				OutBoneIndices.Reset();
				return;
			}
		}

		// reverse the indices (we want root to tip order)
		Algo::Reverse(OutBoneIndices);
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
	InitialBoneTransforms.Reset();
	for (int32 Index=0; Index < BoneIndices.Num(); ++Index)
	{
		const int32 BoneIndex = BoneIndices[Index];
		if (ensure(InitialGlobalPose.IsValidIndex(BoneIndex)))
		{
			InitialBoneTransforms.Emplace(InitialGlobalPose[BoneIndex]);
		}
	}

	// initialize storage for current bones
	CurrentBoneTransforms = InitialBoneTransforms;

	// calculate parameter of each bone, normalized by the length of the bone chain
	return CalculateBoneParameters();
}

bool FChainFK::CalculateBoneParameters()
{
	Params.Reset();
	
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
		const float Param = Params[ChainIndex];
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
	const float& Param) const
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

bool FChainRetargeterIK::InitializeSource(
	const TArray<int32>& BoneIndices,
	const TArray<FTransform>& SourceInitialGlobalPose)
{
	if (BoneIndices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget source bone chain with IK but it has less than 3 joints."));
		return false;
	}
	
	Source.BoneIndexA = BoneIndices[0];
	Source.BoneIndexB = BoneIndices[1];
	Source.BoneIndexC = BoneIndices.Last();
	
	const FTransform& End = SourceInitialGlobalPose[Source.BoneIndexC];
	Source.InitialEndPosition = End.GetTranslation();
	Source.InitialEndRotation = End.GetRotation();

	const FTransform& Start = SourceInitialGlobalPose[Source.BoneIndexA];
	const float Length = (Start.GetTranslation() - Source.InitialEndPosition).Size();

	if (Length <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget source bone chain with IK, but it is zero length!"));
    	return false;
	}

	Source.InvInitialLength = 1.0f / Length;
	
	return true;
}

void FChainRetargeterIK::EncodePose(const TArray<FTransform>& InputGlobalPose)
{
	const FVector A = InputGlobalPose[Source.BoneIndexA].GetTranslation();
	//FVector B = InputGlobalPose[BoneIndexB].GetTranslation(); TODO use for pole vector 
	const FVector C = InputGlobalPose[Source.BoneIndexC].GetTranslation();

    // get the normalized direction / length of the IK limb (how extended it is as percentage of original length)
    const FVector AC = C - A;
	float ACLength;
	FVector ACDirection;
	AC.ToDirectionAndLength(ACDirection, ACLength);
	const float NormalizedLimbLength = ACLength * Source.InvInitialLength;

	Source.CurrentEndDirectionNormalized = ACDirection * NormalizedLimbLength;
	Source.CurrentEndRotation = InputGlobalPose[Source.BoneIndexC].GetRotation();
	Source.CurrentHeightFromGroundNormalized = (C.Z - Source.InitialEndPosition.Z)  * Source.InvInitialLength;
	Source.PoleVectorDirection = FVector::OneVector; // TBD
}

bool FChainRetargeterIK::InitializeTarget(
	const TArray<int32>& BoneIndices,
	const TArray<FTransform> &TargetInitialGlobalPose)
{
	if (BoneIndices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget target bone chain with IK but it has less than 3 joints."));
		return false;
	}
	
	Target.BoneIndexA = BoneIndices[0];
	const FTransform& Last = TargetInitialGlobalPose[BoneIndices.Last()];
	Target.InitialEndPosition = Last.GetTranslation();
	Target.InitialEndRotation = Last.GetRotation();
	Target.InitialLength = (TargetInitialGlobalPose[Target.BoneIndexA].GetTranslation() - Last.GetTranslation()).Size();

	if (Target.InitialLength <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter trying to retarget bone chain with IK, but it is zero length!"));
		return false;
	}
	
	return true;
}
	
void FChainRetargeterIK::DecodePose(
    const TArray<FTransform>& OutGlobalPose,
    FDecodedIKChain& OutResults) const
{
	// calculate end effector position
	const FVector Start = OutGlobalPose[Target.BoneIndexA].GetTranslation();
	OutResults.EndEffectorPosition = Start + Source.CurrentEndDirectionNormalized * Target.InitialLength; // * LimbScale (todo)
	// optionally factor in ground height
	//const bool IsGrounded = false;
	//if (IsGrounded) // todo expose this
	//{
	//	OutResults.EndEffectorPosition.Z = (EncodedChain.HeightFromGroundNormalized * Length) + EndPositionOrig.Z;
	//}

	// calculate end effector rotation
	const FQuat RotationDelta = Source.CurrentEndRotation * Source.InitialEndRotation.Inverse();
	OutResults.EndEffectorRotation = RotationDelta * Target.InitialEndRotation;

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
			*SourceBoneChain.ChainName.ToString(), *SourceSkeleton.SkeletalMesh->GetName());
		return false;
	}

	// validate target bone chain is compatible with target skeletal mesh
	const bool bIsTargetValid = ValidateBoneChainWithSkeletalMesh(false, TargetBoneChain, TargetSkeleton);
	if (!bIsTargetValid)
    {
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter target bone chain, '%s', is not compatible with Skeletal Mesh: '%s'"),
            *TargetBoneChain.ChainName.ToString(), *TargetSkeleton.SkeletalMesh->GetName());
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
	// record the chain indices
	TArray<int32>& BoneIndices = IsSource ? SourceBoneIndices : TargetBoneIndices;
	
	// resolve the bone bone to the skeleton
	const FResolvedBoneChain ResolvedChain = FResolvedBoneChain(BoneChain, RetargetSkeleton, BoneIndices);
	
	// warn if START bone not found
	if (!ResolvedChain.bFoundStartBone)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain, %s, could not find start bone, %s in mesh %s"),
            *BoneChain.ChainName.ToString(), *BoneChain.StartBone.BoneName.ToString(), *RetargetSkeleton.SkeletalMesh->GetName());
	}
	
	// warn if END bone not found
	if (!ResolvedChain.bFoundEndBone)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain, %s, could not find end bone, %s in mesh %s"),
            *BoneChain.ChainName.ToString(), *BoneChain.EndBone.BoneName.ToString(), *RetargetSkeleton.SkeletalMesh->GetName());
	}

	// warn if END bone was not a child of START bone
	if (!ResolvedChain.bEndIsChildOfStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter bone chain, %s, end bone, '%s' was not a child of the start bone '%s'."),
			*BoneChain.ChainName.ToString(), *BoneChain.EndBone.BoneName.ToString(), *BoneChain.StartBone.BoneName.ToString());
	}
	
	return ResolvedChain.IsValid();
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
            *SourceBoneChainName.ToString(), *SourceSkeleton.SkeletalMesh->GetName());
		return false;
	}

	// initialize TARGET FK chain decoder with retarget pose
	const bool bFKDecoderInitialized = FKDecoder.Initialize(TargetBoneIndices, TargetSkeleton.RetargetGlobalPose);
	if (!bFKDecoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize FK decoder, '%s', on Skeletal Mesh: '%s'"),
            *TargetBoneChainName.ToString(), *TargetSkeleton.SkeletalMesh->GetName());
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
	const bool bIKEncoderInitialized = IKChainRetargeter.InitializeSource(SourceBoneIndices, SourceSkeleton.RetargetGlobalPose);
	if (!bIKEncoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize IK encoder, '%s', on Skeletal Mesh: '%s'"),
        *SourceBoneChainName.ToString(), *SourceSkeleton.SkeletalMesh->GetName());
		return false;
	}

	// initialize TARGET IK chain decoder with retarget pose
	const bool bIKDecoderInitialized = IKChainRetargeter.InitializeTarget(TargetBoneIndices, TargetSkeleton.RetargetGlobalPose);
	if (!bIKDecoderInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter failed to initialize IK decoder, '%s', on Skeletal Mesh: '%s'"),
        *TargetBoneChainName.ToString(), *TargetSkeleton.SkeletalMesh->GetName());
		return false;
	}

	return true;
}

bool FRootRetargeter::InitializeSource(const FName SourceRootBoneName, const FRetargetSkeleton& SourceSkeleton)
{
	// validate target root bone exists
	Source.BoneIndex = SourceSkeleton.FindBoneIndexByName(SourceRootBoneName);
	if (Source.BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter could not find source root bone, %s in mesh %s"),
			*SourceRootBoneName.ToString(), *SourceSkeleton.SkeletalMesh->GetName());
		return false;
	}
	
	// record initial root data
	const FTransform InitialTransform = SourceSkeleton.RetargetGlobalPose[Source.BoneIndex]; 
	float InitialHeight = InitialTransform.GetTranslation().Z;
	Source.InitialRotation = InitialTransform.GetRotation();

	// ensure root height is not at origin, this happens if user sets root to ACTUAL skeleton root and not pelvis
	if (InitialHeight < KINDA_SMALL_NUMBER)
	{
		// warn user and push it up slightly to avoid divide by zero
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter root bone is very near the ground plane. This is probably not correct."));
		InitialHeight = 1.0f;
	}

	// invert height
	Source.InitialHeightInverse = 1.0f / InitialHeight;

	return true;
}

bool FRootRetargeter::InitializeTarget(const FName TargetRootBoneName, const FTargetSkeleton& TargetSkeleton)
{
	// validate target root bone exists
	Target.BoneIndex = TargetSkeleton.FindBoneIndexByName(TargetRootBoneName);
	if (Target.BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter could not find target root bone, %s in mesh %s"),
            *TargetRootBoneName.ToString(), *TargetSkeleton.SkeletalMesh->GetName());
		return false;
	}

	const FTransform TargetInitialTransform = TargetSkeleton.RetargetGlobalPose[Target.BoneIndex];
	Target.InitialHeight = TargetInitialTransform.GetTranslation().Z;
	Target.InitialRotation = TargetInitialTransform.GetRotation();

	return true;
}

void FRootRetargeter::Reset()
{
	Source = FRootSource();
	Target = FRootTarget();
}

void FRootRetargeter::EncodePose(const TArray<FTransform>& SourceGlobalPose)
{
	const FTransform& SourceTransform = SourceGlobalPose[Source.BoneIndex];
	Source.CurrentPositionNormalized = SourceTransform.GetTranslation() * Source.InitialHeightInverse;
	Source.CurrentRotation = SourceTransform.GetRotation();	
}

void FRootRetargeter::DecodePose(
	TArray<FTransform>& OutTargetGlobalPose,
	const float StrideScale) const
{
	// scale normalized position by root height
	FVector Position = Source.CurrentPositionNormalized * Target.InitialHeight;
	// scale horizontal displacement by stride scale
	Position *= FVector(StrideScale, StrideScale, 1.0f);

	// calc offset between initial source/target root rotations
	const FQuat RotationDelta = Source.CurrentRotation * Source.InitialRotation.Inverse();
	// add offset to the current source rotation
	const FQuat Rotation = RotationDelta * Target.InitialRotation;

	// apply to target
	FTransform& TargetRootTransform = OutTargetGlobalPose[Target.BoneIndex];
	TargetRootTransform.SetTranslation(Position);
	TargetRootTransform.SetRotation(Rotation);
}

void UIKRetargetProcessor::Initialize(
		USkeletalMesh* SourceSkeletalMesh,
		USkeletalMesh* TargetSkeletalMesh,
		UIKRetargeter* InRetargeterAsset)
{
	bIsInitialized = false;

	// record source asset
	RetargeterAsset = InRetargeterAsset;

	// reset all data structures
	SourceSkeleton.Reset();
	TargetSkeleton.Reset();
	IKRigProcessor = nullptr;
	ChainPairsFK.Reset();
	ChainPairsIK.Reset();
	RootRetargeter.Reset();

	// check prerequisite assets
	if (!SourceSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("IK Retargeter unable to initialize. Missing source Skeletal Mesh asset."));
		return;
	}
	if (!TargetSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("IK Retargeter unable to initialize. Missing target Skeletal Mesh asset."));
		return;
	}
	if (!RetargeterAsset->GetSourceIKRig())
	{
		UE_LOG(LogTemp, Error, TEXT("IK Retargeter unable to initialize. Missing source IK Rig asset."));
		return;
	}
	if (!RetargeterAsset->GetTargetIKRig())
	{
		UE_LOG(LogTemp, Error, TEXT("IK Retargeter unable to initialize. Missing target IK Rig asset."));
		return;
	}
	if (!RetargeterAsset->GetCurrentRetargetPose())
	{
		UE_LOG(LogTemp, Error, TEXT("IK Retargeter unable to initialize. Missing retarget pose."));
		return;
	}
	
	// initialize skeleton data for source and target
	SourceSkeleton.Initialize(SourceSkeletalMesh);
	TargetSkeleton.Initialize(
		TargetSkeletalMesh,
		RetargeterAsset->GetCurrentRetargetPose(),
		RetargeterAsset->GetTargetIKRig()->GetRetargetRoot(),
		RetargeterAsset->GetTargetIKRig()->GetRetargetChains());

	// initialize roots
	bRootsInitialized = InitializeRoots();
	if (!bRootsInitialized)
	{
		// couldn't match up any BoneChain pairs, no retargeting possible
		UE_LOG(LogTemp, Error, TEXT("IK Retargeter unable to initialize one or more root bones on source, %s and target, %s"),
            *SourceSkeleton.SkeletalMesh->GetName(), *TargetSkeleton.SkeletalMesh->GetName());
	}

	// initialize pairs of bone chains
	bAtLeastOneValidBoneChainPair = InitializeBoneChainPairs();
	if (!bAtLeastOneValidBoneChainPair)
	{
		// couldn't match up any BoneChain pairs, no limb retargeting possible
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to create any Bone Chain pairs between source, %s and target, %s"),
            *SourceSkeleton.SkeletalMesh->GetName(), *TargetSkeleton.SkeletalMesh->GetName());
	}

	// initialize the IKRigProcessor for doing IK decoding
	bIKRigInitialized = InitializeIKRig(this, TargetSkeletalMesh->GetRefSkeleton());
	if (!bIKRigInitialized)
	{
		// couldn't initialize the IK Rig, we don't disable the retargeter in this case, just warn the user
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize IK Rig for %s. See output for details."),
			*TargetSkeleton.SkeletalMesh->GetName());
	}

	bIsInitialized = true;
}

bool UIKRetargetProcessor::InitializeRoots()
{
	// initialize root encoder
	const FName SourceRootBoneName = RetargeterAsset->GetSourceIKRig()->GetRetargetRoot();
	const bool bRootEncoderInit = RootRetargeter.InitializeSource(SourceRootBoneName, SourceSkeleton);
	if (!bRootEncoderInit)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize source root, '%s' on skeletal mesh: '%s'"),
            *SourceRootBoneName.ToString(), *SourceSkeleton.SkeletalMesh->GetName());
	}

	// initialize root decoder
	const FName TargetRootBoneName = RetargeterAsset->GetTargetIKRig()->GetRetargetRoot();
	const bool bRootDecoderInit = RootRetargeter.InitializeTarget(TargetRootBoneName, TargetSkeleton);
	if (!bRootDecoderInit)
	{
		UE_LOG(LogTemp, Warning, TEXT("IK Retargeter unable to initialize target root, '%s' on skeletal mesh: '%s'"),
            *TargetRootBoneName.ToString(), *TargetSkeleton.SkeletalMesh->GetName());
	}

	return bRootEncoderInit && bRootDecoderInit;
}

bool UIKRetargetProcessor::InitializeBoneChainPairs()
{
	const UIKRigDefinition* TargetIKRig = RetargeterAsset->GetTargetIKRig();
	const UIKRigDefinition* SourceIKRig = RetargeterAsset->GetSourceIKRig();
	
	check(SourceIKRig && TargetIKRig);

	// check that chains are available in both IKRig assets before sorting them based on StartBone index
	const TArray<FRetargetChainMap>& ChainMapping = RetargeterAsset->GetChainMapping();	
	for (const FRetargetChainMap& ChainMap : ChainMapping)
	{
		// get target bone chain
		const FBoneChain* TargetBoneChain = RetargeterAsset->GetTargetIKRig()->GetRetargetChainByName(ChainMap.TargetChain);
		if (!TargetBoneChain)
		{
			UE_LOG(LogTemp, Error, TEXT("IK Retargeter missing target bone chain: %s. Please update the mapping."), *ChainMap.TargetChain.ToString());
			continue;
		}
		
		// user opted to not map this to anything, we don't need to spam a warning about it
		if (ChainMap.SourceChain == NAME_None)
		{
			continue; 
		}
		
		// get source bone chain
		const FBoneChain* SourceBoneChain = RetargeterAsset->GetSourceIKRig()->GetRetargetChainByName(ChainMap.SourceChain);
		if (!SourceBoneChain)
		{
			UE_LOG(LogTemp, Error, TEXT("IK Retargeter missing source bone chain: %s"), *ChainMap.SourceChain.ToString());
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

	// sort the chains based on their StartBone's index
	auto ChainsSorter = [this](const FRetargetChainPair& A, const FRetargetChainPair& B)
	{
		const int32 IndexA = A.TargetBoneIndices.Num() > 0 ? A.TargetBoneIndices[0] : INDEX_NONE;
		const int32 IndexB = B.TargetBoneIndices.Num() > 0 ? B.TargetBoneIndices[0] : INDEX_NONE;
		if (IndexA == IndexB)
		{
			// fallback to sorting alphabetically
			return A.TargetBoneChainName.LexicalLess(B.TargetBoneChainName);
		}
		return IndexA < IndexB;
	};
	
	ChainPairsFK.Sort(ChainsSorter);
	ChainPairsIK.Sort(ChainsSorter);
	
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
			RootRetargeter.Target.BoneIndex,
			FKChainPair.TargetBoneIndices[0],
			TargetSkeleton);
	}

	// root is updated before IK as well
	if (bRootsInitialized)
	{
		TargetSkeleton.SetBoneIsRetargeted(RootRetargeter.Target.BoneIndex, true);	
	}

	// return true if at least 1 pair of bone chains were initialized
	return !(ChainPairsIK.IsEmpty() && ChainPairsFK.IsEmpty());
}

bool UIKRetargetProcessor::InitializeIKRig(UObject* Outer, const FReferenceSkeleton& InRefSkeleton)
{	
	// initialize IK Rig runtime processor
	if (!IKRigProcessor)
	{
		IKRigProcessor = NewObject<UIKRigProcessor>(Outer);	
	}
	IKRigProcessor->Initialize(RetargeterAsset->GetTargetIKRig(), InRefSkeleton);
	if (!IKRigProcessor->IsInitialized())
	{
		return false;
	}

	// validate that all IK bone chains have an associated Goal
	for (FRetargetChainPairIK& ChainPair : ChainPairsIK)
	{
		// does the IK rig have the IK goal this bone chain requires?
		if (!IKRigProcessor->GetGoalContainer().FindGoalByName(ChainPair.IKGoalName))
		{
			UE_LOG(LogTemp, Error, TEXT("IK Retargeter has target bone chain, %s that references an IK Goal, %s that is not present in any of the solvers in the IK Rig asset."),
            *ChainPair.TargetBoneChainName.ToString(), *ChainPair.IKGoalName.ToString());
			return false;
		}
	}
	
	return true;
}

TArray<FTransform>&  UIKRetargetProcessor::RunRetargeter(const TArray<FTransform>& InSourceGlobalPose)
{
	check(bIsInitialized);

#if WITH_EDITOR
	// in edit mode we just want to see the edited reference pose, not actually run the retargeting
	// as long as the retargeter is reinitialized after every modification to the limb rotation offsets,
	// then the TargetSkeleton.RetargetGlobalPose will contain the updated retarget pose.
	if (RetargeterAsset->IsInEditRetargetPoseMode() && RetargeterAsset->GetTargetIKRig())
	{
		const FName RootBoneName = RetargeterAsset->GetTargetIKRig()->GetRetargetRoot();
		TargetSkeleton.GenerateRetargetPose(RetargeterAsset->GetCurrentRetargetPose(), RootBoneName);
		return TargetSkeleton.RetargetGlobalPose; 
	}
#endif

	// start from retarget pose
	TargetSkeleton.OutputGlobalPose = TargetSkeleton.RetargetGlobalPose;

	// ROOT retargeting
	if (RetargeterAsset->bRetargetRoot && bRootsInitialized)
	{
		RunRootRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
		// update global transforms below root
		TargetSkeleton.UpdateGlobalTransformsBelowBone(RootRetargeter.Target.BoneIndex, TargetSkeleton.RetargetLocalPose, TargetSkeleton.OutputGlobalPose);
	}
	
	// FK CHAIN retargeting
	if (RetargeterAsset->bRetargetFK && bAtLeastOneValidBoneChainPair)
	{
		RunFKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
		// update all the bones that are not controlled by FK chains or root
		TargetSkeleton.UpdateGlobalTransformsAllNonRetargetedBones(TargetSkeleton.OutputGlobalPose);
	}
	
	// IK CHAIN retargeting
	if (RetargeterAsset->bRetargetIK && bAtLeastOneValidBoneChainPair && bIKRigInitialized)
	{
		RunIKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
	}

	return TargetSkeleton.OutputGlobalPose;
}

void UIKRetargetProcessor::RunRootRetarget(
	const TArray<FTransform>& InGlobalTransforms,
    TArray<FTransform>& OutGlobalTransforms)
{
	RootRetargeter.EncodePose(InGlobalTransforms);
	const float StrideScale = 1.0f;
	RootRetargeter.DecodePose(OutGlobalTransforms, StrideScale);
}

void UIKRetargetProcessor::RunFKRetarget(
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

void UIKRetargetProcessor::RunIKRetarget(
	const TArray<FTransform>& InGlobalPose,
    TArray<FTransform>& OutGlobalPose)
{
	if (!IKRigProcessor->IsInitialized())
	{
		return;
	}
	
	if (ChainPairsIK.IsEmpty())
	{
		return; // skip IK
	}
	
	// spin through IK chains
	for (FRetargetChainPairIK& ChainPair : ChainPairsIK)
	{
		// encode them all using the input pose
		ChainPair.IKChainRetargeter.EncodePose(InGlobalPose);
		// decode the IK goal and apply to IKRig
		FDecodedIKChain OutIKGoal;
		ChainPair.IKChainRetargeter.DecodePose(OutGlobalPose, OutIKGoal);
		// set the goal transform on the IK Rig
		FIKRigGoal Goal = FIKRigGoal(
			ChainPair.IKGoalName,
			OutIKGoal.EndEffectorPosition,
			OutIKGoal.EndEffectorRotation,
			1.0f,
			0.0f,
			EIKRigGoalSpace::Component,
			EIKRigGoalSpace::Component);
		IKRigProcessor->SetIKGoal(Goal);
	}

	// copy input pose to start IK solve from
	IKRigProcessor->SetInputPoseGlobal(OutGlobalPose);
	// run IK solve
	IKRigProcessor->Solve();
	// copy results of solve
	IKRigProcessor->CopyOutputGlobalPoseToArray(OutGlobalPose);
}

FTransform UIKRetargetProcessor::GetTargetBoneRetargetPoseGlobalTransform(const int32& TargetBoneIndex) const
{
	check(TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	// get the current retarget pose
	return TargetSkeleton.RetargetGlobalPose[TargetBoneIndex];
}

FTransform UIKRetargetProcessor::GetTargetBoneRetargetPoseLocalTransform(const int32& TargetBoneIndex) const
{
	check(TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	// get the current retarget pose
	return TargetSkeleton.RetargetLocalPose[TargetBoneIndex];
}

#if WITH_EDITOR
void UIKRetargetProcessor::SetNeedsInitialized()
{
	bIsInitialized = false;
	if (IKRigProcessor)
	{
		// may not be initialized yet (during setup as prerequisites are being created)
		IKRigProcessor->SetNeedsInitialized();
	}
}

void UIKRetargetProcessor::CopyTargetIKRigSettingsFromAsset()
{
	IKRigProcessor->CopyAllInputsFromSourceAssetAtRuntime(RetargeterAsset->GetTargetIKRig());
}
#endif
