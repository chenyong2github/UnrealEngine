// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkSource.h"
#include "LiveLinkXRConnectionSettings.h"
#include "LiveLinkXRSourceSettings.h"
#include "Roles/LiveLinkTransformTypes.h"

#include "Delegates/IDelegateInstance.h"
#include "MessageEndpoint.h"
#include "IMessageContext.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/Runnable.h"

#include "IXRTrackingSystem.h"		// for GEngine->XRSystem and EXRTrackedDeviceType

struct ULiveLinkXRSettings;

class ILiveLinkClient;

class LIVELINKXR_API FLiveLinkXRSource : public ILiveLinkSource, public FRunnable, public TSharedFromThis<FLiveLinkXRSource>
{
public:

	FLiveLinkXRSource(const FLiveLinkXRConnectionSettings& ConnectionSettings);

	virtual ~FLiveLinkXRSource();

	// Begin ILiveLinkSource Interface
	
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual void InitializeSettings(ULiveLinkSourceSettings* Settings) override;

	virtual bool IsSourceStillValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override { return SourceType; };
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override { return SourceStatus; }

	virtual TSubclassOf<ULiveLinkSourceSettings> GetSettingsClass() const override { return ULiveLinkXRSourceSettings::StaticClass(); }
	virtual void OnSettingsChanged(ULiveLinkSourceSettings* Settings, const FPropertyChangedEvent& PropertyChangedEvent) override;

	// End ILiveLinkSource Interface

	// Begin FRunnable Interface

	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	void Start();
	virtual void Stop() override;
	virtual void Exit() override { }

	// End FRunnable Interface

	void Send(FLiveLinkFrameDataStruct* FrameDataToSend, FName SubjectName);
	const FString GetDeviceTypeName(EXRTrackedDeviceType DeviceType);

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

	// Deferred start delegate handle.
	FDelegateHandle DeferredStartDelegateHandle;

	// frame counter for data
	int32 FrameCounter = 0;

	// Track all SteamVR tracker "pucks"
	bool bTrackTrackers = true;

	// Track all controllers
	bool bTrackControllers = false;

	// Track all HMDs
	bool bTrackHMDs = false;

	// Update rate (in Hz) at which to read the tracking data for each device
	uint32 LocalUpdateRateInHz;

	// Array of DeviceIDs for local SteamVR tracked devices
	TArray<int32> TrackedDevices;

	// Array of device types for local SteamVR tracked devices
	TArray<EXRTrackedDeviceType> TrackedDeviceTypes;

	// Array of Tracker Subject Names
	TArray<FString> TrackedSubjectNames;

	// Enumerate and save all connected and trackable XR devices
	void EnumerateTrackedDevices();
};
