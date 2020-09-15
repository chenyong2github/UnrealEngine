// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/EnumClassFlags.h"

DECLARE_CYCLE_STAT(TEXT("CameraShakeStartShake"), STAT_StartShake, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("CameraShakeUpdateShake"), STAT_UpdateShake, STATGROUP_Game);

UCameraShakeBase::UCameraShakeBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bSingleInstance(false)
	, ShakeScale(1.f)
	, PlaySpace(ECameraShakePlaySpace::CameraLocal)
{
}

void UCameraShakeBase::GetShakeInfo(FCameraShakeInfo& OutInfo) const
{
	GetShakeInfoImpl(OutInfo);
}

void UCameraShakeBase::StartShake(APlayerCameraManager* Camera, float Scale, ECameraShakePlaySpace InPlaySpace, FRotator UserPlaySpaceRot)
{
	SCOPE_CYCLE_COUNTER(STAT_StartShake);

	// Check that we were correctly stopped before we are asked to play again.
	// Note that single-instance shakes can be restarted while they're running.
	checkf(!State.bIsActive || bSingleInstance, TEXT("Starting to play a shake that was already playing."));

	// Remember the various settings for this run.
	// Note that the camera manager can be null, for example in unit tests.
	CameraManager = Camera;
	ShakeScale = Scale;
	PlaySpace = InPlaySpace;
	UserPlaySpaceMatrix = (InPlaySpace == ECameraShakePlaySpace::UserDefined) ? 
		FRotationMatrix(UserPlaySpaceRot) : FRotationMatrix::Identity;

	// Acquire info about the shake we're running.
	GetShakeInfo(ActiveInfo);

	// Set the active state.
	State.ElapsedTime = 0.f;
	State.bIsActive = true;
	State.bHasDuration = ActiveInfo.Duration.IsFixed();
	State.bHasBlendIn = ActiveInfo.BlendIn > 0.f;
	State.bHasBlendOut = ActiveInfo.BlendOut > 0.f;

	// Let the sub-class do any initialization work.
	StartShakeImpl();
}

void UCameraShakeBase::UpdateAndApplyCameraShake(float DeltaTime, float Alpha, FMinimalViewInfo& InOutPOV)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateShake);

	checkf(State.bIsActive, TEXT("Updating a camera shake that wasn't started with a call to StartShake!"));

	// If we have a fixed duration for our shake, we can do all the time-keeping stuff ourselves.
	// This includes figuring out if the shake is finished, and what kind of blend in/out weight
	// we should apply.
	float BlendingWeight = 1.f;
	if (State.bHasDuration)
	{
		// Advance progress into the shake.
		const float ShakeDuration = ActiveInfo.Duration.Get();
		State.ElapsedTime = FMath::Min(State.ElapsedTime + DeltaTime, ShakeDuration);
		if (State.ElapsedTime >= ShakeDuration)
		{
			// The shake has ended.
			State.bIsActive = false;
			return;
		}

		// Blending in?
		if (State.bHasBlendIn && State.ElapsedTime < ActiveInfo.BlendIn)
		{
			BlendingWeight *= (State.ElapsedTime / ActiveInfo.BlendIn);
		}

		// Blending out?
		const float DurationRemaining = (ShakeDuration - State.ElapsedTime);
		if (State.bHasBlendOut && DurationRemaining < ActiveInfo.BlendOut)
		{
			BlendingWeight *= (DurationRemaining / ActiveInfo.BlendOut);
		}
	}
	
	// Make the sub-class do the actual work.
	FCameraShakeUpdateParams Params(InOutPOV);
	Params.DeltaTime = DeltaTime;
	Params.DynamicScale = Alpha;
	Params.BlendingWeight = BlendingWeight;
	Params.TotalScale = FMath::Max(Alpha * ShakeScale * BlendingWeight, 0.f);

	// Result object is initialized with zero values since the default flags make us handle it
	// as an additive offset.
	FCameraShakeUpdateResult Result;

	UpdateShakeImpl(Params, Result);

	// If the sub-class gave us a delta-transform, we can help with some of the basic functionality
	// of a camera shake... namely: apply shake scaling and play space transformation.
	if (!EnumHasAnyFlags(Result.Flags, ECameraShakeUpdateResultFlags::ApplyAsAbsolute))
	{
		if (!EnumHasAnyFlags(Result.Flags, ECameraShakeUpdateResultFlags::SkipAutoScale))
		{
			ApplyScale(Params, Result);
		}
		if (!EnumHasAnyFlags(Result.Flags, ECameraShakeUpdateResultFlags::SkipAutoPlaySpace))
		{
			ApplyPlaySpace(Params, Result);
		}
	}

	// Now we can apply the shake to the camera matrix.
	if (EnumHasAnyFlags(Result.Flags, ECameraShakeUpdateResultFlags::ApplyAsAbsolute))
	{
		InOutPOV.Location = Result.Location;
		InOutPOV.Rotation = Result.Rotation;
		InOutPOV.FOV = Result.FOV;
	}
	else
	{
		InOutPOV.Location += Result.Location;
		InOutPOV.Rotation += Result.Rotation;
		InOutPOV.FOV += Result.FOV;
	}
}

bool UCameraShakeBase::IsFinished() const
{
	if (State.bIsActive)
	{
		if (State.bHasDuration)
		{
			// If we have duration information, we can simply figure out ourselves if
			// we are finished.
			return State.ElapsedTime >= ActiveInfo.Duration.Get();
		}
		else
		{
			// Ask the sub-class whether it's finished.
			return IsFinishedImpl();
		}
	}
	// We're not active, so we're finished.
	return true;
}

void UCameraShakeBase::StopShake(bool bImmediately)
{
	if (!ensureMsgf(State.bIsActive, TEXT("Stopping a shake that wasn't active")))
	{
		return;
	}

	if (State.bHasDuration)
	{
		// If we have duration information, we can set our time-keeping accordingly to stop the shake.
		const float ShakeDuration = ActiveInfo.Duration.Get();
		if (bImmediately)
		{
			State.ElapsedTime = ShakeDuration;
		}
		else
		{
			State.ElapsedTime = State.bHasBlendOut ? (ShakeDuration - ActiveInfo.BlendOut) : ShakeDuration;
		}
	}

	// Let the sub-class do any custom stopping logic.
	StopShakeImpl(bImmediately);
}

void UCameraShakeBase::TeardownShake()
{
	TeardownShakeImpl();

	State = FCameraShakeState();
}

void UCameraShakeBase::ApplyScale(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& InOutResult) const
{
	return ApplyScale(Params.TotalScale, InOutResult);
}

void UCameraShakeBase::ApplyScale(float Scale, FCameraShakeUpdateResult& InOutResult) const
{
	InOutResult.Location *= Scale;
	InOutResult.Rotation *= Scale;
	InOutResult.FOV *= Scale;
}

void UCameraShakeBase::ApplyPlaySpace(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& InOutResult) const
{
	const FRotationMatrix CameraRot(Params.POV.Rotation);
	const FRotationMatrix OffsetRot(InOutResult.Rotation);

	if (PlaySpace == ECameraShakePlaySpace::CameraLocal)
	{
		// Apply translation offset in the camera's local space.
		InOutResult.Location = Params.POV.Location + CameraRot.TransformVector(InOutResult.Location);

		// Apply rotation offset to camera's local orientation.
		InOutResult.Rotation = (OffsetRot * CameraRot).Rotator();
	}
	else
	{
		// Apply translation offset using the desired space.
		// (it's FMatrix::Identity if the space is World, and whatever value was passed to StartShake if UserDefined)
		InOutResult.Location = Params.POV.Location + UserPlaySpaceMatrix.TransformVector(InOutResult.Location);

		// Apply rotation offset using the desired space.
		//
		// Compute the transform from camera to play space.
		FMatrix const CameraToPlaySpace = CameraRot * UserPlaySpaceMatrix.Inverse();

		// Compute the transform from shake (applied in playspace) back to camera.
		FMatrix const ShakeToCamera = OffsetRot * CameraToPlaySpace.Inverse();

		// RCS = rotated camera space, meaning camera space after it's been animated.
		// This is what we're looking for, the diff between rotated cam space and regular cam space.
		// Apply the transform back to camera space from the post-animated transform to get the RCS.
		FMatrix const RCSToCamera = CameraToPlaySpace * ShakeToCamera;

		// Now apply to real camera
		InOutResult.Rotation = (RCSToCamera * CameraRot).Rotator();
		
		// Math breakdown:
		//
		// ResultRot = RCSToCamera * CameraRot
		// ResultRot = CameraToPlaySpace * ShakeToCamera * CameraRot
		// ResultRot = (CameraToPlaySpace) * OffsetRot * (CameraToPlaySpace^-1) * CameraRot
		//
		// ...where CameraToPlaySpace = (CameraRot * (UserPlaySpaceMatrix^-1))
	}

	// We have a final location/rotation for the camera, so it should be applied verbatim.
	InOutResult.Flags = (InOutResult.Flags | ECameraShakeUpdateResultFlags::ApplyAsAbsolute);

	// And since we set that flag, we need to make the FOV absolute too.
	InOutResult.FOV = Params.POV.FOV + InOutResult.FOV;
}

FCameraShakeDuration UCameraShakeBase::GetCameraShakeDuration() const
{
	FCameraShakeInfo TempInfo;
	GetShakeInfo(TempInfo);
	return TempInfo.Duration;
}

void UCameraShakeBase::GetCameraShakeBlendTimes(float& OutBlendIn, float& OutBlendOut) const
{
	FCameraShakeInfo TempInfo;
	GetShakeInfo(TempInfo);
	OutBlendIn = TempInfo.BlendIn;
	OutBlendOut = TempInfo.BlendOut;
}
