// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "AnimationDataRegistry.h"

namespace UE::AnimNext
{

class FModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
		{
			UE::AnimNext::FAnimationDataRegistry::Init();
		});

		FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			UE::AnimNext::FAnimationDataRegistry::Destroy();
		});
		
	}
};

}

IMPLEMENT_MODULE(UE::AnimNext::FModule, AnimNext)