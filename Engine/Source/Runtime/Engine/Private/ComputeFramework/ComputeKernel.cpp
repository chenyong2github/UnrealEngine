// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeFramework/ComputeKernelSource.h"

DEFINE_LOG_CATEGORY(ComputeKernel);

void UComputeKernel::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (FApp::CanEverRender())
	{
		CacheResourceShadersForRendering();
	}
#endif
}

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
			PermutationSetOverrides = KernelSource->PermutationSet;
			DefinitionsSetOverrides = KernelSource->DefinitionsSet;
		}
		else
		{
			PermutationSetOverrides = FComputeKernelPermutationSet();
			DefinitionsSetOverrides = FComputeKernelDefinitionsSet();
		}
		
		CacheResourceShadersForRendering();
	}
	else if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, PermutationSetOverrides) ||
		ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, DefinitionsSetOverrides))
	{
		CacheResourceShadersForRendering();
	}
}

void UComputeKernel::CacheResourceShadersForRendering(
	uint32 CompilationFlags /*= uint32(EComputeKernelCompilationFlags::ApplyCompletedShaderMapForRendering)*/
	)
{
	if (!KernelSource)
	{
		if (KernelResource)
		{
			KernelResource->Invalidate();
			KernelResource = nullptr;
		}

		return;
	}

	if (!KernelResource)
	{
		KernelResource = MakeUnique<FComputeKernelResource>();
	}

	KernelResource = MakeUnique<FComputeKernelResource>();
	KernelResource->SetupResource(
		GMaxRHIFeatureLevel, 
		KernelSource,  
		GetName()
		);

	ERHIFeatureLevel::Type CacheFeatureLevel = GMaxRHIFeatureLevel;
	const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[CacheFeatureLevel];

	UComputeKernel::CacheShadersForResource(ShaderPlatform, nullptr, CompilationFlags | uint32(EComputeKernelCompilationFlags::Force), KernelResource.Get());
}

void UComputeKernel::CacheShadersForResource(
	EShaderPlatform ShaderPlatform,
	const ITargetPlatform* TargetPlatform,
	uint32 CompilationFlags,
	FComputeKernelResource* KernelResource
	)
{
	bool bCooking = (CompilationFlags & uint32(EComputeKernelCompilationFlags::IsCooking)) != 0;
	
	const bool bIsDefault = (KernelResource->GetKernelFlags() & uint32(EComputeKernelFlags::IsDefaultKernel)) != 0;
	if (!GIsEditor || GIsAutomationTesting || bIsDefault || bCooking)
	{
		CompilationFlags |= uint32(EComputeKernelCompilationFlags::Synchronous);
	}

	const bool bIsSuccess = KernelResource->CacheShaders(
		ShaderPlatform, 
		TargetPlatform, 
		CompilationFlags & uint32(EComputeKernelCompilationFlags::ApplyCompletedShaderMapForRendering),
		CompilationFlags & uint32(EComputeKernelCompilationFlags::Synchronous)
		);

	if (!bIsSuccess)
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

		auto& CompilationErrors = KernelResource->GetCompileErrors();
		uint32 ErrorCount = CompilationErrors.Num();
		for (uint32 i = 0; i < ErrorCount; ++i)
		{
			UE_LOG(ComputeKernel, Warning, TEXT("      [Error] - %s"), *CompilationErrors[i]);
		}
	}
}
#endif
