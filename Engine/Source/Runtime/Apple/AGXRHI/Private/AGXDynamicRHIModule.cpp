// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	AGXDynamicRHIModule.cpp: AGX Dynamic RHI Module Class Implementation.
==============================================================================*/


#include "AGXLLM.h"
#include "DynamicRHI.h"
#include "AGXDynamicRHIModule.h"
#include "Modules/ModuleManager.h"


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Module Implementation


IMPLEMENT_MODULE(FAGXDynamicRHIModule, AGXRHI);


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Module Class Methods


bool FAGXDynamicRHIModule::IsSupported()
{
	return true;
}

FDynamicRHI* FAGXDynamicRHIModule::CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel)
{
	LLM(AGXLLM::Initialise());
	return new FAGXDynamicRHI(RequestedFeatureLevel);
}
