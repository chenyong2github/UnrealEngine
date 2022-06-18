// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXComputeShader.cpp: AGX RHI Compute Shader Class Implementation.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "Templates/AGXBaseShader.h"
#include "AGXComputeShader.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Compute Shader Class


FAGXComputeShader::FAGXComputeShader(TArrayView<const uint8> InCode, const TRefCountPtr<FMTLLibrary>& InLibrary)
	: NumThreadsX(0)
	, NumThreadsY(0)
	, NumThreadsZ(0)
{
	Pipeline = nil;
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);

	NumThreadsX = FMath::Max((int32)Header.NumThreadsX, 1);
	NumThreadsY = FMath::Max((int32)Header.NumThreadsY, 1);
	NumThreadsZ = FMath::Max((int32)Header.NumThreadsZ, 1);
}

FAGXComputeShader::~FAGXComputeShader()
{
	[Pipeline release];
	Pipeline = nil;
}

FAGXShaderPipeline* FAGXComputeShader::GetPipeline()
{
	if (!Pipeline)
	{
		id<MTLFunction> Func = GetCompiledFunction();
		check(Func != nil);

		NSError* Error = nil;
		MTLComputePipelineDescriptor* Descriptor = [[MTLComputePipelineDescriptor alloc] init];
		[Descriptor setLabel:[Func name]];
		[Descriptor setComputeFunction:Func];
		[Descriptor setMaxTotalThreadsPerThreadgroup:(NumThreadsX * NumThreadsY * NumThreadsZ)];

		if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesPipelineBufferMutability))
		{
			MTLPipelineBufferDescriptorArray* PipelineBuffers = [Descriptor buffers];

			uint32 ImmutableBuffers = Bindings.ConstantBuffers;
			while (ImmutableBuffers)
			{
				uint32 Index = __builtin_ctz(ImmutableBuffers);
				ImmutableBuffers &= ~(1 << Index);

				if (Index < ML_MaxBuffers)
				{
					[[PipelineBuffers objectAtIndexedSubscript:Index] setMutability:MTLMutabilityImmutable];
				}
			}
			if (SideTableBinding > 0)
			{
				[[PipelineBuffers objectAtIndexedSubscript:SideTableBinding] setMutability:MTLMutabilityImmutable];
			}
		}

		id<MTLComputePipelineState> Kernel = nil;
#if METAL_DEBUG_OPTIONS
		MTLAutoreleasedComputePipelineReflection Reflection = nil;
#endif

		METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("NewComputePipeline: %d_%d"), SourceLen, SourceCRC)));
#if METAL_DEBUG_OPTIONS
		if (GetAGXDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelFastValidation)
		{
			Kernel = [GMtlDevice newComputePipelineStateWithDescriptor:Descriptor
															  options:(MTLPipelineOptionArgumentInfo | MTLPipelineOptionBufferTypeInfo)
														   reflection:&Reflection
														   		error:&Error];
		}
		else
#endif // METAL_DEBUG_OPTIONS
		{
			Kernel = [GMtlDevice newComputePipelineStateWithDescriptor:Descriptor
															  options:MTLPipelineOptionNone
														   reflection:nil
														   		error:&Error];
		}

		if (Kernel == nil)
		{
			UE_LOG(LogRHI, Error, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));
			UE_LOG(LogRHI, Fatal, TEXT("Failed to create compute kernel: %s"), *FString([Error description]));
		}

		Pipeline = [FAGXShaderPipeline new];
		Pipeline->ComputePipelineState = Kernel;
#if METAL_DEBUG_OPTIONS
		Pipeline->ComputePipelineReflection = [Reflection retain];
		Pipeline->ComputeSource = GetSourceCode();
#endif // METAL_DEBUG_OPTIONS
		METAL_DEBUG_OPTION(FMemory::Memzero(Pipeline->ResourceMask, sizeof(Pipeline->ResourceMask)));

		[Descriptor release];
	}
	check(Pipeline);

	return Pipeline;
}

id<MTLFunction> FAGXComputeShader::GetFunction()
{
	return GetCompiledFunction();
}

