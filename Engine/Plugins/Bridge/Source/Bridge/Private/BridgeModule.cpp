// Copyright Epic Games, Inc. All Rights Reserved.
#include "IBridgeModule.h"
#include "UI/BridgeUIManager.h"
#include "Misc/Paths.h"
#include "CoreMinimal.h"
#include "NodeProcess.h"

#define LOCTEXT_NAMESPACE "Bridge"

class FBridgeModule : public IBridgeModule
{
public:
	virtual void StartupModule() override
	{
		if (GIsEditor && !IsRunningCommandlet())
		{
			 FBridgeUIManager::Initialize();
		}
	}

	virtual void ShutdownModule() override
	{
		// TODO: Do some clean up (if required)
	}
};

IMPLEMENT_MODULE(FBridgeModule, Bridge);

#undef LOCTEXT_NAMESPACE
