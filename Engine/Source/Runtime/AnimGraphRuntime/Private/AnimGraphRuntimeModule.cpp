// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "UObject/UObjectIterator.h"
#include "Animation/AnimInstance.h"
#include "Animation/AttributeTypes.h"

//////////////////////////////////////////////////////////////////////////
// FAnimGraphRuntimeModule

class FAnimGraphRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([]()
		{			
			UE::Anim::AttributeTypes::Initialize();
		});
	}

	virtual void ShutdownModule() override
	{
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FAnimGraphRuntimeModule, AnimGraphRuntime);
