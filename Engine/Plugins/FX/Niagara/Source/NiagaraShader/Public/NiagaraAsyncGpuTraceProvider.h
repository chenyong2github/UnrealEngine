// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RHIDefinitions.h"
#include "RHIUtilities.h"

class FPrimitiveComponentId;
class FSceneInterface;
namespace ENDICollisionQuery_AsyncGpuTraceProvider { enum Type : int; }

class FNiagaraGpuComputeDispatchInterface;
class FScene;
class FSceneView;

// mirrors structure in Engine\Plugins\FX\Niagara\Shaders\Private\NiagaraAsyncGpuTraceCommon.ush
struct FNiagaraAsyncGpuTrace
{
	float Origin[3];
	float TFar;
	float Direction[3];
	uint32 CollisionGroup;
};

// mirrors structure in Engine\Plugins\FX\Niagara\Shaders\Private\NiagaraAsyncGpuTraceCommon.ush
struct FNiagaraAsyncGpuTraceResult
{
	float WorldPosition[3];
	float HitT;
	float WorldNormal[3];
	float _Pad0; // padding to force 16 byte alignment to meet requirements for VK
};

class NIAGARASHADER_API FNiagaraAsyncGpuTraceProvider
{
public:
	using EProviderType = ENDICollisionQuery_AsyncGpuTraceProvider::Type;
	using FProviderPriorityArray = TArray<TEnumAsByte<EProviderType>>;

	FNiagaraAsyncGpuTraceProvider(EShaderPlatform InShaderPlatform, FNiagaraGpuComputeDispatchInterface* InDispatcher)
		: ShaderPlatform(InShaderPlatform)
		, Dispatcher(InDispatcher)
	{
	}

	FNiagaraAsyncGpuTraceProvider() = delete;
	virtual ~FNiagaraAsyncGpuTraceProvider() = default;

	struct FDispatchRequest
	{
		FRWBufferStructured* TracesBuffer = nullptr;
		FRWBufferStructured* ResultsBuffer = nullptr;
		FRWBuffer* TraceCountsBuffer = nullptr;
		uint32 TracesOffset = 0;
		uint32 ResultsOffset = 0;
		uint32 TraceCountsOffset = 0;
		uint32 MaxTraceCount = 0;
		uint32 MaxRetraceCount = 0;
	};

	static EProviderType ResolveSupportedType(EProviderType InType, const FProviderPriorityArray& Priorities);
	static bool RequiresDistanceFieldData(EProviderType InType, const FProviderPriorityArray& Priorities);
	static bool RequiresRayTracingScene(EProviderType InType, const FProviderPriorityArray& Priorities);

	/** Hash table.
		PrimIdHashTable is the main hash table that maps GPUSceneInstanceIndex to and Index we can use to store Collision Groups inside HashToCollisionGroups.
	*/
	struct FCollisionGroupHashMap
	{
		FRWBufferStructured PrimIdHashTable;
		FRWBuffer HashToCollisionGroups;
		uint32 HashTableSize = 0;
	};

	static void BuildCollisionGroupHashMap(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FSceneInterface* Scene, const TMap<FPrimitiveComponentId, uint32>& CollisionGroupMap, FCollisionGroupHashMap& Result);

	static TArray<TUniquePtr<FNiagaraAsyncGpuTraceProvider>> CreateSupportedProviders(EShaderPlatform ShaderPlatform, FNiagaraGpuComputeDispatchInterface* Dispatcher, const FProviderPriorityArray& Priorities);
	static void ClearResults(FRHICommandList& RHICmdList, EShaderPlatform ShaderPlatform, const FDispatchRequest& Request);

	virtual bool IsAvailable() const = 0;
	virtual EProviderType GetType() const = 0;

	virtual void PostRenderOpaque(FRHICommandList& RHICmdList, TConstStridedView<FSceneView> Views, FCollisionGroupHashMap* CollisionGroupHash);

	virtual void IssueTraces(FRHICommandList& RHICmdList, const FDispatchRequest& Request, FCollisionGroupHashMap* CollisionGroupHash)
	{
	}

	virtual void Reset()
	{
	}

	const EShaderPlatform ShaderPlatform;
	FNiagaraGpuComputeDispatchInterface* Dispatcher;

private:
	UE_NONCOPYABLE(FNiagaraAsyncGpuTraceProvider);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_3
#include "CoreMinimal.h"
#include "NiagaraSettings.h"
#endif
