// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NiagaraAsyncGpuTraceProvider.h"

#if RHI_RAYTRACING

class FNiagaraAsyncGpuTraceProviderHwrt : public FNiagaraAsyncGpuTraceProvider
{
public:
	static const EProviderType Type;

	FNiagaraAsyncGpuTraceProviderHwrt(EShaderPlatform InShaderPlatform, FNiagaraGpuComputeDispatchInterface* Dispatcher);

	static bool IsSupported();
	virtual void PostRenderOpaque(FRHICommandList& RHICmdList, TConstStridedView<FSceneView> Views, FCollisionGroupHashMap* CollisionGroupHash) override;
	virtual bool IsAvailable() const override;
	virtual void IssueTraces(FRHICommandList& RHICmdList, const FDispatchRequest& Request, FCollisionGroupHashMap* CollisionGroupHash) override;
	virtual void Reset() override;
	virtual EProviderType GetType() const override { return Type; }

private:
	FRayTracingPipelineState* RayTracingPipelineState = nullptr;
	FRHIRayTracingScene* RayTracingScene = nullptr;
	FShaderResourceViewRHIRef RayTracingSceneView = nullptr;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
};

#endif