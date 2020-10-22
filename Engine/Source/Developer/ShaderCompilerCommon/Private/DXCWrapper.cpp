// Copyright Epic Games, Inc. All Rights Reserved.

#include "DXCWrapper.h"
#include "HAL/PlatformProcess.h"
#include "Templates/RefCounting.h"
#include "Misc/Paths.h"

static TRefCountPtr<FDllHandle> GDxcHandle;
static TRefCountPtr<FDllHandle> GShaderConductorHandle;

FDllHandle::FDllHandle(const TCHAR* InFilename)
{
#if PLATFORM_WINDOWS
	check(InFilename && *InFilename);
	FString ShaderConductorDir = FPaths::EngineDir() / TEXT("Binaries/ThirdParty/ShaderConductor/Win64");
	FString ModulePath = ShaderConductorDir / InFilename;
	Handle = FPlatformProcess::GetDllHandle(*ModulePath);
	checkf(Handle, TEXT("Failed to load module: %s"), *ModulePath);
#endif
}

FDllHandle::~FDllHandle()
{
	if (Handle)
	{
		FPlatformProcess::FreeDllHandle(Handle);
		Handle = nullptr;
	}
}


FDxcModuleWrapper::FDxcModuleWrapper()
{
	if (GDxcHandle.GetRefCount() == 0)
	{
		GDxcHandle = new FDllHandle(TEXT("dxcompiler.dll"));
	}
	else
	{
		GDxcHandle->AddRef();
	}
}

FDxcModuleWrapper::~FDxcModuleWrapper()
{
	GDxcHandle.SafeRelease();
}


FShaderConductorModuleWrapper::FShaderConductorModuleWrapper()
{
	if (GShaderConductorHandle.GetRefCount() == 0)
	{
		GShaderConductorHandle = new FDllHandle(TEXT("ShaderConductor.dll"));
	}
	else
	{
		GShaderConductorHandle->AddRef();
	}
}

FShaderConductorModuleWrapper::~FShaderConductorModuleWrapper()
{
	GShaderConductorHandle.SafeRelease();
}
