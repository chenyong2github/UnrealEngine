// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIValidation.h: Public Valdation RHI definitions.
=============================================================================*/

#pragma once 

#include "RHIValidationCommon.h"

#if ENABLE_RHI_VALIDATION
class FValidationComputeContext;
class FValidationContext;

class FValidationRHI : public FDynamicRHI
{
public:
	RHI_API FValidationRHI(FDynamicRHI* InRHI);
	RHI_API virtual ~FValidationRHI();

	virtual void Init() override final
	{
		RHI->Init();
		RHIName = RHI->GetName();
		RHIName += TEXT("_Validation");
	}

	/** Called after the RHI is initialized; before the render thread is started. */
	virtual void PostInit() override final
	{
		// Need to copy this as each DynamicRHI has an instance
		check(RHI->PixelFormatBlockBytes.Num() <= PixelFormatBlockBytes.Num());
		RHI->PixelFormatBlockBytes = PixelFormatBlockBytes;
		RHI->PostInit();
	}

	/** Shutdown the RHI; handle shutdown and resource destruction before the RHI's actual destructor is called (so that all resources of the RHI are still available for shutdown). */
	virtual void Shutdown() override final
	{
		RHI->Shutdown();
	}

	virtual const TCHAR* GetName() override final
	{
		return *RHIName;
	}

	/////// RHI Methods

	// FlushType: Thread safe
	virtual FSamplerStateRHIRef RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer) override final
	{
		return RHI->RHICreateSamplerState(Initializer);
	}

	// FlushType: Thread safe
	virtual FRasterizerStateRHIRef RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer) override final
	{
		return RHI->RHICreateRasterizerState(Initializer);
	}

	// FlushType: Thread safe
	virtual FDepthStencilStateRHIRef RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer) override final
	{
		FDepthStencilStateRHIRef State = RHI->RHICreateDepthStencilState(Initializer);
		DepthStencilStates.FindOrAdd(State.GetReference()) = Initializer;
		return State;
	}

	// FlushType: Thread safe
	virtual FBlendStateRHIRef RHICreateBlendState(const FBlendStateInitializerRHI& Initializer) override final
	{
		return RHI->RHICreateBlendState(Initializer);
	}

	// FlushType: Wait RHI Thread
	virtual FVertexDeclarationRHIRef RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements) override final
	{
		return RHI->RHICreateVertexDeclaration(Elements);
	}

	// FlushType: Wait RHI Thread
	virtual FPixelShaderRHIRef RHICreatePixelShader(const TArray<uint8>& Code) override final
	{
		return RHI->RHICreatePixelShader(Code);
	}

	// FlushType: Wait RHI Thread
	virtual FPixelShaderRHIRef RHICreatePixelShader(FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		return RHI->RHICreatePixelShader(Library, Hash);
	}

	// FlushType: Wait RHI Thread
	virtual FVertexShaderRHIRef RHICreateVertexShader(const TArray<uint8>& Code) override final
	{
		return RHI->RHICreateVertexShader(Code);
	}

	// FlushType: Wait RHI Thread
	virtual FVertexShaderRHIRef RHICreateVertexShader(FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		return RHI->RHICreateVertexShader(Library, Hash);
	}

	// FlushType: Wait RHI Thread
	virtual FHullShaderRHIRef RHICreateHullShader(const TArray<uint8>& Code) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->RHICreateHullShader(Code);
	}

	// FlushType: Wait RHI Thread
	virtual FHullShaderRHIRef RHICreateHullShader(FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->RHICreateHullShader(Library, Hash);
	}

	// FlushType: Wait RHI Thread
	virtual FDomainShaderRHIRef RHICreateDomainShader(const TArray<uint8>& Code) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->RHICreateDomainShader(Code);
	}

	// FlushType: Wait RHI Thread
	virtual FDomainShaderRHIRef RHICreateDomainShader(FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->RHICreateDomainShader(Library, Hash);
	}

	// FlushType: Wait RHI Thread
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(const TArray<uint8>& Code) override final
	{
		check(RHISupportsGeometryShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateGeometryShader(Code);
	}

	// FlushType: Wait RHI Thread
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsGeometryShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateGeometryShader(Library, Hash);
	}

	// Some RHIs can have pending messages/logs for error tracking, or debug modes
	virtual void FlushPendingLogs() override final
	{
		RHI->FlushPendingLogs();
	}

	// FlushType: Wait RHI Thread
	virtual FComputeShaderRHIRef RHICreateComputeShader(const TArray<uint8>& Code) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateComputeShader(Code);
	}
	// FlushType: Wait RHI Thread
	virtual FComputeShaderRHIRef RHICreateComputeShader(FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateComputeShader(Library, Hash);
	}

	/**
	 * Attempts to open a shader library for the given shader platform & name within the provided directory.
	 * @param Platform The shader platform for shaders withing the library.
	 * @param FilePath The directory in which the library should exist.
	 * @param Name The name of the library, i.e. "Global" or "Unreal" without shader-platform or file-extension qualification.
	 * @return The new library if one exists and can be constructed, otherwise nil.
	 */
	 // FlushType: Must be Thread-Safe.
	virtual FRHIShaderLibraryRef RHICreateShaderLibrary(EShaderPlatform Platform, FString const& FilePath, FString const& Name) override final
	{
		return RHI->RHICreateShaderLibrary(Platform, FilePath, Name);
	}

	/**
	* Creates a compute fence.  Compute fences are named GPU fences which can be written to once before resetting.
	* A command to write the fence must be enqueued before any commands to wait on them.  This is enforced on the CPU to avoid GPU hangs.
	* @param Name - Friendly name for the Fence.  e.g. ReflectionEnvironmentComplete
	* @return The new Fence.
	*/
	// FlushType: Thread safe, but varies depending on the RHI	
	virtual FComputeFenceRHIRef RHICreateComputeFence(const FName& Name) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateComputeFence(Name);
	}

	virtual FGPUFenceRHIRef RHICreateGPUFence(const FName &Name) override final
	{
		return RHI->RHICreateGPUFence(Name);
	}

	/**
	* Creates a staging buffer, which is memory visible to the cpu without any locking.
	* @return The new staging-buffer.
	*/
	// FlushType: Thread safe.	
	virtual FStagingBufferRHIRef RHICreateStagingBuffer() override final
	{
		return RHI->RHICreateStagingBuffer();
	}

	/**
	 * Lock a staging buffer to read contents on the CPU that were written by the GPU, without having to stall.
	 * @discussion This function requires that you have issued an CopyToStagingBuffer invocation and verified that the FRHIGPUFence has been signaled before calling.
	 * @param StagingBuffer The buffer to lock.
	 * @param Offset The offset in the buffer to return.
	 * @param SizeRHI The length of the region in the buffer to lock.
	 * @returns A pointer to the data starting at 'Offset' and of length 'SizeRHI' from 'StagingBuffer', or nullptr when there is an error.
	 */
	virtual void* RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 SizeRHI) override final
	{
		return RHI->RHILockStagingBuffer(StagingBuffer, Offset, SizeRHI);
	}

	/**
	 * Unlock a staging buffer previously locked with RHILockStagingBuffer.
	 * @param StagingBuffer The buffer that was previously locked.
	 */
	virtual void RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer) override final
	{
		RHI->RHIUnlockStagingBuffer(StagingBuffer);
	}

	/**
	 * Lock a staging buffer to read contents on the CPU that were written by the GPU, without having to stall.
	 * @discussion This function requires that you have issued an CopyToStagingBuffer invocation and verified that the FRHIGPUFence has been signaled before calling.
	 * @param RHICmdList The command-list to execute on or synchronize with.
	 * @param StagingBuffer The buffer to lock.
	 * @param Offset The offset in the buffer to return.
	 * @param SizeRHI The length of the region in the buffer to lock.
	 * @returns A pointer to the data starting at 'Offset' and of length 'SizeRHI' from 'StagingBuffer', or nullptr when there is an error.
	 */
	virtual void* LockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer, uint32 Offset, uint32 SizeRHI) override final
	{
		return RHI->LockStagingBuffer_RenderThread(RHICmdList, StagingBuffer, Offset, SizeRHI);
	}

	/**
	 * Unlock a staging buffer previously locked with LockStagingBuffer_RenderThread.
	 * @param RHICmdList The command-list to execute on or synchronize with.
	 * @param StagingBuffer The buffer what was previously locked.
	 */
	virtual void UnlockStagingBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStagingBuffer* StagingBuffer) override final
	{
		RHI->UnlockStagingBuffer_RenderThread(RHICmdList, StagingBuffer);
	}

	/**
	* Creates a bound shader state instance which encapsulates a decl, vertex shader, hull shader, domain shader and pixel shader
	* CAUTION: Even though this is marked as threadsafe, it is only valid to call from the render thread or the RHI thread. It need not be threadsafe unless the RHI support parallel translation.
	* CAUTION: Platforms that support RHIThread but don't actually have a threadsafe implementation must flush internally with FScopedRHIThreadStaller StallRHIThread(FRHICommandListExecutor::GetImmediateCommandList()); when the call is from the render thread
	* @param VertexDeclaration - existing vertex decl
	* @param VertexShader - existing vertex shader
	* @param HullShader - existing hull shader
	* @param DomainShader - existing domain shader
	* @param GeometryShader - existing geometry shader
	* @param PixelShader - existing pixel shader
	*/
	// FlushType: Thread safe, but varies depending on the RHI
	virtual FBoundShaderStateRHIRef RHICreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIHullShader* HullShader, FRHIDomainShader* DomainShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader) override final
	{
		return RHI->RHICreateBoundShaderState(VertexDeclaration, VertexShader, HullShader, DomainShader, PixelShader, GeometryShader);
	}

	/**
	* Creates a graphics pipeline state object (PSO) that represents a complete gpu pipeline for rendering.
	* This function should be considered expensive to call at runtime and may cause hitches as pipelines are compiled.
	* @param Initializer - Descriptor object defining all the information needed to create the PSO, as well as behavior hints to the RHI.
	* @return FGraphicsPipelineStateRHIRef that can be bound for rendering; nullptr if the compilation fails.
	* CAUTION: On certain RHI implementations (eg, ones that do not support runtime compilation) a compilation failure is a Fatal error and this function will not return.
	* CAUTION: Even though this is marked as threadsafe, it is only valid to call from the render thread or the RHI thread. It need not be threadsafe unless the RHI support parallel translation.
	* CAUTION: Platforms that support RHIThread but don't actually have a threadsafe implementation must flush internally with FScopedRHIThreadStaller StallRHIThread(FRHICommandListExecutor::GetImmediateCommandList()); when the call is from the render thread
	*/
	// FlushType: Thread safe
	// TODO: [PSO API] Make pure virtual
	virtual FGraphicsPipelineStateRHIRef RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer) override final
	{
		ValidatePipeline(Initializer);
		return RHI->RHICreateGraphicsPipelineState(Initializer);
	}

	virtual TRefCountPtr<FRHIComputePipelineState> RHICreateComputePipelineState(FRHIComputeShader* ComputeShader) override final
	{
		return RHI->RHICreateComputePipelineState(ComputeShader);
	}

	virtual FGraphicsPipelineStateRHIRef RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer, FRHIPipelineBinaryLibrary* PipelineBinary) override final
	{
		ValidatePipeline(Initializer);
		return RHI->RHICreateGraphicsPipelineState(Initializer);
	}

	virtual TRefCountPtr<FRHIComputePipelineState> RHICreateComputePipelineState(FRHIComputeShader* ComputeShader, FRHIPipelineBinaryLibrary* PipelineBinary) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateComputePipelineState(ComputeShader, PipelineBinary);
	}

	/**
	* Creates a uniform buffer.  The contents of the uniform buffer are provided in a parameter, and are immutable.
	* CAUTION: Even though this is marked as threadsafe, it is only valid to call from the render thread or the RHI thread. Thus is need not be threadsafe on platforms that do not support or aren't using an RHIThread
	* @param Contents - A pointer to a memory block of size NumBytes that is copied into the new uniform buffer.
	* @param NumBytes - The number of bytes the uniform buffer should contain.
	* @return The new uniform buffer.
	*/
	// FlushType: Thread safe, but varies depending on the RHI override final
	virtual FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation) override final
	{
		return RHI->RHICreateUniformBuffer(Contents, Layout, Usage, Validation);
	}

	virtual void RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents) override final
	{
		RHI->RHIUpdateUniformBuffer(UniformBufferRHI, Contents);
	}

	// FlushType: Wait RHI Thread
	virtual FIndexBufferRHIRef RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateIndexBuffer(Stride, Size, InUsage, CreateInfo);
	}

	// FlushType: Flush RHI Thread
	virtual void* RHILockIndexBuffer(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) override final
	{
		return RHI->RHILockIndexBuffer(RHICmdList, IndexBuffer, Offset, SizeRHI, LockMode);
	}

	// FlushType: Flush RHI Thread
	virtual void RHIUnlockIndexBuffer(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer) override final
	{
		RHI->RHIUnlockIndexBuffer(RHICmdList, IndexBuffer);
	}

	/**
	* @param ResourceArray - An optional pointer to a resource array containing the resource's data.
	*/
	// FlushType: Wait RHI Thread
	virtual FVertexBufferRHIRef RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateVertexBuffer(Size, InUsage, CreateInfo);
	}

	// FlushType: Flush RHI Thread
	virtual void* RHILockVertexBuffer(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) override final
	{
		return RHI->RHILockVertexBuffer(RHICmdList, VertexBuffer, Offset, SizeRHI, LockMode);
	}

	// FlushType: Flush RHI Thread
	virtual void RHIUnlockVertexBuffer(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer) override final
	{
		RHI->RHIUnlockVertexBuffer(RHICmdList, VertexBuffer);
	}

	/** Copies the contents of one vertex buffer to another vertex buffer.  They must have identical sizes. */
	// FlushType: Flush Immediate (seems dangerous)
	virtual void RHICopyVertexBuffer(FRHIVertexBuffer* SourceBuffer, FRHIVertexBuffer* DestBuffer) override final
	{
		RHI->RHICopyVertexBuffer(SourceBuffer, DestBuffer);
	}

	/**
	* @param ResourceArray - An optional pointer to a resource array containing the resource's data.
	*/
	// FlushType: Wait RHI Thread
	virtual FStructuredBufferRHIRef RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateStructuredBuffer(Stride, Size, InUsage, CreateInfo);
	}

	// FlushType: Flush RHI Thread
	virtual void* RHILockStructuredBuffer(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHILockStructuredBuffer(RHICmdList, StructuredBuffer, Offset, SizeRHI, LockMode);
	}

	// FlushType: Flush RHI Thread
	virtual void RHIUnlockStructuredBuffer(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		RHI->RHIUnlockStructuredBuffer(RHICmdList, StructuredBuffer);
	}

	/** Creates an unordered access view of the given structured buffer. */
	// FlushType: Wait RHI Thread
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView(StructuredBuffer, bUseUAVCounter, bAppendBuffer);
	}

	/** Creates an unordered access view of the given texture. */
	// FlushType: Wait RHI Thread
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHITexture* Texture, uint32 MipLevel) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView(Texture, MipLevel);
	}

	/** Creates an unordered access view of the given vertex buffer. */
	// FlushType: Wait RHI Thread
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIVertexBuffer* VertexBuffer, uint8 Format) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView(VertexBuffer, Format);
	}

	/** Creates an unordered access view of the given index buffer. */
	// FlushType: Wait RHI Thread
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(FRHIIndexBuffer* IndexBuffer, uint8 Format) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView(IndexBuffer, Format);
	}

	/** Creates a shader resource view of the given structured buffer. */
	// FlushType: Wait RHI Thread
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIStructuredBuffer* StructuredBuffer) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->RHICreateShaderResourceView(StructuredBuffer);
	}

	/** Creates a shader resource view of the given vertex buffer. */
	// FlushType: Wait RHI Thread
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) override final
	{
		return RHI->RHICreateShaderResourceView(VertexBuffer, Stride, Format);
	}

	/** Creates a shader resource view of the given index buffer. */
	// FlushType: Wait RHI Thread
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHIIndexBuffer* Buffer) override final
	{
		return RHI->RHICreateShaderResourceView(Buffer);
	}

	// Must be called on RHI thread timeline
	// Make sure to call RHIThreadFence(true) afterwards so that parallel translation doesn't refer old resources
	virtual void RHIUpdateShaderResourceView(FRHIShaderResourceView* SRV, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) override final
	{
		RHI->RHIUpdateShaderResourceView(SRV, VertexBuffer, Stride, Format);
	}

	/**
	* Computes the total size of a 2D texture with the specified parameters.
	*
	* @param SizeX - width of the texture to compute
	* @param SizeY - height of the texture to compute
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to compute or 0 for full mip pyramid
	* @param NumSamples - number of MSAA samples, usually 1
	* @param Flags - ETextureCreateFlags creation flags
	* @param OutAlign - Alignment required for this texture.  Output parameter.
	*/
	// FlushType: Thread safe
	virtual uint64 RHICalcTexture2DPlatformSize(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) override final
	{
		return RHI->RHICalcTexture2DPlatformSize(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo, OutAlign);
	}

	/**
	* Computes the total size of a 3D texture with the specified parameters.
	* @param SizeX - width of the texture to create
	* @param SizeY - height of the texture to create
	* @param SizeZ - depth of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	* @param OutAlign - Alignment required for this texture.  Output parameter.
	*/
	// FlushType: Thread safe
	virtual uint64 RHICalcTexture3DPlatformSize(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) override final
	{
		return RHI->RHICalcTexture3DPlatformSize(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo, OutAlign);
	}

	/**
	* Computes the total size of a cube texture with the specified parameters.
	* @param Size - width/height of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	* @param OutAlign - Alignment required for this texture.  Output parameter.
	*/
	// FlushType: Thread safe
	virtual uint64 RHICalcTextureCubePlatformSize(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, const FRHIResourceCreateInfo& CreateInfo, uint32& OutAlign) override final
	{
		return RHI->RHICalcTextureCubePlatformSize(Size, Format, NumMips, Flags, CreateInfo, OutAlign);
	}

	/**
	* Retrieves texture memory stats.
	* safe to call on the main thread
	*/
	// FlushType: Thread safe
	virtual void RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats) override final
	{
		RHI->RHIGetTextureMemoryStats(OutStats);
	}

	/**
	* Fills a texture with to visualize the texture pool memory.
	*
	* @param	TextureData		Start address
	* @param	SizeX			Number of pixels along X
	* @param	SizeY			Number of pixels along Y
	* @param	Pitch			Number of bytes between each row
	* @param	PixelSize		Number of bytes each pixel represents
	*
	* @return true if successful, false otherwise
	*/
	// FlushType: Flush Immediate
	virtual bool RHIGetTextureMemoryVisualizeData(FColor* TextureData, int32 SizeX, int32 SizeY, int32 Pitch, int32 PixelSize) override final
	{
		return RHI->RHIGetTextureMemoryVisualizeData(TextureData, SizeX, SizeY, Pitch, PixelSize);
	}

	// FlushType: Wait RHI Thread
	virtual FTextureReferenceRHIRef RHICreateTextureReference(FLastRenderTimeContainer* LastRenderTime) override final
	{
		return RHI->RHICreateTextureReference(LastRenderTime);
	}

	/**
	* Creates a 2D RHI texture resource
	* @param SizeX - width of the texture to create
	* @param SizeY - height of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param NumSamples - number of MSAA samples, usually 1
	* @param Flags - ETextureCreateFlags creation flags
	*/
	// FlushType: Wait RHI Thread
	virtual FTexture2DRHIRef RHICreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTexture2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	/**
	* Creates a 2D RHI texture external resource
	* @param SizeX - width of the texture to create
	* @param SizeY - height of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param NumSamples - number of MSAA samples, usually 1
	* @param Flags - ETextureCreateFlags creation flags
	*/
	// FlushType: Wait RHI Thread
	virtual FTexture2DRHIRef RHICreateTextureExternal2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTextureExternal2D(SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	/**
	* Thread-safe function that can be used to create a texture outside of the
	* rendering thread. This function can ONLY be called if GRHISupportsAsyncTextureCreation
	* is true.  Cannot create rendertargets with this method.
	* @param SizeX - width of the texture to create
	* @param SizeY - height of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	* @param InitialMipData - pointers to mip data with which to create the texture
	* @param NumInitialMips - how many mips are provided in InitialMipData
	* @returns a reference to a 2D texture resource
	*/
	// FlushType: Thread safe
	virtual FTexture2DRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, void** InitialMipData, uint32 NumInitialMips) override final
	{
		check(GRHISupportsAsyncTextureCreation);
		return RHI->RHIAsyncCreateTexture2D(SizeX, SizeY, Format, NumMips, Flags, InitialMipData, NumInitialMips);
	}

	/**
	* Copies shared mip levels from one texture to another. The textures must have
	* full mip chains, share the same format, and have the same aspect ratio. This
	* copy will not cause synchronization with the GPU.
	* @param DestTexture2D - destination texture
	* @param SrcTexture2D - source texture
	*/
	// FlushType: Flush RHI Thread
	virtual void RHICopySharedMips(FRHITexture2D* DestTexture2D, FRHITexture2D* SrcTexture2D) override final
	{
		RHI->RHICopySharedMips(DestTexture2D, SrcTexture2D);
	}

	/**
	* Synchronizes the content of a texture resource between two GPUs using a copy operation.
	* @param Texture - the texture to synchronize.
	* @param Rect - the rectangle area to update.
	* @param SrcGPUIndex - the index of the gpu which content will be red from
	* @param DestGPUIndex - the index of the gpu which content will be updated.
	* @param PullData - whether the source writes the data to the dest, or the dest reads the data from the source.
	*/
	// FlushType: Flush RHI Thread
	virtual void RHITransferTexture(FRHITexture2D* Texture, FIntRect Rect, uint32 SrcGPUIndex, uint32 DestGPUIndex, bool bPullData) override final
	{
		RHI->RHITransferTexture(Texture, Rect, SrcGPUIndex, DestGPUIndex, bPullData);
	}

	/**
	* Creates a Array RHI texture resource
	* @param SizeX - width of the texture to create
	* @param SizeY - height of the texture to create
	* @param SizeZ - depth of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	*/
	// FlushType: Wait RHI Thread
	virtual FTexture2DArrayRHIRef RHICreateTexture2DArray(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo)
	{
		return RHI->RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	/**
	* Creates a 3d RHI texture resource
	* @param SizeX - width of the texture to create
	* @param SizeY - height of the texture to create
	* @param SizeZ - depth of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	*/
	// FlushType: Wait RHI Thread
	virtual FTexture3DRHIRef RHICreateTexture3D(uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTexture3D(SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
	}

	/**
	* @param Ref may be 0
	*/
	// FlushType: Thread safe
	virtual void RHIGetResourceInfo(FRHITexture* Ref, FRHIResourceInfo& OutInfo) override final
	{
		return RHI->RHIGetResourceInfo(Ref, OutInfo);
	}

	/**
	* Creates a shader resource view for a texture
	*/
	// FlushType: Wait RHI Thread
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(FRHITexture* TextureRHI, const FRHITextureSRVCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateShaderResourceView(TextureRHI, CreateInfo);
	}

	/**
	* Create a shader resource view that can be used to access the write mask metadata of a render target on supported platforms.
	*/
	// FlushType: Wait RHI Thread
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewWriteMask(FRHITexture2D* Texture2DRHI) override final
	{
		return RHI->RHICreateShaderResourceViewWriteMask(Texture2DRHI);
	}

	/**
	* Create a shader resource view that can be used to access the multi-sample fmask metadata of a render target on supported platforms.
	*/
	// FlushType: Wait RHI Thread
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewFMask(FRHITexture2D* Texture2DRHI) override final
	{
		return RHI->RHICreateShaderResourceViewFMask(Texture2DRHI);
	}

	/**
	* Generates mip maps for a texture.
	*/
	// FlushType: Flush Immediate (NP: this should be queued on the command list for RHI thread execution, not flushed)
	virtual void RHIGenerateMips(FRHITexture* Texture) override final
	{
		return RHI->RHIGenerateMips(Texture);
	}

	/**
	* Computes the size in memory required by a given texture.
	*
	* @param	TextureRHI		- Texture we want to know the size of, 0 is safely ignored
	* @return					- Size in Bytes
	*/
	// FlushType: Thread safe
	virtual uint32 RHIComputeMemorySize(FRHITexture* TextureRHI) override final
	{
		return RHI->RHIComputeMemorySize(TextureRHI);
	}

	/**
	* Starts an asynchronous texture reallocation. It may complete immediately if the reallocation
	* could be performed without any reshuffling of texture memory, or if there isn't enough memory.
	* The specified status counter will be decremented by 1 when the reallocation is complete (success or failure).
	*
	* Returns a new reference to the texture, which will represent the new mip count when the reallocation is complete.
	* RHIFinalizeAsyncReallocateTexture2D() must be called to complete the reallocation.
	*
	* @param Texture2D		- Texture to reallocate
	* @param NewMipCount	- New number of mip-levels
	* @param NewSizeX		- New width, in pixels
	* @param NewSizeY		- New height, in pixels
	* @param RequestStatus	- Will be decremented by 1 when the reallocation is complete (success or failure).
	* @return				- New reference to the texture, or an invalid reference upon failure
	*/
	// FlushType: Flush RHI Thread
	// NP: Note that no RHI currently implements this as an async call, we should simplify the API.
	virtual FTexture2DRHIRef RHIAsyncReallocateTexture2D(FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) override final
	{
		return RHI->RHIAsyncReallocateTexture2D(Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}

	/**
	* Finalizes an async reallocation request.
	* If bBlockUntilCompleted is false, it will only poll the status and finalize if the reallocation has completed.
	*
	* @param Texture2D				- Texture to finalize the reallocation for
	* @param bBlockUntilCompleted	- Whether the function should block until the reallocation has completed
	* @return						- Current reallocation status:
	*	TexRealloc_Succeeded	Reallocation succeeded
	*	TexRealloc_Failed		Reallocation failed
	*	TexRealloc_InProgress	Reallocation is still in progress, try again later
	*/
	// FlushType: Wait RHI Thread
	virtual ETextureReallocationStatus RHIFinalizeAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) override final
	{
		return RHI->RHIFinalizeAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}

	/**
	* Cancels an async reallocation for the specified texture.
	* This should be called for the new texture, not the original.
	*
	* @param Texture				Texture to cancel
	* @param bBlockUntilCompleted	If true, blocks until the cancellation is fully completed
	* @return						Reallocation status
	*/
	// FlushType: Wait RHI Thread
	virtual ETextureReallocationStatus RHICancelAsyncReallocateTexture2D(FRHITexture2D* Texture2D, bool bBlockUntilCompleted) override final
	{
		return RHI->RHICancelAsyncReallocateTexture2D(Texture2D, bBlockUntilCompleted);
	}

	/**
	* Locks an RHI texture's mip-map for read/write operations on the CPU
	* @param Texture - the RHI texture resource to lock, must not be 0
	* @param MipIndex - index of the mip level to lock
	* @param LockMode - Whether to lock the texture read-only instead of write-only
	* @param DestStride - output to retrieve the textures row stride (pitch)
	* @param bLockWithinMiptail - for platforms that support packed miptails allow locking of individual mip levels within the miptail
	* @return pointer to the CPU accessible resource data
	*/
	// FlushType: Flush RHI Thread
	virtual void* RHILockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) override final
	{
		return RHI->RHILockTexture2D(Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}

	/**
	* Unlocks a previously locked RHI texture resource
	* @param Texture - the RHI texture resource to unlock, must not be 0
	* @param MipIndex - index of the mip level to unlock
	* @param bLockWithinMiptail - for platforms that support packed miptails allow locking of individual mip levels within the miptail
	*/
	// FlushType: Flush RHI Thread
	virtual void RHIUnlockTexture2D(FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail) override final
	{
		RHI->RHIUnlockTexture2D(Texture, MipIndex, bLockWithinMiptail);
	}

	/**
	* Locks an RHI texture's mip-map for read/write operations on the CPU
	* @param Texture - the RHI texture resource to lock
	* @param MipIndex - index of the mip level to lock
	* @param LockMode - Whether to lock the texture read-only instead of write-only
	* @param DestStride - output to retrieve the textures row stride (pitch)
	* @param bLockWithinMiptail - for platforms that support packed miptails allow locking of individual mip levels within the miptail
	* @return pointer to the CPU accessible resource data
	*/
	// FlushType: Flush RHI Thread
	virtual void* RHILockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) override final
	{
		return RHI->RHILockTexture2DArray(Texture, TextureIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}

	/**
	* Unlocks a previously locked RHI texture resource
	* @param Texture - the RHI texture resource to unlock
	* @param MipIndex - index of the mip level to unlock
	* @param bLockWithinMiptail - for platforms that support packed miptails allow locking of individual mip levels within the miptail
	*/
	// FlushType: Flush RHI Thread
	virtual void RHIUnlockTexture2DArray(FRHITexture2DArray* Texture, uint32 TextureIndex, uint32 MipIndex, bool bLockWithinMiptail) override final
	{
		RHI->RHIUnlockTexture2DArray(Texture, TextureIndex, MipIndex, bLockWithinMiptail);
	}

	/**
	* Updates a region of a 2D texture from system memory
	* @param Texture - the RHI texture resource to update
	* @param MipIndex - mip level index to be modified
	* @param UpdateRegion - The rectangle to copy source image data from
	* @param SourcePitch - size in bytes of each row of the source image
	* @param SourceData - source image data, starting at the upper left corner of the source rectangle (in same pixel format as texture)
	*/
	// FlushType: Flush RHI Thread
	virtual void RHIUpdateTexture2D(FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) override final
	{
		RHI->RHIUpdateTexture2D(Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
	}

	/**
	* Updates a region of a 3D texture from system memory
	* @param Texture - the RHI texture resource to update
	* @param MipIndex - mip level index to be modified
	* @param UpdateRegion - The rectangle to copy source image data from
	* @param SourceRowPitch - size in bytes of each row of the source image, usually Bpp * SizeX
	* @param SourceDepthPitch - size in bytes of each depth slice of the source image, usually Bpp * SizeX * SizeY
	* @param SourceData - source image data, starting at the upper left corner of the source rectangle (in same pixel format as texture)
	*/
	// FlushType: Flush RHI Thread
	virtual void RHIUpdateTexture3D(FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) override final
	{
		RHI->RHIUpdateTexture3D(Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}

	/**
	* Creates a Cube RHI texture resource
	* @param Size - width/height of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	*/
	// FlushType: Wait RHI Thread
	virtual FTextureCubeRHIRef RHICreateTextureCube(uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTextureCube(Size, Format, NumMips, Flags, CreateInfo);
	}

	/**
	* Creates a Cube Array RHI texture resource
	* @param Size - width/height of the texture to create
	* @param ArraySize - number of array elements of the texture to create
	* @param Format - EPixelFormat texture format
	* @param NumMips - number of mips to generate or 0 for full mip pyramid
	* @param Flags - ETextureCreateFlags creation flags
	*/
	// FlushType: Wait RHI Thread
	virtual FTextureCubeRHIRef RHICreateTextureCubeArray(uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTextureCubeArray(Size, ArraySize, Format, NumMips, Flags, CreateInfo);
	}

	/**
	* Locks an RHI texture's mip-map for read/write operations on the CPU
	* @param Texture - the RHI texture resource to lock
	* @param MipIndex - index of the mip level to lock
	* @param LockMode - Whether to lock the texture read-only instead of write-only.
	* @param DestStride - output to retrieve the textures row stride (pitch)
	* @param bLockWithinMiptail - for platforms that support packed miptails allow locking of individual mip levels within the miptail
	* @return pointer to the CPU accessible resource data
	*/
	// FlushType: Flush RHI Thread
	virtual void* RHILockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) override final
	{
		return RHI->RHILockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}

	/**
	* Unlocks a previously locked RHI texture resource
	* @param Texture - the RHI texture resource to unlock
	* @param MipIndex - index of the mip level to unlock
	* @param bLockWithinMiptail - for platforms that support packed miptails allow locking of individual mip levels within the miptail
	*/
	// FlushType: Flush RHI Thread
	virtual void RHIUnlockTextureCubeFace(FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) override final
	{
		RHI->RHIUnlockTextureCubeFace(Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
	}

	// FlushType: Thread safe
	virtual void RHIBindDebugLabelName(FRHITexture* Texture, const TCHAR* Name) override final
	{
		RHI->RHIBindDebugLabelName(Texture, Name);
	}

	virtual void RHIBindDebugLabelName(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const TCHAR* Name) override final
	{
		RHI->RHIBindDebugLabelName(UnorderedAccessViewRHI, Name);
	}

	/**
	* Reads the contents of a texture to an output buffer (non MSAA and MSAA) and returns it as a FColor array.
	* If the format or texture type is unsupported the OutData array will be size 0
	*/
	// FlushType: Flush Immediate (seems wrong)
	virtual void RHIReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags) override final
	{
		RHI->RHIReadSurfaceData(Texture, Rect, OutData, InFlags);
	}

	// Default fallback; will not work for non-8-bit surfaces and it's extremely slow.
	virtual void RHIReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FLinearColor>& OutData, FReadSurfaceDataFlags InFlags) override final
	{
		RHI->RHIReadSurfaceData(Texture, Rect, OutData, InFlags);
	}

	/** Watch out for OutData to be 0 (can happen on DXGI_ERROR_DEVICE_REMOVED), don't call RHIUnmapStagingSurface in that case. */
	// FlushType: Flush Immediate (seems wrong)
	virtual void RHIMapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex = 0) final override
	{
		RHI->RHIMapStagingSurface(Texture, Fence, OutData, OutWidth, OutHeight, GPUIndex);
	}

	/** call after a succesful RHIMapStagingSurface() call */
	// FlushType: Flush Immediate (seems wrong)
	virtual void RHIUnmapStagingSurface(FRHITexture* Texture, uint32 GPUIndex = 0) override final
	{
		RHI->RHIUnmapStagingSurface(Texture, GPUIndex);
	}

	// FlushType: Flush Immediate (seems wrong)
	virtual void RHIReadSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex) override final
	{
		RHI->RHIReadSurfaceFloatData(Texture, Rect, OutData, CubeFace, ArrayIndex, MipIndex);
	}

	// FlushType: Flush Immediate (seems wrong)
	virtual void RHIRead3DSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData) override final
	{
		RHI->RHIRead3DSurfaceFloatData(Texture, Rect, ZMinMax, OutData);
	}

	// FlushType: Wait RHI Thread
	virtual FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType) override final
	{
		return RHI->RHICreateRenderQuery(QueryType);
	}
	// CAUTION: Even though this is marked as threadsafe, it is only valid to call from the render thread. It is need not be threadsafe on platforms that do not support or aren't using an RHIThread
	// FlushType: Thread safe, but varies by RHI
	virtual bool RHIGetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait) override final
	{
		return RHI->RHIGetRenderQueryResult(RenderQuery, OutResult, bWait);
	}

	// FlushType: Thread safe
	virtual uint32 RHIGetViewportNextPresentGPUIndex(FRHIViewport* Viewport) override final
	{
		return RHI->RHIGetViewportNextPresentGPUIndex(Viewport);
	}

	// With RHI thread, this is the current backbuffer from the perspective of the render thread.
	// FlushType: Thread safe
	virtual FTexture2DRHIRef RHIGetViewportBackBuffer(FRHIViewport* Viewport) override final
	{
		return RHI->RHIGetViewportBackBuffer(Viewport);
	}

	virtual FUnorderedAccessViewRHIRef RHIGetViewportBackBufferUAV(FRHIViewport* ViewportRHI) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHIGetViewportBackBufferUAV(ViewportRHI);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewHTile(FRHITexture2D* RenderTarget) override final
	{
		return RHI->RHICreateShaderResourceViewHTile(RenderTarget);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessViewHTile(FRHITexture2D* RenderTarget) override final
	{
		return RHI->RHICreateUnorderedAccessViewHTile(RenderTarget);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessViewStencil(FRHITexture2D* DepthTarget, int32 MipLevel) override final
	{
		return RHI->RHICreateUnorderedAccessViewStencil(DepthTarget, MipLevel);
	}

	virtual void RHIAliasTextureResources(FRHITexture* DestTexture, FRHITexture* SourceTexture) override final
	{
		// Source and target need to be valid objects.
		check(DestTexture && SourceTexture);
		// Source texture must have been created (i.e. have a native resource backing).
		check(SourceTexture->GetNativeResource() != nullptr);
		RHI->RHIAliasTextureResources(DestTexture, SourceTexture);
	}

	virtual FTextureRHIRef RHICreateAliasedTexture(FRHITexture* SourceTexture) override final
	{
		check(SourceTexture);
		return RHI->RHICreateAliasedTexture(SourceTexture);
	}

	// Only relevant with an RHI thread, this advances the backbuffer for the purpose of GetViewportBackBuffer
	// FlushType: Thread safe
	virtual void RHIAdvanceFrameForGetViewportBackBuffer(FRHIViewport* Viewport) override final
	{
		RHI->RHIAdvanceFrameForGetViewportBackBuffer(Viewport);
	}
	/*
	* Acquires or releases ownership of the platform-specific rendering context for the calling thread
	*/
	// FlushType: Flush RHI Thread
	virtual void RHIAcquireThreadOwnership() override final
	{
		RHI->RHIAcquireThreadOwnership();
	}

	// FlushType: Flush RHI Thread
	virtual void RHIReleaseThreadOwnership() override final
	{
		RHI->RHIReleaseThreadOwnership();
	}

	// Flush driver resources. Typically called when switching contexts/threads
	// FlushType: Flush RHI Thread
	virtual void RHIFlushResources() override final
	{
		RHI->RHIFlushResources();
	}

	/*
	* Returns the total GPU time taken to render the last frame. Same metric as FPlatformTime::Cycles().
	*/
	// FlushType: Thread safe
	virtual uint32 RHIGetGPUFrameCycles() override final
	{
		return RHI->RHIGetGPUFrameCycles();
	}

	//  must be called from the main thread.
	// FlushType: Thread safe
	virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) override final
	{
		return RHI->RHICreateViewport(WindowHandle, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}

	//  must be called from the main thread.
	// FlushType: Thread safe
	virtual void RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen) override final
	{
		RHI->RHIResizeViewport(Viewport, SizeX, SizeY, bIsFullscreen);
	}

	virtual void RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PreferredPixelFormat) override final
	{
		// Default implementation for RHIs that cannot change formats on the fly
		RHI->RHIResizeViewport(Viewport, SizeX, SizeY, bIsFullscreen, PreferredPixelFormat);
	}

	//  must be called from the main thread.
	// FlushType: Thread safe
	virtual void RHITick(float DeltaTime) override final
	{
		RHI->RHITick(DeltaTime);
	}

	// Blocks the CPU until the GPU catches up and goes idle.
	// FlushType: Flush Immediate (seems wrong)
	virtual void RHIBlockUntilGPUIdle() override final
	{
		RHI->RHIBlockUntilGPUIdle();
	}

	// Kicks the current frame and makes sure GPU is actively working on them
	// FlushType: Flush Immediate (copied from RHIBlockUntilGPUIdle)
	virtual void RHISubmitCommandsAndFlushGPU() override final
	{
		RHI->RHISubmitCommandsAndFlushGPU();
	}

	// Tells the RHI we're about to suspend it
	virtual void RHIBeginSuspendRendering() override final
	{
		RHI->RHIBeginSuspendRendering();
	}

	// Operations to suspend title rendering and yield control to the system
	// FlushType: Thread safe
	virtual void RHISuspendRendering() override final
	{
		RHI->RHISuspendRendering();
	}

	// FlushType: Thread safe
	virtual void RHIResumeRendering() override final
	{
		RHI->RHIResumeRendering();
	}

	// FlushType: Flush Immediate
	virtual bool RHIIsRenderingSuspended() override final
	{
		return RHI->RHIIsRenderingSuspended();
	}

	// FlushType: Flush Immediate
	virtual bool RHIEnqueueDecompress(uint8_t* SrcBuffer, uint8_t* DestBuffer, int CompressedSize, void* ErrorCodeBuffer) override final
	{
		return RHI->RHIEnqueueDecompress(SrcBuffer, DestBuffer, CompressedSize, ErrorCodeBuffer);
	}

	virtual bool RHIEnqueueCompress(uint8_t* SrcBuffer, uint8_t* DestBuffer, int UnCompressedSize, void* ErrorCodeBuffer) override final
	{
		return RHI->RHIEnqueueCompress(SrcBuffer, DestBuffer, UnCompressedSize, ErrorCodeBuffer);
	}

	/**
	*	Retrieve available screen resolutions.
	*
	*	@param	Resolutions			TArray<FScreenResolutionRHI> parameter that will be filled in.
	*	@param	bIgnoreRefreshRate	If true, ignore refresh rates.
	*
	*	@return	bool				true if successfully filled the array
	*/
	// FlushType: Thread safe
	virtual bool RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate) override final
	{
		return RHI->RHIGetAvailableResolutions(Resolutions, bIgnoreRefreshRate);
	}

	/**
	* Returns a supported screen resolution that most closely matches input.
	* @param Width - Input: Desired resolution width in pixels. Output: A width that the platform supports.
	* @param Height - Input: Desired resolution height in pixels. Output: A height that the platform supports.
	*/
	// FlushType: Thread safe
	virtual void RHIGetSupportedResolution(uint32& Width, uint32& Height) override final
	{
		RHI->RHIGetSupportedResolution(Width, Height);
	}

	/**
	* Function that is used to allocate / free space used for virtual texture mip levels.
	* Make sure you also update the visible mip levels.
	* @param Texture - the texture to update, must have been created with TexCreate_Virtual
	* @param FirstMip - the first mip that should be in memory
	*/
	// FlushType: Wait RHI Thread
	virtual void RHIVirtualTextureSetFirstMipInMemory(FRHITexture2D* Texture, uint32 FirstMip) override final
	{
		RHI->RHIVirtualTextureSetFirstMipInMemory(Texture, FirstMip);
	}

	/**
	* Function that can be used to update which is the first visible mip to the GPU.
	* @param Texture - the texture to update, must have been created with TexCreate_Virtual
	* @param FirstMip - the first mip that should be visible to the GPU
	*/
	// FlushType: Wait RHI Thread
	virtual void RHIVirtualTextureSetFirstMipVisible(FRHITexture2D* Texture, uint32 FirstMip) override final
	{
		RHI->RHIVirtualTextureSetFirstMipVisible(Texture, FirstMip);
	}

	/**
	* Called once per frame just before deferred deletion in FRHIResource::FlushPendingDeletes
	*/
	// FlushType: called from render thread when RHI thread is flushed 
	virtual void RHIPerFrameRHIFlushComplete() override final
	{
		RHI->RHIPerFrameRHIFlushComplete();
	}

	// FlushType: Wait RHI Thread
	virtual void RHIExecuteCommandList(FRHICommandList* CmdList) override final
	{
		return RHI->RHIExecuteCommandList(CmdList);
	}

	/**
	* Provides access to the native device. Generally this should be avoided but is useful for third party plugins.
	*/
	// FlushType: Flush RHI Thread
	virtual void* RHIGetNativeDevice() override final
	{
		return RHI->RHIGetNativeDevice();
	}
	/**
	* Provides access to the native instance. Generally this should be avoided but is useful for third party plugins.
	*/
	// FlushType: Flush RHI Thread
	virtual void* RHIGetNativeInstance() override final
	{
		return RHI->RHIGetNativeInstance();
	}


	// FlushType: Thread safe
	virtual IRHICommandContext* RHIGetDefaultContext() override final;

	// FlushType: Thread safe
	virtual IRHIComputeContext* RHIGetDefaultAsyncComputeContext() override final;

	// FlushType: Thread safe
	virtual class IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num) override final
	{
		return RHI->RHIGetCommandContextContainer(Index, Num);
	}

#if WITH_MGPU
	virtual IRHICommandContextContainer* RHIGetCommandContextContainer(int32 Index, int32 Num, FRHIGPUMask GPUMask) override final
	{
		return RHI->RHIGetCommandContextContainer(Index, Num, GPUMask);
	}
#endif // WITH_MGPU

	///////// Pass through functions that allow RHIs to optimize certain calls.
	virtual FVertexBufferRHIRef CreateAndLockVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer) override final
	{
		return RHI->CreateAndLockVertexBuffer_RenderThread(RHICmdList, Size, InUsage, CreateInfo, OutDataBuffer);
	}

	virtual FIndexBufferRHIRef CreateAndLockIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer) override final
	{
		return RHI->CreateAndLockIndexBuffer_RenderThread(RHICmdList, Stride, Size, InUsage, CreateInfo, OutDataBuffer);
	}

	virtual FVertexBufferRHIRef CreateVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->CreateVertexBuffer_RenderThread(RHICmdList, Size, InUsage, CreateInfo);
	}

	virtual FStructuredBufferRHIRef CreateStructuredBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->CreateStructuredBuffer_RenderThread(RHICmdList, Stride, Size, InUsage, CreateInfo);
	}

	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) override final
	{
		return RHI->CreateShaderResourceView_RenderThread(RHICmdList, VertexBuffer, Stride, Format);
	}

	virtual FShaderResourceViewRHIRef CreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer) override final
	{
		return RHI->CreateShaderResourceView_RenderThread(RHICmdList, Buffer);
	}

	virtual void* LockVertexBuffer_BottomOfPipe(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) override final
	{
		return RHI->LockVertexBuffer_BottomOfPipe(RHICmdList, VertexBuffer, Offset, SizeRHI, LockMode);
	}

	virtual void UnlockVertexBuffer_BottomOfPipe(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer) override final
	{
		return RHI->UnlockVertexBuffer_BottomOfPipe(RHICmdList, VertexBuffer);
	}

	virtual FTexture2DRHIRef AsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, int32 NewMipCount, int32 NewSizeX, int32 NewSizeY, FThreadSafeCounter* RequestStatus) override final
	{
		return RHI->AsyncReallocateTexture2D_RenderThread(RHICmdList, Texture2D, NewMipCount, NewSizeX, NewSizeY, RequestStatus);
	}

	virtual ETextureReallocationStatus FinalizeAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted) override final
	{
		return RHI->FinalizeAsyncReallocateTexture2D_RenderThread(RHICmdList, Texture2D, bBlockUntilCompleted);
	}

	virtual ETextureReallocationStatus CancelAsyncReallocateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D, bool bBlockUntilCompleted) override final
	{
		return RHI->CancelAsyncReallocateTexture2D_RenderThread(RHICmdList, Texture2D, bBlockUntilCompleted);
	}

	virtual FIndexBufferRHIRef CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->CreateIndexBuffer_RenderThread(RHICmdList, Stride, Size, InUsage, CreateInfo);
	}

	virtual void* LockIndexBuffer_BottomOfPipe(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) override final
	{
		return RHI->LockIndexBuffer_BottomOfPipe(RHICmdList, IndexBuffer, Offset, SizeRHI, LockMode);
	}

	virtual void UnlockIndexBuffer_BottomOfPipe(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer) override final
	{
		RHI->UnlockIndexBuffer_BottomOfPipe(RHICmdList, IndexBuffer);
	}

	virtual void* LockStructuredBuffer_BottomOfPipe(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, uint32 Offset, uint32 SizeRHI, EResourceLockMode LockMode) override final
	{
		return RHI->LockStructuredBuffer_BottomOfPipe(RHICmdList, StructuredBuffer, Offset, SizeRHI, LockMode);
	}
	
	virtual void UnlockStructuredBuffer_BottomOfPipe(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) override final
	{
		RHI->UnlockStructuredBuffer_BottomOfPipe(RHICmdList, StructuredBuffer);
	}

	virtual FVertexShaderRHIRef CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) override final
	{
		return RHI->CreateVertexShader_RenderThread(RHICmdList, Code);
	}

	virtual FVertexShaderRHIRef CreateVertexShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		return RHI->CreateVertexShader_RenderThread(RHICmdList, Library, Hash);
	}

	virtual FPixelShaderRHIRef CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) override final
	{
		return RHI->CreatePixelShader_RenderThread(RHICmdList, Code);
	}

	virtual FPixelShaderRHIRef CreatePixelShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		return RHI->CreatePixelShader_RenderThread(RHICmdList, Library, Hash);
	}

	virtual FGeometryShaderRHIRef CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) override final
	{
		check(RHISupportsGeometryShaders(GMaxRHIShaderPlatform));
		return RHI->CreateGeometryShader_RenderThread(RHICmdList, Code);
	}

	virtual FGeometryShaderRHIRef CreateGeometryShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsGeometryShaders(GMaxRHIShaderPlatform));
		return RHI->CreateGeometryShader_RenderThread(RHICmdList, Library, Hash);
	}

	virtual FComputeShaderRHIRef CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->CreateComputeShader_RenderThread(RHICmdList, Code);
	}

	virtual FComputeShaderRHIRef CreateComputeShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsComputeShaders(GMaxRHIShaderPlatform));
		return RHI->CreateComputeShader_RenderThread(RHICmdList, Library, Hash);
	}

	virtual FHullShaderRHIRef CreateHullShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->CreateHullShader_RenderThread(RHICmdList, Code);
	}

	virtual FHullShaderRHIRef CreateHullShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->CreateHullShader_RenderThread(RHICmdList, Library, Hash);
	}

	virtual FDomainShaderRHIRef CreateDomainShader_RenderThread(class FRHICommandListImmediate& RHICmdList, const TArray<uint8>& Code) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->CreateDomainShader_RenderThread(RHICmdList, Code);
	}

	virtual FDomainShaderRHIRef CreateDomainShader_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIShaderLibrary* Library, FSHAHash Hash) override final
	{
		check(RHISupportsTessellation(GMaxRHIShaderPlatform));
		return RHI->CreateDomainShader_RenderThread(RHICmdList, Library, Hash);
	}

	virtual void* LockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush = true) override final
	{
		return RHI->LockTexture2D_RenderThread(RHICmdList, Texture, MipIndex, LockMode, DestStride, bLockWithinMiptail, bNeedsDefaultRHIFlush);
	}

	virtual void UnlockTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, bool bLockWithinMiptail, bool bNeedsDefaultRHIFlush = true) override final
	{
		RHI->UnlockTexture2D_RenderThread(RHICmdList, Texture, MipIndex, bLockWithinMiptail, bNeedsDefaultRHIFlush);
	}

	virtual void UpdateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) override final
	{
		RHI->UpdateTexture2D_RenderThread(RHICmdList, Texture, MipIndex, UpdateRegion, SourcePitch, SourceData);
	}

	virtual FUpdateTexture3DData BeginUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion) override final
	{
		return RHI->BeginUpdateTexture3D_RenderThread(RHICmdList, Texture, MipIndex, UpdateRegion);
	}

	virtual void EndUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FUpdateTexture3DData& UpdateData) override final
	{
		RHI->EndUpdateTexture3D_RenderThread(RHICmdList, UpdateData);
	}

	virtual void EndMultiUpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, TArray<FUpdateTexture3DData>& UpdateDataArray) override final
	{
		RHI->EndMultiUpdateTexture3D_RenderThread(RHICmdList, UpdateDataArray);
	}

	virtual void UpdateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture3D* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) override final
	{
		RHI->UpdateTexture3D_RenderThread(RHICmdList, Texture, MipIndex, UpdateRegion, SourceRowPitch, SourceDepthPitch, SourceData);
	}

	virtual FRHIShaderLibraryRef RHICreateShaderLibrary_RenderThread(class FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FString FilePath, FString Name) override final
	{
		return RHI->RHICreateShaderLibrary_RenderThread(RHICmdList, Platform, FilePath, Name);
	}

	virtual FTextureReferenceRHIRef RHICreateTextureReference_RenderThread(class FRHICommandListImmediate& RHICmdList, FLastRenderTimeContainer* LastRenderTime) override final
	{
		return RHI->RHICreateTextureReference_RenderThread(RHICmdList, LastRenderTime);
	}

	virtual FTexture2DRHIRef RHICreateTexture2D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTexture2D_RenderThread(RHICmdList, SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	virtual FTexture2DRHIRef RHICreateTextureExternal2D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 NumSamples, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTextureExternal2D_RenderThread(RHICmdList, SizeX, SizeY, Format, NumMips, NumSamples, Flags, CreateInfo);
	}

	virtual FTexture3DRHIRef RHICreateTexture3D_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 SizeX, uint32 SizeY, uint32 SizeZ, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTexture3D_RenderThread(RHICmdList, SizeX, SizeY, SizeZ, Format, NumMips, Flags, CreateInfo);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer, bool bUseUAVCounter, bool bAppendBuffer) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView_RenderThread(RHICmdList, StructuredBuffer, bUseUAVCounter, bAppendBuffer);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, uint32 MipLevel) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView_RenderThread(RHICmdList, Texture, MipLevel);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint8 Format) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView_RenderThread(RHICmdList, VertexBuffer, Format);
	}

	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBuffer, uint8 Format) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateUnorderedAccessView_RenderThread(RHICmdList, IndexBuffer, Format);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, const FRHITextureSRVCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateShaderResourceView_RenderThread(RHICmdList, Texture, CreateInfo);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBuffer, uint32 Stride, uint8 Format) override final
	{
		return RHI->RHICreateShaderResourceView_RenderThread(RHICmdList, VertexBuffer, Stride, Format);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* Buffer) override final
	{
		return RHI->RHICreateShaderResourceView_RenderThread(RHICmdList, Buffer);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBuffer) override final
	{
		check(IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5));
		return RHI->RHICreateShaderResourceView_RenderThread(RHICmdList, StructuredBuffer);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewWriteMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D) override final
	{
		return RHI->RHICreateShaderResourceViewWriteMask_RenderThread(RHICmdList, Texture2D);
	}

	virtual FShaderResourceViewRHIRef RHICreateShaderResourceViewFMask_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture2D) override final
	{
		return RHI->RHICreateShaderResourceViewFMask_RenderThread(RHICmdList, Texture2D);
	}

	virtual FTextureCubeRHIRef RHICreateTextureCube_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTextureCube_RenderThread(RHICmdList, Size, Format, NumMips, Flags, CreateInfo);
	}

	virtual FTextureCubeRHIRef RHICreateTextureCubeArray_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 ArraySize, uint8 Format, uint32 NumMips, uint32 Flags, FRHIResourceCreateInfo& CreateInfo) override final
	{
		return RHI->RHICreateTextureCubeArray_RenderThread(RHICmdList, Size, ArraySize, Format, NumMips, Flags, CreateInfo);
	}
	
	virtual FRenderQueryRHIRef RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType) override final
	{
		return RHI->RHICreateRenderQuery_RenderThread(RHICmdList, QueryType);
	}


	virtual void* RHILockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, EResourceLockMode LockMode, uint32& DestStride, bool bLockWithinMiptail) override final
	{
		return RHI->RHILockTextureCubeFace_RenderThread(RHICmdList, Texture, FaceIndex, ArrayIndex, MipIndex, LockMode, DestStride, bLockWithinMiptail);
	}
	virtual void RHIUnlockTextureCubeFace_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITextureCube* Texture, uint32 FaceIndex, uint32 ArrayIndex, uint32 MipIndex, bool bLockWithinMiptail) override final
	{
		RHI->RHIUnlockTextureCubeFace_RenderThread(RHICmdList, Texture, FaceIndex, ArrayIndex, MipIndex, bLockWithinMiptail);
	}

	virtual void RHIAcquireTransientResource_RenderThread(FRHITexture* Texture) override final
	{
		RHI->RHIAcquireTransientResource_RenderThread(Texture);
	}

	virtual void RHIDiscardTransientResource_RenderThread(FRHITexture* Texture) override final
	{
		RHI->RHIDiscardTransientResource_RenderThread(Texture);
	}

	virtual void RHIAcquireTransientResource_RenderThread(FRHIVertexBuffer* Buffer) override final
	{
		RHI->RHIAcquireTransientResource_RenderThread(Buffer);
	}

	virtual void RHIDiscardTransientResource_RenderThread(FRHIVertexBuffer* Buffer) override final
	{
		RHI->RHIDiscardTransientResource_RenderThread(Buffer);
	}

	virtual void RHIAcquireTransientResource_RenderThread(FRHIStructuredBuffer* Buffer) override final
	{
		RHI->RHIAcquireTransientResource_RenderThread(Buffer);
	}

	virtual void RHIDiscardTransientResource_RenderThread(FRHIStructuredBuffer* Buffer) override final
	{
		RHI->RHIDiscardTransientResource_RenderThread(Buffer);
	}

	virtual void RHIReadSurfaceFloatData_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex) override final
	{
		RHI->RHIReadSurfaceFloatData_RenderThread(RHICmdList, Texture, Rect, OutData, CubeFace, ArrayIndex, MipIndex);
	}

	//Utilities
	virtual void EnableIdealGPUCaptureOptions(bool bEnable) override final
	{
		RHI->EnableIdealGPUCaptureOptions(bEnable);
	}

	//checks if the GPU is still alive.
	virtual bool CheckGpuHeartbeat() const override final
	{
		return RHI->CheckGpuHeartbeat();
	}

	virtual void VirtualTextureSetFirstMipInMemory_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 FirstMip) override final
	{
		RHI->VirtualTextureSetFirstMipInMemory_RenderThread(RHICmdList, Texture, FirstMip);
	}

	virtual void VirtualTextureSetFirstMipVisible_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* Texture, uint32 FirstMip) override final
	{
		RHI->VirtualTextureSetFirstMipVisible_RenderThread(RHICmdList, Texture, FirstMip);
	}

	/* Copy the source box pixels in the destination box texture, return true if implemented for the current platform*/
	virtual void RHICopySubTextureRegion_RenderThread(class FRHICommandListImmediate& RHICmdList, FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox) override final
	{
		int32 Width = static_cast<int32>(SourceBox.GetSize().X);
		int32 Height = static_cast<int32>(SourceBox.GetSize().Y);
		int32 SrcX = static_cast<int32>(SourceBox.Min.X);
		int32 SrcY = static_cast<int32>(SourceBox.Min.Y);
		int32 DestX = static_cast<int32>(DestinationBox.Min.X);
		int32 DestY = static_cast<int32>(DestinationBox.Min.Y);
		FValidationRHIUtils::ValidateCopyTexture(SourceTexture, DestinationTexture, FIntVector(Width, Height, 1), FIntVector(SrcX, SrcY, 0), FIntVector(DestX, DestY, 0));
		return RHI->RHICopySubTextureRegion_RenderThread(RHICmdList, SourceTexture, DestinationTexture, SourceBox, DestinationBox);
	}

	virtual void RHICopySubTextureRegion(FRHITexture2D* SourceTexture, FRHITexture2D* DestinationTexture, FBox2D SourceBox, FBox2D DestinationBox) override final
	{
		int32 Width = static_cast<int32>(SourceBox.GetSize().X);
		int32 Height = static_cast<int32>(SourceBox.GetSize().Y);
		int32 SrcX = static_cast<int32>(SourceBox.Min.X);
		int32 SrcY = static_cast<int32>(SourceBox.Min.Y);
		int32 DestX = static_cast<int32>(DestinationBox.Min.X);
		int32 DestY = static_cast<int32>(DestinationBox.Min.Y);
		FValidationRHIUtils::ValidateCopyTexture(SourceTexture, DestinationTexture, FIntVector(Width, Height, 1), FIntVector(SrcX, SrcY, 0), FIntVector(DestX, DestY, 0));
		RHI->RHICopySubTextureRegion(SourceTexture, DestinationTexture, SourceBox, DestinationBox);
	}

	virtual FRHIFlipDetails RHIWaitForFlip(double TimeoutInSeconds) override final
	{
		return RHI->RHIWaitForFlip(TimeoutInSeconds);
	}

	virtual void RHISignalFlipEvent() override final
	{
		RHI->RHISignalFlipEvent();
	}

	virtual void RHICalibrateTimers() override final
	{
		RHI->RHICalibrateTimers();
	}

	virtual void RHIPollRenderQueryResults() override final
	{
		RHI->RHIPollRenderQueryResults();
	}

	virtual bool RHIIsTypedUAVLoadSupported(EPixelFormat PixelFormat) override final
	{
		return RHI->RHIIsTypedUAVLoadSupported(PixelFormat);
	}

	virtual uint16 RHIGetPlatformTextureMaxSampleCount() override final
	{
		return RHI->RHIGetPlatformTextureMaxSampleCount();
	};


#if RHI_RAYTRACING
	virtual FRayTracingGeometryRHIRef RHICreateRayTracingGeometry(const FRayTracingGeometryInitializer& Initializer) override final
	{
		return RHI->RHICreateRayTracingGeometry(Initializer);
	}

	virtual FRayTracingSceneRHIRef RHICreateRayTracingScene(const FRayTracingSceneInitializer& Initializer) override final
	{
		return RHI->RHICreateRayTracingScene(Initializer);
	}

	virtual FRayTracingShaderRHIRef RHICreateRayTracingShader(const TArray<uint8>& Code, EShaderFrequency ShaderFrequency) override final
	{
		return RHI->RHICreateRayTracingShader(Code, ShaderFrequency);
	}

	virtual FRayTracingPipelineStateRHIRef RHICreateRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer) override final
	{
		return RHI->RHICreateRayTracingPipelineState(Initializer);
	}
#endif // RHI_RAYTRACING


//protected:
	FDynamicRHI*				RHI;
	FValidationContext*			Context;
	FValidationComputeContext*	AsyncComputeContext;

	TMap<FRHIDepthStencilState*, FDepthStencilStateInitializerRHI> DepthStencilStates;

private:
	FString						RHIName;

	void ValidatePipeline(const FGraphicsPipelineStateInitializer& Initializer);
};

extern RHI_API FValidationRHI* GValidationRHI;

template <typename TDynamicRHI>
inline TDynamicRHI* GetDynamicRHI()
{
	return GValidationRHI ? static_cast<TDynamicRHI*>(GValidationRHI->RHI) : static_cast<TDynamicRHI*>(GDynamicRHI);
}

#else

template <typename TDynamicRHI>
inline TDynamicRHI* GetDynamicRHI()
{
	return static_cast<TDynamicRHI*>(GDynamicRHI);
}

#endif	// ENABLE_RHI_VALIDATION
