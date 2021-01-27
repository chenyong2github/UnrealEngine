// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ComputeKernelShared.h"

#include "ComputeKernel.generated.h"

class FComputeKernelResource;
class UComputeKernelSource;

DECLARE_LOG_CATEGORY_EXTERN(ComputeKernel, Log, All);

/* Describes the size and shape (threads) of a kernel invocation. */
USTRUCT(BlueprintType)
struct FKernelInvocationDimension
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Kernel")
	uint16 X = 0;

	UPROPERTY(EditAnywhere, Category = "Kernel")
	uint16 Y = 0;

	UPROPERTY(EditAnywhere, Category = "Kernel")
	uint16 Z = 0;
};

/* Flags that convey kernel behavior to aid compilation/optimizations. */
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EComputeKernelFlags : uint32
{
	/*
	 * Default implies that this kernel must be compiled before the system is functional.
	 * It also implies that this will be compiled synchronously. Other than a pass
	 * through kernel, default shouldn't be used.
	 */
	IsDefaultKernel = 1 << 0, // KERNEL_FLAG(IS_DEFAUL_KERNEL)

	/*
	 * Promise from the author that all memory writes will be unique per shader
	 * dispatch thread. i.e. ThreadX will be the only thread to write to MemoryY,
	 * thus no synchronization is necessary by the compute graph.
	 */
	IsolatedMemoryWrites = 1 << 1, // KERNEL_FLAG(ISOLATED_MEMORY_WRITES)
};
ENUM_CLASS_FLAGS(EComputeKernelFlags);

enum class EComputeKernelCompilationFlags
{
	None		= 0,
	
	/* Force recompilation even if kernel is not dirty and/or DDC data is available. */
	Force		= 1 << 0,

	/* Compile the shader while blocking the main thread. */
	Synchronous = 1 << 1,

	/* Replaces all instances of the shader with the newly compiled version. */
	ApplyCompletedShaderMapForRendering = 1 << 2,

	IsCooking = 1 << 3,
};

/* Base class representing a kernel that will be run as a shader on the GPU. */
UCLASS(hidecategories = (Object))
class ENGINE_API UComputeKernel : public UObject
{
	GENERATED_BODY()

public:
	/* 
	 * A kernel's source may be authored by different mechanisms; e.g. HLSL text, VPL graph, ML Meta Lang, etc
	 * This abstracts the source and compilation process.
	 */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	TObjectPtr<UComputeKernelSource> KernelSource = nullptr;

	/* Specifying certain memory access flags allows for optimizations such as kernel fusing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (Bitmask, BitmaskEnum = "EComputeKernelFlags"), Category = "Kernel")
	int32 KernelFlags = 0;

	UPROPERTY(EditDefaultsOnly, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelPermutationSet PermutationSetOverrides;

	UPROPERTY(EditDefaultsOnly, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelDefinitionsSet DefinitionsSetOverrides;

	/*
	 * The minimum number of invocations (threads) launched by the kernel definition.
	 * Generally aim for multiple of 64 total. i.e. X * Y * Z = 64
	 */
	UPROPERTY(VisibleAnywhere, Category = "Kernel")
	FKernelInvocationDimension GroupSizeDim;

#if WITH_EDITOR
	DECLARE_EVENT_OneParam(UComputeKernel, FShaderResetEvent, const UComputeKernel*);

	/*
	 * Delegate is invoked when the shader or shader input/output list is changed.
	 * All priorly retrieved bind points are invalid after this signal is raised.
	 */
	FShaderResetEvent ShaderResetSignal;
#endif

	void SetKernelSource(UComputeKernelSource* KernelSource);

	/*
	 * Implemented by derived classes to specify how many invocations of the kernel are necessary 
	 * given the specific data payloads the kernel will operate on. 
	 */
	//virtual FKernelInvocationDimension CalculateInvocationDimension(const TArray<FBindPointDataDesc>& BindingCtx) const = 0;

	//=========================================================================
	// UObject interface
	void PostLoad(
		) override;

#if WITH_EDITOR
	void PostEditChangeChainProperty(
		FPropertyChangedChainEvent& PropertyChangedEvent
		) override;
#endif
	//=========================================================================

	FComputeKernelResource* GetResource() { return KernelResource.Get(); }

private:
#if WITH_EDITOR
	/* Entry point to initiate any [re]compilation necessary due to changes. */
	void CacheResourceShadersForRendering(
		uint32 CompilationFlags = uint32(EComputeKernelCompilationFlags::ApplyCompletedShaderMapForRendering) | uint32(EComputeKernelCompilationFlags::Force)
		);

	/* Given a FKernelResouce, its associated shader is compiled - used for rendering and cooking. */
	static void CacheShadersForResource(
		EShaderPlatform ShaderPlatform,
		const ITargetPlatform* TargetPlatform,
		uint32 CompilationFlags,
		FComputeKernelResource* Kernel
		);
#endif

	/* The shader resource encapsulating the kernel. Akin to FMaterialResource */
	TUniquePtr<FComputeKernelResource> KernelResource = nullptr;
};
