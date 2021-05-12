// Copyright Epic Games, Inc. All Rights Reserved.

#include "Retargeter/IKRetargeter.h"

/*
TODO:

Asset Testing:
	- add chain for "upperarm_twist_01_l"
			- will test single-bone chains AND fix twisted upperarm
	- add finger chains to test fk AFTER ik
	- export starfish in mannequin defailt pose

IK Rig:
	- add "alpha" value to goals, set alpha to zero by default
		- internally LERP effectors from input pose to goal transform by alpha
	- port PBIK to IK Rig

IK Retargeter:
	- we need option to override initial poses to be different than reference pose
	- expose root stride scale factor as parameter

FK Retarget:
	- sort FK chains root to tip (no guarantee user sets them up this way)
	
IK Retarget:
	
	- set weight of unused goals to zero (so redundant goals don't screw up solve)
	- set pole vectors goals on IKRig

DONE:
- create IKRigSkeleton to merge hierarchy and IKRigTransforms
- IKRigTransforms to use only global space and provide conversion from local space
- initialize all the things
- encode (remove decode)


NOTES:

// WE PROBABLY DON'T NEED TO DO SEPARATE PASS OF BEFORE/AFTER IK RETARGETS BECAUSE THE
// FK RETARGETING SHOULD ONLY AFFECT ROTATION WHICH IS INVARIANT ??
//
// ALWAYS RUN FK RETARGET?
// WE MAY WANT TO ALWAYS RUN FK RETARGET BEFORE IK
// THAT WAY THE CHAINS WILL BE CLOSE TO GOAL TARGETS!!
// THIS IS DIFFERENT THAN MAYA BECAUSE WE HAVE FULL BODY IK
// !!!! ACTUALLY IF WE DO FK UPDATE OF GLOBAL SPACE POSE BEFORE IK WE SHOULD BE FINE AND MUCH FASTER!!!
	
*/

void FRetargetSkeleton::InitializeHierarchy(
	const USkeletalMesh* SkeletalMesh,
	const TArray<FTransform>& InLocalRetargetPose)
{
	// copy names and parent indices into local storage
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	for (int32 BoneIndex=0; BoneIndex<RefSkeleton.GetNum(); ++BoneIndex)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(BoneIndex));
		ParentIndices.Add(RefSkeleton.GetParentIndex(BoneIndex));	
	}

	// store copy of retarget pose
	RetargetGlobalPose.Reset(InLocalRetargetPose.Num());
	
	// convert local transforms to global
	for (int32 BoneIndex=0; BoneIndex<InLocalRetargetPose.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = ParentIndices[BoneIndex];
		if (ParentIndex == INDEX_NONE)
		{
			RetargetGlobalPose.Add(InLocalRetargetPose[BoneIndex]); // root bone is always in local space
			continue; 
		}

		const FTransform& ChildLocalTransform  = InLocalRetargetPose[BoneIndex];
		const FTransform& ParentGlobalTransform  = RetargetGlobalPose[ParentIndex];
		RetargetGlobalPose.Add(ChildLocalTransform * ParentGlobalTransform);
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

int32 FRetargetSkeleton::GetParentIndex(const int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex>ParentIndices.Num() || BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return ParentIndices[BoneIndex];
}

void FTargetSkeleton::Initialize(
	const USkeletalMesh* SkeletalMesh,
	const TArray<FTransform>& InTargetRetargetLocalPose)
{
	InitializeHierarchy(SkeletalMesh, InTargetRetargetLocalPose);

	// initialize storage for output pose (the result of the retargeting)
	OutputGlobalPose = RetargetGlobalPose;
	// store local retarget pose so we can update partial skeleton during retarget process
	RetargetLocalPose = InTargetRetargetLocalPose;

	// make storage for per-bone "IsFK" flag (used for hierarchy updates)
	IsBoneFK.Init(false, InTargetRetargetLocalPose.Num());
}

void FTargetSkeleton::SetBoneControlledByFKChain(const int32 BoneIndex, const bool IsFK)
{
	check(IsBoneFK.IsValidIndex(BoneIndex));
	IsBoneFK[BoneIndex] = IsFK;
}

void FTargetSkeleton::UpdateGlobalTransformsBelowBone(const int32 StartBoneIndex)
{
	check(OutputGlobalPose.IsValidIndex(StartBoneIndex));
	
	for (int32 BoneIndex=StartBoneIndex+1; BoneIndex<OutputGlobalPose.Num(); ++BoneIndex)
	{
		UpdateGlobalTransformOfSingleBone(BoneIndex);
	}
}

void FTargetSkeleton::UpdateGlobalTransformsAllNonFKBones()
{
	check(IsBoneFK.Num() == OutputGlobalPose.Num());
	
	for (int32 BoneIndex=0; BoneIndex<OutputGlobalPose.Num(); ++BoneIndex)
	{
		if (!IsBoneFK[BoneIndex])
		{
			UpdateGlobalTransformOfSingleBone(BoneIndex);
		}
	}
}

void FTargetSkeleton::UpdateGlobalTransformOfSingleBone(const int32 BoneIndex)
{
	const int32 ParentIndex = ParentIndices[BoneIndex];
	if (ParentIndex == INDEX_NONE)
	{
		return; // root always in global space
	}
	const FTransform ChildLocalTransform = RetargetLocalPose[BoneIndex];
	const FTransform ParentGlobalTransform = OutputGlobalPose[ParentIndex];
	OutputGlobalPose[BoneIndex] = ChildLocalTransform * ParentGlobalTransform;
}

bool FChainEncoderFK::Initialize(const TArray<int32>& BoneIndices, const TArray<FTransform>& InitialGlobalPose)
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

bool FChainEncoderFK::CalculateBoneParameters()
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

void FChainEncoderFK::DecodePose(
	const TArray<int32>& TargetBoneIndices,
    const FChainEncoderFK& SourceChain,
    TArray<FTransform> &InOutGlobalPose)
{
	check(TargetBoneIndices.Num() == CurrentBoneTransforms.Num());
	check(TargetBoneIndices.Num() == Params.Num());
	
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

		// the initial transform on the target chain
		const FTransform& TargetInitialTransform = InitialBoneTransforms[ChainIndex];

		// calculate output ROTATION
		const FQuat& SourceCurrentRotation = SourceCurrentTransform.GetRotation();
		const FQuat& SourceInitialRotation = SourceInitialTransform.GetRotation();
		const FQuat& TargetInitialRotation = TargetInitialTransform.GetRotation();
		const FQuat RotationDelta = TargetInitialRotation * SourceInitialRotation.Inverse();
		const FQuat OutRotation = RotationDelta * SourceCurrentRotation;

		// calculate output POSITION
		const FVector& SourceCurrentPosition = SourceCurrentTransform.GetTranslation();
		const FVector& SourceInitialPosition = SourceInitialTransform.GetTranslation();
		const FVector& TargetInitialPosition = TargetInitialTransform.GetTranslation();
		const FVector OutPosition = SourceCurrentPosition + (TargetInitialPosition - SourceInitialPosition);

		// calculate output SCALE
		const FVector& SourceCurrentScale = SourceCurrentTransform.GetScale3D();
		const FVector& SourceInitialScale = SourceInitialTransform.GetScale3D();
		const FVector& TargetInitialScale = TargetInitialTransform.GetScale3D();
		const FVector OutScale = SourceCurrentScale + (TargetInitialScale - SourceInitialScale);

		InOutGlobalPose[TargetBoneIndices[ChainIndex]] = FTransform(OutRotation, OutPosition, OutScale);
	}
}

FTransform FChainEncoderFK::GetTransformAtParam(
	const TArray<FTransform>& Transforms,
	const TArray<float>& InParams,
	const float Param) const
{
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
		const FQuat Rotation = FQuat::FastLerp(Prev.GetRotation(), Next.GetRotation(), PercentBetweenParams);
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
	FVector A = InputGlobalPose[BoneIndexA].GetTranslation();
	FVector B = InputGlobalPose[BoneIndexB].GetTranslation();
	FVector C = InputGlobalPose[BoneIndexC].GetTranslation();

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
	OutResults.EndEffectorPosition = Start + EncodedChain.EndDirectionNormalized * Length; // * LimbScale (TBD)
	// optionally factor in ground height
	const bool IsGrounded = false;
	if (IsGrounded) // TBD expose this
	{
		OutResults.EndEffectorPosition.Z = (EncodedChain.HeightFromGroundNormalized * Length) + EndPositionOrig.Z;
	}

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
	const bool bChainInitialized = FRetargetChainPair::Initialize(SourceBoneChain, TargetBoneChain, SourceSkeleton, TargetSkeleton);
	if (!bChainInitialized)
	{
		return false;
	}
	
	IKGoalName = TargetBoneChain.IKGoalName;

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
	const TArray<FTransform>& SourceRetargetPoseLocal = SourceSkeletalMesh->GetRefSkeleton().GetRefBonePose();
	const TArray<FTransform>& TargetRetargetPoseLocal = TargetSkeletalMesh->GetRefSkeleton().GetRefBonePose();
	SourceSkeleton.InitializeHierarchy(SourceSkeletalMesh, SourceRetargetPoseLocal);
	TargetSkeleton.Initialize(TargetSkeletalMesh, TargetRetargetPoseLocal);

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
	const FName TargetRootBoneName = SourceIKRigAsset->RetargetDefinition.RootBone;
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

	for (const TPair<FName, FName>& NamePair : ChainMapping)
	{
		// get source bone chain
		FBoneChain* SourceBoneChain = SourceRetargetDef.GetBoneChainByName(NamePair.Key);
		if (!SourceBoneChain)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Retargeter missing source bone chain: %s"), *NamePair.Key.ToString());
			continue;
		}

		// get target bone chain
		FBoneChain* TargetBoneChain = TargetRetargetDef.GetBoneChainByName(NamePair.Value);
		if (!TargetBoneChain)
		{
			UE_LOG(LogTemp, Warning, TEXT("IK Retargeter missing target bone chain: %s"), *NamePair.Value.ToString());
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
		if (TargetBoneChain->bUseIK)
		{
			FRetargetChainPairIK ChainPairIK;
			const bool bChainPairIKValid = ChainPairIK.Initialize(*SourceBoneChain, *TargetBoneChain, SourceSkeleton, TargetSkeleton);
			if (bChainPairIKValid)
			{
				ChainPairsIK.Add(ChainPairIK);
			}
		}
	}

	// record which bones in the target skeleton are controlled by FK
	for (const FRetargetChainPairFK& FKChainPair : ChainPairsFK)
	{
		for (const int32 BoneIndex : FKChainPair.TargetBoneIndices)
		{
			TargetSkeleton.SetBoneControlledByFKChain(BoneIndex, true);
		}
	}

	// root is updated before IK as well
	TargetSkeleton.SetBoneControlledByFKChain(RootDecoder.BoneIndex, true);

	// return true if at least 1 pair of bone chains were initialized
	return !(ChainPairsIK.IsEmpty() && ChainPairsFK.IsEmpty());
}

bool UIKRetargeter::InitializeIKRig(UObject* Outer, const FReferenceSkeleton& InRefSkeleton)
{
	// make a new IKRigProcessor if we haven't already
	if (!IKRigProcessor)
	{
		IKRigProcessor = UIKRigProcessor::MakeNewIKRigProcessor(Outer);
	}
	
	// initialize IK Rig runtime processor
	IKRigProcessor->Initialize(TargetIKRigAsset, InRefSkeleton);
	if (!IKRigProcessor->IsInitialized())
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

void UIKRetargeter::RunRetargeter(const TArray<FTransform>& InSourceGlobalPose, bool bEnableIK)
{
	check(bIsLoadedAndValid);

	// ROOT retargeting
	RunRootRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);

	// update global transforms below root
	TargetSkeleton.UpdateGlobalTransformsBelowBone(RootDecoder.BoneIndex);

	// FK CHAIN retargeting
	RunFKRetarget(InSourceGlobalPose,TargetSkeleton.OutputGlobalPose, true);

	// update all the bones that are not controlled by FK chains or root
	TargetSkeleton.UpdateGlobalTransformsAllNonFKBones();
	
	// IK CHAIN retargeting
	if (bEnableIK)
	{
		RunIKRetarget(InSourceGlobalPose, TargetSkeleton.OutputGlobalPose);
	}	
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
    TArray<FTransform>& OutGlobalTransforms,
    const bool bPreIK)
{
	// spin through chains and encode/decode them all using the input pose
    for (FRetargetChainPairFK& ChainPair : ChainPairsFK)
    {
    	ChainPair.FKEncoder.EncodePose(ChainPair.SourceBoneIndices, InGlobalTransforms);
    	ChainPair.FKDecoder.DecodePose(ChainPair.TargetBoneIndices, ChainPair.FKEncoder,OutGlobalTransforms);
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
