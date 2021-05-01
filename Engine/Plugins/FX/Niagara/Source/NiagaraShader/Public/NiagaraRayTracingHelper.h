// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "RHIDefinitions.h"

#if RHI_RAYTRACING

class FViewInfo;
class FRayTracingPipelineState;
class FRHICommandList;
class FRHIRayTracingScene;
class FRHIShaderResourceView;
class FRHIUniformBuffer;
class FRHIUnorderedAccessView;
struct FRWBuffer;

// c++ mirror of the struct defined in NiagaraRayTracingCommon.ush
struct FNiagaraRayTracingPayload
{
	float	HitT;            // Distance from ray origin to the intersection point in the ray direction. Negative on miss.
	float	WorldPosition[3];
	float	WorldNormal[3];
};

class NIAGARASHADER_API FNiagaraRayTracingHelper
{
	public:
		FNiagaraRayTracingHelper(EShaderPlatform InShaderPlatform)
		: ShaderPlatform(InShaderPlatform)
		{}

		FNiagaraRayTracingHelper() = delete;

		void Reset();
		void BuildRayTracingSceneInfo(FRHICommandList& RHICmdList, TConstArrayView<FViewInfo> Views);
		void IssueRayTraces(FRHICommandList& RHICmdList, const FIntPoint& RayTraceCounts, FRHIShaderResourceView* RayTraceRequests, FRWBuffer* IndirectArgsBuffer, uint32 IndirectArgsOffset, FRHIUnorderedAccessView* RayTraceResults) const;
		bool IsValid() const;

	private:
		const EShaderPlatform ShaderPlatform;

		mutable FRayTracingPipelineState* RayTracingPipelineState = nullptr;
		mutable FRHIRayTracingScene* RayTracingScene = nullptr;
		mutable FRHIShaderResourceView* RayTracingSceneView = nullptr;
		mutable FRHIUniformBuffer* ViewUniformBuffer = nullptr;
};

#endif
