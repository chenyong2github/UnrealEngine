// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderCore.h: Render core module implementation.
=============================================================================*/

#include "RenderCore.h"
#include "HAL/IConsoleManager.h"
#include "UniformBuffer.h"
#include "Modules/ModuleManager.h"
#include "Shader.h"
#include "HDRHelper.h"

void UpdateShaderDevelopmentMode();

void InitRenderGraph();

class FRenderCoreModule : public FDefaultModuleImpl
{
public:

	virtual void StartupModule() override
	{
		// TODO(RDG): Why is this not getting called?!
		IConsoleManager::Get().RegisterConsoleVariableSink_Handle(FConsoleCommandDelegate::CreateStatic(&UpdateShaderDevelopmentMode));

		InitRenderGraph();
	}
};

IMPLEMENT_MODULE(FRenderCoreModule, RenderCore);

DEFINE_LOG_CATEGORY(LogRendererCore);

/*------------------------------------------------------------------------------
	Stat declarations.
-----------------------------------------------------------------------------*/
// Cycle stats are rendered in reverse order from what they are declared in.
// They are organized so that stats at the top of the screen are earlier in the frame, 
// And stats that are indented are lower in the call hierarchy.

// The purpose of the SceneRendering stat group is to show where rendering thread time is going from a high level.
// It should only contain stats that are likely to track a lot of time in a typical scene, not edge case stats that are rarely non-zero.


// Amount of time measured by 'RenderViewFamily' that is not accounted for in its children stats
// Use a more detailed profiler (like an instruction trace or sampling capture on Xbox 360) to track down where this time is going if needed
DEFINE_STAT(STAT_RenderVelocities);
DEFINE_STAT(STAT_FinishRenderViewTargetTime);
DEFINE_STAT(STAT_CacheUniformExpressions);
DEFINE_STAT(STAT_TranslucencyDrawTime);
DEFINE_STAT(STAT_BeginOcclusionTestsTime);
// Use 'stat shadowrendering' to get more detail
DEFINE_STAT(STAT_ProjectedShadowDrawTime);
DEFINE_STAT(STAT_LightingDrawTime);
DEFINE_STAT(STAT_DynamicPrimitiveDrawTime);
DEFINE_STAT(STAT_StaticDrawListDrawTime);
DEFINE_STAT(STAT_BasePassDrawTime);
DEFINE_STAT(STAT_AnisotropyPassDrawTime);
DEFINE_STAT(STAT_DepthDrawTime);
DEFINE_STAT(STAT_WaterPassDrawTime);
DEFINE_STAT(STAT_DynamicShadowSetupTime);
DEFINE_STAT(STAT_RenderQueryResultTime);
// Use 'stat initviews' to get more detail
DEFINE_STAT(STAT_InitViewsTime);
DEFINE_STAT(STAT_GatherRayTracingWorldInstances);
DEFINE_STAT(STAT_InitViewsPossiblyAfterPrepass);
// Measures the time spent in RenderViewFamily_RenderThread
// Note that this is not the total rendering thread time, any other rendering commands will not be counted here
DEFINE_STAT(STAT_TotalSceneRenderingTime);
DEFINE_STAT(STAT_TotalGPUFrameTime);
DEFINE_STAT(STAT_PresentTime);

DEFINE_STAT(STAT_SceneLights);
DEFINE_STAT(STAT_MeshDrawCalls);

DEFINE_STAT(STAT_SceneDecals);
DEFINE_STAT(STAT_Decals);
DEFINE_STAT(STAT_DecalsDrawTime);

// Memory stats for tracking virtual allocations used by the renderer to represent the scene
// The purpose of these memory stats is to capture where most of the renderer allocated memory is going, 
// Not to track all of the allocations, and not to track resource memory (index buffers, vertex buffers, etc).

DEFINE_STAT(STAT_PrimitiveInfoMemory);
DEFINE_STAT(STAT_RenderingSceneMemory);
DEFINE_STAT(STAT_ViewStateMemory);
DEFINE_STAT(STAT_LightInteractionMemory);

// The InitViews stats group contains information on how long visibility culling took and how effective it was

DEFINE_STAT(STAT_GatherShadowPrimitivesTime);
DEFINE_STAT(STAT_BuildCSMVisibilityState);
DEFINE_STAT(STAT_UpdateIndirectLightingCache);
DEFINE_STAT(STAT_UpdateIndirectLightingCachePrims);
DEFINE_STAT(STAT_UpdateIndirectLightingCacheBlocks);
DEFINE_STAT(STAT_InterpolateVolumetricLightmapOnCPU);
DEFINE_STAT(STAT_UpdateIndirectLightingCacheTransitions);
DEFINE_STAT(STAT_UpdateIndirectLightingCacheFinalize);
DEFINE_STAT(STAT_SortStaticDrawLists);
DEFINE_STAT(STAT_InitDynamicShadowsTime);
DEFINE_STAT(STAT_InitProjectedShadowVisibility);
DEFINE_STAT(STAT_UpdatePreshadowCache);
DEFINE_STAT(STAT_CreateWholeSceneProjectedShadow);
DEFINE_STAT(STAT_AddViewDependentWholeSceneShadowsForView);
DEFINE_STAT(STAT_SetupInteractionShadows);
DEFINE_STAT(STAT_GetDynamicMeshElements);
DEFINE_STAT(STAT_SetupMeshPass);
DEFINE_STAT(STAT_UpdateStaticMeshesTime);
DEFINE_STAT(STAT_StaticRelevance);
DEFINE_STAT(STAT_ViewRelevance);
DEFINE_STAT(STAT_ComputeViewRelevance);
DEFINE_STAT(STAT_OcclusionCull);
DEFINE_STAT(STAT_UpdatePrimitiveFading);
DEFINE_STAT(STAT_PrimitiveCull);
DEFINE_STAT(STAT_DecompressPrecomputedOcclusion);
DEFINE_STAT(STAT_ViewVisibilityTime);

DEFINE_STAT(STAT_RayTracingInstances);
DEFINE_STAT(STAT_ProcessedPrimitives);
DEFINE_STAT(STAT_CulledPrimitives);
DEFINE_STAT(STAT_VisibleRayTracingPrimitives);
DEFINE_STAT(STAT_StaticallyOccludedPrimitives);
DEFINE_STAT(STAT_OccludedPrimitives);
DEFINE_STAT(STAT_OcclusionQueries);
DEFINE_STAT(STAT_VisibleStaticMeshElements);
DEFINE_STAT(STAT_VisibleDynamicPrimitives);
DEFINE_STAT(STAT_IndirectLightingCacheUpdates);
DEFINE_STAT(STAT_PrecomputedLightingBufferUpdates);
DEFINE_STAT(STAT_CSMSubjects);
DEFINE_STAT(STAT_CSMStaticMeshReceivers);
DEFINE_STAT(STAT_CSMStaticPrimitiveReceivers);

DEFINE_STAT(STAT_BindRayTracingPipeline);

// The ShadowRendering stats group shows what kind of shadows are taking a lot of rendering thread time to render
// Shadow setup is tracked in the InitViews group

DEFINE_STAT(STAT_RenderWholeSceneShadowProjectionsTime);
DEFINE_STAT(STAT_RenderWholeSceneShadowDepthsTime);
DEFINE_STAT(STAT_RenderPerObjectShadowProjectionsTime);
DEFINE_STAT(STAT_RenderPerObjectShadowDepthsTime);

DEFINE_STAT(STAT_WholeSceneShadows);
DEFINE_STAT(STAT_CachedWholeSceneShadows);
DEFINE_STAT(STAT_PerObjectShadows);
DEFINE_STAT(STAT_PreShadows);
DEFINE_STAT(STAT_CachedPreShadows);
DEFINE_STAT(STAT_ShadowDynamicPathDrawCalls);

DEFINE_STAT(STAT_TranslucentInjectTime);
DEFINE_STAT(STAT_DirectLightRenderingTime);
DEFINE_STAT(STAT_LightRendering);

DEFINE_STAT(STAT_NumShadowedLights);
DEFINE_STAT(STAT_NumLightFunctionOnlyLights);
DEFINE_STAT(STAT_NumBatchedLights);
DEFINE_STAT(STAT_NumLightsInjectedIntoTranslucency);
DEFINE_STAT(STAT_NumLightsUsingStandardDeferred);

DEFINE_STAT(STAT_LightShaftsLights);

DEFINE_STAT(STAT_ParticleUpdateRTTime);
DEFINE_STAT(STAT_InfluenceWeightsUpdateRTTime);
DEFINE_STAT(STAT_GPUSkinUpdateRTTime);
DEFINE_STAT(STAT_CPUSkinUpdateRTTime);

DEFINE_STAT(STAT_UpdateGPUSceneTime);

DEFINE_STAT(STAT_RemoveSceneLightTime);
DEFINE_STAT(STAT_UpdateSceneLightTime);
DEFINE_STAT(STAT_AddSceneLightTime);

DEFINE_STAT(STAT_RemoveScenePrimitiveTime);
DEFINE_STAT(STAT_AddScenePrimitiveRenderThreadTime);
DEFINE_STAT(STAT_UpdateScenePrimitiveRenderThreadTime);
DEFINE_STAT(STAT_UpdatePrimitiveTransformRenderThreadTime);
DEFINE_STAT(STAT_UpdatePrimitiveInstanceRenderThreadTime);
DEFINE_STAT(STAT_FlushAsyncLPICreation);

DEFINE_STAT(STAT_RemoveScenePrimitiveGT);
DEFINE_STAT(STAT_AddScenePrimitiveGT);
DEFINE_STAT(STAT_UpdatePrimitiveTransformGT);
DEFINE_STAT(STAT_UpdatePrimitiveInstanceGT);
DEFINE_STAT(STAT_UpdateCustomPrimitiveDataGT);

DEFINE_STAT(STAT_Scene_SetShaderMapsOnMaterialResources_RT);
DEFINE_STAT(STAT_Scene_UpdateStaticDrawLists_RT);
DEFINE_STAT(STAT_Scene_UpdateStaticDrawListsForMaterials_RT);
DEFINE_STAT(STAT_GameToRendererMallocTotal);

DEFINE_STAT(STAT_NumReflectiveShadowMapLights);

DEFINE_STAT(STAT_ShadowmapAtlasMemory);
DEFINE_STAT(STAT_CachedShadowmapMemory);

DEFINE_STAT(STAT_RenderTargetPoolSize);
DEFINE_STAT(STAT_RenderTargetPoolUsed);
DEFINE_STAT(STAT_RenderTargetPoolCount);

#define EXPOSE_FORCE_LOD !(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR

#if EXPOSE_FORCE_LOD

static TAutoConsoleVariable<int32> CVarForceLOD(
	TEXT("r.ForceLOD"),
	-1,
	TEXT("LOD level to force, -1 is off."),
	ECVF_Scalability | ECVF_Default | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarForceLODShadow(
	TEXT("r.ForceLODShadow"),
	-1,
	TEXT("LOD level to force for the shadow map generation only, -1 is off."),
	ECVF_Scalability | ECVF_Default | ECVF_RenderThreadSafe
);

#endif // EXPOSE_FORCE_LOD

/** Whether to pause the global realtime clock for the rendering thread (read and write only on main thread). */
bool GPauseRenderingRealtimeClock;

/** Global realtime clock for the rendering thread. */
FTimer GRenderingRealtimeClock;

FInputLatencyTimer GInputLatencyTimer( 2.0f );

//
// FInputLatencyTimer implementation.
//

/** Potentially starts the timer on the gamethread, based on the UpdateFrequency. */
void FInputLatencyTimer::GameThreadTick()
{
#if STATS
	if (FThreadStats::IsCollectingData())
	{
		if ( !bInitialized )
		{
			LastCaptureTime	= FPlatformTime::Seconds();
			bInitialized	= true;
		}
		const double CurrentTimeInSeconds = FPlatformTime::Seconds();
		if ( (CurrentTimeInSeconds - LastCaptureTime) > UpdateFrequency )
		{
			LastCaptureTime		= CurrentTimeInSeconds;
			StartTime			= FPlatformTime::Cycles();
			GameThreadTrigger	= true;
		}
	}
#endif
}

// Can be optimized to avoid the virtual function call but it's compiled out for final release anyway
RENDERCORE_API int32 GetCVarForceLOD()
{
	int32 Ret = -1;

#if EXPOSE_FORCE_LOD
	{
		Ret = CVarForceLOD.GetValueOnRenderThread();
	}
#endif // EXPOSE_FORCE_LOD

	return Ret;
}

RENDERCORE_API int32 GetCVarForceLOD_AnyThread()
{
	int32 Ret = -1;

#if EXPOSE_FORCE_LOD
	{
		Ret = CVarForceLOD.GetValueOnAnyThread();
	}
#endif // EXPOSE_FORCE_LOD

	return Ret;
}

RENDERCORE_API int32 GetCVarForceLODShadow()
{
	int32 Ret = -1;

#if EXPOSE_FORCE_LOD
	{
		Ret = CVarForceLODShadow.GetValueOnRenderThread();
	}
#endif // EXPOSE_FORCE_LOD

	return Ret;
}

RENDERCORE_API int32 GetCVarForceLODShadow_AnyThread()
{
	int32 Ret = -1;

#if EXPOSE_FORCE_LOD
	{
		Ret = CVarForceLODShadow.GetValueOnAnyThread();
	}
#endif // EXPOSE_FORCE_LOD

	return Ret;
}

FMatrix44f FVirtualTextureUniformData::Invalid = FMatrix44f::Identity;

// Note: Enables or disables HDR support for a project. Typically this would be set on a per-project/per-platform basis in defaultengine.ini
TAutoConsoleVariable<int32> CVarAllowHDR(
	TEXT("r.AllowHDR"),
	0,
	TEXT("Creates an HDR compatible swap-chain and enables HDR display output.")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Allow HDR, if supported by the platform and display \n"),
	ECVF_ReadOnly);

// Note: These values are directly referenced in code. They are set in code at runtime and therefore cannot be set via ini files
// Please update all paths if changing
TAutoConsoleVariable<int32> CVarDisplayColorGamut(
	TEXT("r.HDR.Display.ColorGamut"),
	0,
	TEXT("Color gamut of the output display:\n")
	TEXT("0: Rec709 / sRGB, D65 (default)\n")
	TEXT("1: DCI-P3, D65\n")
	TEXT("2: Rec2020 / BT2020, D65\n")
	TEXT("3: ACES, D60\n")
	TEXT("4: ACEScg, D60\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarDisplayOutputDevice(
	TEXT("r.HDR.Display.OutputDevice"),
	0,
	TEXT("Device format of the output display:\n")
	TEXT("0: sRGB (LDR)\n")
	TEXT("1: Rec709 (LDR)\n")
	TEXT("2: Explicit gamma mapping (LDR)\n")
	TEXT("3: ACES 1000 nit ST-2084 (Dolby PQ) (HDR)\n")
	TEXT("4: ACES 2000 nit ST-2084 (Dolby PQ) (HDR)\n")
	TEXT("5: ACES 1000 nit ScRGB (HDR)\n")
	TEXT("6: ACES 2000 nit ScRGB (HDR)\n")
	TEXT("7: Linear EXR (HDR)\n")
	TEXT("8: Linear final color, no tone curve (HDR)\n")
	TEXT("9: Linear final color with tone curve\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarHDRNits(
	TEXT("r.HDR.DisplayNitsLevel"),
	0,
	TEXT("The configured display output nit level, assuming HDR output is enabled."),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarHDROutputEnabled(
	TEXT("r.HDR.EnableHDROutput"),
	0,
	TEXT("Creates an HDR compatible swap-chain and enables HDR display output.")
	TEXT("0: Disabled (default)\n")
	TEXT("1: Enable hardware-specific implementation\n"),
	ECVF_RenderThreadSafe
);

RENDERCORE_API bool IsHDREnabled()
{
	return GRHISupportsHDROutput && CVarHDROutputEnabled.GetValueOnAnyThread() != 0;
}

RENDERCORE_API bool IsHDRAllowed()
{
	// HDR can be forced on or off on the commandline. Otherwise we check the cvar r.AllowHDR
	if (FParse::Param(FCommandLine::Get(), TEXT("hdr")))
	{
		return true;
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("nohdr")))
	{
		return false;
	}

	return (CVarAllowHDR.GetValueOnAnyThread() != 0);
}

RENDERCORE_API EDisplayOutputFormat HDRGetDefaultDisplayOutputFormat()
{
	return static_cast<EDisplayOutputFormat>(FMath::Clamp(CVarDisplayOutputDevice.GetValueOnAnyThread(), 0, static_cast<int32>(EDisplayOutputFormat::MAX) - 1));
}

RENDERCORE_API EDisplayColorGamut HDRGetDefaultDisplayColorGamut()
{
	return static_cast<EDisplayColorGamut>(FMath::Clamp(CVarDisplayColorGamut.GetValueOnAnyThread(), 0, static_cast<int32>(EDisplayColorGamut::MAX) - 1));
}

struct FHDRMetaData
{
	EDisplayOutputFormat DisplayOutputFormat;
	EDisplayColorGamut DisplayColorGamut;
	uint32 MaximumLuminanceInNits;
	bool bHDRSupported;
};


inline FHDRMetaData HDRGetDefaultMetaData()
{
	FHDRMetaData HDRMetaData{};
	HDRMetaData.DisplayOutputFormat = HDRGetDefaultDisplayOutputFormat();
	HDRMetaData.DisplayColorGamut = HDRGetDefaultDisplayColorGamut();
	HDRMetaData.bHDRSupported = IsHDREnabled() && GRHISupportsHDROutput;
	HDRMetaData.MaximumLuminanceInNits = CVarHDRNits.GetValueOnAnyThread();
	return HDRMetaData;
}

inline int32 WindowDisplayIntersectionArea(FIntRect WindowRect, FIntRect DisplayRect)
{
	return FMath::Max<int32>(0, FMath::Min<int32>(WindowRect.Max.X, DisplayRect.Max.X) - FMath::Max<int32>(WindowRect.Min.X, DisplayRect.Min.X)) *
		FMath::Max<int32>(0, FMath::Min<int32>(WindowRect.Max.Y, DisplayRect.Max.Y) - FMath::Max<int32>(WindowRect.Min.Y, DisplayRect.Min.Y));
}

TMap<void*, FHDRMetaData> GWindowsWithDefaultParams;
FCriticalSection GWindowsWithDefaultParamsCS;

RENDERCORE_API void HDRAddCustomMetaData(void* OSWindow, EDisplayOutputFormat DisplayOutputFormat, EDisplayColorGamut DisplayColorGamut, bool bHDREnabled)
{
	ensure(OSWindow != nullptr);
	if (OSWindow == nullptr)
	{
		return;
	}
	FHDRMetaData HDRMetaData{};
	HDRMetaData.DisplayOutputFormat = DisplayOutputFormat;
	HDRMetaData.DisplayColorGamut = DisplayColorGamut;
	HDRMetaData.bHDRSupported = bHDREnabled;
	HDRMetaData.MaximumLuminanceInNits = CVarHDRNits.GetValueOnAnyThread();

	FScopeLock Lock(&GWindowsWithDefaultParamsCS);
	GWindowsWithDefaultParams.Add(OSWindow, HDRMetaData);
}

RENDERCORE_API void HDRRemoveCustomMetaData(void* OSWindow)
{
	ensure(OSWindow != nullptr);
	if (OSWindow == nullptr)
	{
		return;
	}
	FScopeLock Lock(&GWindowsWithDefaultParamsCS);
	GWindowsWithDefaultParams.Remove(OSWindow);
}

bool HdrHasWindowParamsFromCVars(void* OSWindow, FHDRMetaData& HDRMetaData)
{
	if (GWindowsWithDefaultParams.Num() == 0)
	{
		return false;
	}
	FScopeLock Lock(&GWindowsWithDefaultParamsCS);
	FHDRMetaData* FoundHDRMetaData = GWindowsWithDefaultParams.Find(OSWindow);
	if (FoundHDRMetaData)
	{
		HDRMetaData = *FoundHDRMetaData;
		return true;
	}

	return false;
}

RENDERCORE_API void HDRGetMetaData(EDisplayOutputFormat& OutDisplayOutputFormat, EDisplayColorGamut& OutDisplayColorGamut, bool& OutbHDRSupported, 
								   const FVector2D& WindowTopLeft, const FVector2D& WindowBottomRight, void* OSWindow)
{
	FHDRMetaData HDRMetaData;

#if WITH_EDITOR
	// this has priority over IsHDREnabled because MovieSceneCapture might request custom parameters
	if (HdrHasWindowParamsFromCVars(OSWindow, HDRMetaData))
	{
		return;
	}
#endif
	
	HDRMetaData = HDRGetDefaultMetaData();

	OutDisplayOutputFormat = HDRMetaData.DisplayOutputFormat;
	OutDisplayColorGamut = HDRMetaData.DisplayColorGamut;
	OutbHDRSupported = HDRMetaData.bHDRSupported;
	if (!IsHDREnabled() || OSWindow == nullptr)
	{
		return;
	}

	FDisplayInformationArray DisplayList;
	RHIGetDisplaysInformation(DisplayList);
	// In case we have 1 display or less, the CVars that were setup do represent the state of the displays
	if (DisplayList.Num() <= 1)
	{
		return;
	}

	FIntRect WindowRect((int32)WindowTopLeft.X, (int32)WindowTopLeft.Y, (int32)WindowBottomRight.X, (int32)WindowBottomRight.Y);
	int32 BestDisplayForWindow = 0;
	int32 BestArea = 0;
	for (int32 DisplayIndex = 0; DisplayIndex < DisplayList.Num(); ++DisplayIndex)
	{
		// Compute the intersection
		int32 CurrentArea = WindowDisplayIntersectionArea(WindowRect, DisplayList[DisplayIndex].DesktopCoordinates);
		if (CurrentArea > BestArea)
		{
			BestDisplayForWindow = DisplayIndex;
			BestArea = CurrentArea;
		}
	}

	OutbHDRSupported = DisplayList[BestDisplayForWindow].bHDRSupported;
	OutDisplayOutputFormat = EDisplayOutputFormat::SDR_sRGB;
	OutDisplayColorGamut = EDisplayColorGamut::sRGB_D65;

	if (OutbHDRSupported)
	{
		FPlatformMisc::ChooseHDRDeviceAndColorGamut(GRHIVendorId, CVarHDRNits.GetValueOnAnyThread(), OutDisplayOutputFormat, OutDisplayColorGamut);
	}

}

RENDERCORE_API void HDRConfigureCVars(bool bIsHDREnabled, uint32 DisplayNits, bool bFromGameSettings)
{
	if (bIsHDREnabled && !GRHISupportsHDROutput)
	{
		UE_LOG(LogRendererCore, Warning, TEXT("Trying to enable HDR but it is not supported by the RHI: IsHDREnabled will return false"));
		bIsHDREnabled = false;
	}

	EDisplayOutputFormat OutputDevice = EDisplayOutputFormat::SDR_sRGB;
	EDisplayColorGamut ColorGamut = EDisplayColorGamut::sRGB_D65;

	// If we are turning HDR on we must set the appropriate OutputDevice and ColorGamut.
	// If we are turning it off, we'll reset back to 0/0
	if (bIsHDREnabled)
	{
		FPlatformMisc::ChooseHDRDeviceAndColorGamut(GRHIVendorId, DisplayNits, OutputDevice, ColorGamut);
	}

	//CVarHDRNits is ECVF_SetByCode as it's only a mean of communicating the information from UGameUserSettings to the rest of the engine
	if (bIsHDREnabled)
	{
		CVarHDROutputEnabled->Set(1, bFromGameSettings ? ECVF_SetByGameSetting : ECVF_SetByCode);
		CVarHDRNits->Set((int32)DisplayNits, ECVF_SetByCode);
	}
	else
	{
		CVarHDROutputEnabled->Set(0, bFromGameSettings ? ECVF_SetByGameSetting : ECVF_SetByCode);
		CVarHDRNits->Set(0, ECVF_SetByCode);
	}

	CVarDisplayOutputDevice->Set((int32)OutputDevice, ECVF_SetByDeviceProfile);
	CVarDisplayColorGamut->Set((int32)ColorGamut, ECVF_SetByDeviceProfile);
}

RENDERCORE_API FMatrix44f GamutToXYZMatrix(EDisplayColorGamut ColorGamut)
{
	static const FMatrix44f sRGB_2_XYZ_MAT(
		FVector3f(0.4124564, 0.3575761, 0.1804375),
		FVector3f(0.2126729, 0.7151522, 0.0721750),
		FVector3f(0.0193339, 0.1191920, 0.9503041),
		FVector3f(0, 0, 0)
	);

	static const FMatrix44f Rec2020_2_XYZ_MAT(
		FVector3f(0.6369736, 0.1446172, 0.1688585),
		FVector3f(0.2627066, 0.6779996, 0.0592938),
		FVector3f(0.0000000, 0.0280728, 1.0608437),
		FVector3f(0, 0, 0)
	);

	static const FMatrix44f P3D65_2_XYZ_MAT(
		FVector3f(0.4865906, 0.2656683, 0.1981905),
		FVector3f(0.2289838, 0.6917402, 0.0792762),
		FVector3f(0.0000000, 0.0451135, 1.0438031),
		FVector3f(0, 0, 0)
	);
	switch (ColorGamut)
	{
	case EDisplayColorGamut::sRGB_D65: return sRGB_2_XYZ_MAT;
	case EDisplayColorGamut::Rec2020_D65: return Rec2020_2_XYZ_MAT;
	case EDisplayColorGamut::DCIP3_D65: return P3D65_2_XYZ_MAT;
	default:
		checkNoEntry();
		return FMatrix44f::Identity;
	}

}

RENDERCORE_API FMatrix44f XYZToGamutMatrix(EDisplayColorGamut ColorGamut)
{
	static const FMatrix44f XYZ_2_sRGB_MAT(
		FVector3f(3.2409699419, -1.5373831776, -0.4986107603),
		FVector3f(-0.9692436363, 1.8759675015, 0.0415550574),
		FVector3f(0.0556300797, -0.2039769589, 1.0569715142),
		FVector3f(0, 0, 0)
	);

	static const FMatrix44f XYZ_2_Rec2020_MAT(
		FVector3f(1.7166084, -0.3556621, -0.2533601),
		FVector3f(-0.6666829, 1.6164776, 0.0157685),
		FVector3f(0.0176422, -0.0427763, 0.94222867),
		FVector3f(0, 0, 0)
	);

	static const FMatrix44f XYZ_2_P3D65_MAT(
		FVector3f(2.4933963, -0.9313459, -0.4026945),
		FVector3f(-0.8294868, 1.7626597, 0.0236246),
		FVector3f(0.0358507, -0.0761827, 0.9570140),
		FVector3f(0, 0, 0)
	);

	switch (ColorGamut)
	{
	case EDisplayColorGamut::sRGB_D65: return XYZ_2_sRGB_MAT;
	case EDisplayColorGamut::Rec2020_D65: return XYZ_2_Rec2020_MAT;
	case EDisplayColorGamut::DCIP3_D65: return XYZ_2_P3D65_MAT;
	default:
		checkNoEntry();
		return FMatrix44f::Identity;
	}

}
