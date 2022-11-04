// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/EngineSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "InputDeviceSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogInputDeviceProperties, Log, All);

class UInputDeviceProperty;

/** Contains a pointer to an active device property and keeps track of how long it has been evaluated for */
USTRUCT()
struct FActiveDeviceProperty
{
	GENERATED_BODY()

	/** The active device property */
	UPROPERTY(Transient)
	TObjectPtr<UInputDeviceProperty> Property = nullptr;

	/** How long this property has been evaluated for. DeltaTime is added to this on tick */
	double EvaluatedDuration = 0.0;

	/** The platform user that is actively receiving this device property */
	FPlatformUserId PlatformUser = PLATFORMUSERID_NONE;

	/**
	* If true, then when the InputDeviceProperty is done being evaluated and has fulfilled its duration, then 
	* it will have it's Reset function called. Set this to false if you want your device property to stay 
	* in it's ending state. 
	*/
	bool bResetUponCompletion = true;
};

/** Parameters for the UInputDeviceSubsystem::SetDeviceProperty function */
USTRUCT(BlueprintType)
struct FSetDevicePropertyParams
{
	GENERATED_BODY()

	FSetDevicePropertyParams();
	
	/** The device Property to set */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	TSubclassOf<UInputDeviceProperty> DevicePropertyClass;
	
	/** The Platform User whose device's should receive the device property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	FPlatformUserId UserId;

	/**
	* If true, then when the InputDeviceProperty is done being evaluated and has fulfilled its duration, then
	* it will have it's Reset function called. Set this to false if you want your device property to stay
	* in it's ending state.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	bool bResetUponCompletion = true;
};

/**
* The input device subsystem provides an interface to allow users to set Input Device Properties
* on any Platform User. 
*/
UCLASS(BlueprintType)
class UInputDeviceSubsystem : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:

	//~ Begin FTickableGameObject interface	
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsAllowedToTick() const override;
	virtual bool IsTickableInEditor() const override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	//~ End FTickableGameObject interface

	/** Set the given device property, which will start the evaluation and application of it to the given platform user. */
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (AutoCreateRefTerm = "Params"))
	void SetDeviceProperty(const FSetDevicePropertyParams& Params);

	/** 
	* Remove any active device properties that have the same class as the one given.
	* 
	* @param UserId					The PlatformUser that would have the device property applied to them
	* @param DevicePropertyClass	The Device Property type to remove
	* 
	* @return						The number of removed device properties. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (ReturnDisplayName = "Num Removed"))
	int32 RemoveDeviceProperty(const FPlatformUserId UserId, TSubclassOf<UInputDeviceProperty> DevicePropertyClass);

protected:

	/**
	* Array of the active device properties that are currently being evaluated on Tick.
	*/
	UPROPERTY(Transient)
	TArray<FActiveDeviceProperty> ActiveProperties;
};