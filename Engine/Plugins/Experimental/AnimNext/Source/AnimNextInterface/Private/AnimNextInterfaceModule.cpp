// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "AnimNextInterfaceParam.h"
#include "AnimationDataRegistry.h"

namespace UE::AnimNext::Interface
{

class FModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
		{
			FParamType::FRegistrar::RegisterDeferredTypes();

			UE::AnimNext::Interface::FAnimationDataRegistry::Init();
		});

		FCoreDelegates::OnEnginePreExit.AddLambda([]()
		{
			UE::AnimNext::Interface::FAnimationDataRegistry::Destroy();
		});
		
	}
};

}

IMPLEMENT_MODULE(UE::AnimNext::Interface::FModule, AnimNextInterface)