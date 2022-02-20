// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImportTestFunctions/TextureImportTestFunctions.h"
#include "InterchangeTestFunction.h"
#include "Engine/Texture.h"


UClass* UTextureImportTestFunctions::GetAssociatedAssetType() const
{
	return UTexture::StaticClass();
}
