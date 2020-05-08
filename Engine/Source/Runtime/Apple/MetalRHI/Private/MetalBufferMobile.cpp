// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalBufferMobile.cpp: Implements buffer backing on Apple Mobile platforms
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "MetalCommandQueue.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"
#include "MetalLLM.h"
#include <objc/runtime.h>

#if PLATFORM_IOS

int32 GMetalForceOrphanRename = 0;
static FAutoConsoleVariableRef CVarMetalForceOrphanRename(
	TEXT("rhi.Metal.MobileForceOrphanRename"),
	GMetalForceOrphanRename,
	TEXT("Forces Buffer Lock() to orphan and rename the backing."));

int32 GMetalBlitLocksInsideRenderpasses = 0;
static FAutoConsoleVariableRef CVarMetalBlitLocksInsideRenderpasses(
	TEXT("rhi.Metal.MobileForceBlitLocksInsideRenderpasses"),
	GMetalBlitLocksInsideRenderpasses,
	TEXT("Forces Buffer Lock() inside a renderpass to Blit updates. BEWARE: This may introduce ordering issues."));

int32 GMetalAllowMultipleBlitsPerFrame = 0;
static FAutoConsoleVariableRef CVarMetalAllowMultipleBlitsPerFrame(
	TEXT("rhi.Metal.MobileAllowMultipleBlitsPerFrame"),
	GMetalAllowMultipleBlitsPerFrame,
	TEXT("Allows Buffer Lock() to Blit even if this buffer has been updated previously this frame. BEWARE: This may introduce ordering issues."));

#if STATS
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size) \
	switch(Type)	{ \
		case RRT_UniformBuffer: INC_DWORD_STAT_BY(STAT_MetalUniform##Name, Size); break; \
		case RRT_IndexBuffer: INC_DWORD_STAT_BY(STAT_MetalIndex##Name, Size); break; \
		case RRT_StructuredBuffer: \
		case RRT_VertexBuffer: INC_DWORD_STAT_BY(STAT_MetalVertex##Name, Size); break; \
		default: break; \
	}
#else
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size)
#endif

@implementation FMetalBufferData

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FMetalBufferData)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		self->Data = nullptr;
		self->Len = 0;
	}
	return Self;
}
-(instancetype)initWithSize:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
	}
	return Self;
}
-(instancetype)initWithBytes:(void const*)InData length:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
		FMemory::Memcpy(self->Data, InData, InSize);
	}
	return Self;
}
-(void)dealloc
{
	if (self->Data)
	{
		FMemory::Free(self->Data);
		self->Data = nullptr;
		self->Len = 0;
	}
	[super dealloc];
}
@end

void FMetalRHIBuffer::Swap(FMetalRHIBuffer& Other)
{
	::Swap(*this, Other);
	Other.Buffer.SetOwner(&Other, true);
	Buffer.SetOwner(this, true);
}

FMetalRHIBuffer::FMetalRHIBuffer(uint32 InSize, uint32 InUsage, ERHIResourceType InType)
: Data(nullptr)
, LastUpdate(0)
, LockOffset(0)
, LockSize(0)
, Size(InSize)
, Mode(mtlpp::StorageMode::Shared)
, Usage(InUsage)
{
	Type = InType;
	bLocked = false;
	bLocked_Blit = false;
	bLocked_Rename = false;
	bLocked_Immediate = false;
	bLocked_Read = false;
	// No life-time usage information? Enforce Dynamic.
	if ((Usage & (BUF_Volatile|BUF_Dynamic|BUF_Static)) == 0)
	{
		Usage |= BUF_Dynamic;
	}
	
	if (InSize)
	{
		checkf(InSize <= 1024 * 1024 * 1024, TEXT("Metal doesn't support buffers > 1GB"));
		
		// Temporary buffers less than the buffer page size - currently 4Kb - is better off going through the set*Bytes API if available.
		// These can't be used for shader resources or UAVs if we want to use the 'Linear Texture' code path
		if (!(InUsage & (BUF_UnorderedAccess|BUF_ShaderResource|EMetalBufferUsage_GPUOnly)) && (InUsage & BUF_Volatile) && InSize < MetalBufferPageSize && (InSize < MetalBufferBytesSize))
		{
			Data = [[FMetalBufferData alloc] initWithSize:InSize];
			METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
		}
		else
		{
			uint32 AllocSize = Size;
			
			if ((InUsage & EMetalBufferUsage_LinearTex) && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
			{
				if ((InUsage & BUF_UnorderedAccess) && ((InSize - AllocSize) < 512))
				{
					// Padding for write flushing when not using linear texture bindings for buffers
					AllocSize = Align(AllocSize + 512, 1024);
				}
				
				if (InUsage & (BUF_ShaderResource|BUF_UnorderedAccess))
				{
					uint32 NumElements = AllocSize;
					uint32 SizeX = NumElements;
					uint32 SizeY = 1;
					uint32 Dimension = GMaxTextureDimensions;
					while (SizeX > GMaxTextureDimensions)
					{
						while((NumElements % Dimension) != 0)
						{
							check(Dimension >= 1);
							Dimension = (Dimension >> 1);
						}
						SizeX = Dimension;
						SizeY = NumElements / Dimension;
						if(SizeY > GMaxTextureDimensions)
						{
							Dimension <<= 1;
							checkf(SizeX <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %u to a 2D texture."), Dimension, (int32)GMaxTextureDimensions, AllocSize);
							check(Dimension <= GMaxTextureDimensions);
							AllocSize = Align(Size, Dimension);
							NumElements = AllocSize;
							SizeX = NumElements;
						}
					}
					
					AllocSize = Align(AllocSize, 1024);
				}
			}
			
			Allocate(AllocSize);
		}
	}
}

FMetalRHIBuffer::~FMetalRHIBuffer()
{
	for (auto& Pair : LinearTextures)
	{
		SafeReleaseMetalTexture(Pair.Value);
		Pair.Value = nil;
	}
	LinearTextures.Empty();
	
	if (CPUBuffer)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, CPUBuffer.GetLength());
		SafeReleaseMetalBuffer(CPUBuffer);
	}
	if (Buffer)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Buffer.GetLength());
		SafeReleaseMetalBuffer(Buffer);
	}
	if (Data)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Size);
		SafeReleaseMetalObject(Data);
	}
}

void FMetalRHIBuffer::Allocate(uint32 InSize)
{
	check(!Buffer);
	// Note that iOS buffers are always shared.
	check(Mode == mtlpp::StorageMode::Shared);
	
	FMetalPooledBufferArgs Args(GetMetalDeviceContext().GetDevice(), InSize, Usage, Mode);
	Buffer = GetMetalDeviceContext().CreatePooledBuffer(Args);
	METAL_FATAL_ASSERT(Buffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)Mode);

	Buffer.SetOwner(this, false);

	METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
	
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)))
	{
		for (auto& Pair : LinearTextures)
		{
			SafeReleaseMetalTexture(Pair.Value);
			Pair.Value = nil;
			
			Pair.Value = AllocLinearTexture(Pair.Key.Key, Pair.Key.Value);
			check(Pair.Value);
		}
	}
}

void FMetalRHIBuffer::AllocateTransferBuffer(uint32 InSize)
{
	check(!CPUBuffer);
	
	FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), InSize, BUF_Dynamic, mtlpp::StorageMode::Shared);
    CPUBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
	CPUBuffer.SetOwner(this, false);
	check(CPUBuffer && CPUBuffer.GetPtr());
	METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
	METAL_FATAL_ASSERT(CPUBuffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)mtlpp::StorageMode::Shared);
}

FMetalTexture FMetalRHIBuffer::AllocLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor& LinearTextureDesc)
{
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)))
	{
		mtlpp::PixelFormat MTLFormat = (mtlpp::PixelFormat)GMetalBufferFormats[InFormat].LinearTextureFormat;
		
		mtlpp::TextureDescriptor Desc;
		NSUInteger Options = ((NSUInteger)Mode << mtlpp::ResourceStorageModeShift) | ((NSUInteger)Buffer.GetCpuCacheMode() << mtlpp::ResourceCpuCacheModeShift);
		Options = FMetalCommandQueue::GetCompatibleResourceOptions(mtlpp::ResourceOptions(Options | mtlpp::ResourceOptions::HazardTrackingModeUntracked));
		NSUInteger TexUsage = mtlpp::TextureUsage::Unknown;
		if (Usage & BUF_ShaderResource)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderRead;
		}
		if (Usage & BUF_UnorderedAccess)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderWrite;
		}
		
		uint32 BytesPerElement = (0 == LinearTextureDesc.BytesPerElement) ? GPixelFormats[InFormat].BlockBytes : LinearTextureDesc.BytesPerElement;
		if (MTLFormat == mtlpp::PixelFormat::RG11B10Float && MTLFormat != (mtlpp::PixelFormat)GPixelFormats[InFormat].PlatformFormat)
		{
			BytesPerElement = 4;
		}

		const uint32 MinimumByteAlignment = GetMetalDeviceContext().GetDevice().GetMinimumLinearTextureAlignmentForPixelFormat((mtlpp::PixelFormat)GMetalBufferFormats[InFormat].LinearTextureFormat);
		const uint32 MinimumElementAlignment = MinimumByteAlignment / BytesPerElement;

		uint32 Offset = LinearTextureDesc.StartElement * BytesPerElement;
		check(Offset % MinimumByteAlignment == 0);

		uint32 NumElements = (UINT_MAX == LinearTextureDesc.NumElements) ? ((Size - Offset) / BytesPerElement) : LinearTextureDesc.NumElements;
		NumElements = Align(NumElements, MinimumElementAlignment);

		uint32 RowBytes = NumElements * BytesPerElement;

		if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
		{
			Desc = mtlpp::TextureDescriptor::TextureBufferDescriptor(MTLFormat, NumElements, mtlpp::ResourceOptions(Options), mtlpp::TextureUsage(TexUsage));
			Desc.SetAllowGPUOptimisedContents(false);
		}
		else
		{
			uint32 Width = NumElements;
			uint32 Height = 1;

			if (NumElements > GMaxTextureDimensions)
			{
				uint32 Dimension = GMaxTextureDimensions;
				while ((NumElements % Dimension) != 0)
				{
					check(Dimension >= 1);
					Dimension = (Dimension >> 1);
				}

				Width = Dimension;
				Height = NumElements / Dimension;

				// If we're just trying to fit as many elements as we can into
				// the available buffer space, we can trim some padding at the
				// end of the buffer in order to create widest possible linear
				// texture that will fit.
				if ((UINT_MAX == LinearTextureDesc.NumElements) && (Height > GMaxTextureDimensions))
				{
					Width = GMaxTextureDimensions;
					Height = 1;

					while ((Width * Height) < NumElements)
					{
						Height <<= 1;
					}

					while ((Width * Height) > NumElements)
					{
						Height -= 1;
					}
				}

				checkf(Width <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), Width, (int32)GMaxTextureDimensions, Buffer.GetLength(), BytesPerElement, NumElements);
				checkf(Height <= GMaxTextureDimensions, TEXT("Calculated height %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), Height, (int32)GMaxTextureDimensions, Buffer.GetLength(), BytesPerElement, NumElements);
			}

			RowBytes = Width * BytesPerElement;

			check(RowBytes % MinimumByteAlignment == 0);
			check((RowBytes * Height) + Offset <= Buffer.GetLength());

			Desc = mtlpp::TextureDescriptor::Texture2DDescriptor(MTLFormat, Width, Height, NO);
			Desc.SetStorageMode(Mode);
			Desc.SetCpuCacheMode(Buffer.GetCpuCacheMode());
			Desc.SetUsage((mtlpp::TextureUsage)TexUsage);
			Desc.SetResourceOptions((mtlpp::ResourceOptions)Options);
		}

		FMetalTexture Texture = MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, NewTexture(Desc, Offset, RowBytes));
		METAL_FATAL_ASSERT(Texture, TEXT("Failed to create linear texture, desc %s from buffer %s"), *FString([Desc description]), *FString([Buffer description]));

		return Texture;
	}
	else
	{
		return nil;
	}
}

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[InFormat].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		if (IsRunningRHIInSeparateThread() && !IsInRHIThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass())
		{
			// Impossible on iOS
			check(0);
		}
		else
		{
			LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FMetalLinearTextureDescriptor());

			FMetalTexture* ExistingTexture = LinearTextures.Find(MapKey);
			if (ExistingTexture)
			{
				Texture = *ExistingTexture;
			}
			else
			{
				FMetalTexture NewTexture = AllocLinearTexture(InFormat, MapKey.Value);
				check(NewTexture);
				check(GMetalBufferFormats[InFormat].LinearTextureFormat == mtlpp::PixelFormat::RG11B10Float || GMetalBufferFormats[InFormat].LinearTextureFormat == (mtlpp::PixelFormat)NewTexture.GetPixelFormat());
				LinearTextures.Add(MapKey, NewTexture);
				Texture = NewTexture;
			}
		}
	}
	return Texture;
}

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::GetLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[InFormat].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FMetalLinearTextureDescriptor());

		FMetalTexture* ExistingTexture = LinearTextures.Find(MapKey);
		if (ExistingTexture)
		{
			Texture = *ExistingTexture;
		}
	}
	return Texture;
}

// Assumes 'Buffer' exists.
// Assumes we are on the Rendering Thread.
uint8* FMetalRHIBuffer::GetPointerForReadLock(uint32 InOffset, uint32 InSize)
{
	// On iOS there is nothing special for a read lock. The backing is already available.
	bLocked = true;
	bLocked_Read = true;
	return (uint8*) MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents()) + InOffset;
}

// Assumes the backing 'Buffer' exists.
// Assumes we are in the RenderingThread
uint8* FMetalRHIBuffer::GetPointerForWriteLock(uint32 InOffset, uint32 InSize)
{
	FRHICommandListImmediate& CmdList = FRHICommandListExecutor::GetImmediateCommandList();
	
	// If this buffer has no views it is faster to orphan -> rename
	// If we are locking inside a renderpass we must orphan -> rename to preserve ordering
	// If we previously updated this buffer in this frame we will very conservatively orphan -> rename
	const bool bIsInsideRenderpass = CmdList.IsInsideRenderPass() && !GMetalBlitLocksInsideRenderpasses;
	const bool bIsRawBuffer = (Usage & (BUF_UnorderedAccess | BUF_ShaderResource)) == 0;
	const bool bWasLockedThisFrame = (LastUpdate == GetMetalDeviceContext().GetFrameNumberRHIThread()) && !GMetalAllowMultipleBlitsPerFrame;
	const bool bOrphanRename = bIsInsideRenderpass || bIsRawBuffer || bWasLockedThisFrame || GMetalForceOrphanRename;
	
	// If this buffer has never been locked we can return the backing directly as it must be clean.
	if(LastUpdate == 0)
	{
		bLocked = true;
		bLocked_Immediate = true;
		return (uint8*) MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents()) + InOffset;
	}
	
	uint32 ExistingBufferLength = Buffer.GetLength();
	uint8* BufferToReturn = nullptr;
	
	if(bOrphanRename)
	{
		// Orphan 'Buffer'
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, ExistingBufferLength);
		SafeReleaseMetalBuffer(Buffer);
		Buffer = nil;
		
		// Rename this buffer to a new allocation.
		// Recreate all texture views.
		// We use ExistingBufferLength here because the buffer may have been padded to deal with
		// texture view alignment issues in the ctor.
		Allocate(ExistingBufferLength);
		
		bLocked = true;
		bLocked_Rename = true;
		
		BufferToReturn = (uint8*) MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents()) + InOffset;
	}
	else
	{
		// Should be safe to blit this buffer async.
		// Can probably get away with just allocating InSize.
		AllocateTransferBuffer(ExistingBufferLength);
		
		bLocked = true;
		bLocked_Blit = true;
		
		BufferToReturn = (uint8*) MTLPP_VALIDATE(mtlpp::Buffer, CPUBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents());
	}
	
	// Should never get here.
	check(BufferToReturn);
	return BufferToReturn;
}

/*
 Main Lock() logic.
 Assumes all backing buffers are Shared. Assumes we are on the render thread.
 There are several possible paths:
 Read:
	Immediately returns the backing.
 
 Write:
	WriteOnly_NoOverwrite
		Immediately returns the backing, similar to D3D NO_OVERWRITE.
	WriteOnly:
		If we can we will perform an inline blit.
		This is only possible if we are not inside a renderpass or if the resource has not already been updated this frame.
		The RHI does not strictly define what happens with a Lock() inside a renderpass so this assumes it expects the D3D MAP_DISCARD behavior.
		Blits are applied at the start of the renderpass so it would be technically correct to blit a buffer that was modified earlier this frame
		but within a different renderpass. We don't currently track that so we must be conservative and assume the MAP_DISCARD behavior.
 
		Otherwise we will orphan the current backing and rename this buffer.
		Note that will will also orphan->rename buffers that do not have any views as that ends up being faster.
 */
void* FMetalRHIBuffer::Lock(bool bIsOnRHIThread, EResourceLockMode InLockMode, uint32 InOffset, uint32 InSize)
{
	check(IsInRenderingThread());
	check(!bLocked);
	check(LockSize == 0 && LockOffset == 0);
	check(!CPUBuffer);
	
	if (Data)
	{
		check(Data->Data);
		bLocked = true;
		return ((uint8*)Data->Data) + InOffset;
	}
	
	check(Buffer);
	
	uint32 ExistingBufferLength = Buffer.GetLength();
	
	uint8* Backing = nullptr;
	
	// Returns the pointer immediately and it's the caller's responsibility to not stomp.
	if(InLockMode == RLM_WriteOnly_NoOverwrite)
	{
		bLocked = true;
		bLocked_Immediate = true;
		
		Backing = (uint8*)MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents()) + InOffset;
	}
	else if(InLockMode == RLM_WriteOnly)
	{
		Backing = GetPointerForWriteLock(InOffset, InSize);
	}
	else if(InLockMode == RLM_ReadOnly)
	{
		Backing = GetPointerForReadLock(InOffset, InSize);
	}
	
	LockSize = InSize;
	LockOffset = InOffset;
	
	check(Backing);
	return Backing;
}

void FMetalRHIBuffer::Unlock()
{
	check(IsInRenderingThread());
	check(bLocked);
	
	// This is a fake buffer so do nothing.
	if(Data)
	{
		bLocked = false;
		return;
	}
	
	if(bLocked_Blit)
	{
		check(CPUBuffer);
		// Update via inline blit.
		// Async copies will be placed in a blit encoder before the current renderpass.
		check(CPUBuffer.GetLength() + LockOffset <= Buffer.GetLength());
		GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(CPUBuffer, 0, Buffer, LockOffset, CPUBuffer.GetLength());

		METAL_INC_DWORD_STAT_BY(Type, MemFreed, CPUBuffer.GetLength());
		SafeReleaseMetalBuffer(CPUBuffer);
		CPUBuffer = nil;
	}
	else
	{
		// Since Metal buffers always have a cpu mapping nothing happens here.
		check(bLocked_Read || bLocked_Immediate || bLocked_Rename);
	}
	
	bLocked_Blit = false;
	bLocked_Rename = false;
	bLocked_Immediate = false;
	bLocked_Read = false;
	bLocked = false;
	LockOffset = 0;
	LockSize = 0;
	LastUpdate = GetMetalDeviceContext().GetFrameNumberRHIThread();
	
	check(Buffer);
	check(!CPUBuffer);
}

void FMetalRHIBuffer::Init_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource)
{
	check(IsInRenderingThread());
	
	if(Data)
	{
		if(CreateInfo.ResourceArray)
		{
			FMemory::Memcpy(Data->Data, CreateInfo.ResourceArray->GetResourceData(), InSize);
		}
		return;
	}
	
	check(Buffer);
	check(!CPUBuffer);
	
	if (CreateInfo.ResourceArray)
	{
		check(InSize == CreateInfo.ResourceArray->GetResourceDataSize());
		check(Buffer.GetLength() >= InSize);
		
		FMemory::Memcpy(Buffer.GetContents(), CreateInfo.ResourceArray->GetResourceData(), InSize);

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
	
	if (GMetalBufferZeroFill)
	{
		FMemory::Memzero(Buffer.GetContents(), Buffer.GetLength());
	}
}

#endif
