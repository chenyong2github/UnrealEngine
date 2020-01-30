// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Reflection Environment - feature that provides HDR glossy reflections on any surfaces, leveraging precomputation to prefilter cubemaps of the scene
=============================================================================*/

#include "ReflectionEnvironment.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UniformBuffer.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "DeferredShadingRenderer.h"
#include "BasePassRendering.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessSubsurface.h"
#include "LightRendering.h"
#include "LightPropagationVolumeSettings.h"
#include "PipelineStateCache.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "ScreenSpaceDenoise.h"
#include "ScreenSpaceRayTracing.h"
#include "RayTracing/RaytracingOptions.h"
#include "RenderGraph.h"
#include "PixelShaderUtils.h"
#include "SceneTextureParameters.h"
#include "HairStrands/HairStrandsRendering.h"


extern TAutoConsoleVariable<int32> CVarLPVMixing;

static TAutoConsoleVariable<int32> CVarReflectionEnvironment(
	TEXT("r.ReflectionEnvironment"),
	1,
	TEXT("Whether to render the reflection environment feature, which implements local reflections through Reflection Capture actors.\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on and blend with scene (default)")
	TEXT(" 2: on and overwrite scene (only in non-shipping builds)"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

int32 GReflectionEnvironmentLightmapMixing = 1;
FAutoConsoleVariableRef CVarReflectionEnvironmentLightmapMixing(
	TEXT("r.ReflectionEnvironmentLightmapMixing"),
	GReflectionEnvironmentLightmapMixing,
	TEXT("Whether to mix indirect specular from reflection captures with indirect diffuse from lightmaps for rough surfaces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GReflectionEnvironmentLightmapMixBasedOnRoughness = 1;
FAutoConsoleVariableRef CVarReflectionEnvironmentLightmapMixBasedOnRoughness(
	TEXT("r.ReflectionEnvironmentLightmapMixBasedOnRoughness"),
	GReflectionEnvironmentLightmapMixBasedOnRoughness,
	TEXT("Whether to reduce lightmap mixing with reflection captures for very smooth surfaces.  This is useful to make sure reflection captures match SSR / planar reflections in brightness."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GReflectionEnvironmentBeginMixingRoughness = .1f;
FAutoConsoleVariableRef CVarReflectionEnvironmentBeginMixingRoughness(
	TEXT("r.ReflectionEnvironmentBeginMixingRoughness"),
	GReflectionEnvironmentBeginMixingRoughness,
	TEXT("Min roughness value at which to begin mixing reflection captures with lightmap indirect diffuse."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GReflectionEnvironmentEndMixingRoughness = .3f;
FAutoConsoleVariableRef CVarReflectionEnvironmentEndMixingRoughness(
	TEXT("r.ReflectionEnvironmentEndMixingRoughness"),
	GReflectionEnvironmentEndMixingRoughness,
	TEXT("Min roughness value at which to end mixing reflection captures with lightmap indirect diffuse."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GReflectionEnvironmentLightmapMixLargestWeight = 10000;
FAutoConsoleVariableRef CVarReflectionEnvironmentLightmapMixLargestWeight(
	TEXT("r.ReflectionEnvironmentLightmapMixLargestWeight"),
	GReflectionEnvironmentLightmapMixLargestWeight,
	TEXT("When set to 1 can be used to clamp lightmap mixing such that only darkening from lightmaps are applied to reflection captures."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarDoTiledReflections(
	TEXT("r.DoTiledReflections"),
	1,
	TEXT("Compute Reflection Environment with Tiled compute shader..\n")
	TEXT(" 0: off\n")
	TEXT(" 1: on (default)"),
	ECVF_RenderThreadSafe);


// to avoid having direct access from many places
int GetReflectionEnvironmentCVar()
{
	int32 RetVal = CVarReflectionEnvironment.GetValueOnAnyThread();

#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Disabling the debug part of this CVar when in shipping
	if (RetVal == 2)
	{
		RetVal = 1;
	}
#endif

	return RetVal;
}

FVector GetReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight()
{
	float RoughnessMixingRange = 1.0f / FMath::Max(GReflectionEnvironmentEndMixingRoughness - GReflectionEnvironmentBeginMixingRoughness, .001f);

	if (GReflectionEnvironmentLightmapMixing == 0)
	{
		return FVector(0, 0, GReflectionEnvironmentLightmapMixLargestWeight);
	}

	if (GReflectionEnvironmentEndMixingRoughness == 0.0f && GReflectionEnvironmentBeginMixingRoughness == 0.0f)
	{
		// Make sure a Roughness of 0 results in full mixing when disabling roughness-based mixing
		return FVector(0, 1, GReflectionEnvironmentLightmapMixLargestWeight);
	}

	if (!GReflectionEnvironmentLightmapMixBasedOnRoughness)
	{
		return FVector(0, 1, GReflectionEnvironmentLightmapMixLargestWeight);
	}

	return FVector(RoughnessMixingRange, -GReflectionEnvironmentBeginMixingRoughness * RoughnessMixingRange, GReflectionEnvironmentLightmapMixLargestWeight);
}

bool IsReflectionEnvironmentAvailable(ERHIFeatureLevel::Type InFeatureLevel)
{
	return (InFeatureLevel >= ERHIFeatureLevel::SM5) && (GetReflectionEnvironmentCVar() != 0);
}

bool IsReflectionCaptureAvailable()
{
	static IConsoleVariable* AllowStaticLightingVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AllowStaticLighting"));
	return (!AllowStaticLightingVar || AllowStaticLightingVar->GetInt() != 0);
}

void FReflectionEnvironmentCubemapArray::InitDynamicRHI()
{
	if (GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(CubemapSize) + 1;

		ReleaseCubeArray();

		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::CreateCubemapArrayDesc(
			CubemapSize,
			// Alpha stores sky mask
			PF_FloatRGBA,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_None,
			false,
			MaxCubemaps,
			NumReflectionCaptureMips,
			false
		);
	
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		// Allocate TextureCubeArray for the scene's reflection captures
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ReflectionEnvs, TEXT("ReflectionEnvs"));
	}
}

void FReflectionEnvironmentCubemapArray::ReleaseCubeArray()
{
	// it's unlikely we can reuse the TextureCubeArray so when we release it we want to really remove it
	GRenderTargetPool.FreeUnusedResource(ReflectionEnvs);
}

void FReflectionEnvironmentCubemapArray::ReleaseDynamicRHI()
{
	ReleaseCubeArray();
}

void FReflectionEnvironmentSceneData::ResizeCubemapArrayGPU(uint32 InMaxCubemaps, int32 InCubemapSize)
{
	check(IsInRenderingThread());

	// If the cubemap array isn't setup yet then no copying/reallocation is necessary. Just go through the old path
	if (!CubemapArray.IsInitialized())
	{
		CubemapArraySlotsUsed.Init(false, InMaxCubemaps);
		CubemapArray.UpdateMaxCubemaps(InMaxCubemaps, InCubemapSize);
		return;
	}

	// Generate a remapping table for the elements
	TArray<int32> IndexRemapping;
	int32 Count = 0;
	for (int i = 0; i < CubemapArray.GetMaxCubemaps(); i++)
	{
		bool bUsed = i < CubemapArraySlotsUsed.Num() ? CubemapArraySlotsUsed[i] : false;
		if (bUsed)
		{
			IndexRemapping.Add(Count);
			Count++;
		}
		else
		{
			IndexRemapping.Add(-1);
		}
	}

	// Reset the CubemapArraySlotsUsed array (we'll recompute it below)
	CubemapArraySlotsUsed.Init(false, InMaxCubemaps);

	// Spin through the AllocatedReflectionCaptureState map and remap the indices based on the LUT
	TArray<const UReflectionCaptureComponent*> Components;
	AllocatedReflectionCaptureState.GetKeys(Components);
	int32 UsedCubemapCount = 0;
	for (int32 i=0; i<Components.Num(); i++ )
	{
		FCaptureComponentSceneState* ComponentStatePtr = AllocatedReflectionCaptureState.Find(Components[i]);
		check(ComponentStatePtr->CubemapIndex < IndexRemapping.Num());
		int32 NewIndex = IndexRemapping[ComponentStatePtr->CubemapIndex];
		CubemapArraySlotsUsed[NewIndex] = true; 
		ComponentStatePtr->CubemapIndex = NewIndex;
		check(ComponentStatePtr->CubemapIndex > -1);
		UsedCubemapCount = FMath::Max(UsedCubemapCount, ComponentStatePtr->CubemapIndex + 1);
	}

	// Clear elements in the remapping array which are outside the range of the used components (these were allocated but not used)
	for (int i = 0; i < IndexRemapping.Num(); i++)
	{
		if (IndexRemapping[i] >= UsedCubemapCount)
		{
			IndexRemapping[i] = -1;
		}
	}

	CubemapArray.ResizeCubemapArrayGPU(InMaxCubemaps, InCubemapSize, IndexRemapping);
}

void FReflectionEnvironmentCubemapArray::ResizeCubemapArrayGPU(uint32 InMaxCubemaps, int32 InCubemapSize, const TArray<int32>& IndexRemapping)
{
	check(IsInRenderingThread());
	check(GetFeatureLevel() >= ERHIFeatureLevel::SM5);
	check(IsInitialized());
	check(InCubemapSize == CubemapSize);

	// Take a reference to the old cubemap array and then release it to prevent it getting destroyed during InitDynamicRHI
	TRefCountPtr<IPooledRenderTarget> OldReflectionEnvs = ReflectionEnvs;
	ReflectionEnvs = nullptr;
	int OldMaxCubemaps = MaxCubemaps;
	MaxCubemaps = InMaxCubemaps;

	InitDynamicRHI();

	FTextureRHIRef TexRef = OldReflectionEnvs->GetRenderTargetItem().TargetableTexture;
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	const int32 NumMips = FMath::CeilLogTwo(InCubemapSize) + 1;

	{
		SCOPED_DRAW_EVENT(RHICmdList, ReflectionEnvironment_ResizeCubemapArray);
		//SCOPED_GPU_STAT(RHICmdList, ReflectionEnvironment)

		// Copy the cubemaps, remapping the elements as necessary
		FResolveParams ResolveParams;
		ResolveParams.Rect = FResolveRect();
		for (int32 SourceCubemapIndex = 0; SourceCubemapIndex < OldMaxCubemaps; SourceCubemapIndex++)
		{
			int32 DestCubemapIndex = IndexRemapping[SourceCubemapIndex];
			if (DestCubemapIndex != -1)
			{
				ResolveParams.SourceArrayIndex = SourceCubemapIndex;
				ResolveParams.DestArrayIndex = DestCubemapIndex;

				check(SourceCubemapIndex < OldMaxCubemaps);
				check(DestCubemapIndex < (int32)MaxCubemaps);

				for (int Face = 0; Face < 6; Face++)
				{
					ResolveParams.CubeFace = (ECubeFace)Face;
					for (int Mip = 0; Mip < NumMips; Mip++)
					{
						ResolveParams.MipIndex = Mip;
						//@TODO: We should use an explicit copy method for this rather than CopyToResolveTarget, but that doesn't exist right now. 
						// For now, we'll just do this on RHIs where we know CopyToResolveTarget does the right thing. In future we should look to 
						// add a a new RHI method
						check(GRHISupportsResolveCubemapFaces);
						RHICmdList.CopyToResolveTarget(OldReflectionEnvs->GetRenderTargetItem().ShaderResourceTexture, ReflectionEnvs->GetRenderTargetItem().ShaderResourceTexture, ResolveParams);
					}
				}
			}
		}
	}
	GRenderTargetPool.FreeUnusedResource(OldReflectionEnvs);
}

void FReflectionEnvironmentCubemapArray::UpdateMaxCubemaps(uint32 InMaxCubemaps, int32 InCubemapSize)
{
	MaxCubemaps = InMaxCubemaps;
	CubemapSize = InCubemapSize;

	// Reallocate the cubemap array
	if (IsInitialized())
	{
		UpdateRHI();
	}
	else
	{
		InitResource();
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FReflectionCaptureShaderData, "ReflectionCapture");
