// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusComponentBindingsProvider.h"
#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeGraphInstance.h"

#include "OptimusDeformerInstance.generated.h"

enum class EOptimusNodeGraphType;
struct FOptimusPersistentStructuredBuffer;
class FRDGBuffer;
class FRDGBuilder;
class UActorComponent;
class UMeshComponent;
class UOptimusDeformer;
class UOptimusVariableContainer;
class UOptimusVariableDescription;
class UOptimusComponentSourceBinding;

class FOptimusPersistentBufferPool
{
public:
	/** 
	 * Get or allocate buffers for the given resource
	 * If the buffer already exists but has different sizing characteristics the allocation fails. 
	 * The number of buffers will equal the size of the InElementCount array.
	 * But if the allocation fails, the returned array will be empty.
	 */
	void GetResourceBuffers(
		FRDGBuilder& GraphBuilder,
		FName InResourceName,
		int32 InElementStride,
		TArray<int32> const& InElementCounts,
		TArray<FRDGBuffer*>& OutBuffers );

	/** Release _all_ resources allocated by this pool */
	void ReleaseResources();
	
private:
	TMap<FName, TArray<FOptimusPersistentStructuredBuffer>> ResourceBuffersMap;
};
using FOptimusPersistentBufferPoolPtr = TSharedPtr<FOptimusPersistentBufferPool>;


/** Structure with cached state for a single compute graph. */
USTRUCT()
struct FOptimusDeformerInstanceExecInfo
{
	GENERATED_BODY()

	FOptimusDeformerInstanceExecInfo();

	/** The name of the graph */
	UPROPERTY()
	FName GraphName;

	/** The graph type. */
	UPROPERTY()
	EOptimusNodeGraphType GraphType;
	
	/** The ComputeGraph asset. */
	UPROPERTY()
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	/** The cached state for the ComputeGraph. */
	UPROPERTY()
	FComputeGraphInstance ComputeGraphInstance;
};


/** Defines a binding between a component provider in the graph and an actor component in the component hierarchy on
 *  the actor whose deformable component we're bound to.
 */
USTRUCT(BlueprintType)
struct FOptimusDeformerInstanceComponentBinding
{
	GENERATED_BODY()
	
	UPROPERTY(VisibleAnywhere, Category="Binding")
	FName ProviderName;
	
	UPROPERTY(EditAnywhere, Category="Binding")
	TSoftObjectPtr<UActorComponent> ActorComponent;
};


UCLASS(Blueprintable, BlueprintType)
class OPTIMUSCORE_API UOptimusDeformerInstanceSettings :
	public UMeshDeformerInstanceSettings,
	public IOptimusComponentBindingsProvider
{
	GENERATED_BODY()

public:
	/** */
	UPROPERTY(EditAnywhere, Category="Deformer|Settings", EditFixedSize, meta=(NoResetToDefault, EditFixedOrder))
	TArray<FOptimusDeformerInstanceComponentBinding> Bindings;

	void RefreshComponentBindings(
		UOptimusDeformer* InDeformer,
		UMeshComponent* InMeshComponent
		);

	// -- IOptimusComponentBindingsProvider
	TArray<UActorComponent*> GetBoundComponents() const override;
	AActor* GetActor() const override;
	UOptimusComponentSourceBinding* GetComponentBindingByName(FName InBindingName) const override;

#if WITH_EDITOR
	// -- UObject
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
protected:
	friend class UOptimusDeformer;

	void InitializeSettings(
		UOptimusDeformer* InDeformer,
		UMeshComponent* InMeshComponent);

private:
	UPROPERTY()
	TWeakObjectPtr<UOptimusDeformer> Deformer; 
};


/** 
 * Class representing an instance of an Optimus Mesh Deformer.
 * This implements the UMeshDeformerInstance interface to enqueue the graph execution.
 * It also contains the per instance deformer variable state and local state for each of the graphs in the deformer.
 */
UCLASS(Blueprintable, BlueprintType)
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
	 * Set the instance settings that control this deformer instance. The deformer instance is transient whereas
	 * the settings are persistent.
	 */
	void SetInstanceSettings(UOptimusDeformerInstanceSettings* InInstanceSettings);
	
	/** 
	 * Setup the instance. 
	 * Needs to be called after the UOptimusDeformer creates this instance, and whenever the instance is invalidated.
	 * Invalidation happens whenever any bound Data Providers become invalid.
	 */
	void SetupFromDeformer(
		UOptimusDeformer* InDeformer,
		const bool bInRefreshBindings = false
		);

	void SetupFromDeformerAndRefreshBindings(UOptimusDeformer* InDeformer)
	{
		SetupFromDeformer(InDeformer, true);
	}

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

	void SetCanBeActive(bool bInCanBeActive);

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

	/** The Mesh Component that owns this Mesh Deformer Instance. */
	UPROPERTY()
	TWeakObjectPtr<UOptimusDeformerInstanceSettings> InstanceSettings;
	
	/** An array of state. One for each graph owned by the deformer. */
	UPROPERTY()
	TArray<FOptimusDeformerInstanceExecInfo> ComputeGraphExecInfos;

	/** Storage for variable data. */
	UPROPERTY()
	TObjectPtr<UOptimusVariableContainer> Variables;
	
	// List of graphs that should be run on the next tick. 
	TSet<FName> GraphsToRunOnNextTick;
	FCriticalSection GraphsToRunOnNextTickLock;

	FOptimusPersistentBufferPoolPtr BufferPool;

	bool bCanBeActive = true;
};
