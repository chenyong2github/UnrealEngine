// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/OutputRemap/DisplayClusterPostprocessOutputRemapFactory.h"
#include "PostProcess/OutputRemap/DisplayClusterPostprocessOutputRemap.h"

#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "DisplayClusterConfigurationTypes.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterPostProcessFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> FDisplayClusterPostprocessOutputRemapFactory::Create(const FString& PostProcessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostProcess)
{
	check(InConfigurationPostProcess != nullptr);

	UE_LOG(LogDisplayClusterPostprocessOutputRemap, Log, TEXT("Instantiating postprocess <%s>...id='%s'"), *InConfigurationPostProcess->Type, *PostProcessId);

	return MakeShared<FDisplayClusterPostprocessOutputRemap, ESPMode::ThreadSafe>(PostProcessId, InConfigurationPostProcess);
};
