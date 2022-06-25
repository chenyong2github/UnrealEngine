// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/TextureShare/DisplayClusterPostprocessTextureShareFactory.h"
#include "PostProcess/TextureShare/DisplayClusterPostprocessTextureShare.h"
#include "Module/TextureShareDisplayClusterLog.h"

#include "DisplayClusterConfigurationTypes_Base.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterPostProcessTextureShareFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> FDisplayClusterPostProcessTextureShareFactory::Create(const FString& InPostProcessId, const FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess)
{
	check(InConfigurationPostProcess != nullptr);

	UE_LOG(LogTextureShareDisplayClusterPostProcess, Verbose, TEXT("Instantiating postprocess <%s> id='%s'"), *InConfigurationPostProcess->Type, *InPostProcessId);

	return  MakeShared<FDisplayClusterPostProcessTextureShare, ESPMode::ThreadSafe>(InPostProcessId, InConfigurationPostProcess);
}
