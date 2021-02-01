// Copyright Epic Games, Inc. All Rights Reserved.

#include "RootMotionModifier.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "MotionWarpingComponent.h"
#include "DrawDebugHelpers.h"

// FRootMotionModifier
///////////////////////////////////////////////////////////////

void FRootMotionModifier::Update(UMotionWarpingComponent& OwnerComp)
{
	const FAnimMontageInstance* RootMotionMontageInstance = OwnerComp.GetCharacterOwner()->GetRootMotionAnimMontageInstance();
	const UAnimMontage* Montage = RootMotionMontageInstance ? ToRawPtr(RootMotionMontageInstance->Montage) : nullptr;

	// Mark for removal if our animation is not relevant anymore
	if (Montage == nullptr || Montage != Animation)
	{
		UE_LOG(LogMotionWarping, Verbose, TEXT("MotionWarping: Marking RootMotionModifier for removal. Reason: Animation is not valid. Char: %s Current Montage: %s. Window: Animation: %s [%f %f] [%f %f]"),
			*GetNameSafe(OwnerComp.GetCharacterOwner()), *GetNameSafe(Montage), *GetNameSafe(Animation.Get()), StartTime, EndTime, PreviousPosition, CurrentPosition);

		State = ERootMotionModifierState::MarkedForRemoval;
		return;
	}

	// Update playback times and weight
	PreviousPosition = RootMotionMontageInstance->GetPreviousPosition();
	CurrentPosition = RootMotionMontageInstance->GetPosition();
	Weight = RootMotionMontageInstance->GetWeight();

	// Mark for removal if the animation already passed the warping window
	if (PreviousPosition >= EndTime)
	{
		UE_LOG(LogMotionWarping, Verbose, TEXT("MotionWarping: Marking RootMotionModifier for removal. Reason: Window has ended. Char: %s Animation: %s [%f %f] [%f %f]"),
			*GetNameSafe(OwnerComp.GetCharacterOwner()), *GetNameSafe(Animation.Get()), StartTime, EndTime, PreviousPosition, CurrentPosition);

		State = ERootMotionModifierState::MarkedForRemoval;
		return;
	}

	// Check if we are inside the warping window
	if (PreviousPosition >= StartTime && PreviousPosition < EndTime)
	{
		// If we were waiting, switch to active
		if (State == ERootMotionModifierState::Waiting)
		{
			State = ERootMotionModifierState::Active;
		}
	}
}

// FRootMotionModifier_Warp
///////////////////////////////////////////////////////////////

void FRootMotionModifier_Warp::Update(UMotionWarpingComponent& OwnerComp)
{
	// Update playback times and state
	FRootMotionModifier::Update(OwnerComp);

	// Cache sync point transform and trigger OnSyncPointChanged if needed
	if (State == ERootMotionModifierState::Active)
	{
		const FMotionWarpingSyncPoint* SyncPointPtr = OwnerComp.FindSyncPoint(SyncPointName);

		// Disable if there is no sync point for us
		if (SyncPointPtr == nullptr)
		{
			UE_LOG(LogMotionWarping, Verbose, TEXT("MotionWarping: Marking RootMotionModifier as Disabled. Reason: Invalid Sync Point (%s). Char: %s Animation: %s [%f %f] [%f %f]"),
				*SyncPointName.ToString(), *GetNameSafe(OwnerComp.GetCharacterOwner()), *GetNameSafe(Animation.Get()), StartTime, EndTime, PreviousPosition, CurrentPosition);

			State = ERootMotionModifierState::Disabled;
			return;
		}

		if (CachedSyncPoint != *SyncPointPtr)
		{
			CachedSyncPoint = *SyncPointPtr;

			OnSyncPointChanged(OwnerComp);
		}
	}
}

FTransform FRootMotionModifier_Warp::ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds)
{
	const ACharacter* CharacterOwner = OwnerComp.GetCharacterOwner();
	check(CharacterOwner);

	const FTransform& CharacterTransform = CharacterOwner->GetActorTransform();

	FTransform FinalRootMotion = InRootMotion;

	const FTransform RootMotionTotal = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(), PreviousPosition, EndTime);

	if (bWarpTranslation)
	{
		FVector DeltaTranslation = InRootMotion.GetTranslation();

		const FTransform RootMotionDelta = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(), PreviousPosition, FMath::Min(CurrentPosition, EndTime));

		const float HorizontalDelta = RootMotionDelta.GetTranslation().Size2D();
		const float HorizontalTarget = FVector::Dist2D(CharacterTransform.GetLocation(), CachedSyncPoint.GetLocation());
		const float HorizontalOriginal = RootMotionTotal.GetTranslation().Size2D();
		const float HorizontalTranslationWarped = HorizontalOriginal != 0.f ? ((HorizontalDelta * HorizontalTarget) / HorizontalOriginal) : 0.f;

		if (bInLocalSpace)
		{
			const FTransform MeshRelativeTransform = FTransform(CharacterOwner->GetBaseRotationOffset(), CharacterOwner->GetBaseTranslationOffset());
			const FTransform MeshTransform = MeshRelativeTransform * CharacterOwner->GetActorTransform();
			DeltaTranslation = MeshTransform.InverseTransformPositionNoScale(CachedSyncPoint.GetLocation()).GetSafeNormal2D() * HorizontalTranslationWarped;
		}
		else
		{
			DeltaTranslation = (CachedSyncPoint.GetLocation() - CharacterTransform.GetLocation()).GetSafeNormal2D() * HorizontalTranslationWarped;
		}

		if (!bIgnoreZAxis)
		{
			const FVector CapsuleBottomLocation = (CharacterOwner->GetActorLocation() - FVector::UpVector * CharacterOwner->GetSimpleCollisionHalfHeight());
			const float VerticalDelta = RootMotionDelta.GetTranslation().Z;
			const float VerticalTarget = CachedSyncPoint.GetLocation().Z - CapsuleBottomLocation.Z;
			const float VerticalOriginal = RootMotionTotal.GetTranslation().Z;
			const float VerticalTranslationWarped = VerticalOriginal != 0.f ? ((VerticalDelta * VerticalTarget) / VerticalOriginal) : 0.f;

			DeltaTranslation.Z = VerticalTranslationWarped;
		}

		FinalRootMotion.SetTranslation(DeltaTranslation);
	}

	if (bWarpRotation)
	{
		const FQuat WarpedRotation = WarpRotation(OwnerComp, InRootMotion, RootMotionTotal, DeltaSeconds);
		FinalRootMotion.SetRotation(WarpedRotation);
	}

	// Debug
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const int32 DebugLevel = FMotionWarpingCVars::CVarMotionWarpingDebug.GetValueOnGameThread();
	if (DebugLevel > 0)
	{
		PrintLog(OwnerComp, TEXT("FRootMotionModifier_Simple"), InRootMotion, FinalRootMotion);

		if (DebugLevel >= 2)
		{
			const float DrawDebugDuration = FMotionWarpingCVars::CVarMotionWarpingDrawDebugDuration.GetValueOnGameThread();
			DrawDebugCoordinateSystem(OwnerComp.GetWorld(), CachedSyncPoint.GetLocation(), CachedSyncPoint.Rotator(), 50.f, false, DrawDebugDuration, 0, 1.f);
		}
	}
#endif

	return FinalRootMotion;
}

FQuat FRootMotionModifier_Warp::WarpRotation(UMotionWarpingComponent& OwnerComp, const FTransform& RootMotionDelta, const FTransform& RootMotionTotal, float DeltaSeconds)
{
	const FTransform& CharacterTransform = OwnerComp.GetCharacterOwner()->GetActorTransform();

	const FQuat CurrentRotation = CharacterTransform.GetRotation();
	const float TimeRemaining = EndTime - PreviousPosition;
	const FQuat Desired = CachedSyncPoint.GetRotation();

	const FQuat RemainingRootRotationInWorld = RootMotionTotal.GetRotation();
	const FQuat CurrentPlusRemainingRootMotion = RemainingRootRotationInWorld * CurrentRotation;
	const float PercentThisStep = FMath::Clamp(DeltaSeconds / TimeRemaining, 0.f, 1.f);
	const FQuat TargetRotThisFrame = FQuat::Slerp(CurrentPlusRemainingRootMotion, Desired, PercentThisStep);
	const FQuat DeltaOut = TargetRotThisFrame * CurrentPlusRemainingRootMotion.Inverse();

	return (DeltaOut * RootMotionDelta.GetRotation());
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void FRootMotionModifier_Warp::PrintLog(const UMotionWarpingComponent& OwnerComp, const FString& Name, const FTransform& OriginalRootMotion, const FTransform& WarpedRootMotion) const
{
	const ACharacter* CharacterOwner = OwnerComp.GetCharacterOwner();
	check(CharacterOwner);

	const FVector CurrentLocation = (CharacterOwner->GetActorLocation() - FVector::UpVector * CharacterOwner->GetSimpleCollisionHalfHeight());
	const FVector CurrentToTarget = (CachedSyncPoint.GetLocation() - CurrentLocation).GetSafeNormal2D();
	const FVector FutureLocation = CurrentLocation + (bInLocalSpace ? (CharacterOwner->GetMesh()->ConvertLocalRootMotionToWorld(WarpedRootMotion)).GetTranslation() : WarpedRootMotion.GetTranslation());
	const FRotator CurrentRotation = CharacterOwner->GetActorRotation();
	const FRotator FutureRotation = (WarpedRootMotion.GetRotation() * CharacterOwner->GetActorQuat()).Rotator();
	const float Dot = FVector::DotProduct(CharacterOwner->GetActorForwardVector(), CurrentToTarget);
	const float CurrentDist2D = FVector::Dist2D(CachedSyncPoint.GetLocation(), CurrentLocation);
	const float FutureDist2D = FVector::Dist2D(CachedSyncPoint.GetLocation(), FutureLocation);
	const float DeltaSeconds = CharacterOwner->GetWorld()->GetDeltaSeconds();
	const float Speed = WarpedRootMotion.GetTranslation().Size() / DeltaSeconds;
	const float EndTimeOffset = CurrentPosition - EndTime;

	UE_LOG(LogMotionWarping, Log, TEXT("MotionWarping: %s. NetMode: %d Char: %s Anim: %s Window [%f %f][%f %f] DeltaTime: %f WorldTime: %f EndTimeOffset: %f Dist2D: %f FutureDist2D: %f Dot: %f OriginalMotionDelta: %s (%f) FinalMotionDelta: %s (%f) Speed: %f Loc: %s FutureLoc: %s Rot: %s FutureRot: %s"),
		*Name, (int32)CharacterOwner->GetWorld()->GetNetMode(), *GetNameSafe(CharacterOwner), *GetNameSafe(Animation.Get()), StartTime, EndTime, PreviousPosition, CurrentPosition, DeltaSeconds, CharacterOwner->GetWorld()->GetTimeSeconds(), EndTimeOffset, CurrentDist2D, FutureDist2D, Dot,
		*OriginalRootMotion.GetTranslation().ToString(), OriginalRootMotion.GetTranslation().Size(), *WarpedRootMotion.GetTranslation().ToString(), WarpedRootMotion.GetTranslation().Size(), Speed,
		*CurrentLocation.ToString(), *FutureLocation.ToString(), *CurrentRotation.ToCompactString(), *FutureRotation.ToCompactString());
}
#endif

// URootMotionModifierConfig_Warp
///////////////////////////////////////////////////////////////

void URootMotionModifierConfig_Warp::AddRootMotionModifierSimpleWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, FName InSyncPointName, bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation)
{
	if (ensureAlways(InMotionWarpingComp))
	{
		TSharedPtr<FRootMotionModifier_Warp> NewModifier = MakeShared<FRootMotionModifier_Warp>();
		NewModifier->Animation = InAnimation;
		NewModifier->StartTime = InStartTime;
		NewModifier->EndTime = InEndTime;
		NewModifier->SyncPointName = InSyncPointName;
		NewModifier->bWarpTranslation = bInWarpTranslation;
		NewModifier->bIgnoreZAxis = bInIgnoreZAxis;
		NewModifier->bWarpRotation = bInWarpRotation;
		InMotionWarpingComp->AddRootMotionModifier(NewModifier);
	}
}

// URootMotionModifierConfig_Scale
///////////////////////////////////////////////////////////////

void URootMotionModifierConfig_Scale::AddRootMotionModifierScale(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime, FVector InScale)
{
	if (ensureAlways(InMotionWarpingComp))
	{
		TSharedPtr<FRootMotionModifier_Scale> NewModifier = MakeShared<FRootMotionModifier_Scale>();
		NewModifier->Animation = InAnimation;
		NewModifier->StartTime = InStartTime;
		NewModifier->EndTime = InEndTime;
		NewModifier->Scale = InScale;
		InMotionWarpingComp->AddRootMotionModifier(NewModifier);
	}
}