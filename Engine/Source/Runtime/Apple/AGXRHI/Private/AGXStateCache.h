// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXRHIPrivate.h"
#include "AGXUniformBuffer.h"
#include "Shaders/AGXShaderParameterCache.h"
#include "AGXCommandEncoder.h"
#include "AGXPipeline.h"

class FAGXGraphicsPipelineState;
class FAGXQueryBuffer;

enum EAGXPipelineFlags
{
	EAGXPipelineFlagPipelineState = 1 << 0,
    EAGXPipelineFlagComputeShader = 1 << 5,
    EAGXPipelineFlagRasterMask = 0xF,
    EAGXPipelineFlagComputeMask = 0x30,
    EAGXPipelineFlagMask = 0x3F
};

enum EAGXRenderFlags
{
    EAGXRenderFlagViewport = 1 << 0,
    EAGXRenderFlagFrontFacingWinding = 1 << 1,
    EAGXRenderFlagCullMode = 1 << 2,
    EAGXRenderFlagDepthBias = 1 << 3,
    EAGXRenderFlagScissorRect = 1 << 4,
    EAGXRenderFlagTriangleFillMode = 1 << 5,
    EAGXRenderFlagBlendColor = 1 << 6,
    EAGXRenderFlagDepthStencilState = 1 << 7,
    EAGXRenderFlagStencilReferenceValue = 1 << 8,
    EAGXRenderFlagVisibilityResultMode = 1 << 9,
    EAGXRenderFlagMask = 0x1FF
};

class FAGXStateCache
{
public:
	FAGXStateCache(bool const bInImmediate);
	~FAGXStateCache();
	
	/** Reset cached state for reuse */
	void Reset(void);

	void SetScissorRect(bool const bEnable, mtlpp::ScissorRect const& Rect);
	void SetBlendFactor(FLinearColor const& InBlendFactor);
	void SetStencilRef(uint32 const InStencilRef);
	void SetComputeShader(FAGXComputeShader* InComputeShader);
	bool SetRenderPassInfo(FRHIRenderPassInfo const& InRenderTargets, FAGXQueryBuffer* QueryBuffer, bool const bRestart);
	void InvalidateRenderTargets(void);
	void SetRenderTargetsActive(bool const bActive);
	void SetViewport(const mtlpp::Viewport& InViewport);
	void SetViewports(const mtlpp::Viewport InViewport[], uint32 Count);
	void SetVertexStream(uint32 const Index, FAGXBuffer* Buffer, FAGXBufferData* Bytes, uint32 const Offset, uint32 const Length);
	void SetGraphicsPipelineState(FAGXGraphicsPipelineState* State);
	void BindUniformBuffer(EAGXShaderStages const Freq, uint32 const BufferIndex, FRHIUniformBuffer* BufferRHI);
	void SetDirtyUniformBuffers(EAGXShaderStages const Freq, uint32 const Dirty);
	
	/*
	 * Monitor if samples pass the depth and stencil tests.
	 * @param Mode Controls if the counter is disabled or moniters passing samples.
	 * @param Offset The offset relative to the occlusion query buffer provided when the command encoder was created.  offset must be a multiple of 8.
	 */
	void SetVisibilityResultMode(mtlpp::VisibilityResultMode const Mode, NSUInteger const Offset);
	
#pragma mark - Public Shader Resource Mutators -
	/*
	 * Set a global buffer for the specified shader frequency at the given bind point index.
	 * @param Frequency The shader frequency to modify.
	 * @param Buffer The buffer to bind or nil to clear.
	 * @param Bytes The FAGXBufferData to bind or nil to clear.
	 * @param Offset The offset in the buffer or 0 when Buffer is nil.
	 * @param Offset The length of data (caller accounts for Offset) in the buffer or 0 when Buffer is nil.
	 * @param Index The index to modify.
	 * @param Usage The resource usage flags.
	 * @param Format The UAV pixel format.
	 */
	void SetShaderBuffer(EAGXShaderStages const Frequency, FAGXBuffer const& Buffer, FAGXBufferData* const Bytes, NSUInteger const Offset, NSUInteger const Length, NSUInteger const Index, mtlpp::ResourceUsage const Usage, EPixelFormat const Format = PF_Unknown, NSUInteger const ElementRowPitch = 0);
	
	/*
	 * Set a global texture for the specified shader frequency at the given bind point index.
	 * @param Frequency The shader frequency to modify.
	 * @param Texture The texture to bind or nil to clear.
	 * @param Index The index to modify.
	 * @param Usage The resource usage flags.
	 */
	void SetShaderTexture(EAGXShaderStages const Frequency, FAGXTexture const& Texture, NSUInteger const Index, mtlpp::ResourceUsage const Usage);
	
	/*
	 * Set a global sampler for the specified shader frequency at the given bind point index.
	 * @param Frequency The shader frequency to modify.
	 * @param Sampler The sampler state to bind or nil to clear.
	 * @param Index The index to modify.
	 */
	void SetShaderSamplerState(EAGXShaderStages const Frequency, FAGXSamplerState* const Sampler, NSUInteger const Index);

	void SetShaderResourceView(FAGXContext* Context, EAGXShaderStages ShaderStage, uint32 BindIndex, FAGXShaderResourceView* RESTRICT SRV);
	
	void SetShaderUnorderedAccessView(EAGXShaderStages ShaderStage, uint32 BindIndex, FAGXUnorderedAccessView* RESTRICT UAV);

	void SetStateDirty(void);
	
	void SetShaderBufferDirty(EAGXShaderStages const Frequency, NSUInteger const Index);
	
	void SetRenderStoreActions(FAGXCommandEncoder& CommandEncoder, bool const bConditionalSwitch);
	
	void SetRenderState(FAGXCommandEncoder& CommandEncoder, FAGXCommandEncoder* PrologueEncoder);

	void CommitRenderResources(FAGXCommandEncoder* Raster);

	void CommitComputeResources(FAGXCommandEncoder* Compute);
	
	void CommitResourceTable(EAGXShaderStages const Frequency, mtlpp::FunctionType const Type, FAGXCommandEncoder& CommandEncoder);
	
	bool PrepareToRestart(bool const bCurrentApplied);
	
	FAGXShaderParameterCache& GetShaderParameters(EAGXShaderStages const Stage) { return ShaderParameters[Stage]; }
	FLinearColor const& GetBlendFactor() const { return BlendFactor; }
	uint32 GetStencilRef() const { return StencilRef; }
	FAGXDepthStencilState* GetDepthStencilState() const { return DepthStencilState; }
	FAGXRasterizerState* GetRasterizerState() const { return RasterizerState; }
	FAGXGraphicsPipelineState* GetGraphicsPSO() const { return GraphicsPSO; }
	FAGXComputeShader* GetComputeShader() const { return ComputeShader; }
	CGSize GetFrameBufferSize() const { return FrameBufferSize; }
	FRHIRenderPassInfo const& GetRenderPassInfo() const { return RenderPassInfo; }
	int32 GetNumRenderTargets() { return bHasValidColorTarget ? RenderPassInfo.GetNumColorRenderTargets() : -1; }
	bool GetHasValidRenderTarget() const { return bHasValidRenderTarget; }
	bool GetHasValidColorTarget() const { return bHasValidColorTarget; }
	const mtlpp::Viewport& GetViewport(uint32 const Index) const { check(Index < ML_MaxViewports); return Viewport[Index]; }
	uint32 GetVertexBufferSize(uint32 const Index);
	uint32 GetRenderTargetArraySize() const { return RenderTargetArraySize; }
	const FRHIUniformBuffer** GetBoundUniformBuffers(EAGXShaderStages const Freq) { return (const FRHIUniformBuffer**)&BoundUniformBuffers[Freq][0]; }
	uint32 GetDirtyUniformBuffers(EAGXShaderStages const Freq) const { return DirtyUniformBuffers[Freq]; }
	FAGXQueryBuffer* GetVisibilityResultsBuffer() const { return VisibilityResults; }
	bool GetScissorRectEnabled() const { return bScissorRectEnabled; }
	bool NeedsToSetRenderTarget(const FRHIRenderPassInfo& RenderPassInfo);
	bool HasValidDepthStencilSurface() const { return IsValidRef(DepthStencilSurface); }
    bool CanRestartRenderPass() const { return bCanRestartRenderPass; }
	mtlpp::RenderPassDescriptor GetRenderPassDescriptor(void) const { return RenderPassDesc; }
	uint32 GetSampleCount(void) const { return SampleCount; }
    bool IsLinearBuffer(EAGXShaderStages ShaderStage, uint32 BindIndex);
	FAGXShaderPipeline* GetPipelineState() const;
	EPrimitiveType GetPrimitiveType();
	mtlpp::VisibilityResultMode GetVisibilityResultMode() { return VisibilityMode; }
	uint32 GetVisibilityResultOffset() { return VisibilityOffset; }
	
	FTexture2DRHIRef CreateFallbackDepthStencilSurface(uint32 Width, uint32 Height);
	bool GetFallbackDepthStencilBound(void) const { return bFallbackDepthStencilBound; }
	
	void SetRenderPipelineState(FAGXCommandEncoder& CommandEncoder, FAGXCommandEncoder* PrologueEncoder);
    void SetComputePipelineState(FAGXCommandEncoder& CommandEncoder);
	void FlushVisibilityResults(FAGXCommandEncoder& CommandEncoder);

	void DiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMask);
private:
	void ConditionalUpdateBackBuffer(FAGXSurface& Surface);
	
	void SetDepthStencilState(FAGXDepthStencilState* InDepthStencilState);
	void SetRasterizerState(FAGXRasterizerState* InRasterizerState);

	FORCEINLINE void SetResource(uint32 ShaderStage, uint32 BindIndex, FRHITexture* RESTRICT TextureRHI, float CurrentTime);
	
	FORCEINLINE void SetResource(uint32 ShaderStage, uint32 BindIndex, FAGXShaderResourceView* RESTRICT SRV, float CurrentTime);
	
	FORCEINLINE void SetResource(uint32 ShaderStage, uint32 BindIndex, FAGXSamplerState* RESTRICT SamplerState, float CurrentTime);
	
	FORCEINLINE void SetResource(uint32 ShaderStage, uint32 BindIndex, FAGXUnorderedAccessView* RESTRICT UAV, float CurrentTime);
	
	template <typename MetalResourceType>
	inline int32 SetShaderResourcesFromBuffer(uint32 ShaderStage, FAGXUniformBuffer* RESTRICT Buffer, const uint32* RESTRICT ResourceMap, int32 BufferIndex, float CurrentTime);
	
	template <class ShaderType>
	void SetResourcesFromTables(ShaderType Shader, uint32 ShaderStage);
	
	void SetViewport(uint32 Index, const mtlpp::Viewport& InViewport);
	void SetScissorRect(uint32 Index, bool const bEnable, mtlpp::ScissorRect const& Rect);

private:

	void EnsureTextureAndType(EAGXShaderStages Stage, uint32 Index, const TMap<uint8, uint8>& TexTypes) const;
	
private:
#pragma mark - Private Type Declarations -
	struct FAGXBufferBinding
	{
		FAGXBufferBinding() : Bytes(nil), Offset(0), Length(0), Usage((mtlpp::ResourceUsage)0) {}
		/** The bound buffers or nil. */
		ns::AutoReleased<FAGXBuffer> Buffer;
		/** Optional bytes buffer used instead of an FAGXBuffer */
		FAGXBufferData* Bytes;
		/** The bound buffer offsets or 0. */
		NSUInteger Offset;
		/** The bound buffer lengths or 0. */
		NSUInteger Length;
		/** The bound buffer element row pitch or 0 */
		NSUInteger ElementRowPitch;
		/** The bound buffer usage or 0 */
		mtlpp::ResourceUsage Usage;
	};
	
	/** A structure of arrays for the current buffer binding settings. */
	struct FAGXBufferBindings
	{
		FAGXBufferBindings() : Bound(0) {}
		/** The bound buffers/bytes or nil. */
		FAGXBufferBinding Buffers[ML_MaxBuffers];
		/** The pixel formats for buffers bound so that we emulate [RW]Buffer<T> type conversion */
		EPixelFormat Formats[ML_MaxBuffers];
		/** A bitmask for which buffers were bound by the application where a bit value of 1 is bound and 0 is unbound. */
		uint32 Bound;
	};
	
	/** A structure of arrays for the current texture binding settings. */
	struct FAGXTextureBindings
	{
		FAGXTextureBindings() : Bound(0) { FMemory::Memzero(Usage); }
		/** The bound textures or nil. */
		ns::AutoReleased<FAGXTexture> Textures[ML_MaxTextures];
		/** The bound texture usage or 0 */
		mtlpp::ResourceUsage Usage[ML_MaxTextures];
		/** A bitmask for which textures were bound by the application where a bit value of 1 is bound and 0 is unbound. */
		FAGXTextureMask Bound;
	};
	
	/** A structure of arrays for the current sampler binding settings. */
	struct FAGXSamplerBindings
	{
		FAGXSamplerBindings() : Bound(0) {}
		/** The bound sampler states or nil. */
		ns::AutoReleased<FAGXSampler> Samplers[ML_MaxSamplers];
		/** A bitmask for which samplers were bound by the application where a bit value of 1 is bound and 0 is unbound. */
		uint16 Bound;
	};
    
private:
	FAGXShaderParameterCache ShaderParameters[EAGXShaderStages::Num];

	uint32 SampleCount;

	TSet<TRefCountPtr<FRHIUniformBuffer>> ActiveUniformBuffers;
	FRHIUniformBuffer* BoundUniformBuffers[EAGXShaderStages::Num][ML_MaxBuffers];
	
	/** Bitfield for which uniform buffers are dirty */
	uint32 DirtyUniformBuffers[EAGXShaderStages::Num];
	
	/** Vertex attribute buffers */
	FAGXBufferBinding VertexBuffers[MaxVertexElementCount];
	
	/** Bound shader resource tables. */
	FAGXBufferBindings ShaderBuffers[EAGXShaderStages::Num];
	FAGXTextureBindings ShaderTextures[EAGXShaderStages::Num];
	FAGXSamplerBindings ShaderSamplers[EAGXShaderStages::Num];
	
	mtlpp::StoreAction ColorStore[MaxSimultaneousRenderTargets];
	mtlpp::StoreAction DepthStore;
	mtlpp::StoreAction StencilStore;

	FAGXQueryBuffer* VisibilityResults;
	mtlpp::VisibilityResultMode VisibilityMode;
	NSUInteger VisibilityOffset;
	NSUInteger VisibilityWritten;

	TRefCountPtr<FAGXDepthStencilState> DepthStencilState;
	TRefCountPtr<FAGXRasterizerState> RasterizerState;
	TRefCountPtr<FAGXGraphicsPipelineState> GraphicsPSO;
	TRefCountPtr<FAGXComputeShader> ComputeShader;
	uint32 StencilRef;
	
	FLinearColor BlendFactor;
	CGSize FrameBufferSize;
	
	uint32 RenderTargetArraySize;

	mtlpp::Viewport Viewport[ML_MaxViewports];
	mtlpp::ScissorRect Scissor[ML_MaxViewports];
	
	uint32 ActiveViewports;
	uint32 ActiveScissors;
	
	FRHIRenderPassInfo RenderPassInfo;
	FTextureRHIRef ColorTargets[MaxSimultaneousRenderTargets];
	FTextureRHIRef ResolveTargets[MaxSimultaneousRenderTargets];
	FTextureRHIRef DepthStencilSurface;
	FTextureRHIRef DepthStencilResolve;
	/** A fallback depth-stencil surface for draw calls that write to depth without a depth-stencil surface bound. */
	FTexture2DRHIRef FallbackDepthStencilSurface;
	mtlpp::RenderPassDescriptor RenderPassDesc;
	uint32 RasterBits;
    uint8 PipelineBits;
	bool bIsRenderTargetActive;
	bool bHasValidRenderTarget;
	bool bHasValidColorTarget;
	bool bScissorRectEnabled;
    bool bCanRestartRenderPass;
    bool bImmediate;
	bool bFallbackDepthStencilBound;
};
