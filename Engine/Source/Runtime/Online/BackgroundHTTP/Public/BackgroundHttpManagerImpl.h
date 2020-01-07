// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IBackgroundHttpManager.h"
#include "Interfaces/IBackgroundHttpResponse.h"

#include "Containers/Ticker.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBackgroundHttpManager, Log, All)

/**
 * Contains implementation of some common functions that don't vary between implementation
 */
class FBackgroundHttpManagerImpl 
	: public IBackgroundHttpManager
	, public FTickerObjectBase
{
public:
	FBackgroundHttpManagerImpl();
	virtual ~FBackgroundHttpManagerImpl();

	virtual void AddRequest(const FBackgroundHttpRequestPtr Request) override;
	virtual void RemoveRequest(const FBackgroundHttpRequestPtr Request) override;

	virtual void Initialize() override;
	virtual void Shutdown() override;

	virtual void CleanUpTemporaryFiles() override;

	virtual int GetMaxActiveDownloads() const override;
	virtual void SetMaxActiveDownloads(int MaxActiveDownloads) override;

	//FTickerObjectBase implementation
	virtual bool Tick(float DeltaTime) override;

protected:
	virtual bool AssociateWithAnyExistingRequest(const FBackgroundHttpRequestPtr Request) override;

	virtual bool CheckForExistingCompletedDownload(const FBackgroundHttpRequestPtr Request, FString& ExistingFilePathOut, int64& ExistingFileSizeOut);
	virtual void ActivatePendingRequests();

	virtual void ClearAnyTempFilesFromTimeOut();
protected:

	/** List of Background Http requests that we have called AddRequest on, but have not yet started due to platform active download limits **/
	TArray<FBackgroundHttpRequestPtr> PendingStartRequests;
	FRWLock PendingRequestLock;

	/** List of Background Http requests that are actively being processed **/
	TArray<FBackgroundHttpRequestPtr> ActiveRequests;
	FRWLock ActiveRequestLock;

	/** Count of how many requests we have active **/
	volatile int NumCurrentlyActiveRequests;
	TAtomic<int> MaxActiveDownloads;
};
