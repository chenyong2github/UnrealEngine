// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "ComputeGraph.generated.h"

class FComputeKernelResource;
class UComputeDataInterface;
class UComputeKernel;

/** Compute Kernel compilation flags. */
enum class EComputeKernelCompilationFlags
{
	None = 0,

	/* Force recompilation even if kernel is not dirty and/or DDC data is available. */
	Force = 1 << 0,

	/* Compile the shader while blocking the main thread. */
	Synchronous = 1 << 1,

	/* Replaces all instances of the shader with the newly compiled version. */
	ApplyCompletedShaderMapForRendering = 1 << 2,

	IsCooking = 1 << 3,
};

/** 
 * Description of a single edge in a UComputeGraph. 
 * todo[CF]: Consider better storage for graph data structure that is easier to interrogate efficiently.
 */
USTRUCT()
struct FComputeGraphEdge
{
	GENERATED_BODY()

	bool bKernelInput;
	int32 KernelIndex;
	int32 KernelBindingIndex;
	int32 DataInterfaceIndex;
	int32 DataInterfaceBindingIndex;
};

/** 
 * Class representing a Compute Graph.
 * This holds the basic topology of the graph and is responsible for linking Kernels with Data Interfaces and compiling the resulting shader code.
 * Multiple Compute Graph asset types can derive from this to specialize the graph creation process. 
 * For example the Animation Deformer system provides a UI for creating UComputeGraph assets.
 */
UCLASS()
class ENGINE_API UComputeGraph : public UObject
{
	GENERATED_BODY()

protected:
	/** Kernels in the graph. */
	UPROPERTY()
	TArray< TObjectPtr<UComputeKernel> > KernelInvocations;

	/** Data interfaces in the graph. */
	UPROPERTY()
	TArray< TObjectPtr<UComputeDataInterface> > DataInterfaces;

	/** Edges in the graph between kernels and data interfaces. */
	UPROPERTY()
	TArray<FComputeGraphEdge> GraphEdges;

	/** 
	 * Kernel resources that hold the compiled shader resources.
	 * This is stored with the same indexing as the KernelInvocations array. 
	 */
	TArray< TUniquePtr<FComputeKernelResource> > KernelResources;

public:
	UComputeGraph(const FObjectInitializer& ObjectInitializer);
	UComputeGraph(FVTableHelper& Helper);
	virtual ~UComputeGraph();
	
	/** Get the kernel instances in the graph. Note that it is valid for the array to have holes in it. */
	TArray< TObjectPtr<UComputeKernel> > const& GetKernelInvocations() const { return KernelInvocations; }
	/** Get the kernel resources in the graph. This array is in sync with the one returned by GetKernelInvocations(). */
	TArray< TUniquePtr<FComputeKernelResource> > const& GetKernelResources() const { return KernelResources; }

	/** Returns true if graph is valid. A valid graph should be guaranteed to compile, assuming the underlying shader code is well formed. */
	bool ValidateGraph(FString* OutErrors = nullptr);

	/**
	 * Get unique data interface id.
	 * This is just a string containing the index of the data interface in UComputeGraph::DataInterfaces.
	 * It is used as a prefix to disambiguate shader code etc.
	 * This function permanently allocates the UID on first use so that returned TCHAR pointers can be held by structures with long lifetimes.
	 */
	static TCHAR const* GetDataInterfaceUID(int32 DataInterfaceIndex);

protected:
	//~ Begin UObject Interface.
	void PostLoad() override;
	//~ End UObject Interface.

#if WITH_EDITOR
	/** Triggers compilation of all kernels in the graph. */
	void CacheResourceShadersForRendering();
	void CacheResourceShadersForRendering(uint32 CompilationFlags);

	/** Trigger compilation of a specific kernel. */
	static void CacheShadersForResource(
		EShaderPlatform ShaderPlatform,
		const ITargetPlatform* TargetPlatform,
		uint32 CompilationFlags,
		FComputeKernelResource* Kernel
	);
#endif
};
