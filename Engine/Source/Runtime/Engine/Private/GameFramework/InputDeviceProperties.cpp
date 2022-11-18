// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/InputDeviceProperties.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Framework/Application/SlateApplication.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveFloat.h"

///////////////////////////////////////////////////////////////////////
// UInputDeviceProperty

UInputDeviceProperty::UInputDeviceProperty()
{
	RecalculateDuration();
}

void UInputDeviceProperty::ApplyDeviceProperty(const FPlatformUserId UserId)
{
	UInputDeviceProperty::ApplyDeviceProperty(UserId, GetInternalDeviceProperty());
}

void UInputDeviceProperty::ApplyDeviceProperty(const FPlatformUserId UserId, FInputDeviceProperty* RawProperty)
{
	if (ensure(RawProperty))
	{
		IInputInterface* InputInterface = FSlateApplication::Get().IsInitialized() ? FSlateApplication::Get().GetInputInterface() : nullptr;
		if (InputInterface)
		{
			int32 ControllerId = INDEX_NONE;
			IPlatformInputDeviceMapper::Get().RemapUserAndDeviceToControllerId(UserId, ControllerId);

			// TODO_BH: Refactor input interface to take an FPlatformUserId directly (UE-158881)
			InputInterface->SetDeviceProperty(ControllerId, RawProperty);
		}
	}
}

float UInputDeviceProperty::GetDuration() const
{
	return PropertyDuration;
}

float UInputDeviceProperty::RecalculateDuration()
{
	return PropertyDuration;
}

#if WITH_EDITOR
void UInputDeviceProperty::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
	RecalculateDuration();
}
#endif	// WITH_EDITOR

void UInputDeviceProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{

}

void UInputDeviceProperty::ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser)
{

}

///////////////////////////////////////////////////////////////////////
// UColorInputDeviceProperty

void UColorInputDeviceProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{
	InternalProperty.bEnable = bEnable;
	InternalProperty.Color = LightColor;
}

void UColorInputDeviceProperty::ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser)
{
	// Disabling the light will reset the color
	InternalProperty.bEnable = false;
	ApplyDeviceProperty(PlatformUser);
}

FInputDeviceProperty* UColorInputDeviceProperty::GetInternalDeviceProperty()
{
	return &InternalProperty;
}

///////////////////////////////////////////////////////////////////////
// UColorInputDeviceCurveProperty

void UColorInputDeviceCurveProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{
	InternalProperty.bEnable = bEnable;

	if (ensure(DeviceColorCurve))
	{
		FLinearColor CurveColor = DeviceColorCurve->GetLinearColorValue(Duration);
		InternalProperty.Color = CurveColor.ToFColorSRGB();
	}
}

void UColorInputDeviceCurveProperty::ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser)
{
	// Disabling the light will reset the color
	InternalProperty.bEnable = false;
	ApplyDeviceProperty(PlatformUser);
}

FInputDeviceProperty* UColorInputDeviceCurveProperty::GetInternalDeviceProperty()
{
	return &InternalProperty;
}

float UColorInputDeviceCurveProperty::RecalculateDuration()
{
	float MinTime, MaxTime;
	if (DeviceColorCurve)
	{
		DeviceColorCurve->GetTimeRange(MinTime, MaxTime);
		PropertyDuration = MaxTime;
	}
	else
	{
		PropertyDuration = 1.0f;
	}
	return PropertyDuration;
}

///////////////////////////////////////////////////////////////////////
// UInputDeviceTriggerEffect

FInputDeviceProperty* UInputDeviceTriggerEffect::GetInternalDeviceProperty()
{
	return &ResetProperty;
}

void UInputDeviceTriggerEffect::ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser)
{
	if (bResetUponCompletion)
	{
		// Pass in our reset property
		ResetProperty.AffectedTriggers = AffectedTriggers;
		ApplyDeviceProperty(PlatformUser, &ResetProperty);
	}	
}

///////////////////////////////////////////////////////////////////////
// UInputDeviceTriggerFeedbackProperty

UInputDeviceTriggerFeedbackProperty::UInputDeviceTriggerFeedbackProperty()
	: UInputDeviceTriggerEffect()
{
	InternalProperty.AffectedTriggers = AffectedTriggers;
}

int32 UInputDeviceTriggerFeedbackProperty::GetPositionValue(const float Duration) const
{
	if (ensure(FeedbackPositionCurve))
	{
		// TODO: Make the max position a cvar
		int32 Position = FeedbackPositionCurve->GetFloatValue(Duration);
		return FMath::Clamp(Position, 0, 9);
	}
	return 0;
}

int32 UInputDeviceTriggerFeedbackProperty::GetStrengthValue(const float Duration) const
{
	if (ensure(FeedbackStrenghCurve))
	{
		// TODO: Make the max Strength a cvar
		int32 Strength = FeedbackPositionCurve->GetFloatValue(Duration);
		return FMath::Clamp(Strength, 0, 8);
	}
	return 0;
}

void UInputDeviceTriggerFeedbackProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{		
	InternalProperty.AffectedTriggers = AffectedTriggers;

	InternalProperty.Position = GetPositionValue(Duration);
	InternalProperty.Strengh = GetStrengthValue(Duration);
}

FInputDeviceProperty* UInputDeviceTriggerFeedbackProperty::GetInternalDeviceProperty()
{
	return &InternalProperty;
}

float UInputDeviceTriggerFeedbackProperty::RecalculateDuration()
{
	// Get the max time from the two curves on this property
	float MinTime, MaxTime = 0.0f;
	if (FeedbackPositionCurve)
	{
		FeedbackPositionCurve->GetTimeRange(MinTime, MaxTime);
	}
	
	if (FeedbackStrenghCurve)
	{
		FeedbackStrenghCurve->GetTimeRange(MinTime, MaxTime);
	}
	
	PropertyDuration = MaxTime;
	return PropertyDuration;
}

///////////////////////////////////////////////////////////////////////
// UInputDeviceTriggerResistanceProperty

UInputDeviceTriggerResistanceProperty::UInputDeviceTriggerResistanceProperty()
	: UInputDeviceTriggerEffect()
{
	PropertyDuration = 1.0f;
}

void UInputDeviceTriggerResistanceProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{
	InternalProperty.AffectedTriggers = AffectedTriggers;
	InternalProperty.StartPosition = StartPosition;
	InternalProperty.StartStrengh = StartStrengh;
	InternalProperty.EndPosition = EndPosition;
	InternalProperty.EndStrengh = EndStrengh;
}

FInputDeviceProperty* UInputDeviceTriggerResistanceProperty::GetInternalDeviceProperty()
{
	return &InternalProperty;
}


///////////////////////////////////////////////////////////////////////
// UInputDeviceTriggerVibrationProperty

UInputDeviceTriggerVibrationProperty::UInputDeviceTriggerVibrationProperty()
	: UInputDeviceTriggerEffect()
{
	PropertyDuration = 1.0f;
}

void UInputDeviceTriggerVibrationProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{
	InternalProperty.AffectedTriggers = AffectedTriggers;
	InternalProperty.TriggerPosition = GetTriggerPositionValue(Duration);
	InternalProperty.VibrationFrequency = GetVibrationFrequencyValue(Duration);
	InternalProperty.VibrationAmplitude = GetVibrationAmplitudeValue(Duration);
}

FInputDeviceProperty* UInputDeviceTriggerVibrationProperty::GetInternalDeviceProperty()
{
	return &InternalProperty;
}

float UInputDeviceTriggerVibrationProperty::RecalculateDuration()
{
	// Get the max time from the curves on this property
	float MaxTime = 0.0f;

	auto EvaluateMaxTime = [&MaxTime](TObjectPtr<UCurveFloat> InCurve)
	{
		float MinCurveTime, MaxCurveTime = 0.0f;
		if (InCurve)
		{
			InCurve->GetTimeRange(MinCurveTime, MaxCurveTime);
			if (MaxCurveTime > MaxTime)
			{
				MaxTime = MaxCurveTime;
			}
		}
	};

	EvaluateMaxTime(TriggerPositionCurve);
	EvaluateMaxTime(VibrationFrequencyCurve);
	EvaluateMaxTime(VibrationAmplitudeCurve);

	PropertyDuration = MaxTime;
	return PropertyDuration;
}

int32 UInputDeviceTriggerVibrationProperty::GetTriggerPositionValue(const float Duration) const
{
	if (ensure(TriggerPositionCurve))
	{
		// TODO: Make the max Strength a cvar
		int32 Strength = TriggerPositionCurve->GetFloatValue(Duration);
		return FMath::Clamp(Strength, 0, 9);
	}
	return 0;
}

int32 UInputDeviceTriggerVibrationProperty::GetVibrationFrequencyValue(const float Duration) const
{
	if (ensure(VibrationFrequencyCurve))
	{
		// TODO: Make the max Frequency a cvar
		int32 Strength = VibrationFrequencyCurve->GetFloatValue(Duration);
		return FMath::Clamp(Strength, 0, 255);
	}
	return 0;
}

int32 UInputDeviceTriggerVibrationProperty::GetVibrationAmplitudeValue(const float Duration) const
{
	if (ensure(VibrationAmplitudeCurve))
	{
		// TODO: Make the max Amplitude a cvar
		int32 Strength = VibrationAmplitudeCurve->GetFloatValue(Duration);
		return FMath::Clamp(Strength, 0, 8);
	}
	return 0;
}
