// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXComputeShader.cpp: AGX RHI Compute Shader Class Implementation.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "Templates/AGXBaseShader.h"
#include "AGXComputeShader.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Compute Shader Class


FAGXComputeShader::FAGXComputeShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
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
		mtlpp::Function Func = GetCompiledFunction();
		check(Func);

		ns::Error Error;
		mtlpp::ComputePipelineDescriptor Descriptor;
		Descriptor.SetLabel(Func.GetName());
		Descriptor.SetComputeFunction(Func);
		if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesTextureBuffers))
		{
			Descriptor.SetMaxTotalThreadsPerThreadgroup(NumThreadsX*NumThreadsY*NumThreadsZ);
		}

		if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesPipelineBufferMutability))
		{
			ns::AutoReleased<ns::Array<mtlpp::PipelineBufferDescriptor>> PipelineBuffers = Descriptor.GetBuffers();

			uint32 ImmutableBuffers = Bindings.ConstantBuffers;
			while(ImmutableBuffers)
			{
				uint32 Index = __builtin_ctz(ImmutableBuffers);
				ImmutableBuffers &= ~(1 << Index);

				if (Index < ML_MaxBuffers)
				{
					ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = PipelineBuffers[Index];
					PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
				}
			}
			if (SideTableBinding > 0)
			{
				ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = PipelineBuffers[SideTableBinding];
				PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
			}
		}

		mtlpp::ComputePipelineState Kernel;
		mtlpp::ComputePipelineReflection Reflection;

		METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("NewComputePipeline: %d_%d"), SourceLen, SourceCRC)));
#if METAL_DEBUG_OPTIONS
		if (GetAGXDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelFastValidation)
		{
			ns::AutoReleasedError ComputeError;
			mtlpp::AutoReleasedComputePipelineReflection ComputeReflection;

			NSUInteger ComputeOption = mtlpp::PipelineOption::ArgumentInfo | mtlpp::PipelineOption::BufferTypeInfo;
			Kernel = GMtlppDevice.NewComputePipelineState(Descriptor, mtlpp::PipelineOption(ComputeOption), &ComputeReflection, &ComputeError);
			Error = ComputeError;
			Reflection = ComputeReflection;
		}
		else
#endif // METAL_DEBUG_OPTIONS
		{
			ns::AutoReleasedError ComputeError;
			Kernel = GMtlppDevice.NewComputePipelineState(Descriptor, mtlpp::PipelineOption(0), nil, &ComputeError);
			Error = ComputeError;
		}

		if (Kernel == nil)
		{
			UE_LOG(LogRHI, Error, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));
			UE_LOG(LogRHI, Fatal, TEXT("Failed to create compute kernel: %s"), *FString([Error description]));
		}

		Pipeline = [FAGXShaderPipeline new];
		Pipeline->ComputePipelineState = Kernel;
#if METAL_DEBUG_OPTIONS
		Pipeline->ComputePipelineReflection = Reflection;
		Pipeline->ComputeSource = GetSourceCode();
		if (Reflection)
		{
			Pipeline->ComputeDesc = Descriptor;
		}
#endif // METAL_DEBUG_OPTIONS
		METAL_DEBUG_OPTION(FMemory::Memzero(Pipeline->ResourceMask, sizeof(Pipeline->ResourceMask)));
	}
	check(Pipeline);

	return Pipeline;
}

mtlpp::Function FAGXComputeShader::GetFunction()
{
	return GetCompiledFunction();
}

