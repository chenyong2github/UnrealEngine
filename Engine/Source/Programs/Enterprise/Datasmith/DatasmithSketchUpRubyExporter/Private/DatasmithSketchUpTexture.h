// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace DatasmithSketchUp
{
	class FTexture;
	class FTextureImageFile;

	class FTextureImageFile
	{
	public:
		FString TextureName;
		FString TextureFileName;
		TSharedPtr<IDatasmithTextureElement> TextureElement; // Texture element is created once per texture image file

		TSet<TSharedPtr<FTexture>> Textures;  // textures, using this image
		static TSharedPtr<FTextureImageFile> Create(TSharedPtr<FTexture> Texture);


		void Update(FExportContext& Context);
	};

	// Represents texture instantiated for Datasmith
	// Each SketchUp texture can have at least two instances in Datasmith - for regular and 'colorized' materials(SketchUp applies color to texture itself)
	class FTexture
	{
	public:
		FTexture(SUTextureRef InTextureRef) : TextureRef(InTextureRef) {}

		bool GetTextureUseAlphaChannel();

		void WriteImageFile(FExportContext& Context, const FString& TextureFilePath);

		const TCHAR* GetDatasmithElementName();

		// Sketchup reference
		SUTextureRef TextureRef;

		FString SourceTextureFileName;
		FString TextureBaseName;
		TSharedPtr<FTextureImageFile> TextureImageFile;

		// Extracted from Sketchup
		FVector2D TextureScale;
	};
}



