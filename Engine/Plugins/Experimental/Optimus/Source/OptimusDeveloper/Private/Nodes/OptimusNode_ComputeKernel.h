// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusContextNames.h"
#include "OptimusNode.h"
#include "OptimusDataType.h"
#include "OptimusResourceDescription.h"
#include "ComputeFramework/ComputeKernelSource.h"

#include "Types/OptimusType_ShaderText.h"

#include "OptimusNode_ComputeKernel.generated.h"


class UOptimusComputeDataInterface;
class USkeletalMesh;
enum class EOptimusNodePinDirection : uint8;


UCLASS()
class UOptimusKernelSource : public UComputeKernelSource
{
	GENERATED_BODY()
public:
	void SetSourceAndEntryPoint(
		const FString& InSource,
		const FString& InEntryPoint
		)
	{
		Source = InSource;
		EntryPoint = InEntryPoint;
		Hash = GetTypeHash(InSource);
	}
	
	
	FString GetEntryPoint() const override
	{
		return EntryPoint;
	}
	
	FString GetSource() const override
	{
		return Source;
	}
	
	/** Get a hash of the kernel source code. */
	uint64 GetSourceCodeHash() const override
	{
		return Hash;
	}

private:
	UPROPERTY()
	FString EntryPoint;
	
	UPROPERTY()
	FString Source;

	UPROPERTY()
	uint64 Hash;

	
};

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
	const UOptimusNode* ValueNode;
	
	// The name of the shader parameter 
	FString ParameterName;
	
	// The value type of the parameter
	FShaderValueTypeHandle ValueType;
};
using FOptimus_KernelParameterBindingList = TArray<FOptimus_KernelParameterBinding>;

// Maps from a data interface node to the data interface that it represents.
using FOptimus_NodeToDataInterfaceMap =  TMap<const UOptimusNode*, UOptimusComputeDataInterface*>;

// Maps from an output pin to the transient data interface, used to store intermediate results,
// that it represents.
using FOptimus_PinToDataInterfaceMap = TMap<const UOptimusNodePin*, UOptimusComputeDataInterface*>;


USTRUCT()
struct FOptimus_ShaderBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category=Binding)
	FName Name;

	UPROPERTY(EditAnywhere, Category = Binding, meta=(UseInResource))
	FOptimusDataTypeRef DataType;
};


USTRUCT()
struct FOptimus_ShaderContextBinding :
	public FOptimus_ShaderBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Binding)
	FOptimusNestedResourceContext Context;
};



UCLASS()
class UOptimusNode_ComputeKernel :
	public UOptimusNode
{
	GENERATED_BODY()

public:
	UOptimusNode_ComputeKernel();

	FName GetNodeCategory() const override 
	{
		return CategoryName::Deformers;
	}

	UOptimusKernelSource* CreateComputeKernel(
		UObject* InKernelSourceOuter,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		const TSet<const UOptimusNode *>& InValueNodeSet,
		FOptimus_KernelParameterBindingList& OutParameterBindings,
		FOptimus_InterfaceBindingMap& OutInputDataBindings,
		FOptimus_InterfaceBindingMap& OutOutputDataBindings
		) const;

	UPROPERTY(EditAnywhere, Category=KernelConfiguration)
	FString KernelName = "MyKernel";

	UPROPERTY(EditAnywhere, Category = KernelConfiguration, meta=(Min=1))
	int32 ThreadCount = 64;

	// HACK: Replace with contexts gathered from supported DataInterfaces.
	UPROPERTY(EditAnywhere, Category = KernelConfiguration)
	FOptimusResourceContext DriverContext;

	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderBinding> Parameters;
	
	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderContextBinding> InputBindings;

	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderContextBinding> OutputBindings;

	UPROPERTY(EditAnywhere, Category = ShaderSource)
	FOptimusType_ShaderText ShaderSource;

#if defined(WITH_EDITOR)
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	void ConstructNode() override;;

private:
	void ProcessInputPinForComputeKernel(
		const UOptimusNodePin* InInputPin,
		const UOptimusNodePin* InOutputPin,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		const TSet<const UOptimusNode *>& InValueNodeSet,
		UOptimusKernelSource* InKernelSource,
		TArray<FString>& OutGeneratedFunctions,
		FOptimus_KernelParameterBindingList& OutParameterBindings,
		FOptimus_InterfaceBindingMap& OutInputDataBindings
		) const;

	void ProcessOutputPinForComputeKernel(
		const UOptimusNodePin* InOutputPin,
		const TArray<UOptimusNodePin *>& InInputPins,
		const FOptimus_NodeToDataInterfaceMap& InNodeDataInterfaceMap,
		const FOptimus_PinToDataInterfaceMap& InLinkDataInterfaceMap,
		UOptimusKernelSource* InKernelSource,
		TArray<FString>& OutGeneratedFunctions,
		FOptimus_InterfaceBindingMap& OutOutputDataBindings
		) const;
	
	void UpdatePinTypes(
		EOptimusNodePinDirection InPinDirection
		);

	void UpdatePinNames(
	    EOptimusNodePinDirection InPinDirection);

	void UpdatePinResourceContexts(
		EOptimusNodePinDirection InPinDirection
		);
	
	void UpdatePreamble();

	TArray<UOptimusNodePin *> GetKernelPins(
		EOptimusNodePinDirection InPinDirection
		) const;

	FString GetWrappedShaderSource() const;
};
