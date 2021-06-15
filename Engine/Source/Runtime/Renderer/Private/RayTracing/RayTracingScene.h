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

	// Starts asynchronous creation of RayTracingSceneRHI.
	// This can be an expensive process, depending on the number of instances in the scene.
	// Immediately allocates GPU memory to fit at least the current number of instances and updates the SRV.
	// Returns the task that must be waited upon before accessing RayTracingSceneRHI.
	FGraphEventRef BeginCreate(FRDGBuilder& GraphBuilder);
	void WaitForTasks() const;

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

	// Returns true if RHI ray tracing scene has been created or asynchronous creation has been kicked.
	// i.e. returns true after BeginCreate() and before Reset().
	RENDERER_API bool IsCreated() const;

	// Waits for the async creation task if it's active and then returns RayTracingSceneRHI object (may return null).
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

	// Total flattened number of ray tracing geometry instances (a single FRayTracingGeometryInstance may represent many).
	uint32 NumNativeInstances = 0;

	// Geometries which still have a pending build request but are used this frame and require a force build.
	TArray<const FRayTracingGeometry*> GeometriesToBuild;

	FRayTracingAccelerationStructureSize SizeInfo = {};

	FRDGBufferRef BuildScratchBuffer;

private:
	// RHI object that abstracts mesh instnaces in this scene
	FRayTracingSceneRHIRef RayTracingSceneRHI;

	// Task that asynchronously creates RayTracingSceneRHI
	mutable FGraphEventRef CreateRayTracingSceneTask;

	// Persistently allocated buffer that holds the built TLAS
	FBufferRHIRef RayTracingSceneBuffer;

	// View for the TLAS buffer that should be used in ray tracing shaders
	FShaderResourceViewRHIRef RayTracingSceneSRV;

	// Transient memory allocator
	FMemStackBase Allocator;

};

#endif // RHI_RAYTRACING
