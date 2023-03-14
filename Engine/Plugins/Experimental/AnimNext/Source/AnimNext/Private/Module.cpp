// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "DataRegistry.h"

namespace UE::AnimNext
{

class FModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
		{
			FDataRegistry::Init();
		});

		FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			FDataRegistry::Destroy();
		});
		
	}
};

}

IMPLEMENT_MODULE(UE::AnimNext::FModule, AnimNext)