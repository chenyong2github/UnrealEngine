// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLVertexBuffer.cpp: OpenGL vertex buffer RHI implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Containers/ResourceArray.h"
#include "HAL/IConsoleManager.h"
#include "OpenGLDrv.h"

namespace OpenGLConsoleVariables
{
#if PLATFORM_ANDROID
	int32 bUseStagingBuffer = 0;
#else
	int32 bUseStagingBuffer = 1;
#endif

	static FAutoConsoleVariableRef CVarUseStagingBuffer(
		TEXT("OpenGL.UseStagingBuffer"),
		bUseStagingBuffer,
		TEXT("Enables maps of dynamic vertex buffers to go to a staging buffer"),
		ECVF_ReadOnly
		);

	extern int32 bUsePersistentMappingStagingBuffer;
};

static const uint32 MAX_ALIGNMENT_BITS = 8;
static const uint32 MAX_OFFSET_BITS = 32 - MAX_ALIGNMENT_BITS;

struct PoolAllocation
{
	uint8* BasePointer;
	uint32 SizeWithoutPadding;
	uint32 Offset				: MAX_OFFSET_BITS;		// into the target buffer
	uint32 AlignmentPadding		: MAX_ALIGNMENT_BITS;
	int32 FrameRetired;
};

static TArray<PoolAllocation*> AllocationList;
static TMap<void*,PoolAllocation*> AllocationMap;

static GLuint PoolVB = 0;
static uint8* PoolPointer = 0;
static uint32 FrameBytes = 0;
static uint32 FreeSpace = 0;
static uint32 OffsetVB = 0;
static const uint32 PerFrameMax = 1024*1024*4;
static const uint32 MaxAlignment = 1 << MAX_ALIGNMENT_BITS;
static const uint32 MaxOffset = 1 << MAX_OFFSET_BITS;

void* GetAllocation( void* Target, uint32 Size, uint32 Offset, uint32 Alignment = 16)
{
	check(Alignment < MaxAlignment);
	check(Offset < MaxOffset);
	check(FMath::IsPowerOfTwo(Alignment));

	uintptr_t AlignmentSubOne = Alignment - 1;

	if (FOpenGL::SupportsBufferStorage() && OpenGLConsoleVariables::bUseStagingBuffer)
	{
		if (PoolVB == 0)
		{
			FOpenGL::GenBuffers(1, &PoolVB);
			glBindBuffer(GL_COPY_READ_BUFFER, PoolVB);
			FOpenGL::BufferStorage(GL_COPY_READ_BUFFER, PerFrameMax * 4, NULL, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
			PoolPointer = (uint8*)FOpenGL::MapBufferRange(GL_COPY_READ_BUFFER, 0, PerFrameMax * 4, FOpenGL::EResourceLockMode::RLM_WriteOnlyPersistent);

			FreeSpace = PerFrameMax * 4;

			check(PoolPointer);
		}
		check (PoolVB);

		uintptr_t AllocHeadPtr = *reinterpret_cast<const uintptr_t*>(&PoolPointer) + OffsetVB;
		uint32 AlignmentPadBytes = ((AllocHeadPtr + AlignmentSubOne) & (~AlignmentSubOne)) - AllocHeadPtr;
		uint32 SizeWithAlignmentPad = Size + AlignmentPadBytes;

		if (SizeWithAlignmentPad > PerFrameMax - FrameBytes || SizeWithAlignmentPad > FreeSpace)
		{
			return nullptr;
		}

		if (SizeWithAlignmentPad > (PerFrameMax*4 - OffsetVB))
		{
			// We're wrapping, create dummy allocation and start at the begining
			uint32 Leftover = PerFrameMax*4 - OffsetVB;
			PoolAllocation* Alloc = new PoolAllocation;
			Alloc->BasePointer = 0;
			Alloc->Offset = 0;
			Alloc->AlignmentPadding = 0;
			Alloc->SizeWithoutPadding = Leftover;
			Alloc->FrameRetired = GFrameNumberRenderThread;

			AllocationList.Add(Alloc);
			OffsetVB = 0;
			FreeSpace -= Leftover;

			AllocHeadPtr = *reinterpret_cast<const uintptr_t*>(&PoolPointer) + OffsetVB;
			AlignmentPadBytes = ((AllocHeadPtr + AlignmentSubOne) & (~AlignmentSubOne)) - AllocHeadPtr;
			SizeWithAlignmentPad = Size + AlignmentPadBytes;
		}

		//Again check if we have room
		if (SizeWithAlignmentPad > FreeSpace)
		{
			return nullptr;
		}

		PoolAllocation* Alloc = new PoolAllocation;
		Alloc->BasePointer = PoolPointer + OffsetVB;
		Alloc->Offset = Offset;
		Alloc->AlignmentPadding = AlignmentPadBytes;
		Alloc->SizeWithoutPadding = Size;
		Alloc->FrameRetired = -1;

		AllocationList.Add(Alloc);
		AllocationMap.Add(Target, Alloc);
		OffsetVB += SizeWithAlignmentPad;
		FreeSpace -= SizeWithAlignmentPad;
		FrameBytes += SizeWithAlignmentPad;

		return Alloc->BasePointer + Alloc->AlignmentPadding;

	}

	return nullptr;
}

bool RetireAllocation( FOpenGLVertexBuffer* Target)
{
	if (FOpenGL::SupportsBufferStorage() && OpenGLConsoleVariables::bUseStagingBuffer)
	{
		PoolAllocation *Alloc = 0;

		if ( AllocationMap.RemoveAndCopyValue(Target, Alloc))
		{
			check(Alloc);
			Target->Bind();

			FOpenGL::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_ARRAY_BUFFER, (Alloc->BasePointer + Alloc->AlignmentPadding) - PoolPointer, Alloc->Offset, Alloc->SizeWithoutPadding);

			Alloc->FrameRetired = GFrameNumberRenderThread;

			return true;
		}
	}
	return false;
}

void BeginFrame_VertexBufferCleanup()
{
	if (GFrameNumberRenderThread < 3)
	{
		return;
	}

	int32 NumToRetire = 0;
	int32 FrameToRecover = GFrameNumberRenderThread - 3;

	while (NumToRetire < AllocationList.Num())
	{
		PoolAllocation *Alloc = AllocationList[NumToRetire];
		if (Alloc->FrameRetired < 0 || Alloc->FrameRetired > FrameToRecover)
		{
			break;
		}
		FreeSpace += (Alloc->SizeWithoutPadding + Alloc->AlignmentPadding);
		delete Alloc;
		NumToRetire++;
	}

	AllocationList.RemoveAt(0,NumToRetire);
	FrameBytes = 0;
}

FVertexBufferRHIRef FOpenGLDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FOpenGLVertexBuffer();
	}

	const void *Data = NULL;

	// If a resource array was provided for the resource, create the resource pre-populated
	if(CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
		Data = CreateInfo.ResourceArray->GetResourceData();
	}

	TRefCountPtr<FOpenGLVertexBuffer> VertexBuffer = new FOpenGLVertexBuffer(0, Size, InUsage, Data);
	
	if (CreateInfo.ResourceArray)
	{
		CreateInfo.ResourceArray->Discard();
	}
	
	return VertexBuffer.GetReference();
}

void* FOpenGLDynamicRHI::LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	check(Size > 0);
	RHITHREAD_GLCOMMAND_PROLOGUE();

	VERIFY_GL_SCOPE();
	FOpenGLVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	{
		if (VertexBuffer->IsDynamic() && LockMode == EResourceLockMode::RLM_WriteOnly)
		{
			void *Staging = GetAllocation(VertexBuffer, Size, Offset);
			if (Staging)
			{
				return Staging;
			}
		}
		return (void*)VertexBuffer->Lock(Offset, Size, LockMode == EResourceLockMode::RLM_ReadOnly, VertexBuffer->IsDynamic());
	}
	RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(void*);
}

void FOpenGLDynamicRHI::UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	RHITHREAD_GLCOMMAND_PROLOGUE();
	VERIFY_GL_SCOPE();
	FOpenGLVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	if (!RetireAllocation(VertexBuffer))
	{
		VertexBuffer->Unlock();
	}
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

void FOpenGLDynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	check(SourceBufferRHI && DestBufferRHI && SourceBufferRHI->GetSize() == DestBufferRHI->GetSize());
	RHICopyBufferRegion(DestBufferRHI, 0, SourceBufferRHI, 0, SourceBufferRHI->GetSize());
}

void FOpenGLDynamicRHI::RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
{
	VERIFY_GL_SCOPE();
	check(DestVertexBuffer);
	FOpenGLVertexBuffer* Dest = ResourceCast(DestVertexBuffer);
	if (!SrcVertexBuffer)
	{
		TRefCountPtr<FOpenGLVertexBuffer> Src = new FOpenGLVertexBuffer();
		Dest->Swap(*Src);
	}
	else
	{
		FOpenGLVertexBuffer* Src = ResourceCast(SrcVertexBuffer);
		Dest->Swap(*Src);
	}
}

void FOpenGLDynamicRHI::RHICopyBufferRegion(FRHIVertexBuffer* DestBufferRHI, uint64 DstOffset, FRHIVertexBuffer* SourceBufferRHI, uint64 SrcOffset, uint64 NumBytes)
{
	VERIFY_GL_SCOPE();
	FOpenGLVertexBuffer* SourceBuffer = ResourceCast(SourceBufferRHI);
	FOpenGLVertexBuffer* DestBuffer = ResourceCast(DestBufferRHI);

	glBindBuffer(GL_COPY_READ_BUFFER, SourceBuffer->Resource);
	glBindBuffer(GL_COPY_WRITE_BUFFER, DestBuffer->Resource);
	FOpenGL::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, SrcOffset, DstOffset, NumBytes);
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

FStagingBufferRHIRef FOpenGLDynamicRHI::RHICreateStagingBuffer()
{
	return new FOpenGLStagingBuffer();
}

void FOpenGLStagingBuffer::Initialize()
{
	ShadowBuffer = 0;
	ShadowSize = 0;
	Mapping = nullptr;
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	RHITHREAD_GLCOMMAND_PROLOGUE();
	VERIFY_GL_SCOPE();
	glGenBuffers(1, &ShadowBuffer);
	RHITHREAD_GLCOMMAND_EPILOGUE();
}

FOpenGLStagingBuffer::~FOpenGLStagingBuffer()
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	RHITHREAD_GLCOMMAND_PROLOGUE();
	VERIFY_GL_SCOPE();
	glDeleteBuffers(1, &ShadowBuffer);
	RHITHREAD_GLCOMMAND_EPILOGUE_NORETURN();
}

// If we do not support the BufferStorage extension or if PersistentMapping is set to false, this will send the command to the RHI and flush it
// If we do support BufferStorage extension and PersistentMapping is set to true, we just return the pointer + offset
void* FOpenGLStagingBuffer::Lock(uint32 Offset, uint32 NumBytes)
{
	if (!FOpenGL::SupportsBufferStorage() || !OpenGLConsoleVariables::bUsePersistentMappingStagingBuffer)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		RHITHREAD_GLCOMMAND_PROLOGUE();
		VERIFY_GL_SCOPE();

		check(ShadowBuffer != 0);
		glBindBuffer(GL_COPY_WRITE_BUFFER, ShadowBuffer);
		void* LocalMapping = FOpenGL::MapBufferRange(GL_COPY_WRITE_BUFFER, 0, NumBytes, FOpenGL::EResourceLockMode::RLM_ReadOnly);
		check(LocalMapping);
		return reinterpret_cast<uint8*>(LocalMapping) + Offset;

		RHITHREAD_GLCOMMAND_EPILOGUE_RETURN(void*);
	}
	else
	{
		check(Mapping != nullptr);
		return reinterpret_cast<uint8*>(Mapping) + Offset;
	}
}

// If we do not support the BufferStorage extension or if PersistentMapping is set to false, this will send the command to the RHI and flush it
// If we do support BufferStorage extension and PersistentMapping is set to true, we do nothing
void FOpenGLStagingBuffer::Unlock()
{
	if (!FOpenGL::SupportsBufferStorage() || !OpenGLConsoleVariables::bUsePersistentMappingStagingBuffer)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		RHITHREAD_GLCOMMAND_PROLOGUE();
		FOpenGL::UnmapBuffer(GL_COPY_WRITE_BUFFER);
		Mapping = nullptr;
		glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
		RHITHREAD_GLCOMMAND_EPILOGUE();
	}
}

void* FOpenGLDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	FOpenGLStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	return Buffer->Lock(Offset, SizeRHI);	
}

void FOpenGLDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	FOpenGLStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	Buffer->Unlock();
}

void* FOpenGLDynamicRHI::LockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	check(IsInRenderingThread());
	if (!Fence || !Fence->Poll() || Fence->NumPendingWriteCommands.GetValue() != 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockStagingBuffer_Flush);
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_LockStagingBuffer_RenderThread);
		return GDynamicRHI->RHILockStagingBuffer(StagingBuffer, Fence, Offset, SizeRHI);
	}
}

void FOpenGLDynamicRHI::UnlockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FDynamicRHI_UnlockStagingBuffer_RenderThread);
	check(IsInRenderingThread());
	GDynamicRHI->RHIUnlockStagingBuffer(StagingBuffer);
}
