// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTextureScalability.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "CoreGlobals.h"
#include "EngineModule.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "RendererInterface.h"
#include "UObject/UObjectIterator.h"
#include "VT/RuntimeVirtualTexture.h"

namespace VirtualTextureScalability
{
#if WITH_EDITOR
	static TAutoConsoleVariable<int32> CVarVTMaxUploadsPerFrameInEditor(
		TEXT("r.VT.MaxUploadsPerFrameInEditor"),
		64,
		TEXT("Max number of page uploads per frame when in editor"),
		ECVF_RenderThreadSafe
	);
#endif 

	static TAutoConsoleVariable<int32> CVarVTMaxUploadsPerFrame(
		TEXT("r.VT.MaxUploadsPerFrame"),
		8,
		TEXT("Max number of page uploads per frame in game"),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

#if WITH_EDITOR
	static TAutoConsoleVariable<int32> CVarVTMaxContinuousUpdatesPerFrameInEditor(
		TEXT("r.VT.MaxContinuousUpdatesPerFrameInEditor"),
		128,
		TEXT("Max number of page uploads for pages that are already mapped when in editor."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);
#endif

	static TAutoConsoleVariable<int32> CVarVTMaxContinuousUpdatesPerFrame(
		TEXT("r.VT.MaxContinuousUpdatesPerFrame"),
		1,
		TEXT("Max number of page uploads for pages that are already mapped."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

	static TAutoConsoleVariable<int32> CVarVTMaxAnisotropy(
		TEXT("r.VT.MaxAnisotropy"),
		8,
		TEXT("MaxAnisotropy setting for Virtual Texture sampling."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

	static const int NumScalabilityGroups = 3;

	static float GPoolSizeScales[NumScalabilityGroups] = { 1.f, 1.f, 1.f };
	static FAutoConsoleVariableRef CVarVTPoolSizeScale_ForBackwardsCompat(
		TEXT("r.VT.PoolSizeScale"),
		GPoolSizeScales[0],
		TEXT("Scale factor for virtual texture physical pool size.\n")
		TEXT(" Group 0"),
		ECVF_Scalability
	);
	static FAutoConsoleVariableRef CVarVTPoolSizeScale0(
		TEXT("r.VT.PoolSizeScale.Group0"),
		GPoolSizeScales[0],
		TEXT("Scale factor for virtual texture physical pool size.\n")
		TEXT(" Group 0"),
		ECVF_Scalability
	);
	static FAutoConsoleVariableRef CVarVTPoolSizeScale1(
		TEXT("r.VT.PoolSizeScale.Group1"),
		GPoolSizeScales[1],
		TEXT("Scale factor for virtual texture physical pool sizes.\n")
		TEXT(" Group 1"),
		ECVF_Scalability
	);
	static FAutoConsoleVariableRef CVarVTPoolSizeScale2(
		TEXT("r.VT.PoolSizeScale.Group2"),
		GPoolSizeScales[2],
		TEXT("Scale factor for virtual texture physical pool sizes.\n")
		TEXT(" Group 2"),
		ECVF_Scalability
	);

	static float GTileCountBiases[NumScalabilityGroups] = { 0 };
	static FAutoConsoleVariableRef CVarVTTileCountBias_ForBackwardsCompat(
		TEXT("r.VT.RVT.TileCountBias"),
		GTileCountBiases[0],
		TEXT("Bias to apply to Runtime Virtual Texture size.\n")
		TEXT(" Group 0"),
		ECVF_Scalability
	);
	static FAutoConsoleVariableRef CVarVTTileCountBias0(
		TEXT("r.VT.RVT.TileCountBias.Group0"),
		GTileCountBiases[0],
		TEXT("Bias to apply to Runtime Virtual Texture size.\n")
		TEXT(" Group 0"),
		ECVF_Scalability
	);
	static FAutoConsoleVariableRef CVarVTTileCountBias1(
		TEXT("r.VT.RVT.TileCountBias.Group1"),
		GTileCountBiases[1],
		TEXT("Bias to apply to Runtime Virtual Texture size.\n")
		TEXT(" Group 1"),
		ECVF_Scalability
	);
	static FAutoConsoleVariableRef CVarVTTileCountBias2(
		TEXT("r.VT.RVT.TileCountBias.Group2"),
		GTileCountBiases[2],
		TEXT("Bias to apply to Runtime Virtual Texture size.\n")
		TEXT(" Group 2"),
		ECVF_Scalability
	);


	/** Track changes and apply to relevant systems. This allows us to dynamically change the scalability settings. */
	static void OnUpdate()
	{
		const float MaxAnisotropy = CVarVTMaxAnisotropy.GetValueOnGameThread();

		static float LastMaxAnisotropy = MaxAnisotropy;
		static float LastPoolSizeScales[3] = { GPoolSizeScales[0], GPoolSizeScales[1], GPoolSizeScales[2] };
		static float LastTileCountBiases[3] = { GTileCountBiases[0], GTileCountBiases[1], GTileCountBiases[2] };

		bool bUpdate = false;
		if (LastMaxAnisotropy != MaxAnisotropy)
		{
			LastMaxAnisotropy = MaxAnisotropy;
			bUpdate = true;
		}
		if (LastPoolSizeScales[0] != GPoolSizeScales[0] || LastPoolSizeScales[1] != GPoolSizeScales[1] || LastPoolSizeScales[2] != GPoolSizeScales[2])
		{
			LastPoolSizeScales[0] = GPoolSizeScales[0];
			LastPoolSizeScales[1] = GPoolSizeScales[1];
			LastPoolSizeScales[2] = GPoolSizeScales[2];
			bUpdate = true;
		}
		if (LastTileCountBiases[0] != GTileCountBiases[0] || LastTileCountBiases[1] != GTileCountBiases[1] || LastTileCountBiases[2] != GTileCountBiases[2])
		{
			LastTileCountBiases[0] = GTileCountBiases[0];
			LastTileCountBiases[1] = GTileCountBiases[1];
			LastTileCountBiases[2] = GTileCountBiases[2];
			bUpdate = true;
		}

		if (bUpdate)
		{
			// Temporarily release runtime virtual textures
			for (TObjectIterator<URuntimeVirtualTexture> It; It; ++It)
			{
				It->Release();
			}

			// Release streaming virtual textures
			TArray<UTexture2D*> ReleasedVirtualTextures;
			for (TObjectIterator<UTexture2D> It; It; ++It)
			{
				if (It->IsCurrentlyVirtualTextured())
				{
					ReleasedVirtualTextures.Add(*It);
					BeginReleaseResource(It->Resource);
				}
			}

			// Force garbage collect of pools
			ENQUEUE_RENDER_COMMAND(VirtualTextureScalability_Release)([](FRHICommandList& RHICmdList)
			{
				GetRendererModule().ReleaseVirtualTexturePendingResources();
			});

			// Now all pools should be flushed...
			// Reinit streaming virtual textures
			for (UTexture2D* Texture : ReleasedVirtualTextures)
			{
				BeginInitResource(Texture->Resource);
			}

			// Reinit runtime virtual textures
			for (TObjectIterator<URuntimeVirtualTextureComponent> It; It; ++It)
			{
				It->MarkRenderStateDirty();
			}
		}
	}

	FAutoConsoleVariableSink GConsoleVariableSink(FConsoleCommandDelegate::CreateStatic(&OnUpdate));


	int32 GetMaxUploadsPerFrame()
	{
#if WITH_EDITOR
		// Don't want this scalability setting to affect editor because we rely on reactive updates while editing.
		return GIsEditor ? CVarVTMaxUploadsPerFrameInEditor.GetValueOnAnyThread() : CVarVTMaxUploadsPerFrame.GetValueOnAnyThread();
#else
		return CVarVTMaxUploadsPerFrame.GetValueOnAnyThread();
#endif
	}

	int32 GetMaxContinuousUpdatesPerFrame()
	{
#if WITH_EDITOR
		// Don't want this scalability setting to affect editor because we rely on reactive updates while editing, like GPULightmass.
		return GIsEditor ? CVarVTMaxContinuousUpdatesPerFrameInEditor.GetValueOnAnyThread() : CVarVTMaxContinuousUpdatesPerFrame.GetValueOnAnyThread();
#else
		return CVarVTMaxContinuousUpdatesPerFrame.GetValueOnAnyThread();
#endif
	}

	int32 GetMaxAnisotropy()
	{
		return CVarVTMaxAnisotropy.GetValueOnAnyThread();
	}

	float GetPoolSizeScale(uint32 GroupIndex)
	{
		// This is called on render thread but uses non render thread cvar. However it should be safe enough due to the calling pattern.
		// Using ECVF_RenderThreadSafe would mean that OnUpdate() logic can fail to detect a change due to the cvar ref pointing at the render thread value.
		return GroupIndex < NumScalabilityGroups ? GPoolSizeScales[GroupIndex] : 1.f;
	}

	int32 GetRuntimeVirtualTextureSizeBias(uint32 GroupIndex)
	{
		return GroupIndex < NumScalabilityGroups ? GTileCountBiases[GroupIndex] : 0;
	}
}
