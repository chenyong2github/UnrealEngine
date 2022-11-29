// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/ForceFeedbackEffect.h"
#include "GenericPlatform/IInputInterface.h"
#include "Misc/App.h"
#include "GameFramework/InputDeviceProperties.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ForceFeedbackEffect)

UForceFeedbackEffect::UForceFeedbackEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Make sure that by default the force feedback effect has an entry
	FForceFeedbackChannelDetails ChannelDetail;
	ChannelDetails.Add(ChannelDetail);
}

FForceFeedbackEffectOverridenChannelDetails::FForceFeedbackEffectOverridenChannelDetails()
{
	// Add one default channel details by default
	ChannelDetails.AddDefaulted(1);
}

#if WITH_EDITOR
void UForceFeedbackEffect::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// After any edit (really we only care about the curve, but easier this way) update the cached duration value
	GetDuration();
	GetTotalDevicePropertyDuration();
}
#endif

float UForceFeedbackEffect::GetDuration()
{
	// Always recalc the duration when in the editor as it could change
	if( GIsEditor || ( Duration < UE_SMALL_NUMBER ) )
	{
		Duration = 0.f;

		// Just use the primary platform user when calculating duration, this won't be affected by which player the effect is for
		const TArray<FForceFeedbackChannelDetails>& CurrentDetails = GetCurrentChannelDetails(IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser());

		float MinTime, MaxTime;
		for (int32 Index = 0; Index < CurrentDetails.Num(); ++Index)
		{
			CurrentDetails[Index].Curve.GetRichCurveConst()->GetTimeRange(MinTime, MaxTime);

			if (MaxTime > Duration)
			{
				Duration = MaxTime;
			}
		}
	}

	return Duration;
}

float UForceFeedbackEffect::GetTotalDevicePropertyDuration()
{
	float LongestDuration = 0.0f;

	// Check the device properties for any longer durations
	for (TObjectPtr<UInputDeviceProperty> DeviceProperty : DeviceProperties)
	{
		if (DeviceProperty)
		{
			const float PropertyDuration = DeviceProperty->RecalculateDuration();
			if (PropertyDuration > LongestDuration)
			{
				LongestDuration = PropertyDuration;
			}
		}
	}

	return LongestDuration;
}

void UForceFeedbackEffect::GetValues(const float EvalTime, FForceFeedbackValues& Values, const FPlatformUserId PlatformUser, const float ValueMultiplier) const
{
	const TArray<FForceFeedbackChannelDetails>& CurrentDetails = GetCurrentChannelDetails(PlatformUser);

	for (int32 Index = 0; Index < CurrentDetails.Num(); ++Index)
	{
		const FForceFeedbackChannelDetails& Details = CurrentDetails[Index];
		const float Value = Details.Curve.GetRichCurveConst()->Eval(EvalTime) * ValueMultiplier;

		if (Details.bAffectsLeftLarge)
		{
			Values.LeftLarge = FMath::Clamp(Value, Values.LeftLarge, 1.f);
		}
		if (Details.bAffectsLeftSmall)
		{
			Values.LeftSmall = FMath::Clamp(Value, Values.LeftSmall, 1.f);
		}
		if (Details.bAffectsRightLarge)
		{
			Values.RightLarge = FMath::Clamp(Value, Values.RightLarge, 1.f);
		}
		if (Details.bAffectsRightSmall)
		{
			Values.RightSmall = FMath::Clamp(Value, Values.RightSmall, 1.f);
		}
	}
}

void UForceFeedbackEffect::SetDeviceProperties(const FPlatformUserId PlatformUser, const float DeltaTime, const float EvalTime)
{
	for (TObjectPtr<UInputDeviceProperty> DeviceProp : DeviceProperties)
	{
		if (DeviceProp)
		{
			if (EvalTime > DeviceProp->GetDuration())
			{
				DeviceProp->ResetDeviceProperty(PlatformUser);
			}
			else
			{
				DeviceProp->EvaluateDeviceProperty(PlatformUser, DeltaTime, EvalTime);
				DeviceProp->ApplyDeviceProperty(PlatformUser);
			}			
		}
	}
}

void UForceFeedbackEffect::ResetDeviceProperties(const FPlatformUserId PlatformUser)
{
	for (TObjectPtr<UInputDeviceProperty> DeviceProp : DeviceProperties)
	{
		if (DeviceProp)
		{
			DeviceProp->ResetDeviceProperty(PlatformUser);
		}
	}
}

const TArray<FForceFeedbackChannelDetails>& UForceFeedbackEffect::GetCurrentChannelDetails(const FPlatformUserId PlatformUser) const
{	
	if (const UInputDeviceSubsystem* SubSystem = UInputDeviceSubsystem::Get())
	{
		FHardwareDeviceIdentifier Hardware = SubSystem->GetMostRecentlyUsedHardwareDevice(PlatformUser);
		// Check if there are any per-input device overrides available
		if (const FForceFeedbackEffectOverridenChannelDetails* Details = PerDeviceOverides.Find(Hardware.HardwareDeviceIdentifier))
		{
			return Details->ChannelDetails;
		}
	}

	return ChannelDetails;
}

void FActiveForceFeedbackEffect::GetValues(FForceFeedbackValues& Values) const
{
	if (ForceFeedbackEffect)
	{
		const float Duration = ForceFeedbackEffect->GetDuration();
		const float EvalTime = PlayTime - Duration * FMath::FloorToFloat(PlayTime / Duration);
		ForceFeedbackEffect->GetValues(EvalTime, Values, PlatformUser);
	}
	else
	{
		Values = FForceFeedbackValues();
	}
}

bool FActiveForceFeedbackEffect::Update(const float DeltaTime, FForceFeedbackValues& Values)
{
	if (ForceFeedbackEffect == nullptr)
	{
		return false;
	}

	const float EffectDuration = ForceFeedbackEffect->GetDuration();
	const float DevicePropDuration = ForceFeedbackEffect->GetTotalDevicePropertyDuration();

	PlayTime += (Parameters.bIgnoreTimeDilation ? FApp::GetDeltaTime() : DeltaTime);

	// If the play time is longer then the force feedback effect curve's last key value, 
	// or if there are still device properties that need to be evaluated
	if (PlayTime > EffectDuration && PlayTime > DevicePropDuration && (!Parameters.bLooping || (EffectDuration == 0.0f && DevicePropDuration == 0.0f)))
	{
		return false;
	}
	// Update the effect values if we can. Always get the values for a looping effect.
	if (PlayTime <= EffectDuration || Parameters.bLooping)
	{
		GetValues(Values);
	}	
	
	// Update device properties if we can
	if (PlayTime <= DevicePropDuration)
	{
		// Set any input device properties associated with this effect
		const float EvalTime = PlayTime - DevicePropDuration * FMath::FloorToFloat(PlayTime / DevicePropDuration);
		ForceFeedbackEffect->SetDeviceProperties(PlatformUser, DeltaTime, EvalTime);
	}	

	return true;
}

void FActiveForceFeedbackEffect::ResetDeviceProperties()
{
	if (ForceFeedbackEffect)
	{
		ForceFeedbackEffect->ResetDeviceProperties(PlatformUser);
	}
}