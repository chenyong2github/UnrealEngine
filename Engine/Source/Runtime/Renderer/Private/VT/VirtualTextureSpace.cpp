// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualTextureSpace.h"
#include "VirtualTexturePhysicalSpace.h"
#include "VirtualTextureSystem.h"
#include "SpriteIndexBuffer.h"
#include "SceneFilterRendering.h"
#include "RenderTargetPool.h"
#include "VisualizeTexture.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "HAL/IConsoleManager.h"
#include "SceneUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualTextureSpace, Log, All);

static TAutoConsoleVariable<int32> CVarVTRefreshEntirePageTable(
	TEXT("r.VT.RefreshEntirePageTable"),
	0,
	TEXT("Refreshes the entire page table texture every frame"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarVTMaskedPageTableUpdates(
	TEXT("r.VT.MaskedPageTableUpdates"),
	1,
	TEXT("Masks the page table update quads to reduce pixel fill costs"),
	ECVF_RenderThreadSafe
	);

static EPixelFormat GetFormatForNumLayers(uint32 NumLayers, EVTPageTableFormat Format)
{
	const bool bUse16Bits = (Format == EVTPageTableFormat::UInt16);
	switch (NumLayers)
	{
	case 1u: return bUse16Bits ? PF_R16_UINT : PF_R32_UINT;
	case 2u: return bUse16Bits ? PF_R16G16_UINT : PF_R32G32_UINT;
	case 3u:
	case 4u: return bUse16Bits ? PF_R16G16B16A16_UINT : PF_R32G32B32A32_UINT;
	default: checkNoEntry(); return PF_Unknown;
	}
}

FVirtualTextureSpace::FVirtualTextureSpace(FVirtualTextureSystem* InSystem, uint8 InID, const FVTSpaceDescription& InDesc, uint32 InSizeNeeded)
	: Description(InDesc)
	, Allocator(InDesc.Dimensions)
	, PageTableSize(0u)
	, NumPageTableLevels(0u)
	, NumRefs(0u)
	, ID(InID)
	, bNeedToAllocatePageTable(true)
	, bForceEntireUpdate(false)
{
	// Initialize page map with large enough capacity to handle largest possible physical texture
	const uint32 PhysicalTileSize = InDesc.TileSize + InDesc.TileBorderSize * 2u;
	const uint32 MaxSizeInTiles = GetMax2DTextureDimension() / PhysicalTileSize;
	const uint32 MaxNumTiles = MaxSizeInTiles * MaxSizeInTiles;
	for (uint32 LayerIndex = 0u; LayerIndex < InDesc.NumPageTableLayers; ++LayerIndex)
	{
		PhysicalPageMap[LayerIndex].Initialize(MaxNumTiles, LayerIndex, InDesc.Dimensions);
	}

	uint32 NumLayersToAllocate = InDesc.NumPageTableLayers;
	uint32 PageTableIndex = 0u;
	FMemory::Memzero(TexturePixelFormat);
	while (NumLayersToAllocate > 0u)
	{
		const uint32 NumLayersForTexture = FMath::Min(NumLayersToAllocate, LayersPerPageTableTexture);
		const EPixelFormat PixelFormat = GetFormatForNumLayers(NumLayersForTexture, InDesc.PageTableFormat);
		TexturePixelFormat[PageTableIndex] = PixelFormat;
		NumLayersToAllocate -= NumLayersForTexture;
		++PageTableIndex;
	}

	PageTableSize = FMath::Max(InSizeNeeded, VIRTUALTEXTURE_MIN_PAGETABLE_SIZE);
	PageTableSize = FMath::RoundUpToPowerOfTwo(PageTableSize);
	ensure(PageTableSize <= Description.MaxSpaceSize);
	ensure(Description.MaxSpaceSize <= VIRTUALTEXTURE_MAX_PAGETABLE_SIZE);
	NumPageTableLevels = FMath::FloorLog2(PageTableSize) + 1;
	Allocator.Initialize(PageTableSize);

	bNeedToAllocatePageTableIndirection = InDesc.IndirectionTextureSize > 0;
}

FVirtualTextureSpace::~FVirtualTextureSpace()
{
}

uint32 FVirtualTextureSpace::AllocateVirtualTexture(FAllocatedVirtualTexture* VirtualTexture)
{
	uint32 vAddress = Allocator.Alloc(VirtualTexture);
	while (vAddress == ~0u && PageTableSize < Description.MaxSpaceSize)
	{
		// Allocation failed, expand the size of page table texture and try again
		PageTableSize *= 2u;
		++NumPageTableLevels;
		bNeedToAllocatePageTable = true;
		Allocator.Grow();
		vAddress = Allocator.Alloc(VirtualTexture);
	}
	return vAddress;
}

void FVirtualTextureSpace::FreeVirtualTexture(FAllocatedVirtualTexture* VirtualTexture)
{
	Allocator.Free(VirtualTexture);
}

void FVirtualTextureSpace::InitRHI()
{
	for (uint32 TextureIndex = 0u; TextureIndex < GetNumPageTableTextures(); ++TextureIndex)
	{
		FTextureEntry& TextureEntry = PageTable[TextureIndex];
		TextureEntry.TextureReferenceRHI = RHICreateTextureReference(nullptr);
	}
	PageTableIndirection.TextureReferenceRHI = RHICreateTextureReference(nullptr);
	RHIUpdateTextureReference(PageTableIndirection.TextureReferenceRHI, GBlackUintTexture->TextureRHI);
}

void FVirtualTextureSpace::ReleaseRHI()
{
	for (uint32 i = 0u; i < TextureCapacity; ++i)
	{
		FTextureEntry& TextureEntry = PageTable[i];
		TextureEntry.TextureReferenceRHI.SafeRelease();
		GRenderTargetPool.FreeUnusedResource(TextureEntry.RenderTarget);
	}

	PageTableIndirection.TextureReferenceRHI.SafeRelease();
	GRenderTargetPool.FreeUnusedResource(PageTableIndirection.RenderTarget);

	UpdateBuffer.SafeRelease();
	UpdateBufferSRV.SafeRelease();
}

uint32 FVirtualTextureSpace::GetSizeInBytes() const
{
	uint32 TotalSize = 0u;
	for (uint32 TextureIndex = 0u; TextureIndex < GetNumPageTableTextures(); ++TextureIndex)
	{
		const SIZE_T TextureSize = CalculateImageBytes(PageTableSize, PageTableSize, 0, TexturePixelFormat[TextureIndex]);
		TotalSize += TextureSize;
	}
	return TotalSize;
}

void FVirtualTextureSpace::QueueUpdate(uint8 Layer, uint8 vLogSize, uint32 vAddress, uint8 vLevel, const FPhysicalTileLocation& pTileLocation)
{
	FPageTableUpdate Update;
	Update.vAddress = vAddress;
	Update.pTileLocation = pTileLocation;
	Update.vLevel = vLevel;
	Update.vLogSize = vLogSize;
	Update.Check( Description.Dimensions );
	PageTableUpdates[Layer].Add( Update );
}


TGlobalResource< FSpriteIndexBuffer<8> > GQuadIndexBuffer;

class FPageTableUpdateVS : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FPageTableUpdateVS, NonVirtual);
protected:
	FPageTableUpdateVS() {}
	
public:
	FPageTableUpdateVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PageTableSize.Bind( Initializer.ParameterMap, TEXT("PageTableSize") );
		FirstUpdate.Bind( Initializer.ParameterMap, TEXT("FirstUpdate") );
		NumUpdates.Bind( Initializer.ParameterMap, TEXT("NumUpdates") );
		UpdateBuffer.Bind( Initializer.ParameterMap, TEXT("UpdateBuffer") );
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) 
			|| GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1;
	}

	LAYOUT_FIELD(FShaderParameter, PageTableSize);
	LAYOUT_FIELD(FShaderParameter, FirstUpdate);
	LAYOUT_FIELD(FShaderParameter, NumUpdates);
	LAYOUT_FIELD(FShaderResourceParameter, UpdateBuffer);
};

class FPageTableUpdatePS : public FGlobalShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FPageTableUpdatePS, NonVirtual);
protected:
	FPageTableUpdatePS() {}

public:
	FPageTableUpdatePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			|| GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1;
	}

	
	
};

template<bool Use16Bits>
class TPageTableUpdateVS : public FPageTableUpdateVS
{
	DECLARE_SHADER_TYPE(TPageTableUpdateVS,Global);

	TPageTableUpdateVS() {}

public:
	TPageTableUpdateVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPageTableUpdateVS(Initializer)
	{}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		OutEnvironment.SetDefine(TEXT("USE_16BIT"), Use16Bits);
	}
};

template<EPixelFormat TargetFormat>
class TPageTableUpdatePS : public FPageTableUpdatePS
{
	DECLARE_SHADER_TYPE(TPageTableUpdatePS, Global);

	TPageTableUpdatePS() {}

public:
	TPageTableUpdatePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FPageTableUpdatePS(Initializer)
	{}
	
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment( Parameters, OutEnvironment );
		OutEnvironment.SetRenderTargetOutputFormat(0u, TargetFormat);
	}
};

IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdateVS<false>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdateVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdateVS<true>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdateVS"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdatePS<PF_R16_UINT>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdatePS_1"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdatePS<PF_R16G16_UINT>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdatePS_2"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdatePS<PF_R16G16B16A16_UINT>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdatePS_4"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdatePS<PF_R32_UINT>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdatePS_1"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdatePS<PF_R32G32_UINT>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdatePS_2"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TPageTableUpdatePS<PF_R32G32B32A32_UINT>, TEXT("/Engine/Private/PageTableUpdate.usf"), TEXT("PageTableUpdatePS_4"), SF_Pixel);

void FVirtualTextureSpace::QueueUpdateEntirePageTable()
{
	bForceEntireUpdate = true;
}

void FVirtualTextureSpace::AllocateTextures(FRHICommandList& RHICmdList)
{
	if (bNeedToAllocatePageTable)
	{
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		const TCHAR* TextureNames[] = { TEXT("PageTable_0"), TEXT("PageTable_1") };
		static_assert(UE_ARRAY_COUNT(TextureNames) == TextureCapacity, "");

		for (uint32 TextureIndex = 0u; TextureIndex < GetNumPageTableTextures(); ++TextureIndex)
		{
			// Page Table
			FTextureEntry& TextureEntry = PageTable[TextureIndex];
			const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(PageTableSize, PageTableSize),
				TexturePixelFormat[TextureIndex],
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_RenderTargetable | TexCreate_ShaderResource,
				false,
				NumPageTableLevels,
				false /* InAutowritable */);

			TRefCountPtr<IPooledRenderTarget> RenderTarget;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RenderTarget, TextureNames[TextureIndex]);
			RHIUpdateTextureReference(TextureEntry.TextureReferenceRHI, RenderTarget->GetRenderTargetItem().ShaderResourceTexture);
			if (TextureEntry.RenderTarget)
			{
				// Copy previously allocated page table to new texture
				const FPooledRenderTargetDesc& SrcDesc = TextureEntry.RenderTarget->GetDesc();
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.Size.X = FMath::Min(Desc.Extent.X, SrcDesc.Extent.X);
				CopyInfo.Size.Y = FMath::Min(Desc.Extent.Y, SrcDesc.Extent.Y);
				CopyInfo.Size.Z = 1;
				CopyInfo.NumMips = FMath::Min(Desc.NumMips, SrcDesc.NumMips);

				FRHITexture* SrcTexture = TextureEntry.RenderTarget->GetRenderTargetItem().ShaderResourceTexture;
				FRHITexture* DstTexture = RenderTarget->GetRenderTargetItem().TargetableTexture;

				FRHITransitionInfo TransitionsBefore[] = {
					FRHITransitionInfo(SrcTexture, ERHIAccess::EReadable, ERHIAccess::CopySrc),
					FRHITransitionInfo(DstTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest)
				};
				RHICmdList.Transition(MakeArrayView(TransitionsBefore, UE_ARRAY_COUNT(TransitionsBefore)));

				RHICmdList.CopyTexture(SrcTexture, DstTexture, CopyInfo);

				FRHITransitionInfo TransitionsAfter[] = {
					FRHITransitionInfo(SrcTexture, ERHIAccess::CopySrc, ERHIAccess::SRVGraphics | ERHIAccess::SRVCompute),
					FRHITransitionInfo(DstTexture, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics | ERHIAccess::SRVCompute)
				};
				RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));

				GRenderTargetPool.FreeUnusedResource(TextureEntry.RenderTarget);
			}

			TextureEntry.RenderTarget = RenderTarget;
		}

		bNeedToAllocatePageTable = false;
	}

	if (bNeedToAllocatePageTableIndirection)
	{
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		if (Description.IndirectionTextureSize > 0)
		{
			const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(Description.IndirectionTextureSize, Description.IndirectionTextureSize),
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_None,
				TexCreate_UAV | TexCreate_ShaderResource,
				false);

			TRefCountPtr<IPooledRenderTarget> RenderTarget;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RenderTarget, TEXT("PageTableIndirection"));
			PageTableIndirection.RenderTarget = RenderTarget;

			FRHITexture* TextureRHI = RenderTarget->GetRenderTargetItem().ShaderResourceTexture;
			RHIUpdateTextureReference(PageTableIndirection.TextureReferenceRHI, TextureRHI);

			RHICmdList.ClearUAVUint(RHICreateUnorderedAccessView(TextureRHI), FUintVector4(ForceInitToZero));
		}

		bNeedToAllocatePageTableIndirection = false;
	}
}


void FVirtualTextureSpace::ApplyUpdates(FVirtualTextureSystem* System, FRHICommandListImmediate& RHICmdList)
{
	static TArray<FPageTableUpdate> ExpandedUpdates[VIRTUALTEXTURE_SPACE_MAXLAYERS][16];

	// Multi-GPU support : May be ineffecient for AFR.
	SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumPageTableLayers; ++LayerIndex)
	{
		FTexturePageMap& PageMap = PhysicalPageMap[LayerIndex];
		if (bForceEntireUpdate || CVarVTRefreshEntirePageTable.GetValueOnRenderThread())
		{
			PageMap.RefreshEntirePageTable(System, ExpandedUpdates[LayerIndex]);
		}
		else
		{
			for (const FPageTableUpdate& Update : PageTableUpdates[LayerIndex])
			{
				if (CVarVTMaskedPageTableUpdates.GetValueOnRenderThread())
				{
					PageMap.ExpandPageTableUpdateMasked(System, Update, ExpandedUpdates[LayerIndex]);
				}
				else
				{
					PageMap.ExpandPageTableUpdatePainters(System, Update, ExpandedUpdates[LayerIndex]);
				}
			}
		}
		PageTableUpdates[LayerIndex].Reset();
	}
	bForceEntireUpdate = false;

	// TODO Expand 3D updates for slices of volume texture

	uint32 TotalNumUpdates = 0;
	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumPageTableLayers; ++LayerIndex)
	{
		for (uint32 Mip = 0; Mip < NumPageTableLevels; Mip++)
		{
			TotalNumUpdates += ExpandedUpdates[LayerIndex][Mip].Num();
		}
	}

	if (TotalNumUpdates == 0u)
	{
		for (uint32 i = 0u; i < GetNumPageTableTextures(); ++i)
		{
			GVisualizeTexture.SetCheckPoint(RHICmdList, PageTable[i].RenderTarget);
		}
		return;
	}

	if (UpdateBuffer == nullptr || TotalNumUpdates * sizeof(FPageTableUpdate) > UpdateBuffer->GetSize())
	{
		// Resize Update Buffer
		const uint32 MaxUpdates = FMath::RoundUpToPowerOfTwo(TotalNumUpdates);
		uint32 NewBufferSize = MaxUpdates * sizeof(FPageTableUpdate);
		if (UpdateBuffer)
		{
			NewBufferSize = FMath::Max(NewBufferSize, UpdateBuffer->GetSize() * 2u);
		}

		FRHIResourceCreateInfo CreateInfo;
		UpdateBuffer = RHICreateVertexBuffer(NewBufferSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
		UpdateBufferSRV = RHICreateShaderResourceView(UpdateBuffer, sizeof(FPageTableUpdate), PF_R16G16B16A16_UINT);
	}

	// This flushes the RHI thread!
	{
		uint8* Buffer = (uint8*)RHILockVertexBuffer(UpdateBuffer, 0, TotalNumUpdates * sizeof(FPageTableUpdate), RLM_WriteOnly);
		for (uint32 LayerIndex = 0u; LayerIndex < Description.NumPageTableLayers; ++LayerIndex)
		{
			for (uint32 Mip = 0; Mip < NumPageTableLevels; Mip++)
			{
				const uint32 NumUpdates = ExpandedUpdates[LayerIndex][Mip].Num();
				if (NumUpdates)
				{
					size_t UploadSize = NumUpdates * sizeof(FPageTableUpdate);
					FMemory::Memcpy(Buffer, ExpandedUpdates[LayerIndex][Mip].GetData(), UploadSize);
					Buffer += UploadSize;
				}
			}
		}
		RHIUnlockVertexBuffer(UpdateBuffer);
	}

	TArray<FRHITransitionInfo, SceneRenderingAllocator> TexturesToTransition;
	TexturesToTransition.SetNumUninitialized(GetNumPageTableTextures());
	for (int32 i = 0; i < TexturesToTransition.Num(); ++i)
	{
		TexturesToTransition[i] = FRHITransitionInfo(PageTable[i].RenderTarget->GetRenderTargetItem().TargetableTexture, ERHIAccess::Unknown, ERHIAccess::RTV);
	}
	RHICmdList.Transition(MakeArrayView(TexturesToTransition.GetData(), TexturesToTransition.Num()));

	// Draw
	SCOPED_DRAW_EVENT(RHICmdList, PageTableUpdate);

	auto ShaderMap = GetGlobalShaderMap(GetFeatureLevel());
	TShaderRef<FPageTableUpdateVS> VertexShader;
	if (Description.PageTableFormat == EVTPageTableFormat::UInt16)
	{
		VertexShader = ShaderMap->GetShader< TPageTableUpdateVS<true> >();
	}
	else
	{
		VertexShader = ShaderMap->GetShader< TPageTableUpdateVS<false> >();
	}
	check(VertexShader.IsValid());

	uint32 FirstUpdate = 0;
	for (uint32 LayerIndex = 0u; LayerIndex < Description.NumPageTableLayers; ++LayerIndex)
	{
		const uint32 TextureIndex = LayerIndex / LayersPerPageTableTexture;
		const uint32 LayerInTexture = LayerIndex % LayersPerPageTableTexture;

		FTextureEntry& PageTableEntry = PageTable[TextureIndex];
		check(PageTableEntry.RenderTarget != nullptr);

		FSceneRenderTargetItem& PageTableTarget = PageTableEntry.RenderTarget->GetRenderTargetItem();

		// Use color write mask to update the proper page table entry for this layer
		FRHIBlendState* BlendStateRHI = nullptr;
		switch (LayerInTexture)
		{
		case 0u: BlendStateRHI = TStaticBlendState<CW_RED>::GetRHI(); break;
		case 1u: BlendStateRHI = TStaticBlendState<CW_GREEN>::GetRHI(); break;
		case 2u: BlendStateRHI = TStaticBlendState<CW_BLUE>::GetRHI(); break;
		case 3u: BlendStateRHI = TStaticBlendState<CW_ALPHA>::GetRHI(); break;
		default: check(false); break;
		}

		TShaderRef<FPageTableUpdatePS> PixelShader;
		switch (TexturePixelFormat[TextureIndex])
		{
		case PF_R16_UINT: PixelShader = ShaderMap->GetShader< TPageTableUpdatePS<PF_R16_UINT> >(); break;
		case PF_R16G16_UINT: PixelShader = ShaderMap->GetShader< TPageTableUpdatePS<PF_R16G16_UINT> >(); break;
		case PF_R16G16B16A16_UINT: PixelShader = ShaderMap->GetShader< TPageTableUpdatePS<PF_R16G16B16A16_UINT> >(); break;
		case PF_R32_UINT: PixelShader = ShaderMap->GetShader< TPageTableUpdatePS<PF_R32_UINT> >(); break;
		case PF_R32G32_UINT: PixelShader = ShaderMap->GetShader< TPageTableUpdatePS<PF_R32G32_UINT> >(); break;
		case PF_R32G32B32A32_UINT: PixelShader = ShaderMap->GetShader< TPageTableUpdatePS<PF_R32G32B32A32_UINT> >(); break;
		default: checkNoEntry(); break;
		}
		check(PixelShader.IsValid());

		uint32 MipSize = PageTableSize;
		for (uint32 Mip = 0; Mip < NumPageTableLevels; Mip++)
		{
			const uint32 NumUpdates = ExpandedUpdates[LayerIndex][Mip].Num();
			if (NumUpdates)
			{
				FRHIRenderPassInfo RPInfo(PageTableTarget.TargetableTexture, ERenderTargetActions::Load_Store, nullptr, Mip);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("PageTableUpdate"));
				
				RHICmdList.SetViewport(0, 0, 0.0f, MipSize, MipSize, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BlendState = BlendStateRHI;
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				{
					FRHIVertexShader* ShaderRHI = VertexShader.GetVertexShader();
					SetShaderValue(RHICmdList, ShaderRHI, VertexShader->PageTableSize, PageTableSize);
					SetShaderValue(RHICmdList, ShaderRHI, VertexShader->FirstUpdate, FirstUpdate);
					SetShaderValue(RHICmdList, ShaderRHI, VertexShader->NumUpdates, NumUpdates);
					SetSRVParameter(RHICmdList, ShaderRHI, VertexShader->UpdateBuffer, UpdateBufferSRV);
				}

				// needs to be the same on shader side (faster on NVIDIA and AMD)
				uint32 QuadsPerInstance = 8;

				RHICmdList.SetStreamSource(0, NULL, 0);
				RHICmdList.DrawIndexedPrimitive(GQuadIndexBuffer.IndexBufferRHI, 0, 0, 32, 0, 2 * QuadsPerInstance, FMath::DivideAndRoundUp(NumUpdates, QuadsPerInstance));

				RHICmdList.EndRenderPass();

				ExpandedUpdates[LayerIndex][Mip].Reset();
			}

			FirstUpdate += NumUpdates;
			MipSize >>= 1;
		}
	}

	for (uint32 i = 0u; i < GetNumPageTableTextures(); ++i)
	{
		FSceneRenderTargetItem& PageTableTarget = PageTable[i].RenderTarget->GetRenderTargetItem();
		RHICmdList.CopyToResolveTarget(PageTableTarget.TargetableTexture, PageTableTarget.ShaderResourceTexture, FResolveParams());
		GVisualizeTexture.SetCheckPoint(RHICmdList, PageTable[i].RenderTarget);
	}
}

void FVirtualTextureSpace::DumpToConsole(bool verbose)
{
	UE_LOG(LogConsoleResponse, Display, TEXT("-= Space ID %i =-"), ID);
	Allocator.DumpToConsole(verbose);
}
