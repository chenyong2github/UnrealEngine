// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "UObject/Interface.h"

#include "IOptimusComputeKernelProvider.generated.h"

class UOptimusComputeDataInterface;
class UOptimusKernelSource;
class UOptimusNode;
class UOptimusNodePin;
struct FOptimusPinTraversalContext;
struct FOptimusCompilerDiagnostic;


// Maps the data interface's data binding index to the function we would like to have present
// during kernel compilation to read/write values from/to that data interface's resource.
struct FOptimus_InterfaceBinding
{
	UOptimusComputeDataInterface* DataInterface;
	int32 DataInterfaceBindingIndex;
	FString BindingFunctionName;
};
using FOptimus_InterfaceBindingMap = TMap<int32 /* Kernel Index */, FOptimus_InterfaceBinding>;

// A map that goes from a value/variable node to a compute shader input parameter.
struct FOptimus_KernelParameterBinding
{
	/** The node to retrieve the value from */
	const UOptimusNode* ValueNode;
	
	/** The name of the shader parameter */ 
	FString ParameterName;
	
	/** The value type of the parameter */
	FShaderValueTypeHandle ValueType;
};
using FOptimus_KernelParameterBindingList = TArray<FOptimus_KernelParameterBinding>;

// Maps from a data interface node to the data interface that it represents.
using FOptimus_NodeToDataInterfaceMap =  TMap<const UOptimusNode*, UOptimusComputeDataInterface*>;

// Maps from an output pin to the transient data interface, used to store intermediate results,
// that it represents.
using FOptimus_PinToDataInterfaceMap = TMap<const UOptimusNodePin*, UOptimusComputeDataInterface*>;


UINTERFACE()
class OPTIMUSCORE_API UOptimusComputeKernelProvider :
	public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface that provides a mechanism to identify and work with node graph owners.
 */
class OPTIMUSCORE_API IOptimusComputeKernelProvider
{
	GENERATED_BODY()

public:
	/**
	 * Return an UOptimusKernelSource object, from a compute kernel node state that implements
	 * this interface.
	 * @param InKernelSourceOuter The outer object that will own the new kernel source.
	 * @param InNodeDataInterfaceMap
	 * @param InLinkDataInterfaceMap
	 * @param InValueNodeSet
	 * @param OutParameterBindings
	 * @param OutInputDataBindings
	 * @param OutOutputDataBindings
	 */
	virtual UOptimusKernelSource* CreateComputeKernel(
		UObject* InKernelSourceOuter,
		const FOptimusPinTraversalContext& InTraversalContext,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		const TSet<const UOptimusNode *>& InValueNodeSet,
		FOptimus_KernelParameterBindingList& OutParameterBindings,
		FOptimus_InterfaceBindingMap& OutInputDataBindings,
		FOptimus_InterfaceBindingMap& OutOutputDataBindings
	) const = 0;

	/** Set the diagnostics resulting from the kernel compilation.
	 *  @param InDiagnostics The diagnostics to set for the node. 
	 */
	virtual void SetCompilationDiagnostics(
		const TArray<FOptimusCompilerDiagnostic>& InDiagnostics
		) = 0;
};
