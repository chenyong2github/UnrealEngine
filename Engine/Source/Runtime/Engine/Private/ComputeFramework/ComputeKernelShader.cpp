// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelShader.h"

#include "Engine/VolumeTexture.h"
#include "ComputeKernelDerivedDataVersion.h"
#include "ComputeFramework/ComputeKernelShaderCompilationManager.h"
#include "ComputeFramework/ComputeKernelShared.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "ShaderCompiler.h"
#include "ShaderParameterUtils.h"
#include "Stats/StatsMisc.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SHADER_TYPE(, FComputeKernelShader, TEXT("/Engine/Private/ComputeKernel.usf"), TEXT("__"), SF_Compute)


FComputeKernelShader::FComputeKernelShader(
	const FComputeKernelShaderType::CompiledShaderInitializerType& Initializer
	)
	: FShader(Initializer)
{
	const FShaderParametersMetadata& ShaderParametersMetadata = 
		static_cast<const FComputeKernelShaderType::FParameters*>(Initializer.Parameters)->ShaderParamMetadata;

	Bindings.BindForLegacyShaderParameters(
		this, 
		Initializer.PermutationId, 
		Initializer.ParameterMap, 
		ShaderParametersMetadata,
		true
		);
}
