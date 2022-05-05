// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeGraphInstance.h"
#include "RenderResource.h"

#include "OptimusDeformerInstance.generated.h"

enum class EOptimusNodeGraphType;
class UMeshComponent;
class UOptimusComputeDataInterface;
class UOptimusVariableDescription;
class UOptimusDeformer;
class UOptimusNode_ConstantValue;
class UOptimusVariableContainer;
struct FShaderValueType;


class FOptimusPersistentStructuredBuffer :
	public FRenderResource
{
public:
	FOptimusPersistentStructuredBuffer(int32 InElementCount, int32 InElementStride) :
		ElementCount(InElementCount),
		ElementStride(InElementStride)
	{
	}
	
	/** Allocate a structured buffer with the given element count and stride.
	  * Note: Should only be called from the render thread.
	  */
	void InitRHI() override;

	/** Release the structured buffer, leaving the UAV in a null state */
	void ReleaseRHI() override;

	FString GetFriendlyName() const override { return TEXT("FPersistentStructuredBuffer"); }

	FUnorderedAccessViewRHIRef GetUAV() const { return BufferUAV; }

	int32 GetElementCount() const { return ElementCount; }
	int32 GetElementStride() const { return ElementStride; }
	
private:
	int32 ElementCount = 0;
	int32 ElementStride = 0;
	FBufferRHIRef Buffer;
	FUnorderedAccessViewRHIRef BufferUAV;
};

using FOptimusPersistentStructuredBufferPtr = TSharedPtr<FOptimusPersistentStructuredBuffer>;

struct FOptimusPersistentBufferPool
{
	/** Allocate buffers for the given resource. If the buffer already exists but has different
	  * sizing characteristics, the allocation fails. The number of buffers will equal the
	  * size of the InInvocationElementCount array, but if the allocation fails, the returned
	  * array will be empty.
	  */
	const TArray<FOptimusPersistentStructuredBufferPtr>& GetResourceBuffers(
		FName InResourceName,
		int32 InElementStride,
		TArray<int32> InInvocationElementCount
		);

	/** Release _all_ resources allocated by this pool */
	void ReleaseResources();
	
private:
	TMap<FName, TArray<FOptimusPersistentStructuredBufferPtr>> ResourceBuffersMap;    
};
using FOptimusPersistentBufferPoolPtr = TSharedPtr<FOptimusPersistentBufferPool>;


/** Structure with cached state for a single compute graph. */
USTRUCT()
struct FOptimusDeformerInstanceExecInfo
{
	GENERATED_BODY()

	/** The name of the graph */
	FName GraphName;

	/** The graph type. */ 
	EOptimusNodeGraphType GraphType;
	
	/** The ComputeGraph asset. */
	UPROPERTY()
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	/** The cached state for the ComputeGraph. */
	UPROPERTY()
	FComputeGraphInstance ComputeGraphInstance;
};

/** 
 * Class representing an instance of an Optimus Mesh Deformer.
 * This implements the UMeshDeformerInstance interface to enqueue the graph execution.
 * It also contains the per instance deformer variable state and local state for each of the graphs in the deformer.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class UOptimusDeformerInstance :
	public UMeshDeformerInstance
{
	GENERATED_BODY()

public:
	/** 
	 * Set the Mesh Component that owns this instance.
	 * Call once before first call to SetupFromDeformer().
	 */
	void SetMeshComponent(UMeshComponent* InMeshComponent);

	/** 
	 * Setup the instance. 
	 * Needs to be called after the UOptimusDeformer creates this instance, and whenever the instance is invalidated.
	 * Invalidation happens whenever any bound Data Providers become invalid.
	 */
	void SetupFromDeformer(UOptimusDeformer* InDeformer);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintPure, Category="Deformer", meta=(DisplayName="Set Variable (bool)"))
	bool SetBoolVariable(FName InVariableName, bool InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (int)"))
	bool SetIntVariable(FName InVariableName, int32 InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (float)"))
	bool SetFloatVariable(FName InVariableName, float InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (Vector)"))
	bool SetVectorVariable(FName InVariableName, const FVector& InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (Vector4)"))
	bool SetVector4Variable(FName InVariableName, const FVector4& InValue);

	/** Get an array containing all the variables. */
	UFUNCTION(BlueprintGetter)
	const TArray<UOptimusVariableDescription*>& GetVariables() const;

	/** Trigger a named trigger graph to run on the next tick */
	UFUNCTION(BlueprintCallable, Category="Deformer")
	bool EnqueueTriggerGraph(FName InTriggerGraphName);
	
	
	
	/** Directly set a graph constant value. */
	void SetConstantValueDirect(FString const& InVariableName, TArray<uint8> const& InValue);

	FOptimusPersistentBufferPoolPtr GetBufferPool() const { return BufferPool; }
	
protected:
	/** Implementation of UMeshDeformerInstance. */
	void AllocateResources() override;
	void ReleaseResources() override;
	bool IsActive() const override;
	void EnqueueWork(FSceneInterface* InScene, EWorkLoad InWorkLoadType, FName InOwnerName) override;

private:
	/** The Mesh Component that owns this Mesh Deformer Instance. */
	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> MeshComponent;

	/** An array of state. One for each graph owned by the deformer. */
	UPROPERTY()
	TArray<FOptimusDeformerInstanceExecInfo> ComputeGraphExecInfos;

	/** Storage for variable data. */
	UPROPERTY()
	TObjectPtr<UOptimusVariableContainer> Variables;
	
	UPROPERTY()
	TArray<TObjectPtr<UOptimusComputeDataInterface>> RetainedDataInterfaces;

	// List of graphs that should be run on the next tick. 
	TSet<FName> GraphsToRunOnNextTick;
	FCriticalSection GraphsToRunOnNextTickLock;

	FOptimusPersistentBufferPoolPtr BufferPool;
};
