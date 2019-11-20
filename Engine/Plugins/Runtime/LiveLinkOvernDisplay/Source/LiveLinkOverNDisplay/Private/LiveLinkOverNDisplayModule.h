// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once


#include "ILiveLinkOverNDisplayModule.h"
#include "LiveLinkVirtualSubject.h"


class FNDisplayLiveLinkSubjectReplicator;


/**
 * Entry point for LiveLinkOverNDisplay functionnality. 
 */
class FLiveLinkOverNDisplayModule : public ILiveLinkOverNDisplayModule, public TSharedFromThis<FLiveLinkOverNDisplayModule>
{
public:

	FLiveLinkOverNDisplayModule();

	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

	virtual FNDisplayLiveLinkSubjectReplicator& GetSubjectReplicator() override;

private:

	void OnEngineLoopInitComplete();

private:

	/** Cluster event listener delegate */
	FDelegateHandle OnEngineLoopInitCompleteDelegate;

	/** Console commands handle. */
	TUniquePtr<FAutoConsoleCommand> StopMessageSyncCommand;

	/** Replicator SyncObject used to transfer data across all cluster machines */
	TUniquePtr<FNDisplayLiveLinkSubjectReplicator> LiveLinkReplicator;
};
