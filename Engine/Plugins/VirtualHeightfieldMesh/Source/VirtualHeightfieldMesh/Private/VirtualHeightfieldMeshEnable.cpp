// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/RuntimeVirtualTextureComponent.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "VirtualHeightfieldMeshComponent.h"
#include "VT/RuntimeVirtualTextureEnum.h"

namespace VirtualHeightfieldMesh
{
	/**  */
	static TAutoConsoleVariable<int32> CVarVHMEnable(
		TEXT("r.VHM.Enable"),
		1,
		TEXT("Enable virtual heightfield mesh"),
		ECVF_RenderThreadSafe
	);

	/**  */
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
				It->SetRenderInMainPass(bEnable);

				if (It->RuntimeVirtualTexture != nullptr)
				{
					RuntimeVirtualTextures.AddUnique(It->RuntimeVirtualTexture);
				}
			}

			for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
			{
				for (URuntimeVirtualTexture* RuntimeVirtualTexture : RuntimeVirtualTextures)
				{
					if (It->GetRuntimeVirtualTextures().Contains(RuntimeVirtualTexture))
					{
						It->SetRenderInMainPass(!bEnable);
						break;
					}
				}
		}
		}
	}

	FAutoConsoleVariableSink GConsoleVariableSink(FConsoleCommandDelegate::CreateStatic(&OnUpdate));
}
