// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/InputDeviceProperties.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Framework/Application/SlateApplication.h"
#include "Curves/CurveLinearColor.h"

///////////////////////////////////////////////////////////////////////
// UInputDeviceProperty

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

void UInputDeviceProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{

}

void UInputDeviceProperty::ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser)
{

}

///////////////////////////////////////////////////////////////////////
// UColorInputDeviceProperty

UColorInputDeviceProperty::UColorInputDeviceProperty()
: UInputDeviceProperty()
{
	PropertyName = FInputDeviceLightColorProperty::PropertyName();
	float MinTime, MaxTime;
	if (DeviceColorCurve)
	{
		DeviceColorCurve->GetTimeRange(MinTime, MaxTime);	
	}
	else
	{
		PropertyDuration = 1.0f;	
	}
}

void UColorInputDeviceProperty::EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration)
{
	InternalProperty.bEnable = bEnable;
	
	FLinearColor CurveColor = DeviceColorCurve->GetLinearColorValue(Duration);
	InternalProperty.Color = CurveColor.ToFColorSRGB();
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