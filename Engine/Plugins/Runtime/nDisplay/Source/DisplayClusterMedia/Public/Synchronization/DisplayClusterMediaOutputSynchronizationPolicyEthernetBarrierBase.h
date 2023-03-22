// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterConfigurationTypes_Media.h"

#include "Cluster/IDisplayClusterGenericBarriersClient.h"

#include "DisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase.generated.h"


/*
 * Base class for Ethernet barrier based media synchronization policies.
 * 
 * It encapsulates network barriers related logic.
 */
UCLASS(Abstract, editinlinenew, BlueprintType, hidecategories = (Object))
class DISPLAYCLUSTERMEDIA_API UDisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase
	: public UDisplayClusterMediaOutputSynchronizationPolicy
{
	GENERATED_BODY()

public:
	/** Starts synchronization of specific capture device. Returns false if failed. */
	virtual bool StartSynchronization(UMediaCapture* MediaCapture, const FString& MediaId) override;

	/** Stops synchronization of specific output stream (capture device). */
	virtual void StopSynchronization() override;

	/** Returns true if currently synchronising a media output. */
	virtual bool IsRunning() override final;

protected:
	/** Synchronizes calling thread at the barrier. */
	void SyncThreadOnBarrier();

	/** Children implement their own sync approaches. */
	virtual void Synchronize() PURE_VIRTUAL(UDisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase::Synchronize, )

protected:
	/** Capture device being used. */
	UPROPERTY(Transient)
	TObjectPtr<UMediaCapture> CapturingDevice;

	/** Barrier timeout (ms) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (DisplayName = "Barrier Timeout (ms)", ClampMin = "1", ClampMax = "10000", UIMin = "1", UIMax = "10000"))
	int32 BarrierTimeoutMs = 3000;

protected:
	/** Returns media device ID being synchronized */
	FString GetMediaDeviceId() const;

private:
	/** Initializes dynamic barrier on the primary node. */
	bool InitializeBarrier(const FString& SyncInstanceId);

	/** Releases dynamic barrier on the primary node. */
	void ReleaseBarrier();

	/** Generates name of the dynamic barrier. */
	FString GenerateBarrierName() const;

	/** Generates array of thread markers that are going to use a barrier. */
	void GenerateListOfThreadMarkers(TArray<FString>& OutMarkers) const;

	/** Handles media capture sync callbacks. */
	void ProcessMediaSynchronizationCallback();

private:
	/** Is synchronization currently active. */
	bool bIsRunning = false;

	/** ID of media device being synchronized. */
	FString MediaDeviceId;

	/** Unique barrier name to use. */
	FString BarrierId;

	/** Unique thread (caller) marker to be used on the barrier. */
	FString ThreadMarker;

	/** Barrier sync client. */
	TUniquePtr<IDisplayClusterGenericBarriersClient> EthernetBarrierClient;
};
