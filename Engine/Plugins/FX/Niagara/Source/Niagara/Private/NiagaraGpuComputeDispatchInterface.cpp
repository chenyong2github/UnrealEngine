// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGpuComputeDispatch.h"
#include "NiagaraGpuComputeDebug.h"
#include "NiagaraGpuReadbackManager.h"

#include "Engine/World.h"
#include "SceneInterface.h"
#include "FXSystem.h"

FNiagaraGpuComputeDispatchInterface::FNiagaraGpuComputeDispatchInterface(EShaderPlatform InShaderPlatform, ERHIFeatureLevel::Type InFeatureLevel)
	: ShaderPlatform(InShaderPlatform)
	, FeatureLevel(InFeatureLevel)
	, GPUInstanceCounterManager(InFeatureLevel)
{
}

FNiagaraGpuComputeDispatchInterface::~FNiagaraGpuComputeDispatchInterface()
{
}

FNiagaraGpuComputeDispatchInterface* FNiagaraGpuComputeDispatchInterface::Get(UWorld* World)
{
	return World ? Get(World->Scene) : nullptr;
}

FNiagaraGpuComputeDispatchInterface* FNiagaraGpuComputeDispatchInterface::Get(FSceneInterface* Scene)
{
	return Scene ? Get(Scene->GetFXSystem()) : nullptr;
}

FNiagaraGpuComputeDispatchInterface* FNiagaraGpuComputeDispatchInterface::Get(FFXSystemInterface* FXSceneInterface)
{
	return FXSceneInterface ? static_cast<FNiagaraGpuComputeDispatchInterface*>(FXSceneInterface->GetInterface(FNiagaraGpuComputeDispatch::Name)) : nullptr;
}
