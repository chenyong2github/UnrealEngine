// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"
#include "ConcertTakeRecorderManager.h"
#include "ConcertTakeRecorderStyle.h"

/**
 * Module that adds multi user synchronization to take recorder.
 */
class FConcertTakeRecorderModule : public IModuleInterface
{
private:
	virtual void StartupModule() override
	{
		FConcertTakeRecorderStyle::Initialize();
		ConcertManager = MakeUnique<FConcertTakeRecorderManager>();
	}

	virtual void ShutdownModule() override
	{
		ConcertManager.Reset();
		FConcertTakeRecorderStyle::Shutdown();
	}

	virtual ~FConcertTakeRecorderModule() = default;


	TUniquePtr<FConcertTakeRecorderManager> ConcertManager;
};


IMPLEMENT_MODULE(FConcertTakeRecorderModule, ConcertTakeRecorder);
