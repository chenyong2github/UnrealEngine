// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InputSettings.h"
#include "GameFramework/InputDevicePropertyHandle.h"
#include "Subsystems/EngineSubsystem.h"
#include "Tickable.h"
#include "Templates/SubclassOf.h"
#include "InputDeviceSubsystem.generated.h"

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogInputDeviceProperties, Log, All);

class UInputDeviceProperty;
class APlayerController;

/** Parameters for the UInputDeviceSubsystem::SetDeviceProperty function */
USTRUCT(BlueprintType)
struct ENGINE_API FSetDevicePropertyParams
{
	GENERATED_BODY()

	FSetDevicePropertyParams();
	
	/** The Platform User whose device's should receive the device property */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	FPlatformUserId UserId;

	/**
	* If true, then the input device property will not be removed after it's evaluation time has completed.
	* Instead, it will remain active until manually removed with a RemoveDeviceProperty call.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	bool bLooping = false;

	/** If true, then this device property will ignore dilated delta time and use the Applications delta time instead */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	bool bIgnoreTimeDilation = false;

	/** If true, then this device property will be played even if the game world is paused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input Devices")
	bool bPlayWhilePaused = false;
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
	friend class FInputDeviceDebugTools;
	
	GENERATED_BODY()

protected:

	/** Contains a pointer to an active device property and keeps track of how long it has been evaluated for */
	struct FActiveDeviceProperty
	{		
		/** Active properties can just use the hash of their FInputDevicePropertyHandle for a fast and unique lookup */
		friend uint32 GetTypeHash(const UInputDeviceSubsystem::FActiveDeviceProperty& InProp)
		{
			return InProp.PropertyHandle.GetTypeHash();
		}

		friend bool operator==(const UInputDeviceSubsystem::FActiveDeviceProperty& ActiveProp, const FInputDevicePropertyHandle& Handle)
		{
			return ActiveProp.PropertyHandle == Handle;
		}

		friend bool operator!=(const UInputDeviceSubsystem::FActiveDeviceProperty& ActiveProp, const FInputDevicePropertyHandle& Handle)
		{
			return ActiveProp.PropertyHandle != Handle;
		}

		bool operator==(const UInputDeviceSubsystem::FActiveDeviceProperty& Other) const;
		bool operator!=(const UInputDeviceSubsystem::FActiveDeviceProperty& Other) const;

		/** The active device property */	
		TObjectPtr<UInputDeviceProperty> Property = nullptr;

		/** How long this property has been evaluated for. DeltaTime is added to this on tick */
		double EvaluatedDuration = 0.0;

		/** The platform user that is actively receiving this device property */
		FPlatformUserId PlatformUser = PLATFORMUSERID_NONE;

		/** The handle of this active property. */
		FInputDevicePropertyHandle PropertyHandle = FInputDevicePropertyHandle::InvalidHandle;

		/**
		* If true, then the input device property will not be removed after it's evaluation time has completed.
		* Instead, it will remain active until manually removed with a RemoveDeviceProperty call.
		*/
		bool bLooping = false;

		/** If true, then this device property will ignore dilated delta time and use the Applications delta time instead */
		bool bIgnoreTimeDilation = false;

		/** If true, then this device property will be played even if the game world is paused. */
		bool bPlayWhilePaused = false;
	};

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
	virtual void Tick(float InDeltaTime) override;
	//~ End FTickableGameObject interface

	/** Get the player controller who has the given Platform User ID. */
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	static APlayerController* GetPlayerControllerFromPlatformUser(const FPlatformUserId UserId);

	/** Get the player controller who owns the given input device id */
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	static APlayerController* GetPlayerControllerFromInputDevice(const FInputDeviceId DeviceId);

	/**
	 * Starts tracking the given device property as an "Active" property. This means that the property will be evaluted and applied to its platform user
	 *
	 * NOTE: This does NOT make a new instance of the given property. If you pass in the same object before it is completely
	 * evaluated, then you see undesired effects.
	 */
	FInputDevicePropertyHandle ActivateDeviceProperty(UInputDeviceProperty* Property, const FSetDevicePropertyParams& Params);

	/** Spawn a new instance of the given device property class and activate it. */
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (AutoCreateRefTerm = "Params", ReturnDisplayName = "Device Property Handle"))
	FInputDevicePropertyHandle ActivateDevicePropertyOfClass(TSubclassOf<UInputDeviceProperty> PropertyClass, const FSetDevicePropertyParams& Params);
	
	/** Returns a pointer to the active input device property with the given handle. Returns null if the property doesn't exist */
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta=(ReturnDisplayName="Device Property"))
	UInputDeviceProperty* GetActiveDeviceProperty(const FInputDevicePropertyHandle Handle) const;
	
	/** Returns true if the property associated with the given handle is currently active, and it is not pending removal */
	UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (ReturnDisplayName = "Is Property Active"))
	bool IsPropertyActive(const FInputDevicePropertyHandle Handle) const;
	
	/** Returns true if the given UInputDeviceProperty object is already being evaluated. */
	//UFUNCTION(BlueprintCallable, Category = "Input Devices", meta = (ReturnDisplayName = "Is Property Active"))
	//bool IsDevicePropertyActive(UInputDeviceProperty* Property) const;

	/**
	* Remove a single device property based on it's handle
	*
	* @param FInputDevicePropertyHandle		Device property handle to be removed	
	*
	* @return								The number of removed device properties.
	*/
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	void RemoveDevicePropertyByHandle(const FInputDevicePropertyHandle HandleToRemove);

	/**
	* Remove a set of device properties based on their handles. 
	* 
	* @param HandlesToRemove	The set of device property handles to remove
	* 
	* @return					The number of removed device properties
	*/
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	void RemoveDevicePropertyHandles(const TSet<FInputDevicePropertyHandle>& HandlesToRemove);

	/** Removes all the current Input Device Properties that are active, regardless of the Platform User */
	UFUNCTION(BlueprintCallable, Category = "Input Devices")
	void RemoveAllDeviceProperties();

	/** Returns true if the given handle is valid */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Input Devices")
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
	* Set of currently active input device properties that will be evaluated on tick
	*/
	TSet<UInputDeviceSubsystem::FActiveDeviceProperty> ActiveProperties;

	/**
	 * Set of property handles the properties that are currently pending manual removal.
	 * This is populated by the "Remove device property" functions. 
	 */
	UPROPERTY(Transient)
	TSet<FInputDevicePropertyHandle> PropertiesPendingRemoval;
	
	/** A map of an input device to it's most recent hardware device identifier */
	TMap<FInputDeviceId, FHardwareDeviceIdentifier> LatestInputDeviceIdentifiers;

	/** A map of platform user's to their most recent hardware device identifier */
	TMap<FPlatformUserId, FHardwareDeviceIdentifier> LatestUserDeviceIdentifiers;

	/** An input processor that is used to determine the current hardware input device */
	TSharedPtr<class FInputDeviceSubsystemProcessor> InputPreprocessor;
};