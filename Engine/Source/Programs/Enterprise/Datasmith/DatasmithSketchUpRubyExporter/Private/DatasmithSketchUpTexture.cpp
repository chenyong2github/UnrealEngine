// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpTexture.h"

#include "DatasmithSketchUpCommon.h"
#include "DatasmithSketchUpUtils.h"
#include "DatasmithSketchUpExportContext.h"
#include "DatasmithSketchUpString.h"

#include "DatasmithSketchUpMaterial.h"


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
	TSharedPtr<FTexture> Texture = MakeShared<FTexture>(TextureRef, TextureId);
	TexturesMap.Add(TextureId, Texture);
	return Texture;
}

FTexture* FTextureCollection::AddTexture(SUTextureRef TextureRef, FString MaterialName)
{
	TSharedPtr<FTexture> Texture = FindOrAdd(TextureRef);
	Texture->Invalidate();
	if (!Texture->TextureImageFile.IsValid())
	{
		Texture->SourceTextureFileName = SuGetString(SUTextureGetFileName, TextureRef);
		Texture->TextureBaseName = FPaths::GetBaseFilename(Texture->SourceTextureFileName);

		// Set texture to be material-specific. SketchUp allows to have different material have different texture images under the same name
		Texture->TextureBaseName = Texture->TextureBaseName + TEXT('-') + MaterialName;
		
		AddImageFileForTexture(Texture);
	}
	else
	{
		if (Texture->TextureImageFile)
		{
			Texture->TextureImageFile->Invalidate();
		}
	}
	return Texture.Get();
}

FTexture* FTextureCollection::AddColorizedTexture(SUTextureRef TextureRef, FString MaterialName)
{
	return AddTexture(TextureRef, MaterialName);
}

void FTextureCollection::AddImageFileForTexture(TSharedPtr<FTexture> Texture)
{
	Texture->TextureImageFile = FTextureImageFile::Create(Texture);
}

void FTextureCollection::Update()
{
	for (TPair<FTextureIDType, TSharedPtr<FTexture>>& TextureNameAndTextureImageFile : TexturesMap)
	{
		TSharedPtr<FTexture> Texture = TextureNameAndTextureImageFile.Value;
		if (Texture->TextureImageFile)
		{
			Texture->TextureImageFile->Update(Context);
		}
	}
}

void FTextureCollection::RegisterMaterial(FMaterial* Material)
{
	FTexture* Texture = Material->GetTexture();
	if (!Texture)
	{
		return;
	}
	Texture->Materials.Add(Material);
}

void FTextureCollection::UnregisterMaterial(FMaterial* Material)
{
	FTexture* Texture = Material->GetTexture();
	if (!Texture)
	{
		return;
	}
	Texture->Materials.Remove(Material);
	if (!Texture->Materials.Num())
	{
		TSharedPtr<FTextureImageFile> TextureImageFile = Texture->TextureImageFile;
		if (TextureImageFile->TextureElement)
		{
			Context.DatasmithScene->RemoveTexture(TextureImageFile->TextureElement); // todo: make sure that texture not created/added twice
		}
		TexturesMap.Remove(Texture->TextureId);
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
	TextureImageFile->Texture = Texture.Get();

	TextureImageFile->TextureFileName = FDatasmithUtils::SanitizeFileName(Texture->TextureBaseName) + FPaths::GetExtension(Texture->SourceTextureFileName, /*bIncludeDot*/ true);
	TextureImageFile->TextureName = FDatasmithUtils::SanitizeObjectName(FPaths::GetBaseFilename(TextureImageFile->TextureFileName));
	TextureImageFile->TextureElement = FDatasmithSceneFactory::CreateTexture(*TextureImageFile->TextureName);
	TextureImageFile->TextureElement->SetSRGB(EDatasmithColorSpace::sRGB);

	return TextureImageFile;
}

void FTextureImageFile::Update(FExportContext& Context)
{
	if (!bInvalidated)
	{
		return;
	}

	FString TextureFilePath = FPaths::Combine(Context.GetAssetsOutputPath(), TextureFileName);
	Texture->WriteImageFile(Context, TextureFilePath);
	TextureElement->SetFile(*TextureFilePath);
	Context.DatasmithScene->AddTexture(TextureElement); // todo: make sure that texture not created/added twice

	bInvalidated = false;
}

void FTexture::Invalidate()
{
	// Get the pixel scale factors of the source SketchUp texture.
	size_t TextureWidth = 0;
	size_t TextureHeight = 0;
	double TextureSScale = 1.0;
	double TextureTScale = 1.0;
	SUTextureGetDimensions(TextureRef, &TextureWidth, &TextureHeight, &TextureSScale, &TextureTScale); // we can ignore the returned SU_RESULT
	TextureScale = FVector2D(TextureSScale, TextureTScale);
}
