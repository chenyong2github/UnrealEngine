// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithSketchUpCommon.h"

// Datasmith SDK.
#include "Containers/UnrealString.h"

class IDatasmithTextureElement;

namespace DatasmithSketchUp
{
	class FExportContext;
	class FTexture;
	class FTextureImageFile;
	class FMaterial;

	class FTextureImageFile
	{
	public:
		FTextureImageFile() : bInvalidated(true) {}

		FString TextureName;
		FString TextureFileName;

		FTexture* Texture;

		TSharedPtr<IDatasmithTextureElement> TextureElement; // Texture element is created once per texture image file

		static TSharedPtr<FTextureImageFile> Create(TSharedPtr<FTexture> Texture);

		void Update(FExportContext& Context);

		void Invalidate()
		{
			bInvalidated = true;
		}

		uint8 bInvalidated:1;
	};

	// Represents texture instantiated for Datasmith
	// Each SketchUp texture can have at least two instances in Datasmith - for regular and 'colorized' materials(SketchUp applies color to texture itself)
	class FTexture
	{
	public:
		FTexture(SUTextureRef InTextureRef, FTextureIDType InTextureId) : TextureRef(InTextureRef), TextureId(InTextureId) {}

		bool GetTextureUseAlphaChannel();

		void WriteImageFile(FExportContext& Context, const FString& TextureFilePath);

		const TCHAR* GetDatasmithElementName();

		void Invalidate();

		// Sketchup reference
		SUTextureRef TextureRef;
		FTextureIDType TextureId;

		FString SourceTextureFileName;
		FString TextureBaseName;
		TSharedPtr<FTextureImageFile> TextureImageFile;

		// Extracted from Sketchup
		FVector2D TextureScale;

		TSet<FMaterial*> Materials; // Materials using this texture
	};
}



