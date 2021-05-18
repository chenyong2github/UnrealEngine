// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"
#include "ComputeGraph.generated.h"

class FArchive;
class FComputeKernelResource;
class FShaderParametersMetadata;
class ITargetPlatform;
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

	UPROPERTY()
	bool bKernelInput;
	UPROPERTY()
	int32 KernelIndex;
	UPROPERTY()
	int32 KernelBindingIndex;
	UPROPERTY()
	int32 DataInterfaceIndex;
	UPROPERTY()
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

public:
	UComputeGraph(const FObjectInitializer& ObjectInitializer);
	UComputeGraph(FVTableHelper& Helper);
	virtual ~UComputeGraph();

	/** 
	 * Returns true if graph is valid. 
	 * A valid graph should be guaranteed to compile, assuming the underlying shader code is well formed. 
	 */
	bool ValidateGraph(FString* OutErrors = nullptr);

	/** Get the number of kernel slots in the graph. Note that some of these kernel slots may be empty due to fragmentation in graph edition. */
	int32 GetNumKernelInvocations() const { return KernelInvocations.Num(); }
	
	/** Get the nth kernel in the graph. Note that it is valid to return nullptr here. */
	UComputeKernel const* GetKernelInvocation(int32 Index) const { return KernelInvocations[Index]; }
	
	/** Get the resource object for the nth kernel in the graph. Note that it is valid to return nullptr here. */
	FComputeKernelResource const* GetKernelResource(int32 Index) const { return KernelResources[Index].Get(); }

	/** Get the shader metadata for the nth kernel in the graph. Note that it is valid to return nullptr here. */
	FShaderParametersMetadata* GetKernelShaderMetadata(int32 Index) const { return ShaderMetadatas[Index]; }

	/**
	 * Get unique data interface id.
	 * This is just a string containing the index of the data interface in UComputeGraph::DataInterfaces.
	 * It is used as a prefix to disambiguate shader code etc.
	 * This function permanently allocates the UID on first use so that returned TCHAR pointers can be held by structures with long lifetimes.
	 */
	static TCHAR const* GetDataInterfaceUID(int32 DataInterfaceIndex);

protected:
	//~ Begin UObject Interface.
	void Serialize(FArchive& Ar) override;
	void PostLoad() override;
#if WITH_EDITOR
	void BeginCacheForCookedPlatformData(ITargetPlatform const* TargetPlatform) override;
	bool IsCachedCookedPlatformDataLoaded(ITargetPlatform const* TargetPlatform) override;
	void ClearCachedCookedPlatformData(ITargetPlatform const* TargetPlatform) override;
	void ClearAllCachedCookedPlatformData() override;
#endif //WITH_EDITOR
	//~ End UObject Interface.

	/**
	 * Call after changing the graph to build the graph resources for rendering.
	 * This will trigger any required shader compilation.
	 */
	void UpdateResources();

private:
	/** Build the shader metadata which describes bindings for a kernel with its linked data interfaces.*/
	FShaderParametersMetadata* BuildKernelShaderMetadata(int32 KernelIndex) const;
	/** Recache the shader metadata for all kernels in the graph. */
	void CacheShaderMetadata();

#if WITH_EDITOR
	/** Build the HLSL source for a kernel with its linked data interfaces. */
	FString BuildKernelSource(int32 KernelIndex) const;

	/** Cache shader resources for all kernels in the graph. */
	void CacheResourceShadersForRendering(uint32 CompilationFlags);

	/** Cache shader resources for a specific compute kernel. This will trigger any required shader compilation. */
	static void CacheShadersForResource(
		EShaderPlatform ShaderPlatform,
		ITargetPlatform const* TargetPlatform,
		uint32 CompilationFlags,
		FComputeKernelResource* Kernel);
#endif

private:
	/** 
	 * Each kernel requires an associated FComputeKernelResource object containing the shader resources.
	 * Depending on the context (during serialization, editor, cooked game) there may me more than one object.
	 * This structure stores them all.
	 */
	struct FComputeKernelResourceSet
	{
#if WITH_EDITORONLY_DATA
		/** Kernel resource objects stored per feature level. */
		TUniquePtr<FComputeKernelResource> KernelResourcesByFeatureLevel[ERHIFeatureLevel::Num];
#else
		/** Cooked game has a single kernel resource object. */
		TUniquePtr<FComputeKernelResource> KernelResource;
#endif

#if WITH_EDITORONLY_DATA
		/** Serialized resources waiting for processing during PostLoad(). */
		TArray< TUniquePtr<FComputeKernelResource> > LoadedKernelResources;
		/** Cached resources waiting for serialization during cook. */
		TMap< const class ITargetPlatform*, TArray< TUniquePtr<FComputeKernelResource> > > CachedKernelResourcesForCooking;
#endif

		/** Release all resources. */
		void Reset();
		/** Get the appropriate kernel resource for rendering. */
		FComputeKernelResource const* Get() const;
		/** Get the appropriate kernel resource for rendering. Create a new empty resource if one doesn't exist. */
		FComputeKernelResource* GetOrCreate();
		/** Serialize the resources including the shader maps. */
		void Serialize(FArchive& Ar);
		/** Apply shader maps found in Serialize(). Call this from PostLoad(). */
		void ProcessSerializedShaderMaps();
	};

	/** Kernel resources stored with the same indexing as the KernelInvocations array. */
	TArray<FComputeKernelResourceSet>  KernelResources;

	/** Shader metadata stored with the same indexing as the KernelInvocations array. */
	TArray<FShaderParametersMetadata*> ShaderMetadatas;
};
