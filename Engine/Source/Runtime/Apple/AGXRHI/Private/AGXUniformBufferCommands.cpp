// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"
#include "AGXShaderTypes.h"
#include "AGXFrameAllocator.h"

static void DoUpdateUniformBuffer(FAGXUniformBuffer* UB, const void* Contents)
{
    check(IsInRenderingThread() || IsInRHIThread());
    
    FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
    // The only way we can be on the RHI thread here is if we're in the process of creating a FLocalUniformBuffer.
    bool bUpdateImmediately = RHICmdList.Bypass() || IsInRHIThread();

	TArray<TRefCountPtr<FRHIResource> > ResourceTable;

	UB->CopyResourceTable_RenderThread(Contents, ResourceTable);

    if(bUpdateImmediately)
    {
        UB->Update(Contents, ResourceTable);
    }
    else
    {
        const uint32 NumBytes = UB->GetLayout().ConstantBufferSize;
        void* Data = RHICmdList.Alloc(NumBytes, 16);
        FMemory::Memcpy(Data, Contents, NumBytes);
        
        RHICmdList.EnqueueLambda([Data, UB, NewResourceTable = MoveTemp(ResourceTable)](FRHICommandListImmediate& RHICmdList)
        {
            UB->Update(Data, NewResourceTable);
        });
        
        RHICmdList.RHIThreadFence(true);
    }
}

FUniformBufferRHIRef FAGXDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout* Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
    FAGXDeviceContext& DeviceContext = (FAGXDeviceContext&)GetAGXDeviceContext();
    FAGXFrameAllocator* UniformAllocator = DeviceContext.GetUniformAllocator();
    
    FAGXUniformBuffer* UB = new FAGXUniformBuffer(Layout, Usage, Validation);
    
    DoUpdateUniformBuffer(UB, Contents);
    
    return UB;
}

void FAGXDynamicRHI::RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents)
{
    check(IsInRenderingThread());
    
    FAGXUniformBuffer* UB = ResourceCast(UniformBufferRHI);
    DoUpdateUniformBuffer(UB, Contents);
}

template<EAGXShaderStages Stage, typename RHIShaderType>
static void SetUniformBufferInternal(FAGXContext* Context, RHIShaderType* ShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* UBRHI)
{
    @autoreleasepool
    {
        auto Shader = ResourceCast(ShaderRHI);
        Context->GetCurrentState().BindUniformBuffer(Stage, BufferIndex, UBRHI);
        
        auto& Bindings = Shader->Bindings;
        if((Bindings.ConstantBuffers) & (1 << BufferIndex))
        {
            FAGXUniformBuffer* UB = ResourceCast(UBRHI);
            UB->PrepareToBind();
            
            FAGXBuffer Buf(UB->Backing, ns::Ownership::AutoRelease);
            Context->GetCurrentState().SetShaderBuffer(Stage, Buf, nil, UB->Offset, UB->GetSize(), BufferIndex, mtlpp::ResourceUsage::Read);
        }
    }
}

void FAGXRHICommandContext::RHISetShaderUniformBuffer(FRHIGraphicsShader* Shader, uint32 BufferIndex, FRHIUniformBuffer* Buffer)
{
	switch (Shader->GetFrequency())
	{
		case SF_Vertex:
			SetUniformBufferInternal<EAGXShaderStages::Vertex, FRHIVertexShader>(Context, static_cast<FRHIVertexShader*>(Shader), BufferIndex, Buffer);
			break;

		case SF_Geometry:
			NOT_SUPPORTED("RHISetShaderUniformBuffer-Geometry");
			break;

		case SF_Pixel:
			SetUniformBufferInternal<EAGXShaderStages::Pixel, FRHIPixelShader>(Context, static_cast<FRHIPixelShader*>(Shader), BufferIndex, Buffer);
			break;

		default:
			checkf(0, TEXT("FRHIShader Type %d is invalid or unsupported!"), (int32)Shader->GetFrequency());
			NOT_SUPPORTED("RHIShaderStage");
			break;
	}
}

void FAGXRHICommandContext::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	SetUniformBufferInternal<EAGXShaderStages::Compute, FRHIComputeShader>(Context, ComputeShaderRHI, BufferIndex, BufferRHI);
}
