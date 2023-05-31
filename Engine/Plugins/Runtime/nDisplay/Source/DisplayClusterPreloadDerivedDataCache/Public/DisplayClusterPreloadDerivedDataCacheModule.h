// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

class FDisplayClusterPreloadDerivedDataCacheModule : public IModuleInterface
{
public:
	
	static FDisplayClusterPreloadDerivedDataCacheModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

protected:
	
	void CreateAsyncTaskWorker();
	TUniquePtr<class FDisplayClusterPreloadDerivedDataCacheWorker> AsyncTaskWorker;

private:
	
	void OnFEngineLoopInitComplete();
};
