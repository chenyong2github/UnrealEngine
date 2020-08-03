// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/RuntimeVirtualTextureComponent.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "VirtualHeightfieldMeshComponent.h"
#include "VT/RuntimeVirtualTextureEnum.h"

namespace VirtualHeightfieldMesh
{
	/** CVar to toggle support for virtual heightfield mesh. */
	static TAutoConsoleVariable<int32> CVarVHMEnable(
		TEXT("r.VHM.Enable"),
		0,
		TEXT("Enable virtual heightfield mesh"),
		ECVF_RenderThreadSafe
	);

	/** Sink to apply updates when virtual heightfield mesh settings change. */
	static void OnUpdate()
	{
		const bool bEnable = CVarVHMEnable.GetValueOnGameThread() != 0;

		static bool bLastEnable = !bEnable;

		if (bEnable != bLastEnable)
		{
			bLastEnable = bEnable;

			TArray<URuntimeVirtualTexture*> RuntimeVirtualTextures;

			for (TObjectIterator<UVirtualHeightfieldMeshComponent> It; It; ++It)
			{
				It->MarkRenderStateDirty();

				if (It->GetVirtualTexture() != nullptr)
				{
					RuntimeVirtualTextures.AddUnique(It->GetVirtualTexture());
				}
			}

			for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
			{
				for (URuntimeVirtualTexture* RuntimeVirtualTexture : RuntimeVirtualTextures)
				{
					if (It->GetRuntimeVirtualTextures().Contains(RuntimeVirtualTexture))
					{
						It->MarkRenderStateDirty();
						break;
					}
				}
			}
		}
	}

	FAutoConsoleVariableSink GConsoleVariableSink(FConsoleCommandDelegate::CreateStatic(&OnUpdate));
}
