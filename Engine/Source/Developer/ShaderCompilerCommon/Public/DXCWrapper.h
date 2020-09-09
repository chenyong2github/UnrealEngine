// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"

struct FDllHandle : public FRefCountedObject
{
protected:
	void* Handle = nullptr;

public:
	FDllHandle(const TCHAR* InFilename);
	virtual ~FDllHandle();
};

class SHADERCOMPILERCOMMON_API FDxcModuleWrapper
{
protected:
	TRefCountPtr<FDllHandle> Dxc;

public:
	FDxcModuleWrapper();
	virtual ~FDxcModuleWrapper();
};

class SHADERCOMPILERCOMMON_API FShaderConductorModuleWrapper : public FDxcModuleWrapper
{
protected:
	TRefCountPtr<FDllHandle> ShaderConductor;

public:
	FShaderConductorModuleWrapper();
	virtual ~FShaderConductorModuleWrapper();
};
