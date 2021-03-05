// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"

#include "LiveLinkPrestonMDRSourceSettings.h"
#include "LiveLinkPrestonMDRConnectionSettings.h"

#include "PrestonMDRMessageThread.h"

#include <atomic>

class ISocketSubsystem;

class LIVELINKPRESTONMDR_API FLiveLinkPrestonMDRSource : public ILiveLinkSource
{
public:

	FLiveLinkPrestonMDRSource(FLiveLinkPrestonMDRConnectionSettings ConnectionSettings);
	~FLiveLinkPrestonMDRSource();

	// Begin ILiveLinkSource Implementation
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;

	virtual bool IsSourceStillValid() const override;

	virtual bool RequestSourceShutdown() override;

	virtual FText GetSourceType() const override;
	virtual FText GetSourceMachineName() const override { return SourceMachineName; }
	virtual FText GetSourceStatus() const override;

	virtual TSubclassOf<ULiveLinkSourceSettings> GetSettingsClass() const { return ULiveLinkPrestonMDRSourceSettings::StaticClass(); }
	// End ILiveLinkSourceImplementation

private:
	void OpenConnection();
	void OnFrameDataReady_AnyThread(FLensDataPacket InData);
	void OnStatusChanged_AnyThread(FMDR3Status InStatus);
	void OnConnectionLost_AnyThread();
	void OnConnectionFailed_AnyThread();
	void UpdateStaticData_AnyThread();

	ILiveLinkClient* Client = nullptr;
	FSocket* Socket = nullptr;
	ISocketSubsystem* SocketSubsystem = nullptr;

	FLiveLinkPrestonMDRConnectionSettings ConnectionSettings;
	FLiveLinkSubjectKey SubjectKey;
	FText SourceMachineName;

	TUniquePtr<FPrestonMDRMessageThread> MessageThread;

	std::atomic<double> LastTimeDataReceived;

	std::atomic<bool> bIsConnectedToDevice;
	std::atomic<bool> bFailedToConnectToDevice;

	FMDR3Status LatestMDRStatus;

private:
	const float DataReceivedTimeout = 1.0f;
};