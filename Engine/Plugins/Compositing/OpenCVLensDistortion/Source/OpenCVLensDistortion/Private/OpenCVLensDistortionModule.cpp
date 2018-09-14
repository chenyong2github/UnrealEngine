// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "IOpenCVLensDistortionModule.h"

#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"


DEFINE_LOG_CATEGORY(LogOpenCVLensDistortion)

//////////////////////////////////////////////////////////////////////////
// FOpenCVLensDistortionModule
class FOpenCVLensDistortionModule : public IOpenCVLensDistortionModule
{

};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FOpenCVLensDistortionModule, OpenCVLensDistortion);


void IOpenCVLensDistortionModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("OpenCVLensDistortion"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/OpenCVLensDistortion"), PluginShaderDir);
}
