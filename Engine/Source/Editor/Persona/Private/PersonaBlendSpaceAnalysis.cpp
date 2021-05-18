// Copyright Epic Games, Inc. All Rights Reserved.
#include "PersonaBlendSpaceAnalysis.h"

#include "AnimPose.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/BoneSocketReference.h"

#define LOCTEXT_NAMESPACE "BlendSpaceAnalysis"
#define ANALYSIS_VERBOSE_LOG


//======================================================================================================================
static FVector GetAxisFromTM(const FTransform& TM, EAnalysisLinearAxis Axis)
{
	switch (Axis)
	{
	case EAnalysisLinearAxis::X: return TM.TransformVectorNoScale(FVector(1, 0, 0));
	case EAnalysisLinearAxis::Y: return TM.TransformVectorNoScale(FVector(0, 1, 0));
	case EAnalysisLinearAxis::Z: return TM.TransformVectorNoScale(FVector(0, 0, 1));
	}
	return FVector(0, 0, 0);
}

//======================================================================================================================
// Retrieves the bone index and transform offset given the BoneSocketTarget. Returns true if found
static bool GetBoneInfo(
	const UAnimSequence& Animation, const FBoneSocketTarget& BoneSocket, FTransform& BoneOffset, FName& BoneName)
{
	const UAnimDataModel* AnimDataModel = Animation.GetDataModel();

	if (BoneSocket.bUseSocket)
	{
		const USkeleton* Skeleton = Animation.GetSkeleton();
		const USkeletalMeshSocket* Socket = Skeleton->FindSocket(BoneSocket.SocketReference.SocketName);
		if (Socket)
		{
			BoneOffset = Socket->GetSocketLocalTransform();
			BoneName = Socket->BoneName;
			return !BoneName.IsNone();
		}
	}
	else
	{
		BoneName = BoneSocket.BoneReference.BoneName;
		return !BoneName.IsNone();
	}
	return false;
}

//======================================================================================================================
template<typename T>
void CalculateFrameTM(
	bool& bNeedToUpdateFrameTM, FTransform& FrameTM, 
	const int32 SampleKey, const T& AnalysisProperties, const UAnimSequence& Animation)
{
	if (bNeedToUpdateFrameTM)
	{
		FrameTM.SetIdentity();
		if (AnalysisProperties->Space != EAnalysisSpace::World)
		{
			FTransform SpaceBoneOffset;
			FName SpaceBoneName;
			if (GetBoneInfo(Animation, AnalysisProperties->SpaceBoneSocket, SpaceBoneOffset, SpaceBoneName))
			{
				FAnimPose AnimPose;
				UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, SampleKey, FAnimPoseEvaluationOptions(), AnimPose);
				FTransform SpaceBoneTM = UAnimPoseExtensions::GetBonePose(AnimPose, SpaceBoneName, EAnimPoseSpaces::World);
				FrameTM = SpaceBoneOffset * SpaceBoneTM;
			}
		}

		bNeedToUpdateFrameTM = (
			AnalysisProperties->Space == EAnalysisSpace::Changing || 
			AnalysisProperties->Space == EAnalysisSpace::Moving);
	}
}

//======================================================================================================================
template<typename T>
void GetFrameDirs(
	FVector& FrameFacingDir, FVector& FrameUpDir, FVector& FrameRightDir, 
	const FTransform& FrameTM, const T& AnalysisProperties)
{
	FrameFacingDir = GetAxisFromTM(FrameTM, AnalysisProperties->CharacterFacingAxis);
	FrameUpDir = GetAxisFromTM(FrameTM, AnalysisProperties->CharacterUpAxis);
	FrameRightDir = FVector::CrossProduct(FrameUpDir, FrameFacingDir);
}

//======================================================================================================================
template <typename T>
static bool CalculatePosition(
	FVector&                         Result,
	const UBlendSpace&               BlendSpace,
	const T*                         AnalysisProperties,
	const UAnimSequence&             Animation,
	const float                      RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys);

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);

	FAnimPose AnimPose;
	Result.Set(0, 0, 0);
	for (int32 Key = FirstKey; Key != LastKey + 1; ++Key)
	{
		CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, Key, AnalysisProperties, Animation);

		UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, Key, FAnimPoseEvaluationOptions(), AnimPose);
		FTransform BoneTM = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
		FTransform TM = BoneOffset * BoneTM;
		FVector RelativePos = FrameTM.InverseTransformPosition(TM.GetTranslation());
		Result += RelativePos;
	}
	Result /= (1 + LastKey - FirstKey);
	return true;
}

//======================================================================================================================
template <typename T>
static bool CalculateDeltaPosition(
	FVector&                         Result,
	const UBlendSpace&               BlendSpace,
	const T*                         AnalysisProperties,
	const UAnimSequence&             Animation,
	const float                      RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys);

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);

	FAnimPose AnimPose;

	UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, FirstKey, FAnimPoseEvaluationOptions(), AnimPose);
	FTransform BoneTM1 = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
	FTransform TM1 = BoneOffset * BoneTM1;
	FVector RelativePos1 = FrameTM.InverseTransformPosition(TM1.GetTranslation());

	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);

	UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, LastKey, FAnimPoseEvaluationOptions(), AnimPose);
	FTransform BoneTM2 = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
	FTransform TM2 = BoneOffset * BoneTM2;
	FVector RelativePos2 = FrameTM.InverseTransformPosition(TM2.GetTranslation());

	Result = RelativePos2 - RelativePos1;
	return true;
}

//======================================================================================================================
template <typename T>
static bool CalculateVelocity(
	FVector&             Result,
	const UBlendSpace&   BlendSpace,
	const T*             AnalysisProperties,
	const UAnimSequence& Animation,
	const float          RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	if (NumSampledKeys == 1)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	float DeltaTime = Animation.GetPlayLength() / NumSampledKeys;

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys-1);

	if (FirstKey >= LastKey)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);

	FAnimPose AnimPose;
	Result.Set(0, 0, 0);
	for (int32 Key = FirstKey; Key != LastKey + 1; ++Key)
	{
		CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, Key, AnalysisProperties, Animation);

		UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, Key, FAnimPoseEvaluationOptions(), AnimPose);
		FTransform BoneTM1 = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
		FTransform TM1 = BoneOffset * BoneTM1;
		FVector RelativePos1 = FrameTM.InverseTransformPosition(TM1.GetTranslation());

		int32 NextKey = (Key + 1) % (NumSampledKeys + 1);
		if (AnalysisProperties->Space == EAnalysisSpace::Moving)
		{
			CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, NextKey, AnalysisProperties, Animation);
		}

		UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, NextKey, FAnimPoseEvaluationOptions(), AnimPose);
		FTransform BoneTM2 = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
		FTransform TM2 = BoneOffset * BoneTM2;
		FVector RelativePos2 = FrameTM.InverseTransformPosition(TM2.GetTranslation());
		FVector Velocity = (RelativePos2 - RelativePos1) / DeltaTime;

#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("%d Velocity = %f %f %f Height = %f"), 
			   Key, Velocity.X, Velocity.Y, Velocity.Z, 0.5f * (RelativePos1 + RelativePos2).Z);
#endif
		Result += Velocity;
	}
	Result /= (1 + LastKey - FirstKey);
	Result *= Animation.RateScale * RateScale;
	UE_LOG(LogAnimation, Log, TEXT("%s vel = %f %f %f"), *BoneName.ToString(), Result.X, Result.Y, Result.Z);
	return true;
}

//======================================================================================================================
// Calculates the movement speed (magnitude) 
template <typename T>
static bool CalculateMovementSpeed(
	float&                             Result,
	const UBlendSpace&                 BlendSpace,
	const T*                           AnalysisProperties,
	const UAnimSequence&               Animation,
	const float                        RateScale)
{
	if (!AnalysisProperties)
	{
		return false;
	}

	FVector Velocity;
	if (CalculateVelocity(Velocity, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		FTransform FrameTM;
		FVector FrameFacingDir = GetAxisFromTM(FrameTM, AnalysisProperties->CharacterFacingAxis);
		FVector FrameUpDir = GetAxisFromTM(FrameTM, AnalysisProperties->CharacterUpAxis);
		FVector FrameRightDir = FVector::CrossProduct(FrameUpDir, FrameFacingDir);
		float Fwd = Velocity | FrameFacingDir;
		float Right = Velocity | FrameRightDir;
		Result = FMath::Sqrt(FMath::Square(Fwd) + FMath::Square(Right));
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the movement speed (magnitude) 
template <typename T>
static bool CalculateMovementDirection(
	float&                             Result,
	const UBlendSpace&                 BlendSpace,
	const T*                           AnalysisProperties,
	const UAnimSequence&               Animation,
	const float                        RateScale)
{
	if (!AnalysisProperties)
	{
		return false;
	}

	FVector Velocity;
	if (CalculateVelocity(Velocity, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		FTransform FrameTM;
		FVector FrameFacingDir = GetAxisFromTM(FrameTM, AnalysisProperties->CharacterFacingAxis);
		FVector FrameUpDir = GetAxisFromTM(FrameTM, AnalysisProperties->CharacterUpAxis);
		FVector FrameRightDir = FVector::CrossProduct(FrameUpDir, FrameFacingDir);

		float Fwd = Velocity | FrameFacingDir;
		float Right = Velocity | FrameRightDir;
		Result = FMath::RadiansToDegrees(FMath::Atan2(Right, Fwd));
		return true;
	}
	return false;
}

//======================================================================================================================
template <typename T>
void CalculateBoneOrientation(
	FVector&                   RollPitchYaw,
	const UAnimSequence&       Animation, 
	const int32                Key, 
	const FName                BoneName, 
	const FTransform&          BoneOffset, 
	const T&                   AnalysisProperties, 
	const FVector&             FrameFacingDir, 
	const FVector&             FrameRightDir, 
	const FVector&             FrameUpDir)
{
	FAnimPose AnimPose;

	UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, Key, FAnimPoseEvaluationOptions(), AnimPose);
	FTransform BoneTM = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);

	FTransform TM = BoneOffset * BoneTM;
	FQuat AimQuat = TM.GetRotation();
	FVector AimFwdDir = GetAxisFromTM(TM, AnalysisProperties->BoneFacingAxis);
	FVector AimRightDir = GetAxisFromTM(TM, AnalysisProperties->BoneRightAxis);

	// Note that Yaw is best taken from the AimRightDir - this is to avoid problems when the gun is pointing
	// up or down - especially if it goes beyond 90 degrees in pitch.
	float Yaw = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::DotProduct(AimRightDir, -FrameFacingDir), FVector::DotProduct(AimRightDir, FrameRightDir)));

	// Undo the yaw to get pitch
	const FQuat YawQuat(FrameUpDir, FMath::DegreesToRadians(Yaw));
	FVector UnYawedAimFwdDir = YawQuat.UnrotateVector(AimFwdDir);
	float Up = UnYawedAimFwdDir | FrameUpDir;
	float Fwd = UnYawedAimFwdDir | FrameFacingDir;
	float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Up, Fwd));

	// Undo the pitch to get roll
	FVector UnYawedAimRightDir = YawQuat.UnrotateVector(AimRightDir);
	const FQuat PitchQuat(FrameRightDir, -FMath::DegreesToRadians(Pitch));

	FVector UnYawedUnPitchedAimRightDir = PitchQuat.UnrotateVector(UnYawedAimRightDir);

	float Roll = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::DotProduct(UnYawedUnPitchedAimRightDir, -FrameUpDir), 
		FVector::DotProduct(UnYawedUnPitchedAimRightDir, FrameRightDir)));

	RollPitchYaw.Set(Roll, Pitch, Yaw);
}

//======================================================================================================================
// Note that if a looping animation has 56 keys, then its first key is 0 and last is 55, but these will be identical poses.
// Thus it has one fewer intervals/unique keys
template <typename T>
static bool CalculateOrientation(
	FVector&                        Result,
	const UBlendSpace&              BlendSpace,
	const T*                        AnalysisProperties,
	const UAnimSequence&            Animation,
	const float                     RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys);

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	FVector FrameFacingDir, FrameUpDir, FrameRightDir;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);
	GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

	FAnimPose AnimPose;
	Result.Set(0, 0, 0);
	for (int32 Key = FirstKey; Key != LastKey + 1; ++Key)
	{
		CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, Key, AnalysisProperties, Animation);
		GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

		FVector RollPitchYaw;
		CalculateBoneOrientation(
			RollPitchYaw, Animation, Key, BoneName, BoneOffset, 
			AnalysisProperties, FrameFacingDir, FrameRightDir, FrameUpDir);
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Roll/pitch/yaw = %f %f %f"), RollPitchYaw.X, RollPitchYaw.Y, RollPitchYaw.Z);
#endif
		Result += RollPitchYaw;
	}
	Result /= (1 + LastKey - FirstKey);
	UE_LOG(LogAnimation, Log, TEXT("%s Orientation = %f %f %f"), *BoneName.ToString(), Result.X, Result.Y, Result.Z);
	return true;
}

//======================================================================================================================
// Note that if a looping animation has 56 keys, then its first key is 0 and last is 55, but these will be identical poses.
// Thus it has one fewer intervals/unique keys
template <typename T>
static bool CalculateDeltaOrientation(
	FVector&                        Result,
	const UBlendSpace&              BlendSpace,
	const T*                        AnalysisProperties,
	const UAnimSequence&            Animation,
	const float                     RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys);

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	FVector FrameFacingDir, FrameUpDir, FrameRightDir;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);
	GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

	FVector RollPitchYaw1;
	CalculateBoneOrientation(
		RollPitchYaw1, Animation, FirstKey, BoneName, BoneOffset,
		AnalysisProperties, FrameFacingDir, FrameRightDir, FrameUpDir);

	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, LastKey, AnalysisProperties, Animation);
	GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

	FVector RollPitchYaw2;
	CalculateBoneOrientation(
		RollPitchYaw2, Animation, LastKey, BoneName, BoneOffset,
		AnalysisProperties, FrameFacingDir, FrameRightDir, FrameUpDir);

	Result = RollPitchYaw2 - RollPitchYaw1;
	return true;
}

//======================================================================================================================
template <typename T>
static bool CalculateAngularVelocity(
	FVector&                         Result,
	const UBlendSpace&               BlendSpace,
	const T*                         AnalysisProperties,
	const UAnimSequence&             Animation,
	const float                      RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	if (NumSampledKeys == 1)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	float DeltaTime = Animation.GetPlayLength() / NumSampledKeys;

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys-1);

	if (FirstKey >= LastKey)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);

	FAnimPose AnimPose;
	Result.Set(0, 0, 0);
	for (int32 Key = FirstKey; Key != LastKey + 1; ++Key)
	{
		CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, Key, AnalysisProperties, Animation);

		UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, Key, FAnimPoseEvaluationOptions(), AnimPose);
		FTransform BoneTM1 = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
		FTransform TM1 = BoneOffset * BoneTM1;
		FQuat RelativeQuat1 = FrameTM.InverseTransformRotation(TM1.GetRotation());

		int32 NextKey = (Key + 1) % (NumSampledKeys + 1);
		if (AnalysisProperties->Space == EAnalysisSpace::Moving)
		{
			CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, NextKey, AnalysisProperties, Animation);
		}

		UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, NextKey, FAnimPoseEvaluationOptions(), AnimPose);
		FTransform BoneTM2 = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
		FTransform TM2 = BoneOffset * BoneTM2;
		FQuat RelativeQuat2 = FrameTM.InverseTransformRotation(TM2.GetRotation());

		FQuat Rotation = RelativeQuat2 * RelativeQuat1.Inverse();
		FVector Axis;
		float Angle;
		Rotation.ToAxisAndAngle(Axis, Angle);
		FVector AngularVelocity = FMath::RadiansToDegrees(Axis * (Angle / DeltaTime));
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Angular Velocity = %f %f %f"), AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z);
#endif
		Result += AngularVelocity;
	}
	Result /= (1 + LastKey - FirstKey);
	Result *= Animation.RateScale * RateScale;
	UE_LOG(LogAnimation, Log, TEXT("%s angular velocity = %f %f %f"), *BoneName.ToString(), Result.X, Result.Y, Result.Z);
	return true;
}

//======================================================================================================================
template <typename T>
static bool CalculateOrientationRate(
	FVector&                        Result,
	const UBlendSpace&              BlendSpace,
	const T*                        AnalysisProperties,
	const UAnimSequence&            Animation,
	const float                     RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys() - 1;
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}

	if (NumSampledKeys == 1)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, AnalysisProperties->BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	float DeltaTime = Animation.GetPlayLength() / NumSampledKeys;

	int32 FirstKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->StartTimeFraction), 0, NumSampledKeys);
	int32 LastKey = FMath::Clamp(
		(int32) (NumSampledKeys * AnalysisProperties->EndTimeFraction), FirstKey, NumSampledKeys-1);

	if (FirstKey >= LastKey)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform FrameTM;
	bool bNeedToUpdateFrameTM = true;
	FVector FrameFacingDir, FrameUpDir, FrameRightDir;
	CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, FirstKey, AnalysisProperties, Animation);
	GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

	FAnimPose AnimPose;
	Result.Set(0, 0, 0);
	for (int32 Key = FirstKey; Key != LastKey + 1; ++Key)
	{
		CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, Key, AnalysisProperties, Animation);
		GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

		FVector RollPitchYaw1;
		CalculateBoneOrientation(
			RollPitchYaw1, Animation, Key, BoneName, BoneOffset, 
			AnalysisProperties, FrameFacingDir, FrameRightDir, FrameUpDir);

		int32 NextKey = (Key + 1) % (NumSampledKeys + 1);
		if (AnalysisProperties->Space == EAnalysisSpace::Moving)
		{
			CalculateFrameTM(bNeedToUpdateFrameTM, FrameTM, NextKey, AnalysisProperties, Animation);
			GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);
		}

		FVector RollPitchYaw2;
		CalculateBoneOrientation(
			RollPitchYaw2, Animation, NextKey, BoneName, BoneOffset, 
			AnalysisProperties, FrameFacingDir, FrameRightDir, FrameUpDir);

		const FVector OrientationRate = (RollPitchYaw2 - RollPitchYaw1) / DeltaTime;
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Orientation rate = %f %f %f"), OrientationRate.X, OrientationRate.Y, OrientationRate.Z);
#endif
		Result += OrientationRate;
	}
	Result /= (1 + LastKey - FirstKey);
	Result *= Animation.RateScale * RateScale;
	UE_LOG(LogAnimation, Log, TEXT("%s Orientation rate = %f %f %f"), *BoneName.ToString(), Result.X, Result.Y, Result.Z);
	return true;
}

//======================================================================================================================
// Note that if a looping animation has 56 keys, then its first key is 0 and last is 55, but these will be identical poses.
// Thus it has one fewer intervals/unique keys
static bool CalculateLocomotionVelocity(
	FVector&                             Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const FBoneSocketTarget&             BoneSocket,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	const int32 NumSampledKeys = Animation.GetNumberOfSampledKeys();
	if (!AnalysisProperties || NumSampledKeys <= 0)
	{
		return false;
	}
	if (NumSampledKeys == 1)
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FTransform BoneOffset;
	FName BoneName;
	if (!GetBoneInfo(Animation, BoneSocket, BoneOffset, BoneName))
	{
		return false;
	}

	// Note that for locomotion we don't support the frame changing
	FTransform FrameTM = FTransform::Identity;
	FVector FrameFacingDir, FrameUpDir, FrameRightDir;
	GetFrameDirs(FrameFacingDir, FrameUpDir, FrameRightDir, FrameTM, AnalysisProperties);

	FAnimPose AnimPose;

	// The frame time delta
	float DeltaTime = Animation.GetPlayLength() / NumSampledKeys;

	// First step is to figure out the approximate direction. Note that the average velocity will be zero (assuming a
	// complete cycle) - but if we apply a weight that is based on the height, then we can bias it towards the foot that
	// is on the ground.
	float MinHeight = FLT_MAX;
	float MaxHeight = -FLT_MAX;
	FVector AveragePos(0.0f);
	TArray<FVector> Positions;
	Positions.SetNum(NumSampledKeys);
	for (int32 Key = 0; Key != NumSampledKeys; ++Key)
	{
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Animation, Key, FAnimPoseEvaluationOptions(), AnimPose);
		FTransform BoneTM = UAnimPoseExtensions::GetBonePose(AnimPose, BoneName, EAnimPoseSpaces::World);
		FTransform TM = BoneOffset * BoneTM;
		FVector Pos = TM.GetTranslation();
		Positions[Key] = Pos;
		float Height = Pos | FrameUpDir;
		MinHeight = FMath::Min(MinHeight, Height);
		MaxHeight = FMath::Max(MaxHeight, Height);
		AveragePos += Pos;
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Pos %f %f %f"), Pos.X, Pos.Y, Pos.Z);
#endif
	}
	AveragePos /= NumSampledKeys;

	// Calculate velocities. 
	TArray<FVector> Velocities;
	Velocities.SetNum(NumSampledKeys);
	for (int32 Key = 0; Key != NumSampledKeys; ++Key)
	{
		int32 PrevKey = (Key + NumSampledKeys - 1) % NumSampledKeys;
		int32 NextKey = (Key +  1) % NumSampledKeys;
		Velocities[Key] = (Positions[NextKey] - Positions[PrevKey]) / (2.0f * DeltaTime);
	}

	FVector BiasedFootVel(0);
	float TotalWeight = 0.0f;
	for (int32 Key = 0 ; Key != NumSampledKeys ; ++Key)
	{
		float Height = Positions[Key] | FrameUpDir;
		float Weight = 1.0f - (Height - MinHeight) / (MaxHeight - MinHeight);
		BiasedFootVel += Velocities[Key] * Weight;
		TotalWeight += Weight;
	}
	BiasedFootVel /= TotalWeight; 

	if (BiasedFootVel.IsNearlyZero())
	{
		Result.Set(0, 0, 0);
		return true;
	}

	FVector ApproxLocoDir = -BiasedFootVel.GetSafeNormal();

	// Now we can form a mask, where 0 means traveling in the wrong direction (so clearly off the ground), and positive
	// numbers will indicate how far into a valid segment we are. We will assume that the animation is looping
	TArray<int32> Mask;
	Mask.SetNum(NumSampledKeys);
	for (int32 Key = 0 ; Key != NumSampledKeys ; ++Key)
	{
		Mask[Key] = (Velocities[Key] | ApproxLocoDir) >= 0.0f ? 0 : 1;
	}

	int32 StartKey = -1;
	TArray<int32> Mask1 = Mask;
	bool bRepeat = true;
	int32 MaxMask = 0;
	int32 PrevNumFound = NumSampledKeys + 1;
	while (bRepeat)
	{
		bRepeat = false;
		int32 NumFound = 0;
		for (int32 Key = 0 ; Key != NumSampledKeys ; ++Key)
		{
			if (Mask[Key] > 0)
			{
				int32 PrevKey = (Key + NumSampledKeys - 1) % NumSampledKeys;
				int32 NextKey = (Key + 1) % NumSampledKeys;
				if (Mask[PrevKey] == Mask[Key] && Mask[NextKey] == Mask[Key])
				{
					++Mask1[Key];
					MaxMask = FMath::Max(MaxMask, Mask1[Key]);
					bRepeat = true;
					++NumFound;
				}
			}
		}
		Mask = Mask1;
		// Avoid a perpetual loop (e.g. can happen if initially all the mask values are 1... though that shouldn't
		// really happen).
		if (NumFound >= PrevNumFound)
		{
			bRepeat = false;
		}
		else
		{
			PrevNumFound = NumFound;
		}
	}

	// When searching we will want to start outside of a "good" region.
	int32 AZeroKey = 0;
	for (int32 Key = 0 ; Key != NumSampledKeys ; ++Key)
	{
		if (Mask[Key] == 0)
		{
			AZeroKey = Key;
		}
	}

	// We use the mask (with a somewhat arbitrary threshold) to get rid of velocities that are near to the foot
	// plant/take-off time (and might be when the foot is in the time). Then We look for the highest velocity. Not that
	// if we're being called with the foot (ankle) joint, it will tend to underestimate velocities since it is nearer
	// the hip than the ground contact point.
	// 
	int32 Threshold = FMath::Max(MaxMask / 2, 1);
	int32 Num = 0;
	FVector AverageFootVel(0);
	float BestSpeed = 0.0f;
	int32 BestSpeedKey = 0;
	for (int32 K = AZeroKey ; K != AZeroKey + NumSampledKeys ; ++K)
	{
		int32 Key = K % NumSampledKeys;
		if (Mask[Key] >= Threshold)
		{
			float Speed = Velocities[Key].Size();
#ifdef ANALYSIS_VERBOSE_LOG
			UE_LOG(LogAnimation, Log, TEXT("Candidate %d Mask %d vel = %f %f %f, speed = %f"), 
				   Key, Mask[Key], Velocities[Key].X, Velocities[Key].Y, Velocities[Key].Z, Speed);
#endif
			if (Speed > BestSpeed)
			{
				BestSpeed = Speed;
				BestSpeedKey = Key;
			}
		}
		else
		{
			if (BestSpeed > 0.0f)
			{
#ifdef ANALYSIS_VERBOSE_LOG
				UE_LOG(LogAnimation, Log, TEXT("Picked Candidate %d vel = %f %f %f, speed = %f"), 
					   BestSpeedKey, Velocities[BestSpeedKey].X, Velocities[BestSpeedKey].Y, Velocities[BestSpeedKey].Z, BestSpeed);
#endif
				AverageFootVel += Velocities[BestSpeedKey];
				++Num;
				BestSpeed = 0.0f;
			}
		}
	}
	// Make sure we didn't miss the last data point
	if (BestSpeed > 0.0f)
	{
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Picked Candidate %d vel = %f %f %f, speed = %f"), 
			   BestSpeedKey, Velocities[BestSpeedKey].X, Velocities[BestSpeedKey].Y, Velocities[BestSpeedKey].Z, BestSpeed);
#endif
		AverageFootVel += Velocities[BestSpeedKey];
		++Num;
		BestSpeed = 0.0f;
	}

	AverageFootVel /= Num;
	float FacingVel = -AverageFootVel | FrameFacingDir;
	float RightVel = -AverageFootVel | FrameRightDir;
	float UpVel = -AverageFootVel | FrameUpDir;

	Result.Set(FacingVel, RightVel, UpVel);
	Result *= Animation.RateScale * RateScale;
	UE_LOG(LogAnimation, Log, TEXT("%s Locomotion vel = %f %f %f"), *BoneName.ToString(), Result.X, Result.Y, Result.Z);
	return true;
}

//======================================================================================================================
static bool CalculateLocomotionVelocity(
	FVector&                             Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	if (!AnalysisProperties)
	{
		return false;
	}

	FVector Result1, Result2;
	int32 Num = 0;
	Result.Set(0, 0, 0);
	if (CalculateLocomotionVelocity(Result1, BlendSpace, AnalysisProperties, 
									AnalysisProperties->PrimaryBoneSocket, Animation, RateScale))
	{
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Loco vel from primary = %f %f %f"), Result1.X, Result1.Y, Result1.Z);
#endif
		Result += Result1;
		++Num;
	}
	if (CalculateLocomotionVelocity(Result2, BlendSpace, AnalysisProperties, 
									AnalysisProperties->SecondaryBoneSocket, Animation, RateScale))
	{
#ifdef ANALYSIS_VERBOSE_LOG
		UE_LOG(LogAnimation, Log, TEXT("Loco vel from secondary = %f %f %f"), Result2.X, Result2.Y, Result2.Z);
#endif
		Result += Result2;
		++Num;
	}
	if (Num)
	{
		Result /= Num;
		UE_LOG(LogAnimation, Log, TEXT("Loco vel = %f %f %f"), Result.X, Result.Y, Result.Z);
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion speed in the character's facing direction
static bool CalculateLocomotionFwdSpeed(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		Result = Movement.X;
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion speed in the character's upwards direction
static bool CalculateLocomotionUpSpeed(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		Result = Movement.Z;
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion speed in the character's right direction
static bool CalculateLocomotionRightSpeed(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		Result = Movement.Y;
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion speed (magnitude)
static bool CalculateLocomotionSpeed(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		Result = Movement.Size();
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion direction (degrees)
static bool CalculateLocomotionDirection(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		Result = FMath::RadiansToDegrees(FMath::Atan2(Movement.Y, Movement.X));
		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion slope angle (degrees) going in the facing direction
static bool CalculateLocomotionFwdSlope(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		if (Movement.X >= 0.0f)
		{
			Result = FMath::RadiansToDegrees(FMath::Atan2(Movement.Z, Movement.X));
		}
		else
		{
			Result = FMath::RadiansToDegrees(FMath::Atan2(-Movement.Z, -Movement.X));
		}

		return true;
	}
	return false;
}

//======================================================================================================================
// Calculates the locomotion slope angle (degrees) going in the rightwards direction
static bool CalculateLocomotionRightSlope(
	float&                               Result,
	const UBlendSpace&                   BlendSpace,
	const ULocomotionAnalysisProperties* AnalysisProperties,
	const UAnimSequence&                 Animation,
	const float                          RateScale)
{
	FVector Movement;
	if (CalculateLocomotionVelocity(Movement, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		if (Movement.Y > 0.0f)
		{
			Result = FMath::RadiansToDegrees(FMath::Atan2(Movement.Z, Movement.Y));
		}
		else
		{
			Result = FMath::RadiansToDegrees(FMath::Atan2(-Movement.Z, -Movement.Y));
		}
		return true;
	}
	return false;
}

//======================================================================================================================
/**
 * Helper to extract the component from the FVector functions
 */
template<typename FunctionType, typename T>
static bool CalculateComponentSampleValue(
	float&                     Result,
	const FunctionType&        Fn,
	const UBlendSpace&         BlendSpace, 
	const T*                   AnalysisProperties, 
	const UAnimSequence&       Animation,
	const float                RateScale)
{
	FVector Value;
	int32 ComponentIndex = (int32) AnalysisProperties->FunctionAxis;
	if (Fn(Value, BlendSpace, AnalysisProperties, Animation, RateScale))
	{
		Result = Value[ComponentIndex];
		return true;
	}
	return false;
}

//======================================================================================================================
void ULinearAnalysisProperties::InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache)
{
	if (Cache)
	{
		Super::InitializeFromCache(Cache);
		FunctionAxis = Cache->LinearFunctionAxis;
		Space = Cache->Space;
		SpaceBoneSocket = Cache->SpaceBoneSocket;
		BoneSocket = Cache->BoneSocket1;
		StartTimeFraction = Cache->StartTimeFraction;
		EndTimeFraction = Cache->EndTimeFraction;
	}
}

//======================================================================================================================
void ULinearAnalysisProperties::MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const
{
	Super::MakeCache(Cache);
	Cache->LinearFunctionAxis = FunctionAxis;
	Cache->Space = Space;
	Cache->SpaceBoneSocket = SpaceBoneSocket;
	Cache->BoneSocket1 = BoneSocket;
	Cache->StartTimeFraction = StartTimeFraction;
	Cache->EndTimeFraction = EndTimeFraction;
}

//======================================================================================================================
void UEulerAnalysisProperties::InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache)
{
	if (Cache)
	{
		Super::InitializeFromCache(Cache);
		FunctionAxis = Cache->EulerFunctionAxis;
		Space = Cache->Space;
		SpaceBoneSocket = Cache->SpaceBoneSocket;
		CharacterFacingAxis = Cache->CharacterFacingAxis;
		CharacterUpAxis = Cache->CharacterUpAxis;
		StartTimeFraction = Cache->StartTimeFraction;
		EndTimeFraction = Cache->EndTimeFraction;
		BoneSocket = Cache->BoneSocket1;
		BoneFacingAxis = Cache->BoneFacingAxis;
		BoneRightAxis = Cache->BoneRightAxis;
	}
}

//======================================================================================================================
void UEulerAnalysisProperties::MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const
{
	Super::MakeCache(Cache);
	Cache->EulerFunctionAxis = FunctionAxis;
	Cache->Space = Space;
	Cache->SpaceBoneSocket = SpaceBoneSocket;
	Cache->CharacterFacingAxis = CharacterFacingAxis;
	Cache->CharacterUpAxis = CharacterUpAxis;
	Cache->StartTimeFraction = StartTimeFraction;
	Cache->EndTimeFraction = EndTimeFraction;
	Cache->BoneSocket1 = BoneSocket;
	Cache->BoneFacingAxis = BoneFacingAxis;
	Cache->BoneRightAxis = BoneRightAxis;
}

//======================================================================================================================
void UMovementAnalysisProperties::InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache)
{
	if (Cache)
	{
		Super::InitializeFromCache(Cache);
		Space = Cache->Space;
		SpaceBoneSocket = Cache->SpaceBoneSocket;
		CharacterFacingAxis = Cache->CharacterFacingAxis;
		CharacterUpAxis = Cache->CharacterUpAxis;
		StartTimeFraction = Cache->StartTimeFraction;
		EndTimeFraction = Cache->EndTimeFraction;
		BoneSocket = Cache->BoneSocket1;
	}
}

//======================================================================================================================
void UMovementAnalysisProperties::MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const
{
	Super::MakeCache(Cache);
	Cache->Space = Space;
	Cache->SpaceBoneSocket = SpaceBoneSocket;
	Cache->CharacterFacingAxis = CharacterFacingAxis;
	Cache->CharacterUpAxis = CharacterUpAxis;
	Cache->StartTimeFraction = StartTimeFraction;
	Cache->EndTimeFraction = EndTimeFraction;
	Cache->BoneSocket1 = BoneSocket;
}

//======================================================================================================================
void ULocomotionAnalysisProperties::InitializeFromCache(TSharedPtr<FCachedAnalysisProperties> Cache)
{
	if (Cache)
	{
		Super::InitializeFromCache(Cache);
		CharacterFacingAxis = Cache->CharacterFacingAxis;
		CharacterUpAxis = Cache->CharacterUpAxis;
		PrimaryBoneSocket = Cache->BoneSocket1;
		SecondaryBoneSocket = Cache->BoneSocket2;
	}
}

//======================================================================================================================
void ULocomotionAnalysisProperties::MakeCache(TSharedPtr<FCachedAnalysisProperties>& Cache) const
{
	Super::MakeCache(Cache);
	Cache->CharacterFacingAxis = CharacterFacingAxis;
	Cache->CharacterUpAxis = CharacterUpAxis;
	Cache->BoneSocket1 = PrimaryBoneSocket;
	Cache->BoneSocket2 = SecondaryBoneSocket;
}


//======================================================================================================================
class FCoreBlendSpaceAnalysisFeature : public IBlendSpaceAnalysisFeature
{
public:
	// This should process the animation according to the analysis properties, or return false if that is not possible.
	bool CalculateSampleValue(float&                     Result,
							  const UBlendSpace&         BlendSpace,
							  const UAnalysisProperties* AnalysisProperties,
							  const UAnimSequence&       Animation,
							  const float                RateScale) const override;

	// This should return an instance derived from UAnalysisProperties that is suitable for the FunctionName
	UAnalysisProperties* MakeAnalysisProperties(UObject* Outer, const FString& FunctionName) const override;

	// This should return the names of the functions handled
	TArray<FString> GetAnalysisFunctions() const override;
};

static FCoreBlendSpaceAnalysisFeature CoreBlendSpaceAnalysisFeature;

//======================================================================================================================
TArray<FString> FCoreBlendSpaceAnalysisFeature::GetAnalysisFunctions() const
{
	TArray<FString> Functions = 
	{
		TEXT("None"),
		TEXT("Position"),
		TEXT("Velocity"),
		TEXT("DeltaPosition"),
		TEXT("Orientation"),
		TEXT("OrientationRate"),
		TEXT("DeltaOrientation"),
		TEXT("AngularVelocity"),
		TEXT("MovementSpeed"),
		TEXT("MovementDirection"),
		TEXT("LocomotionRightSpeed"),
		TEXT("LocomotionForwardSpeed"),
		TEXT("LocomotionUpSpeed"),
		TEXT("LocomotionSpeed"),
		TEXT("LocomotionDirection"),
		TEXT("LocomotionForwardSlope"),
		TEXT("LocomotionRightSlope")
	};
	return Functions;
}

//======================================================================================================================
UAnalysisProperties* FCoreBlendSpaceAnalysisFeature::MakeAnalysisProperties(UObject* Outer, const FString& FunctionName) const
{
	UAnalysisProperties* Result = nullptr;
	if (FunctionName.Equals(TEXT("Position")) ||
		FunctionName.Equals(TEXT("Velocity")) ||
		FunctionName.Equals(TEXT("DeltaPosition")) ||
		FunctionName.Equals(TEXT("AngularVelocity")))
	{
		Result = NewObject<ULinearAnalysisProperties>(Outer);
	}
	else if (FunctionName.Equals(TEXT("Orientation")) ||
			 FunctionName.Equals(TEXT("OrientationRate")) ||
			 FunctionName.Equals(TEXT("DeltaOrientation")))
	{
		Result = NewObject<UEulerAnalysisProperties>(Outer);
	}
	else if (FunctionName.Equals(TEXT("MovementSpeed")) ||
			 FunctionName.Equals(TEXT("MovementDirection")))
	{
		Result = NewObject<UMovementAnalysisProperties>(Outer);
	}
	else if (FunctionName.Equals(TEXT("LocomotionForwardSpeed")) ||
			 FunctionName.Equals(TEXT("LocomotionRightSpeed")) ||
			 FunctionName.Equals(TEXT("LocomotionUpSpeed")) ||
			 FunctionName.Equals(TEXT("LocomotionSpeed")) ||
			 FunctionName.Equals(TEXT("LocomotionDirection")) ||
			 FunctionName.Equals(TEXT("LocomotionForwardSlope")) ||
			 FunctionName.Equals(TEXT("LocomotionRightSlope")))
	{
		Result = NewObject<ULocomotionAnalysisProperties>(Outer);
	}

	if (Result)
	{
		Result->Function = FunctionName;
	}
	return Result;
}

//======================================================================================================================
bool FCoreBlendSpaceAnalysisFeature::CalculateSampleValue(float&                     Result,
														  const UBlendSpace&         BlendSpace,
														  const UAnalysisProperties* AnalysisProperties,
														  const UAnimSequence&       Animation,
														  const float                RateScale) const
{
	if (!AnalysisProperties)
	{
		return false;
	}
	const FString& FunctionName = AnalysisProperties->Function;
	if (FunctionName.Equals(TEXT("Position")))
	{
		return CalculateComponentSampleValue(
			Result, CalculatePosition<ULinearAnalysisProperties>, BlendSpace, 
			Cast<ULinearAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("Velocity")))
	{
		return CalculateComponentSampleValue(
			Result, CalculateVelocity<ULinearAnalysisProperties>, BlendSpace, 
			Cast<ULinearAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("DeltaPosition")))
	{
		return CalculateComponentSampleValue(
			Result, CalculateDeltaPosition<ULinearAnalysisProperties>, BlendSpace, 
			Cast<ULinearAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("AngularVelocity")))
	{
		return CalculateComponentSampleValue(
			Result, CalculateAngularVelocity<ULinearAnalysisProperties>, BlendSpace, 
			Cast<ULinearAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("Orientation")))
	{
		return CalculateComponentSampleValue(
			Result, CalculateOrientation<UEulerAnalysisProperties>, BlendSpace, 
			Cast<UEulerAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("OrientationRate")))
	{
		return CalculateComponentSampleValue(
			Result, CalculateOrientationRate<UEulerAnalysisProperties>, BlendSpace, 
			Cast<UEulerAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("DeltaOrientation")))
	{
		return CalculateComponentSampleValue(
			Result, CalculateDeltaOrientation<UEulerAnalysisProperties>, BlendSpace, 
			Cast<UEulerAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("MovementSpeed")))
	{
		return CalculateMovementSpeed(
			Result, BlendSpace, Cast<UMovementAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("MovementDirection")))
	{
		return CalculateMovementDirection(
			Result, BlendSpace, Cast<UMovementAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionForwardSpeed")))
	{
		return CalculateLocomotionFwdSpeed(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionRightSpeed")))
	{
		return CalculateLocomotionRightSpeed(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionUpSpeed")))
	{
		return CalculateLocomotionUpSpeed(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionSpeed")))
	{
		return CalculateLocomotionSpeed(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionDirection")))
	{
		return CalculateLocomotionDirection(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionForwardSlope")))
	{
		return CalculateLocomotionFwdSlope(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	else if (FunctionName.Equals(TEXT("LocomotionRightSlope")))
	{
		return CalculateLocomotionRightSlope(
			Result, BlendSpace, Cast<ULocomotionAnalysisProperties>(AnalysisProperties), Animation, RateScale);
	}
	return false;
}

//======================================================================================================================
static TArray<IBlendSpaceAnalysisFeature*> GetAnalysisFeatures()
{
	TArray<IBlendSpaceAnalysisFeature*> ModularFeatures = 
		IModularFeatures::Get().GetModularFeatureImplementations<IBlendSpaceAnalysisFeature>(
			IBlendSpaceAnalysisFeature::GetModuleFeatureName());

	// Put the core one on last so that user-defined ones can override the default behaviour
	ModularFeatures.Push(&CoreBlendSpaceAnalysisFeature);
	return ModularFeatures;
}

//======================================================================================================================
FVector BlendSpaceAnalysis::CalculateSampleValue(const UBlendSpace& BlendSpace, const UAnimSequence& Animation,
												  const float RateScale, const FVector& OriginalPosition, bool bAnalyzed[3])
{

	FVector AdjustedPosition = OriginalPosition;
	TArray<IBlendSpaceAnalysisFeature*> ModularFeatures = GetAnalysisFeatures();
	for (int32 Index = 0; Index != 2; ++Index)
	{
		bAnalyzed[Index] = false;
		const UAnalysisProperties* AnalysisProperties = BlendSpace.AnalysisProperties[Index].Get();
		for (const IBlendSpaceAnalysisFeature* Feature : ModularFeatures)
		{
			float FloatValue = (float) AdjustedPosition[Index];
			bAnalyzed[Index] = Feature->CalculateSampleValue(
				FloatValue, BlendSpace, AnalysisProperties, Animation, RateScale);
			if (bAnalyzed[Index])
			{
				AdjustedPosition[Index] = (FVector::FReal) FloatValue;
				break;
			}
		}
	}
	return AdjustedPosition;
}

//======================================================================================================================
UAnalysisProperties* BlendSpaceAnalysis::MakeAnalysisProperties(UObject* Outer, const FString& FunctionName)
{
	UAnalysisProperties* Result = nullptr;

	TArray<IBlendSpaceAnalysisFeature*> ModularFeatures = GetAnalysisFeatures();
	for (const IBlendSpaceAnalysisFeature* Feature : ModularFeatures)
	{
		Result = Feature->MakeAnalysisProperties(Outer, FunctionName);
		if (Result)
		{
			return Result;
		}
	}
	return Result;
}

//======================================================================================================================
TArray<FString> BlendSpaceAnalysis::GetAnalysisFunctions()
{
	TArray<FString> FunctionNames;
	TArray<IBlendSpaceAnalysisFeature*> ModularFeatures = GetAnalysisFeatures();
	for (const IBlendSpaceAnalysisFeature* Feature : ModularFeatures)
	{
		TArray<FString> FeatureFunctionNames = Feature->GetAnalysisFunctions();
		for (const FString& FeatureFunctionName : FeatureFunctionNames)
		{
			FunctionNames.AddUnique(FeatureFunctionName);
		}
	}
	return FunctionNames;
}

//======================================================================================================================
bool BlendSpaceAnalysis::GetLockAfterAnalysis(const TObjectPtr<UAnalysisProperties>& AnalysisProperties)
{
	return AnalysisProperties ? AnalysisProperties->bLockAfterAnalysis : false;
}


#undef LOCTEXT_NAMESPACE
