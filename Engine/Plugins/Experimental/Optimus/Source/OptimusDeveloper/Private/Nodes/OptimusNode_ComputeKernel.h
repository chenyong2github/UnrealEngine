// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

struct FOptimus_InterfaceBinding
{
	UOptimusComputeDataInterface* DataInterface;
	int32 DataInterfaceBindingIndex;
	FString BindingFunctionName;
};
using FOptimus_InterfaceBindingMap = TMap<int32 /* Kernel Index */, FOptimus_InterfaceBinding>;

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

	FOptimus_ShaderContextBinding()
	{
		Contexts.Add(EOptimusResourceContext::Vertex);
	}
	
	UPROPERTY(EditAnywhere, Category = Binding)
	TArray<EOptimusResourceContext> Contexts;
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
		const TMap<const UOptimusNode *, UOptimusComputeDataInterface *>& InNodeDataInterfaceMap,
		TMap<const UOptimusNodePin*, UOptimusComputeDataInterface *>& InLinkDataInterfaceMap,
		FOptimus_InterfaceBindingMap& OutInputDataBindings,
		FOptimus_InterfaceBindingMap& OutOutputDataBindings
		) const;

	UPROPERTY(EditAnywhere, Category=KernelConfiguration)
	FString KernelName = "MyKernel";

	UPROPERTY(EditAnywhere, Category = KernelConfiguration, meta=(Min=1))
	int32 ThreadCount = 64;

	// HACK: Replace with contexts gathered from supported DataInterfaces.
	UPROPERTY(EditAnywhere, Category = KernelConfiguration)
	EOptimusResourceContext DriverContext = EOptimusResourceContext::Vertex;

	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderBinding> Parameters;
	
	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderContextBinding> InputBindings;

	UPROPERTY(EditAnywhere, Category=Bindings)
	TArray<FOptimus_ShaderContextBinding> OutputBindings;

	UPROPERTY(EditAnywhere, Category = ShaderSource)
	FOptimusType_ShaderText ShaderSource;

	UPROPERTY(Transient)
	TArray<FOptimusSourceLocation> ErrorLocations;
		
#if defined(WITH_EDITOR)
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdatePinTypes(
		EOptimusNodePinDirection InPinDirection
		);

	void UpdatePinNames(
	    EOptimusNodePinDirection InPinDirection);

	void UpdatePinContextAndDimensionality(
		EOptimusNodePinDirection InPinDirection
		);
	
	void UpdatePreamble();

	TArray<UOptimusNodePin *> GetKernelPins(
		EOptimusNodePinDirection InPinDirection
		) const;

	FString GetWrappedShaderSource() const;
};
