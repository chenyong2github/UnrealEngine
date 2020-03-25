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

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIVertexShader* VertexShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
    SetUniformBufferInternal<EMetalShaderStages::Vertex, FRHIVertexShader>(Context, VertexShaderRHI, BufferIndex, BufferRHI);
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIHullShader* HullShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
    SetUniformBufferInternal<EMetalShaderStages::Hull, FRHIHullShader>(Context, HullShaderRHI, BufferIndex, BufferRHI);
#endif
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIDomainShader* DomainShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
    SetUniformBufferInternal<EMetalShaderStages::Domain, FRHIDomainShader>(Context, DomainShaderRHI, BufferIndex, BufferRHI);
#endif
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIGeometryShader* GeometryShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
    NOT_SUPPORTED("RHISetShaderUniformBuffer-Geometry");
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIPixelShader* PixelShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
    SetUniformBufferInternal<EMetalShaderStages::Pixel, FRHIPixelShader>(Context, PixelShaderRHI, BufferIndex, BufferRHI);
}

void FMetalRHICommandContext::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
    SetUniformBufferInternal<EMetalShaderStages::Compute, FRHIComputeShader>(Context, ComputeShaderRHI, BufferIndex, BufferRHI);
}

