// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GenericPlatform/IInputInterface.h"

#include "InputDeviceProperties.generated.h"

class UCurveLinearColor;
class UCurveFloat;

#if WITH_EDITOR
	struct FPropertyChangedChainEvent;
#endif	// WITH_EDITOR

/**
* Base class that represents a single Input Device Property. An Input Device Property
* represents a feature that can be set on an input device. Things like what color a
* light is, advanced rumble patterns, or trigger haptics.
* 
* This top level object can then be evaluated at a specific time to create a lower level
* FInputDeviceProperty, which the IInputInterface implementation can interpret however it desires.
* 
* The behavior of device properties can vary depending on the current platform. Some platforms may not
* support certain device properties. An older gamepad may not have any advanced trigger haptics for 
* example. 
*/
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew)
class ENGINE_API UInputDeviceProperty : public UObject
{
	GENERATED_BODY()
public:

	UInputDeviceProperty();

	/**
	* Evaluate this device property for a given duration. 
	* If overriding in Blueprints, make sure to call the parent function!
	* 
 	* @param PlatformUser		The platform user that should receive this device property change
	* @param DeltaTime			Delta time
	* @param Duration			The number of seconds that this property has been active. Use this to get things like curve data over time.
	* @return					A pointer to the evaluated input device property.
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "InputDevice")
	void EvaluateDeviceProperty(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration);

	/** 
	* Native C++ implementation of EvaluateDeviceProperty.
	* 
	* Override this to alter your device property in native code.
	* @see UInputDeviceProperty::EvaluateDeviceProperty
	*/
	virtual void EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration);

	/**
	* Reset the current device property. Provides an opportunity to reset device state after evaluation is complete. 
	* If overriding in Blueprints, make sure to call the parent function!
	* 
	* @param PlatformUser		The platform user that should receive this device property change
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "InputDevice")
	void ResetDeviceProperty(const FPlatformUserId PlatformUser);

	/**
	* Native C++ implementation of ResetDeviceProperty
	* Override this in C++ to alter the device property behavior in native code. 
	* 
	* @see ResetDeviceProperty
	*/
	virtual void ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser);

	/**
	* Apply the given device property to the Input Interface. 
	* Note: To remove any applied affects of this device property, call ResetDeviceProperty.
	* 
	* @param UserId		The owning Platform User whose input device this property should be applied to.
	*/
	UFUNCTION(BlueprintCallable, Category = "InputDevice")
	virtual void ApplyDeviceProperty(const FPlatformUserId UserId);
	
	/** Gets a pointer to the current input device property that the IInputInterface can use. */
	virtual FInputDeviceProperty* GetInternalDeviceProperty() { return nullptr; };

	/**
	* The duration that this device property should last. Override this if your property has any dynamic curves 
	* to be the max time range.
	*/
	float GetDuration() const;
	
	/**
	 * Recalculates this device property's duration. This should be called whenever there are changes made
	 * to things like curves, or other time sensative properties.
	 */
	virtual float RecalculateDuration();

	// Post edit change property to update the duration if there are any dynamic options like for curves
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif	// WITH_EDITOR

protected:

	/**
	* The duration that this device property should last. Override this if your property has any dynamic curves 
	* to be the max time range.
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Info")
	float PropertyDuration = 0.1f;
};

/** 
* A property that can be used to change the color of an input device's light 
* 
* NOTE: This property has platform specific implementations and may behavior differently per platform.
* See the docs for more details on each platform.
*/
UCLASS(Blueprintable, BlueprintType)
class UColorInputDeviceProperty : public UInputDeviceProperty
{
	GENERATED_BODY()

public:

	virtual void EvaluateDeviceProperty_Implementation(const FPlatformUserId PlatformUser, const float DeltaTime, const float Duration) override;
	virtual void ResetDeviceProperty_Implementation(const FPlatformUserId PlatformUser) override;
	virtual FInputDeviceProperty* GetInternalDeviceProperty() override;
	virtual float RecalculateDuration() override;

protected:

	/** True if the light should be enabled at all */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeviceProperty")
	bool bEnable = true;

	/** The color the device light should be */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DeviceProperty")
	TObjectPtr<UCurveLinearColor> DeviceColorCurve;

private:

	/** The internal light color property that this represents; */
	FInputDeviceLightColorProperty InternalProperty;
};
