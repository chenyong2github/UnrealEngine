// Copyright Epic Games, Inc. All Rights Reserved.

#include "RootMotionModifier_SkewWarp.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "MotionWarpingComponent.h"

URootMotionModifier_SkewWarp::URootMotionModifier_SkewWarp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FTransform URootMotionModifier_SkewWarp::ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds)
{
	const ACharacter* CharacterOwner = GetCharacterOwner();
	if(CharacterOwner == nullptr)
	{
		return InRootMotion;
	}

	FTransform FinalRootMotion = InRootMotion;

	const FTransform RootMotionTotal = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(), PreviousPosition, EndTime);
	const FTransform RootMotionDelta = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(), PreviousPosition, FMath::Min(CurrentPosition, EndTime));

	if (bWarpTranslation && !RootMotionDelta.GetTranslation().IsNearlyZero())
	{
		const float CapsuleHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FQuat CurrentRotation = CharacterOwner->GetActorQuat();
		const FVector CurrentLocation = (CharacterOwner->GetActorLocation() - CurrentRotation.GetUpVector() * CapsuleHalfHeight);

		FVector TargetLocation = GetTargetLocation();
		if (bIgnoreZAxis)
		{
			TargetLocation.Z = CurrentLocation.Z;
		}

		const FVector Translation = CharacterOwner->GetMesh()->ConvertLocalRootMotionToWorld(RootMotionDelta).GetLocation();
		const FVector FutureLocation = CurrentLocation + CharacterOwner->GetMesh()->ConvertLocalRootMotionToWorld(RootMotionTotal).GetLocation();
		const FVector CurrentToWorldOffset = TargetLocation - CurrentLocation;
		const FVector CurrentToRootOffset = FutureLocation - CurrentLocation;

		// Create a matrix we can use to put everything into a space looking straight at RootMotionSyncPosition. "forward" should be the axis along which we want to scale. 
		FVector ToRootNormalized = CurrentToRootOffset.GetSafeNormal();

		float BestMatchDot = FMath::Abs(FVector::DotProduct(ToRootNormalized, CurrentRotation.GetAxisX()));
		FMatrix ToRootSyncSpace = FRotationMatrix::MakeFromXZ(ToRootNormalized, CurrentRotation.GetAxisZ());

		float ZDot = FMath::Abs(FVector::DotProduct(ToRootNormalized, CurrentRotation.GetAxisZ()));
		if (ZDot > BestMatchDot)
		{
			ToRootSyncSpace = FRotationMatrix::MakeFromXZ(ToRootNormalized, CurrentRotation.GetAxisX());
			BestMatchDot = ZDot;
		}

		float YDot = FMath::Abs(FVector::DotProduct(ToRootNormalized, CurrentRotation.GetAxisY()));
		if (YDot > BestMatchDot)
		{
			ToRootSyncSpace = FRotationMatrix::MakeFromXZ(ToRootNormalized, CurrentRotation.GetAxisZ());
		}

		// Put everything into RootSyncSpace.
		const FVector RootMotionInSyncSpace = ToRootSyncSpace.InverseTransformVector(Translation);
		const FVector CurrentToWorldSync = ToRootSyncSpace.InverseTransformVector(CurrentToWorldOffset);
		const FVector CurrentToRootMotionSync = ToRootSyncSpace.InverseTransformVector(CurrentToRootOffset);

		FVector CurrentToWorldSyncNorm = CurrentToWorldSync;
		CurrentToWorldSyncNorm.Normalize();

		FVector CurrentToRootMotionSyncNorm = CurrentToRootMotionSync;
		CurrentToRootMotionSyncNorm.Normalize();

		// Calculate skew Yaw Angle. 
		FVector FlatToWorld = FVector(CurrentToWorldSyncNorm.X, CurrentToWorldSyncNorm.Y, 0.0f);
		FlatToWorld.Normalize();
		FVector FlatToRoot = FVector(CurrentToRootMotionSyncNorm.X, CurrentToRootMotionSyncNorm.Y, 0.0f);
		FlatToRoot.Normalize();
		float AngleAboutZ = FMath::Acos(FVector::DotProduct(FlatToWorld, FlatToRoot));
		float AngleAboutZNorm = FMath::DegreesToRadians(FRotator::NormalizeAxis(FMath::RadiansToDegrees(AngleAboutZ)));
		if (FlatToWorld.Y < 0.0f)
		{
			AngleAboutZNorm *= -1.0f;
		}

		// Calculate Skew Pitch Angle. 
		FVector ToWorldNoY = FVector(CurrentToWorldSyncNorm.X, 0.0f, CurrentToWorldSyncNorm.Z);
		ToWorldNoY.Normalize();
		FVector ToRootNoY = FVector(CurrentToRootMotionSyncNorm.X, 0.0f, CurrentToRootMotionSyncNorm.Z);
		ToRootNoY.Normalize();
		const float AngleAboutY = FMath::Acos(FVector::DotProduct(ToWorldNoY, ToRootNoY));
		float AngleAboutYNorm = FMath::DegreesToRadians(FRotator::NormalizeAxis(FMath::RadiansToDegrees(AngleAboutY)));
		if (ToWorldNoY.Z < 0.0f)
		{
			AngleAboutYNorm *= -1.0f;
		}

		FVector SkewedRootMotion = FVector::ZeroVector;
		float ProjectedScale = FVector::DotProduct(CurrentToWorldSync, CurrentToRootMotionSyncNorm) / CurrentToRootMotionSync.Size();
		if (ProjectedScale != 0.0f)
		{
			FMatrix ScaleMatrix;
			ScaleMatrix.SetIdentity();
			ScaleMatrix.SetAxis(0, FVector(ProjectedScale, 0.0f, 0.0f));
			ScaleMatrix.SetAxis(1, FVector(0.0f, 1.0f, 0.0f));
			ScaleMatrix.SetAxis(2, FVector(0.0f, 0.0f, 1.0f));

			FMatrix ShearXAlongYMatrix;
			ShearXAlongYMatrix.SetIdentity();
			ShearXAlongYMatrix.SetAxis(0, FVector(1.0f, FMath::Tan(AngleAboutZNorm), 0.0f));
			ShearXAlongYMatrix.SetAxis(1, FVector(0.0f, 1.0f, 0.0f));
			ShearXAlongYMatrix.SetAxis(2, FVector(0.0f, 0.0f, 1.0f));

			FMatrix ShearXAlongZMatrix;
			ShearXAlongZMatrix.SetIdentity();
			ShearXAlongZMatrix.SetAxis(0, FVector(1.0f, 0.0f, FMath::Tan(AngleAboutYNorm)));
			ShearXAlongZMatrix.SetAxis(1, FVector(0.0f, 1.0f, 0.0f));
			ShearXAlongZMatrix.SetAxis(2, FVector(0.0f, 0.0f, 1.0f));

			FMatrix ScaledSkewMatrix = ScaleMatrix * ShearXAlongYMatrix * ShearXAlongZMatrix;

			// Skew and scale the Root motion. 
			SkewedRootMotion = ScaledSkewMatrix.TransformVector(RootMotionInSyncSpace);
		}
		else if (!CurrentToRootMotionSync.IsZero() && !CurrentToWorldSync.IsZero() && !RootMotionInSyncSpace.IsZero())
		{
			// Figure out ratio between remaining Root and remaining World. Then project scaled length of current Root onto World.
			const float Scale = CurrentToWorldSync.Size() / CurrentToRootMotionSync.Size();
			const float StepTowardTarget = RootMotionInSyncSpace.ProjectOnTo(RootMotionInSyncSpace).Size();
			SkewedRootMotion = CurrentToWorldSyncNorm * (Scale * StepTowardTarget);
		}

		// Put our result back in world space.  
		FinalRootMotion.SetTranslation(ToRootSyncSpace.TransformVector(SkewedRootMotion));
	}

	if(bWarpRotation)
	{
		const FQuat WarpedRotation = WarpRotation(InRootMotion, RootMotionTotal, DeltaSeconds);
		FinalRootMotion.SetRotation(WarpedRotation);
	}

	// Debug
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const int32 DebugLevel = FMotionWarpingCVars::CVarMotionWarpingDebug.GetValueOnGameThread();
	if (DebugLevel == 1 || DebugLevel == 3)
	{
		PrintLog(TEXT("SkewWarp"), InRootMotion, FinalRootMotion);
	}

	if (DebugLevel == 2 || DebugLevel == 3)
	{
		const float DrawDebugDuration = FMotionWarpingCVars::CVarMotionWarpingDrawDebugDuration.GetValueOnGameThread();
		DrawDebugCoordinateSystem(CharacterOwner->GetWorld(), GetTargetLocation(), GetTargetRotator(), 50.f, false, DrawDebugDuration, 0, 1.f);
	}
#endif

	return FinalRootMotion;
}

URootMotionModifier_SkewWarp* URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime,
	FName InWarpTargetName, EWarpPointAnimProvider InWarpPointAnimProvider, FTransform InWarpPointAnimTransform, FName InWarpPointAnimBoneName,
	bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier)
{
	if (ensureAlways(InMotionWarpingComp))
	{
		URootMotionModifier_SkewWarp* NewModifier = NewObject<URootMotionModifier_SkewWarp>(InMotionWarpingComp);
		NewModifier->Animation = InAnimation;
		NewModifier->StartTime = InStartTime;
		NewModifier->EndTime = InEndTime;
		NewModifier->WarpTargetName = InWarpTargetName;
		NewModifier->WarpPointAnimProvider = InWarpPointAnimProvider;
		NewModifier->WarpPointAnimTransform = InWarpPointAnimTransform;
		NewModifier->WarpPointAnimBoneName = InWarpPointAnimBoneName;
		NewModifier->bWarpTranslation = bInWarpTranslation;
		NewModifier->bIgnoreZAxis = bInIgnoreZAxis;
		NewModifier->bWarpRotation = bInWarpRotation;
		NewModifier->RotationType = InRotationType;
		NewModifier->WarpRotationTimeMultiplier = InWarpRotationTimeMultiplier;
		
		InMotionWarpingComp->AddModifier(NewModifier);

		return NewModifier;
	}

	return nullptr;
}