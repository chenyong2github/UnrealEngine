// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "UObject/UObjectIterator.h"
#include "Animation/AnimInstance.h"
#include "AnimGraphRuntimeTrace.h"

//////////////////////////////////////////////////////////////////////////
// FAnimGraphRuntimeModule

class FAnimGraphRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if ANIM_TRACE_ENABLED
		FAnimGraphRuntimeTrace::Init();
#endif
	}

	virtual void ShutdownModule() override
	{
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FAnimGraphRuntimeModule, AnimGraphRuntime);
