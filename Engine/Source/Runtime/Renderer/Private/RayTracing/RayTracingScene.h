// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

#if RHI_RAYTRACING

#include "Async/TaskGraphInterfaces.h"
#include "RHI.h"
#include "RenderGraphResources.h"
#include "Misc/MemStack.h"
#include "Containers/ArrayView.h"
#include "PrimitiveSceneProxy.h"

class FGPUScene;
class FRHIRayTracingScene;
class FRHIShaderResourceView;
class FRayTracingGeometry;
class FRDGBuilder;

/**
* Persistent representation of the scene for ray tracing.
* Manages top level acceleration structure instances, memory and build process.
*/
class FRayTracingScene
{
public:

	FRayTracingScene();
	~FRayTracingScene();

	// Creates RayTracingSceneRHI.
	// Allocates GPU memory to fit at least the current number of instances.
	// Kicks off instance buffer build to parallel thread along with RDG pass
	void Create(FRDGBuilder& GraphBuilder, const FGPUScene& GPUScene);

	// Resets the instance list and reserves memory for this frame.
	void Reset();

	// Similar to Reset(), but also releases any persistent CPU and GPU memory allocations.
	void ResetAndReleaseResources();

	// Allocates temporary memory that will be valid until the next Reset().
	// Can be used to store temporary instance transforms, user data, etc.
	template <typename T>
	TArrayView<T> Allocate(int32 Count)
	{
		return MakeArrayView(new(Allocator) T[Count], Count);
	}

	// Returns true if RHI ray tracing scene has been created.
	// i.e. returns true after BeginCreate() and before Reset().
	RENDERER_API bool IsCreated() const;

	// Returns RayTracingSceneRHI object (may return null).
	RENDERER_API  FRHIRayTracingScene* GetRHIRayTracingScene() const;

	// Similar to GetRayTracingScene, but checks that ray tracing scene RHI object is valid.
	RENDERER_API  FRHIRayTracingScene* GetRHIRayTracingSceneChecked() const;

	// Returns Buffer and SRV for this ray tracing scene.
	// Valid to call immediately after BeginCreate() and does not block.
	RENDERER_API FRHIShaderResourceView* GetShaderResourceViewChecked() const;
	RENDERER_API FRHIBuffer* GetBufferChecked() const;

public:

	// Public members for initial refactoring step (previously were public members of FViewInfo).

	// Persistent storage for ray tracing instance descriptors.
	// Cleared every frame without releasing memory to avoid large heap allocations.
	// This must be filled before calling BeginCreateTask().
	TArray<FRayTracingGeometryInstance> Instances;

	uint32 NumTotalSegments = 0;

	// Geometries which still have a pending build request but are used this frame and require a force build.
	TArray<const FRayTracingGeometry*> GeometriesToBuild;

	// Used coarse mesh streaming handles during the last TLAS build
	TArray<Nanite::CoarseMeshStreamingHandle> UsedCoarseMeshStreamingHandles;

	FRayTracingAccelerationStructureSize SizeInfo = {};

	FRDGBufferRef InstanceBuffer;
	FRDGBufferRef BuildScratchBuffer;

private:
	void WaitForTasks() const;

	// RHI object that abstracts mesh instnaces in this scene
	FRayTracingSceneRHIRef RayTracingSceneRHI;

	// Persistently allocated buffer that holds the built TLAS
	FBufferRHIRef RayTracingSceneBuffer;

	// View for the TLAS buffer that should be used in ray tracing shaders
	FShaderResourceViewRHIRef RayTracingSceneSRV;

	// Transient memory allocator
	FMemStackBase Allocator;

	FBufferRHIRef InstanceUploadBuffer;
	FShaderResourceViewRHIRef InstanceUploadSRV;

	FBufferRHIRef TransformUploadBuffer;
	FShaderResourceViewRHIRef TransformUploadSRV;

	FByteAddressBuffer AccelerationStructureAddressesBuffer;

	mutable FGraphEventRef FillInstanceUploadBufferTask;

};

#endif // RHI_RAYTRACING
