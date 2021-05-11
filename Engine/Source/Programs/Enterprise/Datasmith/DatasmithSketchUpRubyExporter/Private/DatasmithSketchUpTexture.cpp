// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpTexture.h"

#include "DatasmithSketchUpCommon.h"
#include "DatasmithSketchUpUtils.h"
#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpString.h"


#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"

#include "Misc/Paths.h"


// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"

#include "SketchUpAPI/model/texture.h"

#include "DatasmithSketchUpSDKCeases.h"

using namespace DatasmithSketchUp;

TSharedPtr<FTexture> FTextureCollection::FindOrAdd(SUTextureRef TextureRef)
{
	FTextureIDType TextureId = DatasmithSketchUpUtils::GetEntityID(SUTextureToEntity(TextureRef));

	if (TSharedPtr<FTexture>* TexturePtr = TexturesMap.Find(TextureId))
	{
		return *TexturePtr;
	}
	TSharedPtr<FTexture> Texture = MakeShared<FTexture>(TextureRef);
	TexturesMap.Add(TextureId, Texture);

	// Get the pixel scale factors of the source SketchUp texture.
	size_t TextureWidth = 0;
	size_t TextureHeight = 0;
	double TextureSScale = 1.0;
	double TextureTScale = 1.0;
	SUTextureGetDimensions(TextureRef, &TextureWidth, &TextureHeight, &TextureSScale, &TextureTScale); // we can ignore the returned SU_RESULT
	Texture->TextureScale = FVector2D(TextureSScale, TextureTScale);

	return Texture;
}

FTexture* FTextureCollection::AddTexture(SUTextureRef TextureRef, FString MaterialName)
{
	TSharedPtr<FTexture> Texture = FindOrAdd(TextureRef);
	if (!Texture->TextureImageFile.IsValid())
	{
		Texture->SourceTextureFileName = SuGetString(SUTextureGetFileName, TextureRef);
		Texture->TextureBaseName = FPaths::GetBaseFilename(Texture->SourceTextureFileName);

		// Set texture to be material-specific. SketchUp allows to have different material have different texture images under the same name
		Texture->TextureBaseName = Texture->TextureBaseName + TEXT('-') + MaterialName;
		
		AddImageFileForTexture(Texture);
	}
	return Texture.Get();
}

FTexture* FTextureCollection::AddColorizedTexture(SUTextureRef TextureRef, FString MaterialName)
{
	TSharedPtr<FTexture> Texture = FindOrAdd(TextureRef);
	if (!Texture->TextureImageFile.IsValid())
	{
		Texture->SourceTextureFileName = SuGetString(SUTextureGetFileName, TextureRef);
		Texture->TextureBaseName = FPaths::GetBaseFilename(Texture->SourceTextureFileName);

		// Set a material-specific texture file name since the saved SketchUp texture will be colorized with the material color.
		Texture->TextureBaseName = Texture->TextureBaseName + TEXT('-') + MaterialName;

		AddImageFileForTexture(Texture);
	}
	return Texture.Get();
}

void FTextureCollection::AddImageFileForTexture(TSharedPtr<FTexture> Texture)
{
	TSharedPtr<FTextureImageFile>& TextureImageFile = TextureNameToImageFile.FindOrAdd(Texture->TextureBaseName);
	if (!TextureImageFile.IsValid())
	{
		TextureImageFile = FTextureImageFile::Create(Texture);
	}
	else
	{
		TextureImageFile->Textures.Add(Texture);
	}
	Texture->TextureImageFile = TextureImageFile;
}

void FTextureCollection::Update()
{
	for (TPair<FString, TSharedPtr<FTextureImageFile>>& TextureNameAndTextureImageFile : TextureNameToImageFile)
	{
		FTextureImageFile& TextureImageFile = *TextureNameAndTextureImageFile.Value;
		TextureImageFile.Update(Context);
	}
}

bool FTexture::GetTextureUseAlphaChannel()
{
	// Get the flag indicating whether or not the SketchUp texture alpha channel is used.
	bool bUseAlphaChannel = false;
	// Make sure the flag was retrieved properly (no SU_ERROR_NO_DATA).
	return (SUTextureGetUseAlphaChannel(TextureRef, &bUseAlphaChannel) == SU_ERROR_NONE) && bUseAlphaChannel;
}

void FTexture::WriteImageFile(FExportContext& Context, const FString& TextureFilePath)
{
	// Write the SketchUp texture into a file when required.
	SUResult SResult = SResult = SUTextureWriteToFile(TextureRef, TCHAR_TO_UTF8(*TextureFilePath));
	if (SResult == SU_ERROR_SERIALIZATION)
	{
		// TODO: Append an error message to the export summary.
	}
}

const TCHAR* FTexture::GetDatasmithElementName()
{
	return TextureImageFile->TextureElement->GetName();
}

TSharedPtr<FTextureImageFile> FTextureImageFile::Create(TSharedPtr<FTexture> Texture)
{
	TSharedPtr<FTextureImageFile> TextureImageFile = MakeShared<FTextureImageFile>();
	TextureImageFile->TextureFileName = FDatasmithUtils::SanitizeFileName(Texture->TextureBaseName) + FPaths::GetExtension(Texture->SourceTextureFileName, /*bIncludeDot*/ true);
	TextureImageFile->TextureName = FDatasmithUtils::SanitizeObjectName(FPaths::GetBaseFilename(TextureImageFile->TextureFileName));
	TextureImageFile->TextureElement = FDatasmithSceneFactory::CreateTexture(*TextureImageFile->TextureName);
	TextureImageFile->TextureElement->SetSRGB(EDatasmithColorSpace::sRGB);

	TextureImageFile->Textures.Add(Texture);
	return TextureImageFile;
}

void FTextureImageFile::Update(FExportContext& Context)
{
	if (!bInvalidated)
	{
		return;
	}

	TSharedPtr<FTexture> Texture = Textures.Array().Last(); // Texture manages to write image files in SketchUp - so take any of the textures to do it
	FString TextureFilePath = FPaths::Combine(Context.GetAssetsOutputPath(), TextureFileName);
	Texture->WriteImageFile(Context, TextureFilePath);
	TextureElement->SetFile(*TextureFilePath);
	Context.DatasmithScene->AddTexture(TextureElement); // todo: make sure that texture not created/added twice

	bInvalidated = false;
}
