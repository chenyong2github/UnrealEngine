// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputSettings.h"
#include "Subsystems/EngineSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "InputDeviceSubsystem.generated.h"

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogInputDeviceProperties, Log, All);

class UInputDeviceProperty;

/** A handle to an active input device property that is being used by the InputDeviceSubsytem. */
USTRUCT(BlueprintType)
struct ENGINE_API FInputDevicePropertyHandle
{
	friend class UInputDeviceSubsystem;

	GENERATED_BODY()

	FInputDevicePropertyHandle();
	~FInputDevicePropertyHandle() = default;

	/** Returns true if this handle is valid */
	bool IsValid() const;

	/** An invalid Input Device Property handle */
	static FInputDevicePropertyHandle InvalidHandle;

	friend uint32 GetTypeHash(const FInputDevicePropertyHandle& InHandle);

	bool operator==(const FInputDevicePropertyHandle& Other) const;
	bool operator!=(const FInputDevicePropertyHandle& Other) const;

	FString ToString() const;

private:

	// Private constructor because we don't want any other users to make a valid device property handle.
	FInputDevicePropertyHandle(uint32 InternalID);

	/** Static function to get a unique device handle. */
	static FInputDevicePropertyHandle AcquireValidHandle();

	/** The internal ID of this handle. Populated by the private constructor in AcquireValidHandle */
	UPROPERTY()
	uint32 InternalId;
};

/** Contains a pointer to an active device property and keeps track of how long it has been evaluated for */
USTRUCT()
struct ENGINE_API FActiveDeviceProperty
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
	bool bRemoveAfterEvaluationTime = true;

	/** The handle of this active property. */
	FInputDevicePropertyHandle PropertyHandle = FInputDevicePropertyHandle::InvalidHandle;
};

/** Parameters for the UInputDeviceSubsystem::SetDeviceProperty function */
USTRUCT(BlueprintType)
struct ENGINE_API FSetDevicePropertyParams
{
	GENERATED_BODY()

	FSetDevicePropertyParams();

	FSetDevicePropertyParams(TSubclassOf<UInputDeviceProperty> InPropertyClass, const FPlatformUserId InUserId, const bool bInRemoveAfterEvaluationTime = true);
	
	/** The device property to set */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Input Devices")
	TSubclassOf<UInputDeviceProperty> DevicePropertyClass;
	
	/** The Platform User whose device's should receive the device property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	FPlatformUserId UserId;

	/**
	* If true, then when the InputDeviceProperty is done being evaluated and has fulfilled its duration, then
	* it will have it's Reset function called and be removed as an active property. 
	* 
	* Set this to false if you want your device property to keep being applied without regards to it's duration.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	bool bRemoveAfterEvaluationTime = true;
};

/**
 * Delegate called when a user changed the hardware they are using for input.
 *
 * @param UserId		The Platform user whose device has changed
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHardwareInputDeviceChanged, const FPlatformUserId, UserId, const FInputDeviceId, DeviceId);

/**
* The input device subsystem provides an interface to allow users to set Input Device Properties
* on any Platform User. 
*/
UCLASS(BlueprintType)
class ENGINE_API UInputDeviceSubsystem : public UEngineSubsystem, public FTickableGameObject
{
	friend class FInputDeviceSubsystemProcessor;
	friend class FInputDeviceDebugVisualizer;
	
	GENERATED_BODY()

public:

	static UInputDeviceSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const;
	
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
	FInputDevicePropertyHandle SetDeviceProperty(const FSetDevicePropertyParams& Params);

	/** Returns a pointer to the active input device property with the given handle. Returns null if the property doesn't exist */
	TObjectPtr<UInputDeviceProperty> GetActiveDeviceProperty(const FInputDevicePropertyHandle Handle);

	/** 
	* Remove any active device properties that have the same class as the one given.
	* 
	* @param UserId					The PlatformUser that would have the device property applied to them
	* @param DevicePropertyClass	The Device Property type to remove
	* 
	* @return						The number of removed device properties. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (ReturnDisplayName = "Num Removed"))
	int32 RemoveDevicePropertiesOfClass(const FPlatformUserId UserId, TSubclassOf<UInputDeviceProperty> DevicePropertyClass);

	/**
	* Remove a single device property based on it's handle
	*
	* @param FInputDevicePropertyHandle		Device property handle to be removed
	* @param bResetOnRemoval				If true, call ResetDeviceProperty before it is removed. Default is true.
	*
	* @return								The number of removed device properties.
	*/
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (ReturnDisplayName = "Num Removed"))
	int32 RemoveDevicePropertyByHandle(const FInputDevicePropertyHandle HandleToRemove, const bool bResetOnRemoval = true);

	/**
	* Remove a set of device properties based on their handles. 
	* 
	* @param HandlesToRemove	The set of device property handles to remove
	* @param bResetOnRemoval	If true, call ResetDeviceProperty before it is removed. Default is true.
	* 
	* @return					The number of removed device properties
	*/
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (ReturnDisplayName = "Num Removed"))
	int32 RemoveDevicePropertyHandles(const TSet<FInputDevicePropertyHandle>& HandlesToRemove, const bool bResetOnRemoval = true);

	/** Removes all the current Input Device Properties that are active, regardless of the Platform User */
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	void RemoveAllDeviceProperties();

	/** Returns true if the given handle is valid */
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	static bool IsDevicePropertyHandleValid(const FInputDevicePropertyHandle& InHandle);

	/** Gets the most recently used hardware input device for the given platform user */
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	FHardwareDeviceIdentifier GetMostRecentlyUsedHardwareDevice(const FPlatformUserId InUserId) const;

	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	FHardwareDeviceIdentifier GetInputDeviceHardwareIdentifier(const FInputDeviceId InputDevice) const;

	/** A delegate that is fired when a platform user changes what Hardware Input device they are using */
	UPROPERTY(BlueprintAssignable, Category = "Input Device")
	FHardwareInputDeviceChanged OnInputHardwareDeviceChanged;
	
protected:

	/** Set the most recently used hardware device */
	void SetMostRecentlyUsedHardwareDevice(const FInputDeviceId InDeviceId, const FHardwareDeviceIdentifier& InHardwareId);

	// Callbacks for when PIE is started/stopped. We will likely want to pause/resume input device properties
	// when this happens, or just remove all active properties when PIE stops. This will make designer iteration a little easier
#if WITH_EDITOR
	void OnPrePIEStarted(bool bSimulating);
	void OnPIEPaused(bool bSimulating);
	void OnPIEResumed(bool bSimulating);
	void OnPIEStopped(bool bSimulating);

	bool bIsPIEPlaying = false;
#endif
	
	/**
	* Array of currently active input device properties that will be evaluated on tick
	*/
	UPROPERTY(Transient)
	TArray<FActiveDeviceProperty> ActiveProperties;
	
	/** A map of an input device to it's most recent hardware device identifier */
	TMap<FInputDeviceId, FHardwareDeviceIdentifier> LatestInputDeviceIdentifiers;

	/** A map of platform user's to their most recent hardware device identifier */
	TMap<FPlatformUserId, FHardwareDeviceIdentifier> LatestUserDeviceIdentifiers;

	/** An input processor that is used to determine the current hardware input device */
	TSharedPtr<class FInputDeviceSubsystemProcessor> InputPreprocessor;
};