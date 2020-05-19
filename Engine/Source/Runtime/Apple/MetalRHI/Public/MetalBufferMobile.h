// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalBufferMobile.h: Backing for buffers on Apple mobile platforms
=============================================================================*/

#pragma once

class FMetalRHIBuffer
{
public:
	using LinearTextureMapKey = TTuple<EPixelFormat, FMetalLinearTextureDescriptor>;
	using LinearTextureMap = TMap<LinearTextureMapKey, FMetalTexture>;
	
	FMetalRHIBuffer(uint32 InSize, uint32 InUsage, ERHIResourceType InType);
	virtual ~FMetalRHIBuffer();
	
	/**
	 * Initialize the buffer contents from the render-thread.
	 */
	void Init_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource);
	
	uint8* GetPointerForWriteLock(uint32 Offset, uint32 Size);
	uint8* GetPointerForReadLock(uint32 Offset, uint32 Size);
	
	/**
	 * Get a linear texture for given format.
	 */
	ns::AutoReleased<FMetalTexture> CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor = nullptr);
	
	/**
	 * Get a linear texture for given format.
	 */
	ns::AutoReleased<FMetalTexture> GetLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor = nullptr);
	
	/**
	 * Prepare a CPU accessible buffer for uploading to GPU memory
	 */
	void* Lock(bool bIsOnRHIThread, EResourceLockMode LockMode, uint32 Offset, uint32 Size=0);
	
	/**
	 * Prepare a CPU accessible buffer for uploading to GPU memory
	 */
	void Unlock();
	
	void Swap(FMetalRHIBuffer& Other);
	
	void Alias() {}
	void Unalias() {}
	
	// Backing store
	FMetalBuffer Buffer;
	/** Buffer for small buffers < 4Kb to avoid heap fragmentation. */
	FMetalBufferData* Data;
	
	// A temporary shared/CPU accessible buffer for upload/download
	FMetalBuffer CPUBuffer;
	
	// The map of linear textures for this vertex buffer - may be more than one due to type conversion.
	LinearTextureMap LinearTextures;
	
	// The last frame this buffer was Unlock()ed.
	uint32 LastUpdate;
	
	// offset into the buffer (for lock usage)
	uint32 LockOffset;
	
	// Sizeof outstanding lock.
	uint32 LockSize;
	
	// Initial buffer size.
	uint32 Size;
	
	mtlpp::StorageMode Mode;
	
	// Buffer usage.
	uint32 Usage;
	
	constexpr static const uint32 ResourceTypeBits = 5;
	
	// Buffer type (ie, BUF_Dynamic, etc)
	uint32 Type				: 5;
	// Was locked.
	bool bLocked			: 1;
	// Was locked for Read
	bool bLocked_Read		: 1;
	// Was locked for Write. Backing was orphaned and this buffer was renamed.
	bool bLocked_Rename		: 1;
	// Was locked for Write. Buffer was updated via blit.
	bool bLocked_Blit 		: 1;
	// Was locked for Write_NoOverwrite
	bool bLocked_Immediate	: 1;
	uint32 Pad				: 22;
	
	static_assert((1 << ResourceTypeBits) > RRT_Num, "ERHIResourceType doesn't fit");
	
private:
	/**
	 * Allocate a linear texture for given format.
	 */
	FMetalTexture AllocLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor& InLinearTextureDescriptor);
	
	void Allocate(uint32 Size);
	void AllocateTransferBuffer(uint32 Size);
};
