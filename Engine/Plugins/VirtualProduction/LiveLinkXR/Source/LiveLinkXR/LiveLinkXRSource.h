// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSource.h"
#include "LiveLinkSourceSettings.h"
#include "Roles/LiveLinkTransformTypes.h"

#include "Delegates/IDelegateInstance.h"
#include "MessageEndpoint.h"
#include "IMessageContext.h"
#include "HAL/ThreadSafeBool.h"

#include "IXRTrackingSystem.h"		// for GEngine->XRSystem and EXRTrackedDeviceType

#include "HAL/Runnable.h"

struct FLiveLinkXRSettings;

class ILiveLinkClient;

class LIVELINKXR_API FLiveLinkXRSource : public ILiveLinkSource, public FRunnable, public TSharedFromThis<FLiveLinkXRSource>
{
public:

	FLiveLinkXRSource();
	FLiveLinkXRSource(const FLiveLinkXRSettings& Settings);

	virtual ~FLiveLinkXRSource();

	// Begin ILiveLinkSource Interface
	
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;

	virtual bool IsSourceStillValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override { return SourceType; };
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override { return SourceStatus; }

	// End ILiveLinkSource Interface

	// Begin FRunnable Interface

	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	void Start();
	virtual void Stop() override;
	virtual void Exit() override { }

	// End FRunnable Interface

	void Send(TSharedRef<FLiveLinkTransformFrameData> FrameDataToSend, FName SubjectName);
	const FString GetDeviceTypeName(EXRTrackedDeviceType DeviceType);

private:

	// Enumerate tracked devices
	void EnumerateTrackedDevices();

private:
	ILiveLinkClient* Client;

	// Our identifier in LiveLink
	FGuid SourceGuid;

	FMessageAddress ConnectionAddress;

	FText SourceType;
	FText SourceMachineName;
	FText SourceStatus;
	
	// Threadsafe Bool for terminating the main thread loop
	FThreadSafeBool Stopping;
	
	// Thread to run socket operations on
	FRunnableThread* Thread;
	
	// Name of the sockets thread
	FString ThreadName;
	
	// List of subjects we've already encountered
	TSet<FName> EncounteredSubjects;

	// frame counter for data
	int32 FrameCounter;

	// Track all SteamVR tracker "pucks"
	bool bTrackTrackers;

	// Track all controllers
	bool bTrackControllers;

	// Track all HMDs
	bool bTrackHMDs;

	// Update rate (in Hz) at which to read the tracking data for each device
	uint32 LocalUpdateRateInHz;

	// Array of DeviceIDs for local SteamVR tracked devices
	TArray<int32> TrackedDevices;

	// Array of device types for local SteamVR tracked devices
	TArray<EXRTrackedDeviceType> TrackedDeviceTypes;

	// Array of Tracker Subject Names
	TArray<FString> TrackedSubjectNames;

	// Deferred start delegate handle.
	FDelegateHandle DeferredStartDelegateHandle;
};
