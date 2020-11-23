// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraShake.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraAnim.h"
#include "Camera/CameraAnimInst.h"
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h" // for IsHeadTrackingAllowed()

//////////////////////////////////////////////////////////////////////////
// FFOscillator

// static
float FFOscillator::UpdateOffset(FFOscillator const& Osc, float& CurrentOffset, float DeltaTime)
{
	if (Osc.Amplitude != 0.f)
	{
		CurrentOffset += DeltaTime * Osc.Frequency;

		float WaveformSample;
		switch(Osc.Waveform)
		{
			case EOscillatorWaveform::SineWave:
			default:
				WaveformSample = FMath::Sin(CurrentOffset);
				break;

			case EOscillatorWaveform::PerlinNoise:
				WaveformSample = FMath::PerlinNoise1D(CurrentOffset);
				break;
		}

		return Osc.Amplitude * WaveformSample;
	}
	return 0.f;
}

// static
float FFOscillator::GetInitialOffset(FFOscillator const& Osc)
{
	return (Osc.InitialOffset == EOO_OffsetRandom)
		? FMath::FRand() * (2.f * PI)
		: 0.f;
}

// static
float FFOscillator::GetOffsetAtTime(FFOscillator const& Osc, float InitialOffset, float Time)
{
	return InitialOffset + (Time * Osc.Frequency);
}

//////////////////////////////////////////////////////////////////////////
// UMatineeCameraShake

UMatineeCameraShake::UMatineeCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer
				.SetDefaultSubobjectClass<UMatineeCameraShakePattern>(TEXT("RootShakePattern")))
{
	AnimPlayRate = 1.0f;
	AnimScale = 1.0f;
	AnimBlendInTime = 0.2f;
	AnimBlendOutTime = 0.2f;
	OscillationBlendInTime = 0.1f;
	OscillationBlendOutTime = 0.2f;
}

void UMatineeCameraShake::DoStopShake(bool bImmediately)
{
	APlayerCameraManager* CameraOwner = GetCameraManager();

	if (bImmediately)
	{
		// stop cam anim if playing
		if (AnimInst && !AnimInst->bFinished)
		{
			if (CameraOwner)
			{
				CameraOwner->StopCameraAnimInst(AnimInst, true);
			}
			else
			{
				AnimInst->Stop(true);
			}
		}

		AnimInst = nullptr;

		// stop oscillation
		OscillatorTimeRemaining = 0.f;
	}
	else
	{
		// advance to the blend out time
		if (OscillatorTimeRemaining > 0.0f)
		{
			OscillatorTimeRemaining = FMath::Min(OscillatorTimeRemaining, OscillationBlendOutTime);
		}
		else
		{
			OscillatorTimeRemaining = OscillationBlendOutTime;
		}

		if (AnimInst && !AnimInst->bFinished)
		{
			if (CameraOwner)
			{
				CameraOwner->StopCameraAnimInst(AnimInst, false);
			}
			else
			{
				// playing without a cameramanager, stop it ourselves
				AnimInst->Stop(false);
			}
		}
	}

	ReceiveStopShake(bImmediately);
}

void UMatineeCameraShake::DoStartShake()
{
	const float EffectiveOscillationDuration = (OscillationDuration > 0.f) ? OscillationDuration : TNumericLimits<float>::Max();

	// init oscillations
	if (OscillationDuration != 0.f)
	{
		if (OscillatorTimeRemaining > 0.f)
		{
			// this shake was already playing
			OscillatorTimeRemaining = EffectiveOscillationDuration;

			if (bBlendingOut)
			{
				bBlendingOut = false;
				CurrentBlendOutTime = 0.f;

				// stop any blendout and reverse it to a blendin
				if (OscillationBlendInTime > 0.f)
				{
					bBlendingIn = true;
					CurrentBlendInTime = OscillationBlendInTime * (1.f - CurrentBlendOutTime / OscillationBlendOutTime);
				}
				else
				{
					bBlendingIn = false;
					CurrentBlendInTime = 0.f;
				}
			}
		}
		else
		{
			RotSinOffset.X = FFOscillator::GetInitialOffset(RotOscillation.Pitch);
			RotSinOffset.Y = FFOscillator::GetInitialOffset(RotOscillation.Yaw);
			RotSinOffset.Z = FFOscillator::GetInitialOffset(RotOscillation.Roll);

			LocSinOffset.X = FFOscillator::GetInitialOffset(LocOscillation.X);
			LocSinOffset.Y = FFOscillator::GetInitialOffset(LocOscillation.Y);
			LocSinOffset.Z = FFOscillator::GetInitialOffset(LocOscillation.Z);

			FOVSinOffset = FFOscillator::GetInitialOffset(FOVOscillation);

			InitialLocSinOffset = LocSinOffset;
			InitialRotSinOffset = RotSinOffset;
			InitialFOVSinOffset = FOVSinOffset;

			OscillatorTimeRemaining = EffectiveOscillationDuration;

			if (OscillationBlendInTime > 0.f)
			{
				bBlendingIn = true;
				CurrentBlendInTime = 0.f;
			}
		}
	}

	// init cameraanim shakes
	APlayerCameraManager* CameraOwner = GetCameraManager();
	if (Anim != nullptr)
	{
		if (AnimInst)
		{
			float const Duration = bRandomAnimSegment ? RandomAnimSegmentDuration : 0.f;
			float const FinalAnimScale = ShakeScale * AnimScale;
			AnimInst->Update(AnimPlayRate, FinalAnimScale, AnimBlendInTime, AnimBlendOutTime, Duration);
		}
		else
		{
			bool bLoop = false;
			bool bRandomStart = false;
			float Duration = 0.f;
			if (bRandomAnimSegment)
			{
				bLoop = true;
				bRandomStart = true;
				Duration = RandomAnimSegmentDuration;
			}

			float const FinalAnimScale = ShakeScale * AnimScale;
			if (FinalAnimScale > 0.f)
			{
				ECameraShakePlaySpace AnimPlaySpace = GetPlaySpace();
				FRotator UserPlaySpaceRot = GetUserPlaySpaceMatrix().Rotator();

				if (CameraOwner)
				{
					AnimInst = CameraOwner->PlayCameraAnim(Anim, AnimPlayRate, FinalAnimScale, AnimBlendInTime, AnimBlendOutTime, bLoop, bRandomStart, Duration, AnimPlaySpace, UserPlaySpaceRot);
				}
				else
				{
					// allocate our own instance and start it
					AnimInst = NewObject<UCameraAnimInst>(this);
					if (AnimInst)
					{
						// note: we don't have a temp camera actor necessary for evaluating a camera anim.
						// caller is responsible in this case for providing one by calling SetTempCameraAnimActor() on the shake instance before playing the shake
						AnimInst->Play(Anim, TempCameraActorForCameraAnims, AnimPlayRate, FinalAnimScale, AnimBlendInTime, AnimBlendOutTime, bLoop, bRandomStart, Duration);
						AnimInst->SetPlaySpace(AnimPlaySpace, UserPlaySpaceRot);
					}
				}
			}
		}
	}

	ReceivePlayShake(ShakeScale);
}

void UMatineeCameraShake::DoUpdateShake(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const float DeltaTime = Params.DeltaTime;
	const float BaseShakeScale = Params.TotalScale;

	// update anims with any desired scaling
	if (AnimInst)
	{
		AnimInst->TransientScaleModifier *= BaseShakeScale;
	}

	// update oscillation times... only decrease the time remaining if we're not infinite
	if (OscillatorTimeRemaining > 0.f)
	{
		OscillatorTimeRemaining -= DeltaTime;
		OscillatorTimeRemaining = FMath::Max(0.f, OscillatorTimeRemaining);
	}
	if (bBlendingIn)
	{
		CurrentBlendInTime += DeltaTime;
	}
	if (bBlendingOut)
	{
		CurrentBlendOutTime += DeltaTime;
	}

	// see if we've crossed any important time thresholds and deal appropriately
	bool bOscillationFinished = false;

	if (OscillatorTimeRemaining <= 0.f)
	{
		// finished!
		bOscillationFinished = true;
	}
	else if (OscillatorTimeRemaining < OscillationBlendOutTime)
	{
		// start blending out
		bBlendingOut = true;
		CurrentBlendOutTime = OscillationBlendOutTime - OscillatorTimeRemaining;
	}
	else if (OscillationDuration < 0.f)
	{
		// infinite oscillation, keep the time remaining up
		OscillatorTimeRemaining = TNumericLimits<float>::Max();
	}

	if (bBlendingIn)
	{
		if (CurrentBlendInTime > OscillationBlendInTime)
		{
			// done blending in!
			bBlendingIn = false;
		}
	}
	if (bBlendingOut)
	{
		if (CurrentBlendOutTime > OscillationBlendOutTime)
		{
			// done!!
			CurrentBlendOutTime = OscillationBlendOutTime;
			bOscillationFinished = true;
		}
	}

	// Do not update oscillation further if finished
	if (bOscillationFinished == false)
	{
		// calculate blend weight. calculating separately and taking the minimum handles overlapping blends nicely.
		float const BlendInWeight = (bBlendingIn) ? (CurrentBlendInTime / OscillationBlendInTime) : 1.f;
		float const BlendOutWeight = (bBlendingOut) ? (1.f - CurrentBlendOutTime / OscillationBlendOutTime) : 1.f;
		float const CurrentBlendWeight = FMath::Min(BlendInWeight, BlendOutWeight);

		// this is the oscillation scale, which includes oscillation fading
		float const OscillationScale = BaseShakeScale * CurrentBlendWeight;

		if (OscillationScale > 0.f)
		{
			// View location offset, compute sin wave value for each component
			FVector	LocOffset = FVector(0);
			LocOffset.X = FFOscillator::UpdateOffset(LocOscillation.X, LocSinOffset.X, DeltaTime);
			LocOffset.Y = FFOscillator::UpdateOffset(LocOscillation.Y, LocSinOffset.Y, DeltaTime);
			LocOffset.Z = FFOscillator::UpdateOffset(LocOscillation.Z, LocSinOffset.Z, DeltaTime);
			LocOffset *= OscillationScale;

			OutResult.Location = LocOffset;

			// View rotation offset, compute sin wave value for each component
			FRotator RotOffset;
			RotOffset.Pitch = FFOscillator::UpdateOffset(RotOscillation.Pitch, RotSinOffset.X, DeltaTime) * OscillationScale;
			RotOffset.Yaw = FFOscillator::UpdateOffset(RotOscillation.Yaw, RotSinOffset.Y, DeltaTime) * OscillationScale;
			RotOffset.Roll = FFOscillator::UpdateOffset(RotOscillation.Roll, RotSinOffset.Z, DeltaTime) * OscillationScale;

			// Don't allow shake to flip pitch past vertical, if not using a headset (where we can't limit the camera locked to your head).
			APlayerCameraManager* CameraOwner = GetCameraManager();
			AActor * WorldActor = (CameraOwner ? CameraOwner : TempCameraActorForCameraAnims);
			UWorld * World = (WorldActor ? WorldActor->GetWorld() : nullptr);
			if (!GEngine->XRSystem.IsValid() ||
				!(World != nullptr ? GEngine->XRSystem->IsHeadTrackingAllowedForWorld(*World) : GEngine->XRSystem->IsHeadTrackingAllowed()))
			{
				// Find normalized result when combined, and remove any offset that would push it past the limit.
				const float NormalizedInputPitch = FRotator::NormalizeAxis(Params.POV.Rotation.Pitch);
				RotOffset.Pitch = FRotator::NormalizeAxis(RotOffset.Pitch);
				RotOffset.Pitch = FMath::ClampAngle(NormalizedInputPitch + RotOffset.Pitch, -89.9f, 89.9f) - NormalizedInputPitch;
			}

			OutResult.Rotation = RotOffset;

			// Compute FOV change
			OutResult.FOV = OscillationScale * FFOscillator::UpdateOffset(FOVOscillation, FOVSinOffset, DeltaTime);
		}
	}

	// Apply the playspace so we have an absolute result we can pass to the legacy blueprint API.
	ApplyPlaySpace(Params, OutResult);
	check(EnumHasAnyFlags(OutResult.Flags, ECameraShakeUpdateResultFlags::ApplyAsAbsolute));

	// Call the legacy blueprint API. We need to convert back and forth.
	{
		FMinimalViewInfo InOutPOV(Params.POV);
		InOutPOV.Location = OutResult.Location;
		InOutPOV.Rotation = OutResult.Rotation;
		InOutPOV.FOV = OutResult.FOV;

		BlueprintUpdateCameraShake(DeltaTime, Params.DynamicScale, InOutPOV, InOutPOV);

		OutResult.Location = InOutPOV.Location;
		OutResult.Rotation = InOutPOV.Rotation;
		OutResult.FOV = InOutPOV.FOV;
	}
}

void UMatineeCameraShake::DoScrubShake(const FCameraShakeScrubParams& Params, FCameraShakeUpdateResult& OutResult)
{
	const float NewTime = Params.AbsoluteTime;

	// reset to start and advance to desired point
	LocSinOffset = InitialLocSinOffset;
	RotSinOffset = InitialRotSinOffset;
	FOVSinOffset = InitialFOVSinOffset;

	OscillatorTimeRemaining = OscillationDuration;

	if (OscillationBlendInTime > 0.f)
	{
		bBlendingIn = true;
		CurrentBlendInTime = 0.f;
	}

	if (OscillationBlendOutTime > 0.f)
	{
		bBlendingOut = false;
		CurrentBlendOutTime = 0.f;
	}

	if (OscillationDuration > 0.f)
	{
		if ((OscillationBlendOutTime > 0.f) && (NewTime > (OscillationDuration - OscillationBlendOutTime)))
		{
			bBlendingOut = true;
			CurrentBlendOutTime = OscillationBlendOutTime - (OscillationDuration - NewTime);
		}
	}

	FCameraShakeUpdateParams UpdateParams = Params.ToUpdateParams();

	DoUpdateShake(UpdateParams, OutResult);

	check(EnumHasAnyFlags(OutResult.Flags, ECameraShakeUpdateResultFlags::ApplyAsAbsolute));

	if (AnimInst)
	{
		FMinimalViewInfo AnimPOV(Params.POV);
		AnimPOV.Location = OutResult.Location;
		AnimPOV.Rotation = OutResult.Rotation;
		AnimPOV.FOV = OutResult.FOV;

		AnimInst->SetCurrentTime(NewTime);
		AnimInst->ApplyToView(AnimPOV);

		OutResult.Location = AnimPOV.Location;
		OutResult.Rotation = AnimPOV.Rotation;
		OutResult.FOV = AnimPOV.FOV;
	}
}

bool UMatineeCameraShake::DoGetIsFinished() const
{
	return ((OscillatorTimeRemaining <= 0.f) &&							// oscillator is finished
		((AnimInst == nullptr) || AnimInst->bFinished) &&				// anim is finished
		ReceiveIsFinished()												// BP thinks it's finished
		);
}

/// @cond DOXYGEN_WARNINGS

bool UMatineeCameraShake::ReceiveIsFinished_Implementation() const
{
	return true;
}

/// @endcond

bool UMatineeCameraShake::IsLooping() const
{
	return OscillationDuration < 0.0f;
}

void UMatineeCameraShake::SetCurrentTimeAndApplyShake(float NewTime, FMinimalViewInfo& POV)
{
	ScrubAndApplyCameraShake(NewTime, 1.f, POV);
}

void UMatineeCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration::Custom();
}

void UMatineeCameraShakePattern::StopShakePatternImpl(const FCameraShakeStopParams& Params)
{
	UMatineeCameraShake* Shake = GetShakeInstance<UMatineeCameraShake>();
	Shake->DoStopShake(Params.bImmediately);
}

void UMatineeCameraShakePattern::StartShakePatternImpl(const FCameraShakeStartParams& Params)
{
	UMatineeCameraShake* Shake = GetShakeInstance<UMatineeCameraShake>();
	Shake->DoStartShake();
}

void UMatineeCameraShakePattern::UpdateShakePatternImpl(const FCameraShakeUpdateParams& Params, FCameraShakeUpdateResult& OutResult)
{
	UMatineeCameraShake* Shake = GetShakeInstance<UMatineeCameraShake>();
	Shake->DoUpdateShake(Params, OutResult);
}

void UMatineeCameraShakePattern::ScrubShakePatternImpl(const FCameraShakeScrubParams& Params, FCameraShakeUpdateResult& OutResult)
{
	UMatineeCameraShake* Shake = GetShakeInstance<UMatineeCameraShake>();
	Shake->DoScrubShake(Params, OutResult);
}

bool UMatineeCameraShakePattern::IsFinishedImpl() const
{
	UMatineeCameraShake* Shake = GetShakeInstance<UMatineeCameraShake>();
	return Shake->DoGetIsFinished();
}
