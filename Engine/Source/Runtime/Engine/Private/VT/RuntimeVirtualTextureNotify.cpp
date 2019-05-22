// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VT/RuntimeVirtualTextureNotify.h"

#include "Components/PrimitiveComponent.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "UObject/UObjectIterator.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTexturePlane.h"

namespace RuntimeVirtualTexture
{
#if WITH_EDITOR

	void NotifyComponents(URuntimeVirtualTexture const* VirtualTexture)
	{
		for (TObjectIterator<URuntimeVirtualTextureComponent> It; It; ++It)
		{
			if (It->GetVirtualTexture() == VirtualTexture)
			{
				It->MarkRenderStateDirty();
			}
		}
	}

	void NotifyPrimitives(URuntimeVirtualTexture const* VirtualTexture)
	{
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			for (URuntimeVirtualTexture* ItVirtualTexture : It->GetRuntimeVirtualTextures())
			{
				if (ItVirtualTexture == VirtualTexture)
				{
					It->MarkRenderStateDirty();
					break;
				}
			}
		}
	}

#endif

	void NotifyMaterials(URuntimeVirtualTexture const* VirtualTexture)
	{
		//todo [vt]:
		// This operation is very slow! We should only do it during edition, but currently we do it once per virtual texture at runtime load/unload.
		// Can we pre-calculate the list of materials to touch during cook? But even then touching here them will be extra work...
		// Is there a way to set up dependencies so that we load the materials _after_ the virtual texture is allocated?
		// Or maybe we should consider serializing the WorldToUVTransform in the URuntimeVirtualTexture and not depending on a URuntimeVirtualTextureComponent at runtime?

		if (VirtualTexture == nullptr)
		{
			return;
		}

		TSet<UMaterial*> BaseMaterialsThatUseThisTexture;
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			UMaterialInterface* MaterialInterface = *It;

			TArray<UObject*> Textures;
			MaterialInterface->AppendReferencedTextures(Textures);

			for (auto Texture : Textures)
			{
				if (Texture == VirtualTexture)
				{
					BaseMaterialsThatUseThisTexture.Add(MaterialInterface->GetMaterial());
					break;
				}
			}
		}

		if (BaseMaterialsThatUseThisTexture.Num() > 0)
		{
			FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
			for (TSet<UMaterial*>::TConstIterator It(BaseMaterialsThatUseThisTexture); It; ++It)
			{
				(*It)->RecacheUniformExpressions(false);
				UpdateContext.AddMaterial(*It);
			}
		}
	}
}
