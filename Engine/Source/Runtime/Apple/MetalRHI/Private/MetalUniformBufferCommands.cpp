// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "MetalRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalResources.h"
#include "MetalFrameAllocator.h"

static void DoUpdateUniformBuffer(FMetalUniformBuffer* UB, const void* Contents)
{
    check(IsInRenderingThread() || IsInRHIThread());
    
    FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
    // The only way we can be on the RHI thread here is if we're in the process of creating a FLocalUniformBuffer.
    bool bUpdateImmediately = RHICmdList.Bypass() || IsInRHIThread();
    
    if(bUpdateImmediately)
    {
        UB->Update(Contents);
    }
    else
    {
        const uint32 NumBytes = UB->GetLayout().ConstantBufferSize;
        void* Data = RHICmdList.Alloc(NumBytes, 16);
        FMemory::Memcpy(Data, Contents, NumBytes);
        
        RHICmdList.EnqueueLambda([Data, UB](FRHICommandListImmediate& RHICmdList)
        {
            UB->Update(Data);
        });
        
        RHICmdList.RHIThreadFence(true);
    }
}

FUniformBufferRHIRef FMetalDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
    FMetalDeviceContext& DeviceContext = (FMetalDeviceContext&)GetMetalDeviceContext();
    FMetalFrameAllocator* UniformAllocator = DeviceContext.GetUniformAllocator();
    
    FMetalUniformBuffer* UB = new FMetalUniformBuffer(Layout, Usage, Validation);
    
    DoUpdateUniformBuffer(UB, Contents);
    
    return UB;
}

void FMetalDynamicRHI::RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents)
{
    check(IsInRenderingThread());
    
    FMetalUniformBuffer* UB = ResourceCast(UniformBufferRHI);
    DoUpdateUniformBuffer(UB, Contents);
}

template<EMetalShaderStages Stage, typename RHIShaderType>
static void SetUniformBufferInternal(FMetalContext* Context, RHIShaderType* ShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* UBRHI)
{
    @autoreleasepool
    {
        auto Shader = ResourceCast(ShaderRHI);
        Context->GetCurrentState().BindUniformBuffer(Stage, BufferIndex, UBRHI);
        
        auto& Bindings = Shader->Bindings;
        if((Bindings.ConstantBuffers) & (1 << BufferIndex))
        {
            FMetalUniformBuffer* UB = ResourceCast(UBRHI);
            UB->PrepareToBind();
            
            FMetalBuffer Buf(UB->Backing, ns::Ownership::AutoRelease);
            Context->GetCurrentState().SetShaderBuffer(Stage, Buf, nil, UB->Offset, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
        }
    }
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIGraphicsShader* ShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	@autoreleasepool{
		EMetalShaderStages Stage = EMetalShaderStages::Num;
		FMetalShaderBindings* Bindings = nullptr;
		switch (ShaderRHI->GetFrequency())
		{
		case SF_Vertex:
		{
			FMetalVertexShader* VertexShader = ResourceCast(static_cast<FRHIVertexShader*>(ShaderRHI));
			Bindings = &VertexShader->Bindings;
			Stage = EMetalShaderStages::Vertex;
		}
			break;
	#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		case SF_Hull:
		{
			Stage = EMetalShaderStages::Hull;
			FMetalHullShader* HullShader = ResourceCast(static_cast<FRHIHullShader*>(ShaderRHI));
			Bindings = &HullShader->Bindings;
		}
			break;
		case SF_Domain:
		{
			Stage = EMetalShaderStages::Domain;
			FMetalDomainShader* DomainShader = ResourceCast(static_cast<FRHIDomainShader*>(ShaderRHI));
			Bindings = &DomainShader->Bindings;
		}
			break;
	#endif
		case SF_Pixel:
		{
			Stage = EMetalShaderStages::Pixel;
			FMetalPixelShader* PixelShader = ResourceCast(static_cast<FRHIPixelShader*>(ShaderRHI));
			Bindings = &PixelShader->Bindings;
		}
			break;
		default:
			checkf(0, TEXT("FRHIShader Type %d is invalid or unsupported!"), (int32)ShaderRHI->GetFrequency());
			NOT_SUPPORTED("RHIShaderStage");
			break;
		}

		Context->GetCurrentState().BindUniformBuffer(Stage, BufferIndex, BufferRHI);

		check(BufferIndex < Bindings->NumUniformBuffers);
		if ((Bindings->ConstantBuffers) & 1 << BufferIndex)
		{
			auto* UB = (FMetalUniformBuffer*)BufferRHI;
			Context->GetCurrentState().SetShaderBuffer(Stage, UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
		}
	}
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	@autoreleasepool{
		FMetalComputeShader * ComputeShader = ResourceCast(ComputeShaderRHI);
		Context->GetCurrentState().BindUniformBuffer(EMetalShaderStages::Compute, BufferIndex, BufferRHI);

		auto& Bindings = ComputeShader->Bindings;
		check(BufferIndex < Bindings.NumUniformBuffers);
		if ((Bindings.ConstantBuffers) & 1 << BufferIndex)
		{
			auto* UB = (FMetalUniformBuffer*)BufferRHI;
			Context->GetCurrentState().SetShaderBuffer(EMetalShaderStages::Compute, UB->Buffer, UB->Data, 0, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
		}
	}
}
