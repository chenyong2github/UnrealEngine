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
	if (FInputDeviceProperty* RawProp = GetInternalDeviceProperty())
	{
		IInputInterface* InputInterface = FSlateApplication::Get().IsInitialized() ? FSlateApplication::Get().GetInputInterface() : nullptr;
		if (InputInterface)
		{
			int32 ControllerId = INDEX_NONE;
			IPlatformInputDeviceMapper::Get().RemapUserAndDeviceToControllerId(UserId, ControllerId);

			// TODO_BH: Refactor input interface to take an FPlatformUserId directly (UE-158881)
			InputInterface->SetDeviceProperty(ControllerId, RawProp);
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

	if (ensure(DeviceColorCurve))
	{
		FLinearColor CurveColor = DeviceColorCurve->GetLinearColorValue(Duration);
		InternalProperty.Color = CurveColor.ToFColorSRGB();
	}
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

float UColorInputDeviceProperty::RecalculateDuration()
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