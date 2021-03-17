// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderGraphResources.h"
#include "SceneManagement.h"
#include "../Nanite/NaniteRender.h"

class FGPUScene;
class FInstanceCullingContext;

class FInstanceCullingIntermediate
{
public:
	/**
	 * One bit per Instance per registered view produced by CullInstances.
	 */
	// GPUCULL_TODO: Currently external buffers since the instance cull happens early and the one-true RDGBuilder isn't created yet
	//TRefCountPtr<FRDGPooledBuffer> VisibleInstanceFlags;
	FRDGBufferRef VisibleInstanceFlags = nullptr;
	
	/**
	 * Write offset used by all the instance ID expand passes to allocate space in the global instance Id buffer.
	 * Initialized to zero by CullInstances.
	 */
	// GPUCULL_TODO: Currently external buffers since the instance cull happens early and the one-true RDGBuilder isn't created yet
	//TRefCountPtr<FRDGPooledBuffer> InstanceIdOutOffsetBuffer;
	FRDGBufferRef InstanceIdOutOffsetBuffer = nullptr;
	int32 NumInstances = 0;
	int32 NumViews = 0;
};

struct FInstanceCullingResult
{
	//TRefCountPtr<FRDGPooledBuffer> InstanceIdsBuffer;
	FRDGBufferRef DrawIndirectArgsBuffer = nullptr;
	FRDGBufferRef InstanceIdOffsetBuffer = nullptr;

	//FRHIBuffer* GetDrawIndirectArgsBufferRHI() const { return DrawIndirectArgsBuffer.IsValid() ? DrawIndirectArgsBuffer->GetVertexBufferRHI() : nullptr; }
	//FRHIBuffer* GetInstanceIdOffsetBufferRHI() const { return InstanceIdOffsetBuffer.IsValid() ? InstanceIdOffsetBuffer->GetVertexBufferRHI() : nullptr; }
	void GetDrawParameters(FInstanceCullingDrawParams &OutParams) const
	{
		// GPUCULL_TODO: Maybe get dummy buffers?
		OutParams.DrawIndirectArgsBuffer = DrawIndirectArgsBuffer;
		OutParams.InstanceIdOffsetBuffer = InstanceIdOffsetBuffer;
	}

	static void CondGetDrawParameters(const FInstanceCullingResult* InstanceCullingResult, FInstanceCullingDrawParams& OutParams)
	{
		if (InstanceCullingResult)
		{
			InstanceCullingResult->GetDrawParameters(OutParams);
		}
		else
		{
			OutParams.DrawIndirectArgsBuffer = nullptr;
			OutParams.InstanceIdOffsetBuffer = nullptr;
		}
	}
};

/**
 * Manages allocation of indirect arguments and culling jobs for all instanced draws (use the GPU Scene culling).
 */
class FInstanceCullingManager
{
public:
	~FInstanceCullingManager();

	bool IsEnabled() const { return bIsEnabled; }

	// Max average number of instances that primitives are expanded to. GPUCULL_TODO: Not very robust
	static constexpr uint32 MaxAverageInstanceFactor = 128;

	// Register a view for culling, returns integer ID of the view.
	int32 RegisterView(const Nanite::FPackedViewParams& Params);
	
	// Helper to translate from view info, extracts the needed data for setting up instance culling.
	int32 RegisterView(const FViewInfo& ViewInfo);

	/**
	 * Run:
	 *   AFTER views have been initialized and registered (including shadow views), 
	 *   AFTER after GPUScene is updated, but
	 *   BEFORE rendering commands are submitted.
	 */
	void CullInstances(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene);

	FInstanceCullingManager(FInstanceCullingManagerResources& InResources, bool bInIsEnabled)
	: Resources(InResources),
		bIsEnabled(bInIsEnabled)
	{
	}

	// Populated by CullInstances, used when performing final culling & rendering 
	FInstanceCullingIntermediate CullingIntermediate;

private:
	FInstanceCullingManager() = delete;
	FInstanceCullingManager(FInstanceCullingManager &) = delete;

	FInstanceCullingManagerResources& Resources;
	TArray<Nanite::FPackedView> CullingViews;
	bool bIsEnabled;
};

