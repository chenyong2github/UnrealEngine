// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AppleARKitFaceSupport.h"
#include "AppleARKitLiveLinkSourceFactory.h"
#include "AppleARKitConversion.h"


class APPLEARKITFACESUPPORT_API FAppleARKitFaceSupport :
	public IAppleARKitFaceSupport,
	public FSelfRegisteringExec,
	public TSharedFromThis<FAppleARKitFaceSupport, ESPMode::ThreadSafe>
{
public:
	FAppleARKitFaceSupport();
	virtual ~FAppleARKitFaceSupport();

	void Init();
	void Shutdown();

private:
#if SUPPORTS_ARKIT_1_0
	// ~IAppleARKitFaceSupport
	virtual TArray<TSharedPtr<FAppleARKitAnchorData>> MakeAnchorData(NSArray<ARAnchor*>* NewAnchors, const FRotator& AdjustBy, EARFaceTrackingUpdate UpdateSetting) override;
	virtual ARConfiguration* ToARConfiguration(UARSessionConfig* InSessionConfig) override;
	virtual void PublishLiveLinkData(TSharedPtr<FAppleARKitAnchorData> Anchor) override;
	virtual bool DoesSupportFaceAR() override;
#if SUPPORTS_ARKIT_1_5
	virtual TArray<FARVideoFormat> ToARConfiguration() override;
#endif
	// ~IAppleARKitFaceSupport

	/** Publishes the remote publisher and the file writer if present */
	void ProcessRealTimePublishers(TSharedPtr<FAppleARKitAnchorData> AnchorData);
#endif
	/** Inits the real time providers if needed */
	void InitRealtimeProviders();

	//~ FSelfRegisteringExec
	virtual bool Exec(UWorld*, const TCHAR* Cmd, FOutputDevice& Ar) override;
	//~ FSelfRegisteringExec

	/** Whether the face data is mirrored or not */
	bool bFaceMirrored;
	/** If requested, publishes face ar updates to LiveLink for the animation system to use */
	TSharedPtr<ILiveLinkSourceARKit> LiveLinkSource;
	/** Copied from the UARSessionConfig project settings object */
	FName FaceTrackingLiveLinkSubjectName;
	/** The id of this device */
	FName LocalDeviceId;
	/** A publisher that sends to a remote machine */
	TSharedPtr<IARKitBlendShapePublisher, ESPMode::ThreadSafe> RemoteLiveLinkPublisher;
	/** A publisher that writes the data to disk */
	TSharedPtr<IARKitBlendShapePublisher, ESPMode::ThreadSafe> LiveLinkFileWriter;
};

