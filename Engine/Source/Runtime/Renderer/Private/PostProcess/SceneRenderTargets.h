// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRenderTargets.h: Scene render target definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "RendererInterface.h"
#include "RenderGraphUtils.h"
#include "../TranslucencyPass.h"
#include "../VT/VirtualTextureFeedback.h"
#include "SceneRenderTargetParameters.h"

class FViewInfo;
class FRDGBuilder;

/** Number of cube map shadow depth surfaces that will be created and used for rendering one pass point light shadows. */
static const int32 NumCubeShadowDepthSurfaces = 5;

/** 
 * Allocate enough sets of translucent volume textures to cover all the cascades, 
 * And then one more which will be used as a scratch target when doing ping-pong operations like filtering. 
 */
static const int32 NumTranslucentVolumeRenderTargetSets = (TVC_MAX + 1);

/** Forward declaration of console variable controlling translucent volume blur */
extern int32 GUseTranslucencyVolumeBlur;

/** Function returning current translucency lighting volume dimensions. */
inline int32 GetTranslucencyLightingVolumeDim()
{
	extern int32 GTranslucencyLightingVolumeDim;
	return FMath::Clamp(GTranslucencyLightingVolumeDim, 4, 2048);
}

/** Function to select the index of the volume texture that we will hold the final translucency lighting volume texture */
inline int SelectTranslucencyVolumeTarget(ETranslucencyVolumeCascade InCascade)
{
	if (GUseTranslucencyVolumeBlur)
	{
		switch (InCascade)
		{
		case TVC_Inner:
			{
				return 2;
			}
		case TVC_Outer:
			{
				return 0;
			}
		default:
			{
				// error
				check(false);
				return 0;
			}
		}
	}
	else
	{
		switch (InCascade)
		{
		case TVC_Inner:
			{
				return 0;
			}
		case TVC_Outer:
			{
				return 1;
			}
		default:
			{
				// error
				check(false);
				return 0;
			}
		}
	}
}

/** Number of surfaces used for translucent shadows. */
static const int32 NumTranslucencyShadowSurfaces = 2;

/*
* Stencil layout during basepass / deferred decals:
*		BIT ID    | USE
*		[0]       | sandbox bit (bit to be use by any rendering passes, but must be properly reset to 0 after using)
*		[1]       | unallocated
*		[2]       | unallocated
*		[3]       | Temporal AA mask for translucent object.
*		[4]       | Lighting channels
*		[5]       | Lighting channels
*		[6]       | Lighting channels
*		[7]       | primitive receive decal bit
*
* After deferred decals, stencil is cleared to 0 and no longer packed in this way, to ensure use of fast hardware clears and HiStencil.
*/
#define STENCIL_SANDBOX_BIT_ID				0
#define STENCIL_TEMPORAL_RESPONSIVE_AA_BIT_ID 3
#define STENCIL_LIGHTING_CHANNELS_BIT_ID	4
#define STENCIL_RECEIVE_DECAL_BIT_ID		7

// Outputs a compile-time constant stencil's bit mask ready to be used
// in TStaticDepthStencilState<> template parameter. It also takes care
// of masking the Value macro parameter to only keep the low significant
// bit to ensure to not overflow on other bits.
#define GET_STENCIL_BIT_MASK(BIT_NAME,Value) uint8((uint8(Value) & uint8(0x01)) << (STENCIL_##BIT_NAME##_BIT_ID))

#define STENCIL_SANDBOX_MASK GET_STENCIL_BIT_MASK(SANDBOX,1)

#define STENCIL_TEMPORAL_RESPONSIVE_AA_MASK GET_STENCIL_BIT_MASK(TEMPORAL_RESPONSIVE_AA,1)

#define STENCIL_LIGHTING_CHANNELS_MASK(Value) uint8((Value & 0x7) << STENCIL_LIGHTING_CHANNELS_BIT_ID)

// Mobile specific
// Store shading model into stencil [1-3] bits
#define GET_STENCIL_MOBILE_SM_MASK(Value) uint8((Value & 0x7) << 1)

#if !defined(PREVENT_RENDERTARGET_SIZE_THRASHING)
	#define PREVENT_RENDERTARGET_SIZE_THRASHING (PLATFORM_DESKTOP || PLATFORM_PS4 || PLATFORM_ANDROID || PLATFORM_IOS || PLATFORM_SWITCH || PLATFORM_UNIX)
#endif

enum class ESceneColorFormatType
{
	Mobile,
	HighEnd,
	HighEndWithAlpha,
	Num,
};

struct FCustomDepthTextures
{
	FRDGTextureRef CustomDepth{};
	FRDGTextureRef MobileCustomDepth{};
	FRDGTextureRef MobileCustomStencil{};
};

/**
 * Encapsulates the render targets used for scene rendering.
 */
class RENDERER_API FSceneRenderTargets : public FRenderResource
{
public:
	/** Destructor. */
	virtual ~FSceneRenderTargets() {}

	/** Singletons. At the moment parallel tasks get their snapshot from the rhicmdlist */
	static FSceneRenderTargets& Get(FRHIComputeCommandList& RHICmdList);

	// this is a placeholder, the context should come from somewhere. This is very unsafe, please don't use it!
	static FSceneRenderTargets& GetGlobalUnsafe();
	// As above but relaxed checks and always gives the global FSceneRenderTargets. The intention here is that it is only used for constants that don't change during a frame. This is very unsafe, please don't use it!
	static FSceneRenderTargets& Get_FrameConstantsOnly();

	/** Create a snapshot on the scene allocator */
	FSceneRenderTargets* CreateSnapshot(const FViewInfo& InView);
	/** Set a snapshot on the TargetCmdList */
	void SetSnapshotOnCmdList(FRHICommandList& TargetCmdList);	
	/** Destruct all snapshots */
	void DestroyAllSnapshots();

protected:
	/** Constructor */
	FSceneRenderTargets() :
		bScreenSpaceAOIsValid(false),
		bCustomDepthIsValid(false),
		GBufferRefCount(0),
		ThisFrameNumber(0),
		CurrentDesiredSizeIndex(0),
		BufferSize(0, 0),
		LastStereoSize(0, 0),
		SmallColorDepthDownsampleFactor(2),
		bUseDownsizedOcclusionQueries(true),
		CurrentGBufferFormat(0),
		CurrentSceneColorFormat(0),
		CurrentMobileSceneColorFormat(EPixelFormat::PF_Unknown),
		bAllowStaticLighting(true),
		CurrentMaxShadowResolution(0),
		CurrentRSMResolution(0),
		CurrentTranslucencyLightingVolumeDim(64),
		bCurrentLightPropagationVolume(false),
		bCurrentRequireMultiView(false),
		CurrentFeatureLevel(ERHIFeatureLevel::Num),
		CurrentShadingPath(EShadingPath::Num),
		bRequireSceneColorAlpha(false),
		bAllocateVelocityGBuffer(false),
		bSnapshot(false),
		DefaultColorClear(FClearValueBinding::Black),
		DefaultDepthClear(FClearValueBinding::DepthFar),
		bHMDAllocatedDepthTarget(false),
		bKeepDepthContent(true)
		{
			FMemory::Memset(LargestDesiredSizes, 0);
#if PREVENT_RENDERTARGET_SIZE_THRASHING
			FMemory::Memset(HistoryFlags, 0, sizeof(HistoryFlags));
#endif
		}
	/** Constructor that creates snapshot */
	FSceneRenderTargets(const FViewInfo& InView, const FSceneRenderTargets& SnapshotSource);
public:

	bool IsShadingPathValid() const
	{
		return CurrentShadingPath < EShadingPath::Num;
	}

	/**
	 * Checks that scene render targets are ready for rendering a view family of the given dimensions.
	 * If the allocated render targets are too small, they are reallocated.
	 */
	void Allocate(FRHICommandListImmediate& RHICmdList, const FSceneRenderer* SceneRenderer);

	void AllocSceneColor(FRHICommandList& RHICmdList);

	/**
	 *
	 */
	void SetBufferSize(int32 InBufferSizeX, int32 InBufferSizeY);

	void SetKeepDepthContent(bool bKeep)
	{
		bKeepDepthContent = bKeep;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Returns the RT index where the QuadOverdrawUAV will be bound. */
	static int32 GetQuadOverdrawUAVIndex(EShaderPlatform Platform, ERHIFeatureLevel::Type FeatureLevel);
#endif

	void ClearQuadOverdrawUAV(FRDGBuilder& GraphBuilder);

	FUnorderedAccessViewRHIRef GetQuadOverdrawBufferUAV() const;
	FUnorderedAccessViewRHIRef GetVirtualTextureFeedbackUAV() const;
	FIntPoint GetVirtualTextureFeedbackBufferSize() const;
	
	/** Get size of screen tiles used for virtual texture feedback. We evaluate one feedback item from each tile per frame. */
	static int32 GetVirtualTextureFeedbackScale();
	/** Returns a value from the sampling sequence used to traverse the feedback tile. */
	static uint32 SampleVirtualTextureFeedbackSequence(uint32 FrameIndex);

	/** Binds the appropriate shadow depth cube map for rendering. */
	void BeginRenderingCubeShadowDepth(FRHICommandList& RHICmdList, int32 ShadowResolution);

	void FreeReflectionScratchRenderTargets()
	{
		for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(ReflectionColorScratchCubemap); ++Idx)
		{
			ReflectionColorScratchCubemap[Idx].SafeRelease();
		}
		for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(DiffuseIrradianceScratchCubemap); ++Idx)
		{
			DiffuseIrradianceScratchCubemap[Idx].SafeRelease();
		}
	}

	void BeginRenderingPrePass(FRHICommandList& RHICmdList, bool bPerformClear, bool bStencilClear = true);
	void FinishRenderingPrePass(FRHICommandListImmediate& RHICmdList);

	void SetDefaultColorClear(const FClearValueBinding ColorClear)
	{
		DefaultColorClear = ColorClear;
	}

	FClearValueBinding GetDefaultColorClear() const
	{
		return DefaultColorClear;
	}

	void SetDefaultDepthClear(const FClearValueBinding DepthClear)
	{
		DefaultDepthClear = DepthClear;
	}

	FClearValueBinding GetDefaultDepthClear() const
	{
		return DefaultDepthClear;
	}

	/**
	 * Cleans up editor primitive targets that we no longer need
	 */
	void CleanUpEditorPrimitiveTargets();

	/**
	 * Affects the render quality of the editor 3d objects. MSAA is needed if >1
	 * @return clamped to reasonable numbers
	 */
	int32 GetEditorMSAACompositingSampleCount() const;

	/**
	* Affects the render quality of the scene. MSAA is needed if >1
	* @return clamped to reasonable numbers
	*/
	static uint16 GetNumSceneColorMSAASamples(ERHIFeatureLevel::Type InFeatureLevel, bool bRendererSupportMSAA = true);

	bool IsStaticLightingAllowed() const { return bAllowStaticLighting; }

	/**
	 * Gets the editor primitives color target/shader resource.  This may recreate the target
	 * if the msaa settings dont match
	 */
	const FTexture2DRHIRef& GetEditorPrimitivesColor(FRHICommandList& RHICmdList);

	/**
	 * Gets the editor primitives depth target/shader resource.  This may recreate the target
	 * if the msaa settings dont match
	 */
	const FTexture2DRHIRef& GetEditorPrimitivesDepth(FRHICommandList& RHICmdList);


	// FRenderResource interface.
	virtual void ReleaseDynamicRHI() override;

	// Texture Accessors -----------

	const FTextureRHIRef& GetSceneColorTexture() const;
	const FUnorderedAccessViewRHIRef& GetSceneColorTextureUAV() const;

	const FTexture2DRHIRef& GetSceneDepthTexture() const
	{
		static const FTexture2DRHIRef EmptyTexture;
		return SceneDepthZ ? (const FTexture2DRHIRef&)SceneDepthZ->GetRenderTargetItem().ShaderResourceTexture : EmptyTexture;
	}

	const FTexture2DRHIRef& GetGBufferATexture() const { return (const FTexture2DRHIRef&)GBufferA->GetRenderTargetItem().ShaderResourceTexture; }
	const FTexture2DRHIRef& GetGBufferBTexture() const { return (const FTexture2DRHIRef&)GBufferB->GetRenderTargetItem().ShaderResourceTexture; }
	const FTexture2DRHIRef& GetGBufferCTexture() const { return (const FTexture2DRHIRef&)GBufferC->GetRenderTargetItem().ShaderResourceTexture; }
	const FTexture2DRHIRef& GetGBufferDTexture() const { return (const FTexture2DRHIRef&)GBufferD->GetRenderTargetItem().ShaderResourceTexture; }
	const FTexture2DRHIRef& GetGBufferETexture() const { return (const FTexture2DRHIRef&)GBufferE->GetRenderTargetItem().ShaderResourceTexture; }
	const FTexture2DRHIRef& GetGBufferFTexture() const { return (const FTexture2DRHIRef&)GBufferF->GetRenderTargetItem().ShaderResourceTexture; }
	const FTexture2DRHIRef& GetGBufferVelocityTexture() const { return (const FTexture2DRHIRef&)SceneVelocity->GetRenderTargetItem().ShaderResourceTexture; }

	const FTextureRHIRef& GetSceneColorSurface() const;
	const FTexture2DRHIRef& GetSceneDepthSurface() const							{ return (const FTexture2DRHIRef&)SceneDepthZ->GetRenderTargetItem().TargetableTexture; }
	const FTexture2DRHIRef& GetSmallDepthSurface() const							{ return (const FTexture2DRHIRef&)SmallDepthZ->GetRenderTargetItem().TargetableTexture; }

	const FTexture2DRHIRef& GetDirectionalOcclusionTexture() const 
	{	
		return (const FTexture2DRHIRef&)DirectionalOcclusion->GetRenderTargetItem().TargetableTexture; 
	}

	// @return can be empty if the feature is disabled
	FCustomDepthTextures RequestCustomDepth(FRDGBuilder& GraphBuilder, bool bPrimitives);

	static bool IsCustomDepthPassWritingStencil(ERHIFeatureLevel::Type InFeatureLevel);

	bool UseDownsizedOcclusionQueries() const { return bUseDownsizedOcclusionQueries; }

	// ---

	template<int32 NumRenderTargets>
	static void ClearVolumeTextures(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);

	void ClearTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList, int32 ViewIndex);

	/** Get the current translucent ambient lighting volume texture. Can vary depending on whether volume filtering is enabled */
	IPooledRenderTarget* GetTranslucencyVolumeAmbient(ETranslucencyVolumeCascade Cascade, int32 ViewIndex = 0) const
	{
		if (TranslucencyLightingVolumeAmbient.Num())
		{
			return TranslucencyLightingVolumeAmbient[SelectTranslucencyVolumeTarget(Cascade) + ViewIndex * NumTranslucentVolumeRenderTargetSets].GetReference();
		}
		return nullptr;
	}

	/** Get the current translucent directional lighting volume texture. Can vary depending on whether volume filtering is enabled */
	IPooledRenderTarget* GetTranslucencyVolumeDirectional(ETranslucencyVolumeCascade Cascade, int32 ViewIndex = 0) const
	{
		if (TranslucencyLightingVolumeDirectional.Num())
		{
			return TranslucencyLightingVolumeDirectional[SelectTranslucencyVolumeTarget(Cascade) + ViewIndex * NumTranslucentVolumeRenderTargetSets].GetReference();
		}
		return nullptr;
	}

	/** Returns the size of most screen space render targets e.g. SceneColor, SceneDepth, GBuffer, ... might be different from final RT or output Size because of ScreenPercentage use. */
	FIntPoint GetBufferSizeXY() const { return BufferSize; }
	/** */
	uint32 GetSmallColorDepthDownsampleFactor() const { return SmallColorDepthDownsampleFactor; }
	/** Returns an index in the range [0, NumCubeShadowDepthSurfaces) given an input resolution. */
	int32 GetCubeShadowDepthZIndex(int32 ShadowResolution) const;
	/** Returns the appropriate resolution for a given cube shadow index. */
	int32 GetCubeShadowDepthZResolution(int32 ShadowIndex) const;
	/** Returns the size of the shadow depth buffer, taking into account platform limitations and game specific resolution limits. */
	FIntPoint GetShadowDepthTextureResolution() const;
	// @return >= 1x1 <= GMaxShadowDepthBufferSizeX x GMaxShadowDepthBufferSizeY
	FIntPoint GetPreShadowCacheTextureResolution() const;
	FIntPoint GetTranslucentShadowDepthTextureResolution() const;
	int32 GetTranslucentShadowDownsampleFactor() const { return 2; }

	/** Returns the size of the RSM buffer, taking into account platform limitations and game specific resolution limits. */
	inline int32 GetReflectiveShadowMapResolution() const { return CurrentRSMResolution; }

	int32 GetNumGBufferTargets() const;

	int32 GetMSAACount() const { return CurrentMSAACount; }

	// ---

	// needs to be called between AllocSceneColor() and ReleaseSceneColor()
	const TRefCountPtr<IPooledRenderTarget>& GetSceneColor() const;

	TRefCountPtr<IPooledRenderTarget>& GetSceneColor();

	EPixelFormat GetSceneColorFormat(ERHIFeatureLevel::Type InFeatureLevel) const;
	EPixelFormat GetSceneColorFormat() const;
	EPixelFormat GetDesiredMobileSceneColorFormat() const;
	EPixelFormat GetMobileSceneColorFormat() const;


	// changes depending at which part of the frame this is called
	bool IsSceneColorAllocated() const;

	void SetSceneColor(IPooledRenderTarget* In);

	// ---

	// allows to release the GBuffer once post process materials no longer need it
	// @param 1: add a reference, -1: remove a reference
	void AdjustGBufferRefCount(FRHICommandList& RHICmdList, int Delta);

	void PreallocGBufferTargets();
	EPixelFormat GetGBufferAFormat() const;
	EPixelFormat GetGBufferBFormat() const;
	EPixelFormat GetGBufferCFormat() const;
	EPixelFormat GetGBufferDFormat() const;
	EPixelFormat GetGBufferEFormat() const;
	EPixelFormat GetGBufferFFormat() const;
	void AllocGBufferTargets(FRHICommandList& RHICmdList);
	void AllocGBufferTargets(FRHICommandList& RHICmdList, ETextureCreateFlags AddTargetableFlags);

	void AllocateReflectionTargets(FRHICommandList& RHICmdList, int32 TargetSize);

	void AllocateVirtualTextureFeedbackBuffer(FRHICommandList& RHICmdList);

	void AllocateDebugViewModeTargets(FRHICommandList& RHICmdList);

	TRefCountPtr<IPooledRenderTarget>& GetReflectionBrightnessTarget();
	
	// Can be called when the Scene Color content is no longer needed. As we create SceneColor on demand we can make sure it is created with the right format.
	// (as a call to SetSceneColor() can override it with a different format)
	void ReleaseSceneColor();
	
	ERHIFeatureLevel::Type GetCurrentFeatureLevel() const { return CurrentFeatureLevel; }

private: // Get...() methods instead of direct access
	// 0 before BeginRenderingSceneColor and after tone mapping in deferred shading
	// Permanently allocated for forward shading
	TRefCountPtr<IPooledRenderTarget> SceneColor[(int32)ESceneColorFormatType::Num];
public:
	// Light Accumulation is a high precision scratch pad matching the size of the scene color buffer used by many passes.
	TRefCountPtr<IPooledRenderTarget> LightAccumulation;

	// Reflection Environment: Bringing back light accumulation buffer to apply indirect reflections
	TRefCountPtr<IPooledRenderTarget> DirectionalOcclusion;
	
	// Scene depth and stencil.
	TRefCountPtr<IPooledRenderTarget> SceneDepthZ;
	TRefCountPtr<FRHIShaderResourceView> SceneStencilSRV;
	// Scene velocity.
	TRefCountPtr<IPooledRenderTarget> SceneVelocity;

	// Quarter-sized version of the scene depths
	TRefCountPtr<IPooledRenderTarget> SmallDepthZ;

	// GBuffer: Geometry Buffer rendered in base pass for deferred shading, only available between AllocGBufferTargets() and FreeGBufferTargets()
	TRefCountPtr<IPooledRenderTarget> GBufferA;
	TRefCountPtr<IPooledRenderTarget> GBufferB;
	TRefCountPtr<IPooledRenderTarget> GBufferC;
	TRefCountPtr<IPooledRenderTarget> GBufferD;
	TRefCountPtr<IPooledRenderTarget> GBufferE;
	TRefCountPtr<IPooledRenderTarget> GBufferF;

	// Optional color attachment to store SceneDepth
	TRefCountPtr<IPooledRenderTarget> SceneDepthAux;

	// DBuffer: For decals before base pass (only temporarily available after early z pass and until base pass)
	TRefCountPtr<IPooledRenderTarget> DBufferA;
	TRefCountPtr<IPooledRenderTarget> DBufferB;
	TRefCountPtr<IPooledRenderTarget> DBufferC;
	TRefCountPtr<IPooledRenderTarget> DBufferMask;

	// for AmbientOcclusion, only valid for a short time during the frame to allow reuse
	TRefCountPtr<IPooledRenderTarget> ScreenSpaceAO;
	TRefCountPtr<IPooledRenderTarget> ScreenSpaceGTAOHorizons;
	// for shader/quad complexity, the temporary quad descriptors and complexity.
	TRefCountPtr<IPooledRenderTarget> QuadOverdrawBuffer;
	// used by the CustomDepth material feature, is allocated on demand or if r.CustomDepth is 2
	TRefCountPtr<IPooledRenderTarget> CustomDepth;
	// CustomDepth is memoryless on mobile, depth is saved in MobileCustomDepth Color RT 
	TRefCountPtr<IPooledRenderTarget> MobileCustomDepth;
	TRefCountPtr<IPooledRenderTarget> MobileCustomStencil;
	// used by the CustomDepth material feature for stencil
	TRefCountPtr<FRHIShaderResourceView> CustomStencilSRV;

	/** 2 scratch cubemaps used for filtering reflections. */
	TRefCountPtr<IPooledRenderTarget> ReflectionColorScratchCubemap[2];

	/** Temporary storage during SH irradiance map generation. */
	TRefCountPtr<IPooledRenderTarget> DiffuseIrradianceScratchCubemap[2];

	/** Temporary storage during SH irradiance map generation. */
	TRefCountPtr<IPooledRenderTarget> SkySHIrradianceMap;

	/** Volume textures used for lighting translucency. */
	TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<NumTranslucentVolumeRenderTargetSets>> TranslucencyLightingVolumeAmbient;
	TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<NumTranslucentVolumeRenderTargetSets>> TranslucencyLightingVolumeDirectional;

	/** Color and opacity for editor primitives (i.e editor gizmos). */
	TRefCountPtr<IPooledRenderTarget> EditorPrimitivesColor;

	/** Depth for editor primitives */
	TRefCountPtr<IPooledRenderTarget> EditorPrimitivesDepth;

	/** Virtual Texture feedback buffer bound as UAV during the base pass */
	FVertexBufferRHIRef VirtualTextureFeedback;
	FUnorderedAccessViewRHIRef VirtualTextureFeedbackUAV;

	// todo: free ScreenSpaceAO so pool can reuse
	bool bScreenSpaceAOIsValid;

	// todo: free ScreenSpaceAO so pool can reuse
	bool bCustomDepthIsValid;

private:
	/** used by AdjustGBufferRefCount */
	int32 GBufferRefCount;

	/** as we might get multiple BufferSize requests each frame for SceneCaptures and we want to avoid reallocations we can only go as low as the largest request */
	static const uint32 FrameSizeHistoryCount = 3;
	FIntPoint LargestDesiredSizes[FrameSizeHistoryCount];
#if PREVENT_RENDERTARGET_SIZE_THRASHING
	// bit 0 - whether any scene capture rendered
	// bit 1 - whether any reflection capture rendered
	uint8 HistoryFlags[FrameSizeHistoryCount];
#endif

	/** to detect when LargestDesiredSizeThisFrame is outdated */
	uint32 ThisFrameNumber;
	uint32 CurrentDesiredSizeIndex;

	/** CAUTION: When adding new data, make sure you copy it in the snapshot constructor! **/

	/**
	 * Initializes the editor primitive color render target
	 */
	void InitEditorPrimitivesColor(FRHICommandList& RHICmdList);

	/**
	 * Initializes the editor primitive depth buffer
	 */
	void InitEditorPrimitivesDepth(FRHICommandList& RHICmdList);

	/** Allocates render targets for use with the mobile path. */
	void AllocateMobileRenderTargets(FRHICommandListImmediate& RHICmdList);

public:
	/** Allocates render targets for use with the deferred shading path. */
	// Temporarily Public to call from DefferedShaderRenderer to attempt recovery from a crash until cause is found.
	void AllocateDeferredShadingPathRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews = 1);

	void AllocateAnisotropyTarget(FRHICommandListImmediate& RHICmdList);

	/** Fills the given FRenderPassInfo with the current GBuffer */
	int32 FillGBufferRenderPassInfo(ERenderTargetLoadAction ColorLoadAction, FRHIRenderPassInfo& OutRenderPassInfo, int32& OutVelocityRTIndex) const;

	/** Gets all GBuffers to use.  Returns the number actually used. */
	int32 GetGBufferRenderTargets(const TRefCountPtr<IPooledRenderTarget>* OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex, int32& OutGBufferDIndex) const;
	int32 GetGBufferRenderTargets(ERenderTargetLoadAction ColorLoadAction, FRHIRenderTargetView OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex) const;
	int32 GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, TStaticArray<FRDGTextureRef, MaxSimultaneousRenderTargets>& OutRenderTargets, int32& OutGBufferDIndex) const;
	int32 GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, ERenderTargetLoadAction ColorLoadAction, FRenderTargetBinding OutRenderTargets[MaxSimultaneousRenderTargets], int32& OutVelocityRTIndex) const;
	int32 GetGBufferRenderTargets(FRDGBuilder& GraphBuilder, ERenderTargetLoadAction ColorLoadAction, FRenderTargetBindingSlots& OutRenderTargets) const;

private:

	/** Allocates render targets for use with the current shading path. */
	void AllocateRenderTargets(FRHICommandListImmediate& RHICmdList, const int32 NumViews);

	/** Allocates common depth render targets that are used by both mobile and deferred rendering paths */
	void AllocateCommonDepthTargets(FRHICommandList& RHICmdList);

	/** Determine the appropriate render target dimensions. */
	FIntPoint ComputeDesiredSize(const FSceneViewFamily& ViewFamily);

	// internal method, used by AdjustGBufferRefCount()
	void ReleaseGBufferTargets();

	// release all allocated targets to the pool
	void ReleaseAllTargets();

	/** Get the current scene color target based on our current shading path. Will return a null ptr if there is no valid scene color target  */
	const TRefCountPtr<IPooledRenderTarget>& GetSceneColorForCurrentShadingPath() const { check(CurrentShadingPath < EShadingPath::Num); return SceneColor[(int32)GetSceneColorFormatType()]; }
	TRefCountPtr<IPooledRenderTarget>& GetSceneColorForCurrentShadingPath() { check(CurrentShadingPath < EShadingPath::Num); return SceneColor[(int32)GetSceneColorFormatType()]; }

	/** Determine whether the render targets for a particular shading path have been allocated */
	bool AreShadingPathRenderTargetsAllocated(ESceneColorFormatType InSceneColorFormatType) const;

	/** Determine if the default clear values for color and depth match the allocated scene render targets. Mobile only. */
	bool AreRenderTargetClearsValid(ESceneColorFormatType InSceneColorFormatType) const;

	/** Determine if an allocate is required for the render targets. */
	bool IsAllocateRenderTargetsRequired() const;

	/** Determine whether the render targets for any shading path have been allocated */
	bool AreAnyShadingPathRenderTargetsAllocated() const 
	{ 
		return AreShadingPathRenderTargetsAllocated(ESceneColorFormatType::HighEnd) 
			|| AreShadingPathRenderTargetsAllocated(ESceneColorFormatType::HighEndWithAlpha) 
			|| AreShadingPathRenderTargetsAllocated(ESceneColorFormatType::Mobile); 
	}

	ESceneColorFormatType GetSceneColorFormatType() const
	{
		if (CurrentShadingPath == EShadingPath::Mobile)
		{
			return ESceneColorFormatType::Mobile;
		}
		else if (CurrentShadingPath == EShadingPath::Deferred && (bRequireSceneColorAlpha || GetSceneColorFormat() == PF_FloatRGBA))
		{
			return ESceneColorFormatType::HighEndWithAlpha;
		}
		else if (CurrentShadingPath == EShadingPath::Deferred && !bRequireSceneColorAlpha)
		{
			return ESceneColorFormatType::HighEnd;
		}

		check(0);
		return ESceneColorFormatType::Num;
	}

	/** size of the back buffer, in editor this has to be >= than the biggest view port */
	FIntPoint BufferSize;
	/* Size of the first view, used for multiview rendertargets */
	FIntPoint View0Size;
	/* Size of the stereo view, if we're an XR device. */
	FIntPoint LastStereoSize;
	/** e.g. 2 */
	uint32 SmallColorDepthDownsampleFactor;
	/** Whether to use SmallDepthZ for occlusion queries. */
	bool bUseDownsizedOcclusionQueries;
	/** To detect a change of the CVar r.GBufferFormat */
	int32 CurrentGBufferFormat;
	/** To detect a change of the CVar r.SceneColorFormat */
	int32 CurrentSceneColorFormat;
	/** To detect a change of the mobile scene color format */
	EPixelFormat CurrentMobileSceneColorFormat;
	/** Whether render targets were allocated with static lighting allowed. */
	bool bAllowStaticLighting;
	/** To detect a change of the CVar r.Shadow.MaxResolution */
	int32 CurrentMaxShadowResolution;
	/** To detect a change of the CVar r.Shadow.RsmResolution*/
	int32 CurrentRSMResolution;
	/** To detect a change of the CVar r.TranslucencyLightingVolumeDim */
	int32 CurrentTranslucencyLightingVolumeDim;
	/** To detect a change of the CVar r.MobileMSAA or r.MSAA */
	int32 CurrentMSAACount;
	/** To detect a change of the CVar r.Shadow.MinResolution */
	int32 CurrentMinShadowResolution;
	/** To detect a change of the CVar r.LightPropagationVolume */
	bool bCurrentLightPropagationVolume;
	/** To detect a change of the CVar vr.MobileMultiView */
	bool bCurrentRequireMultiView;
	/** Feature level we were initialized for */
	ERHIFeatureLevel::Type CurrentFeatureLevel;
	/** Shading path that we are currently drawing through. Set when calling Allocate at the start of a scene render. */
	EShadingPath CurrentShadingPath;

	bool bRequireSceneColorAlpha;

	// Set this per frame since there might be cases where we don't need an extra GBuffer
	bool bAllocateVelocityGBuffer;

	/** true is this is a snapshot on the scene allocator */
	bool bSnapshot;

	/** Clear color value, defaults to FClearValueBinding::Black */
	FClearValueBinding DefaultColorClear;

	/** Clear depth value, defaults to FClearValueBinding::DepthFar */
	FClearValueBinding DefaultDepthClear;

	/** All outstanding snapshots */
	TArray<FSceneRenderTargets*> Snapshots;

	/** True if the depth target is allocated by an HMD plugin. This is a temporary fix to deal with HMD depth target swap chains not tracking the stencil SRV. */
	bool bHMDAllocatedDepthTarget;

	/** True if the contents of the depth buffer must be kept for post-processing. When this is false, the depth buffer can be allocated as memoryless on mobile platforms which support it. */
	bool bKeepDepthContent;

	/** True if scenecolor and depth should be multiview-allocated */
	bool bRequireMultiView;

	/** CAUTION: When adding new data, make sure you copy it in the snapshot constructor! **/

};

extern template void FSceneRenderTargets::ClearVolumeTextures<0>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<1>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<2>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<3>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<4>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<5>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<6>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);
extern template void FSceneRenderTargets::ClearVolumeTextures<7>(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, FRHITexture** RenderTargets, const FLinearColor* ClearColors);

/** Sets up scene texture parameters for RDG (builder is valid) or passthrough RHI access (builder is null). Intended for temporary use during RDG refactor. */
extern void SetupSceneTextureUniformParameters(
	FRDGBuilder* GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const FSceneRenderTargets& SceneContext,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& SceneTextureParameters);