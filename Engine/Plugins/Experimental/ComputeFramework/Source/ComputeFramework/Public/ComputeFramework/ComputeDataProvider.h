// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeDataProvider.generated.h"

class FComputeDataProviderRenderProxy;
struct FComputeKernelPermutationSet;
class FRDGBuilder;

/**
 * Compute Framework Data Provider.
 * A concrete instance of this is responsible for supplying data declared by a UComputeDataInterface.
 * One of these must be created for each UComputeDataInterface object in an instance of a Compute Graph.
 */
UCLASS(Abstract)
class COMPUTEFRAMEWORK_API UComputeDataProvider : public UObject
{
	GENERATED_BODY()

public:
	/** Return false if the provider has not been fully initialized. */
	virtual bool IsValid() const { return true; }
	
	/**
	 * Get an associated render thread proxy object.
	 * Currently these are created and destroyed per frame by the FComputeGraphInstance.
	 * todo[CF]: Don't destroy FComputeDataProviderRenderProxy objects every frame?
	 */
	virtual FComputeDataProviderRenderProxy* GetRenderProxy() { return nullptr; }
};

/**
 * Compute Framework Data Provider Proxy. 
 * A concrete instance of this is created by the UComputeDataProvider and used for the render thread gathering of data for a Compute Kernel. 
 */
class COMPUTEFRAMEWORK_API FComputeDataProviderRenderProxy
{
public:
	virtual ~FComputeDataProviderRenderProxy() {}

	/** Called on render thread to determine how many dispatches are required to do all work on the associated data provider. */
	virtual int32 GetInvocationCount() const { return 0; }

	/** Called on render thread to determine dispatch dimension required to do all work on the associated data provider. */
	virtual FIntVector GetDispatchDim(int32 InvocationIndex, FIntVector GroupDim) const { return FIntVector(1, 1, 1); }

	/** 
	 * Get the shader permutations required for this data provider. 
	 * All potential data permutations should already have been registered by the associated data interface to ensure that the compiled permutation exists. 
	 */
	virtual void GetPermutations(int32 InvocationIndex, FComputeKernelPermutationSet& OutPermutationSet) const {}

	/* Called once before any calls to GetBindings() to allow any RDG resource allocation. */
	virtual void AllocateResources(FRDGBuilder& GraphBuilder) {}

	/** 
	 * The name-value shader bindings that are collected from data providers.
	 * All names should already have been registered by the associated data interface. 
	 * todo[CF]: Investigate if this can be replaced with something more efficient. We know that data providers should only need to expose structs and could fill and return them directly.
	 */
	struct FBindings
	{
		TMap< FString, int32 > ParamsInt;
		TMap< FString, uint32 > ParamsUint;
		TMap< FString, float > ParamsFloat;
		TMap< FString, TArray<uint8> > Structs;
	};

	/** Gather the shader bindings for the data provider. */
	virtual void GetBindings(int32 InvocationIndex, TCHAR const* UID, FBindings& OutBindings) const {}
};
