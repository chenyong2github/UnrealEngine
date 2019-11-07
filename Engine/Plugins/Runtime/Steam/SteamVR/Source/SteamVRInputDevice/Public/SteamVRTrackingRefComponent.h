// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Runtime/Engine/Classes/Engine/StaticMesh.h"
#include "Components/ActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "SteamVRTrackingRefComponent.generated.h"

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FComponentTrackingActivatedSignature, int32, DeviceID, FName, DeviceClass, FString, DeviceModel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FComponentTrackingDeactivatedSignature, int32, DeviceID, FName, DeviceClass, FString, DeviceModel);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class STEAMVRINPUTDEVICE_API USteamVRTrackingReferences : public UActorComponent
{
	GENERATED_BODY()

public:	
	USteamVRTrackingReferences();

	/** Blueprint event - When a new active device is recognized */
	UPROPERTY(BlueprintAssignable, Category = "VR")
	FComponentTrackingActivatedSignature OnTrackedDeviceActivated;

	/** When an active device gets deactivated */
	UPROPERTY(BlueprintAssignable, Category = "VR")
	FComponentTrackingDeactivatedSignature OnTrackedDeviceDeactivated;

	// TODO: Set default mesh to SteamVR provided render model. Must be backwards compatible to UE4.15

	/** Display Tracking References in-world */
	UFUNCTION(BlueprintCallable, Category = "SteamVR Input")
	bool ShowTrackingReferences(UStaticMesh* TrackingReferenceMesh);

	/** Remove Tracking References in-world */
	UFUNCTION(BlueprintCallable, Category = "SteamVR Input")
	void HideTrackingReferences();

	/** Scale to apply to the tracking reference mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SteamVR Input")
	float ActiveDevicePollFrequency = 1.f;

	/** Scale to apply to the tracking reference mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SteamVR Input")
	FVector TrackingReferenceScale = FVector(1.f);

	/** Currently displayed Tracking References in-world */
	UPROPERTY(BlueprintReadOnly, Category = "SteamVR Input")
	TArray<UStaticMeshComponent*> TrackingReferences;

private:
	/** Represents a tracked device with a flag on whether its activated or not */
	struct FActiveTrackedDevice
	{
		unsigned int		id;		// The SteamVR id of this device
		bool		bActivated;		// Whether or not this device has been activated

		FActiveTrackedDevice(unsigned int inId, bool inActivated)
			: id(inId)
			, bActivated(inActivated)
		{}

	};

	/** Cache for current delta time */
	float CurrentDeltaTime = 0.f;

	/** Cache to hold tracked devices registered in SteamVR */
	TArray<FActiveTrackedDevice> ActiveTrackingDevices;

protected:
	virtual void BeginPlay() override;	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	FName GetDeviceClass(unsigned int id);
	bool FindTrackedDevice(unsigned int id);
};
