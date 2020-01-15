// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTextureScalability.h"

#include "Components/RuntimeVirtualTextureComponent.h"
#include "CoreGlobals.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
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

	static TAutoConsoleVariable<float> CVarVTPoolSizeScale(
		TEXT("r.VT.PoolSizeScale"),
		1.f,
		TEXT("Scale factor for virtual texture physical pool sizes."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

	static TAutoConsoleVariable<int32> CVarVTTileCountBias(
		TEXT("r.VT.RVT.TileCountBias"),
		0,
		TEXT("Bias to apply to Runtime Virtual Texture size."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

	static TAutoConsoleVariable<int32> CVarVTMaxAnisotropy(
		TEXT("r.VT.MaxAnisotropy"),
		8,
		TEXT("MaxAnisotropy setting for Virtual Texture sampling."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);
	

	/** Track changes and apply to relevant systems. This allows us to dynamically change the scalability settings. */
	static void OnUpdate()
	{
		const float PoolSizeScale = CVarVTPoolSizeScale.GetValueOnGameThread();
		const float TileCountBias = CVarVTTileCountBias.GetValueOnGameThread();
		const float MaxAnisotropy = CVarVTMaxAnisotropy.GetValueOnGameThread();

		static float LastPoolSizeScale = PoolSizeScale;
		static float LastTileCountBias = TileCountBias;
		static float LastMaxAnisotropy = MaxAnisotropy;

		bool bUpdate = false;
		if (LastPoolSizeScale != PoolSizeScale)
		{
			LastPoolSizeScale = PoolSizeScale;
			bUpdate = true;
		}
		if (LastTileCountBias != TileCountBias)
		{
			LastTileCountBias = TileCountBias;
			bUpdate = true;
		}
		if (LastMaxAnisotropy != MaxAnisotropy)
		{
			LastMaxAnisotropy = MaxAnisotropy;
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

			// Now all pools should be flushed...
			// Reinit streaming virtual textures
			for (UTexture2D* Texture : ReleasedVirtualTextures)
			{
				BeginReleaseResource(Texture->Resource);
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

	float GetPoolSizeScale()
	{
		return CVarVTPoolSizeScale.GetValueOnAnyThread();
	}

	int32 GetRuntimeVirtualTextureSizeBias()
	{
		return CVarVTTileCountBias.GetValueOnAnyThread();
	}

	int32 GetMaxAnisotropy()
	{
		return CVarVTMaxAnisotropy.GetValueOnAnyThread();
	}
}
