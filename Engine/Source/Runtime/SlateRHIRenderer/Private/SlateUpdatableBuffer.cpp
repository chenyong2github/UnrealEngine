// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SlateUpdatableBuffer.h"
#include "RenderingThread.h"

DECLARE_CYCLE_STAT(TEXT("UpdateInstanceBuffer Time"), STAT_SlateUpdateInstanceBuffer, STATGROUP_Slate);

struct FSlateUpdateInstanceBufferCommand final : public FRHICommand<FSlateUpdateInstanceBufferCommand>
{
	TSlateElementVertexBuffer<FVector4>& InstanceBuffer;
	TArray<FVector4> InstanceData;
	FVertexBufferRHIRef VertexBuffer;

	FSlateUpdateInstanceBufferCommand(TSlateElementVertexBuffer<FVector4>& InInstanceBuffer, const TArray<FVector4>& InInstanceData)
		: InstanceBuffer(InInstanceBuffer)
		, InstanceData(InInstanceData)
		, VertexBuffer(InInstanceBuffer.VertexBufferRHI) // Keep a reference of RHIVertexBuffer in case it changes on RT thread during a resize
	{}

	void Execute(FRHICommandListBase& CmdList)
	{
		SCOPE_CYCLE_COUNTER( STAT_SlateUpdateInstanceBuffer );
		const bool bIsInRenderingThread = !IsRunningRHIInSeparateThread() || CmdList.Bypass();

		uint8* InstanceBufferData = (uint8*)InstanceBuffer.LockBuffer(InstanceData.Num(), bIsInRenderingThread);

		FMemory::Memcpy( InstanceBufferData, InstanceData.GetData(), InstanceData.Num()*sizeof(FVector4) );
	
		InstanceBuffer.UnlockBuffer(bIsInRenderingThread);
	}
};

FSlateUpdatableInstanceBuffer::FSlateUpdatableInstanceBuffer( int32 InitialInstanceCount )
	: FreeBufferIndex(0)
{
	InstanceData = new FInstanceData;

	InstanceData->InstanceBufferResource.Init(InitialInstanceCount);
	for( int32 BufferIndex = 0; BufferIndex < SlateRHIConstants::NumBuffers; ++BufferIndex )
	{
		InstanceData->Array[BufferIndex].Reserve( InitialInstanceCount );
	}
}

FSlateUpdatableInstanceBuffer::~FSlateUpdatableInstanceBuffer()
{
	InstanceData->InstanceBufferResource.Destroy();
	BeginCleanup(InstanceData);
}

FSlateUpdatableInstanceBuffer::FInstanceData::~FInstanceData()
{
}

void FSlateUpdatableInstanceBuffer::BindStreamSource(FRHICommandListImmediate& RHICmdList, int32 StreamIndex, uint32 InstanceOffset)
{
	RHICmdList.SetStreamSource(StreamIndex, InstanceData->InstanceBufferResource.VertexBufferRHI, InstanceOffset*sizeof(FVector4));
}

TSharedPtr<class FSlateInstanceBufferUpdate> FSlateUpdatableInstanceBuffer::BeginUpdate()
{
	return MakeShareable(new FSlateInstanceBufferUpdate(*this));
}

uint32 FSlateUpdatableInstanceBuffer::GetNumInstances() const
{
	return NumInstances;
}

void FSlateUpdatableInstanceBuffer::UpdateRenderingData(int32 NumInstancesToUse)
{
	NumInstances = NumInstancesToUse;
	if (NumInstances > 0)
	{
		// Enqueue a command to unlock the draw buffer after all windows have been drawn
		FSlateUpdatableInstanceBuffer* Self = this;
		int32 BufferIndex = FreeBufferIndex;
		ENQUEUE_RENDER_COMMAND(SlateBeginDrawingWindowsCommand)(
			[Self, BufferIndex](FRHICommandListImmediate& RHICmdList)
			{
				Self->UpdateRenderingData_RenderThread(RHICmdList, BufferIndex);
			});
	
		FreeBufferIndex = (FreeBufferIndex + 1) % SlateRHIConstants::NumBuffers;
	}
}

TArray<FVector4>& FSlateUpdatableInstanceBuffer::GetBufferData()
{
	return InstanceData->Array[FreeBufferIndex];
}

void FSlateUpdatableInstanceBuffer::UpdateRenderingData_RenderThread(FRHICommandListImmediate& RHICmdList, int32 BufferIndex)
{
	SCOPE_CYCLE_COUNTER( STAT_SlateUpdateInstanceBuffer );

	const TArray<FVector4>& RenderThreadBufferData = InstanceData->Array[BufferIndex];
	InstanceData->InstanceBufferResource.PreFillBuffer( RenderThreadBufferData.Num(), false );

	if(!IsRunningRHIInSeparateThread() || RHICmdList.Bypass())
	{
		FSlateUpdateInstanceBufferCommand Command(InstanceData->InstanceBufferResource, RenderThreadBufferData);
		Command.Execute(RHICmdList);
	}
	else
	{
		ALLOC_COMMAND_CL(RHICmdList, FSlateUpdateInstanceBufferCommand)(InstanceData->InstanceBufferResource, RenderThreadBufferData);
	}
}

