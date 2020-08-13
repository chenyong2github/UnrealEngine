// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ComputeKernelSource.h"
#include "ComputeFramework/ComputeKernelResource.h"

DEFINE_LOG_CATEGORY(ComputeKernel);

#if WITH_EDITOR
void UComputeKernel::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* ModifiedProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	if (!ModifiedProperty)
	{
		return;
	}

	FName ModifiedPropName = ModifiedProperty->GetFName();

	if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, KernelSource))
	{
		if (KernelSource)
		{
			PermutationSet = KernelSource->PermutationSet;
			DefinitionsSet = KernelSource->DefinitionsSet;
		}
		else
		{
			PermutationSet = FComputeKernelPermutationSet();
			DefinitionsSet = FComputeKernelDefinitionsSet();
		}
		
		CacheResourceShadersForRendering();
	}
	else if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, PermutationSet) ||
		ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, DefinitionsSet))
	{
		CacheResourceShadersForRendering();
	}
}

void UComputeKernel::CacheResourceShadersForRendering(
	uint32 CompilationFlags /*= uint32(EComputeKernelCompilationFlags::None)*/
	)
{
	if (!KernelSource)
	{
		KernelResource->Invalidate();
		return;
	}

	if (!KernelResource)
	{
		KernelResource = MakeUnique<FComputeKernelResource>();
	}

	// #TODO_ZABIR: Initialize KernelResouce with needed data

	ERHIFeatureLevel::Type CacheFeatureLevel = GMaxRHIFeatureLevel;
	const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[CacheFeatureLevel];

	UComputeKernel::CacheShadersForResource(ShaderPlatform, CompilationFlags, KernelResource.Get());
}

void UComputeKernel::CacheShadersForResource(
	EShaderPlatform ShaderPlatform,
	uint32 CompilationFlags,
	FComputeKernelResource* KernelResource
	)
{
	const bool bIsDefault = (KernelResource->GetKernelFlags() & uint32(EComputeKernelFlags::IsDefaultKernel)) != 0;
	if (!GIsEditor || GIsAutomationTesting || bIsDefault)
	{
		CompilationFlags |= uint32(EComputeKernelCompilationFlags::Synchronous);
	}

	KernelResource->CacheShaders(ShaderPlatform, CompilationFlags);

	const FComputeKernelCompilationResults& Results = KernelResource->GetCompilationResults();

	if (!Results.bIsSuccess)
	{
		if (bIsDefault)
		{
			UE_LOG(
				ComputeKernel, 
				Fatal, 
				TEXT("Failed to compile default FComputeKernelResource [%s] for platform [%s]!"),
				*KernelResource->GetFriendlyName(),
				*LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString()
				);
		}

		UE_LOG(
			ComputeKernel, 
			Warning, 
			TEXT("Failed to compile FComputeKernelResource [%s] for platform [%s]."),
			*KernelResource->GetFriendlyName(),
			*LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString()
			);

		uint32 WarningCount = Results.CompileWarnings.Num();
		for (uint32 i = 0; i < WarningCount; ++i)
		{
			UE_LOG(ComputeKernel, Warning, TEXT("    [Warning] - %s"), *Results.CompileWarnings[i]);
		}

		uint32 ErrorCount = Results.CompileWarnings.Num();
		for (uint32 i = 0; i < ErrorCount; ++i)
		{
			UE_LOG(ComputeKernel, Warning, TEXT("      [Error] - %s"), *Results.CompileErrors[i]);
		}
	}
}
#endif
