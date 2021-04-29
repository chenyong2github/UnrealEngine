// Copyright Epic Games, Inc. All Rights Reserved.

#include "TexturesCache.h"

#include "ModelMaterial.hpp"
#include "Texture.hpp"
#include "AttributeIndex.hpp"
#include "GXImage.hpp"
#include "Graphics2D.h"
#include "Folder.hpp"

BEGIN_NAMESPACE_UE_AC

FTexturesCache::FTexturesCache()
	: bUseRelative(false)
{
	AbsolutePath = GetAddonDataDirectory() + UE_AC_DirSep + GetGSName(kName_Textures) + UE_AC_DirSep;
	RelativePath = AbsolutePath;
	bUseRelative = false;
}

const FTexturesCache::FTexturesCacheElem& FTexturesCache::GetTexture(const FSyncContext& InSyncContext,
																	 GS::Int32			 InTextureIndex)
{
	MapTextureIndex2CacheElem::iterator ExistingTexture = Textures.find(InTextureIndex);
	if (ExistingTexture != Textures.end())
	{
		return ExistingTexture->second;
	}

	UE_AC_Assert(InTextureIndex > 0 && InTextureIndex <= InSyncContext.GetModel().GetTextureCount());

	// Create an new texture element
	FTexturesCacheElem& Texture = Textures[InTextureIndex];

	ModelerAPI::Texture		   AcTexture;
	ModelerAPI::AttributeIndex IndexTextureIndex(ModelerAPI::AttributeIndex::TextureIndex, InTextureIndex);
	InSyncContext.GetModel().GetTexture(IndexTextureIndex, &AcTexture);

	if (AcTexture.GetXSize() > 0)
	{
		Texture.InvXSize = 1 / AcTexture.GetXSize();
	}
	if (AcTexture.GetYSize() > 0)
	{
		Texture.InvYSize = 1 / AcTexture.GetYSize();
	}
	Texture.bHasAlpha = AcTexture.HasAlphaChannel();
	Texture.bMirrorX = AcTexture.IsMirroredInX();
	Texture.bMirrorY = AcTexture.IsMirroredInY();
	Texture.bAlphaIsTransparence = AcTexture.IsTransparentPattern();
	Texture.bIsAvailable = AcTexture.IsAvailable();

	Texture.bUsed = false;

	if (Texture.bIsAvailable)
	{
		if (InSyncContext.bUseFingerPrint)
		{
#if 0
            // Compute pixel fingerprint to be redesing
			Texture.TextureLabel = AcTexture.GetPixelMapCheckSum();
			Texture.Fingerprint = GSGuid2APIGuid(GS::Guid(Texture.TextureLabel));
#endif
			// Compute fingerprint with content and used texture informations
			char Tmp[256];
			AcTexture.GetPixelMapCheckSum(Tmp, sizeof(Tmp));
			size_t FingerprintLen = strnlen(Tmp, sizeof(Tmp));
			UE_AC_Assert(FingerprintLen == 32);
			MD5::Generator MD5Generator;
			MD5Generator.Update(Tmp, (unsigned short)FingerprintLen);
			MD5Generator.Update(&Texture.InvXSize, sizeof(Texture.InvXSize));
			MD5Generator.Update(&Texture.InvYSize, sizeof(Texture.InvYSize));
			MD5Generator.Update(&Texture.bAlphaIsTransparence, Texture.bAlphaIsTransparence);
			MD5::FingerPrint FingerPrint;
			MD5Generator.Finish(FingerPrint);
			UE_AC_Assert(FingerPrint.GetAsString(Tmp) == NoError);
			Texture.TextureLabel = Tmp;
			Texture.Fingerprint = Fingerprint2API_Guid(FingerPrint);

			UE_AC_VerboseF("Texture name=\"%s\": TMFingerPrint=\"%s\"\n", AcTexture.GetName().ToUtf8(), Tmp);
		}
		else
		{
			// Create a unique name
			Texture.TextureLabel = AcTexture.GetName();
			unsigned int SequencialNumber = 0;
			while (TexturesNameSet.find(&Texture.TextureLabel) != TexturesNameSet.end())
			{
				Texture.TextureLabel = AcTexture.GetName() + GS::UniString::Printf(" %d", ++SequencialNumber);
			}
			TexturesNameSet.insert(&Texture.TextureLabel);

			GS::UniString fp(AcTexture.GetFingerprint());
			Texture.Fingerprint = GSGuid2APIGuid(GS::Guid(fp));
			UE_AC_VerboseF("Texture name=\"%s\": ACFingerprint=\"%s\"\n", Texture.TextureLabel.ToUtf8(), fp.ToUtf8());
		}
		Texture.TexturePath =
			(bUseRelative ? RelativePath : AbsolutePath) + Texture.TextureLabel + GetGSName(kName_TextureExtension);

		WriteTexture(AcTexture, AbsolutePath + Texture.TextureLabel + GetGSName(kName_TextureExtension),
					 InSyncContext.bUseFingerPrint);
	}
	else
	{
		GS::UniString fp(AcTexture.GetFingerprint());
		Texture.Fingerprint = GSGuid2APIGuid(GS::Guid(fp));
		UE_AC_DebugF("FTexturesCache::GetTexture - Texture name \"%s\" missing: ACFingerprint=%s\n",
					 AcTexture.GetName().ToUtf8(), fp.ToUtf8());
	}

	GS::UniString Fingerprint = APIGuidToString(Texture.Fingerprint);
	FString		  TextureId = GSStringToUE(Fingerprint);
	if (TexturesIdsSet.find(TextureId) == TexturesIdsSet.end())
	{
		TSharedRef< IDatasmithTextureElement > BaseTexture =
			FDatasmithSceneFactory::CreateTexture(GSStringToUE(Fingerprint));
		BaseTexture->SetLabel(GSStringToUE(AcTexture.GetName()));
		BaseTexture->SetFile(GSStringToUE(Texture.TexturePath));
		if (*BaseTexture->GetFile() != 0)
		{
			FMD5Hash FileHash = FMD5Hash::HashFile(BaseTexture->GetFile());
			BaseTexture->SetFileHash(FileHash);
		}
		else
		{
			BaseTexture->SetFile(TEXT("Missing_Texture_File"));
		}
		InSyncContext.GetScene().AddTexture(BaseTexture);
		TexturesIdsSet.insert(TextureId);
	}

	return Texture;
}

void FTexturesCache::WriteTexture(const ModelerAPI::Texture& InACTexture, const GS::UniString& InPath,
								  bool InIsFingerprint) const
{
	// Create the texture folder if it's not present
	IO::Location FolderLocation(AbsolutePath);

	IO::Folder TextureFolder(FolderLocation, IO::Folder::Create);
	UE_AC_TestGSError(TextureFolder.GetStatus());

	IO::Location TextureLoc(InPath);

	// If texture already exist, we do nothing
	if (InIsFingerprint)
	{
		IO::File TextureFile(TextureLoc);
		if ((TextureFile.GetStatus() == NoError) &&
			(TextureFile.IsOpen() || (TextureFile.Open(IO::File::ReadMode) == NoError)))
			return;
	}

	// Create a pixmap of the same size of the texture
	GSPixMapHandle PixMap = GXCreateGSPixMap(InACTexture.GetPixelMapXSize(), InACTexture.GetPixelMapYSize());
	UE_AC_TestPtr(PixMap);
	try
	{
		// Test the invariant
		UE_AC_Assert(InACTexture.GetPixelMapSize() * sizeof(ModelerAPI::Texture::Pixel) ==
					 GXGetGSPixMapBytesPerRow(PixMap) * InACTexture.GetPixelMapYSize());

		// Copy the pixels from the texture to the PixMap.
		GSPtr Pixels = GXGetGSPixMapBaseAddr(PixMap);
		UE_AC_TestPtr(Pixels);
		InACTexture.GetPixelMap(reinterpret_cast< ModelerAPI::Texture::Pixel* >(Pixels));

		GX::ImageSaveOptions  ImgSaveOpt(GX::PixelBits_MillionsWithAlpha);
		GX::ImageSaveOptions* PixelBitSize = &ImgSaveOpt;
		GX::Image			  Img(PixMap);
		UE_AC_TestGSError(Img.WriteToFile(
			TextureLoc, FTM::FileTypeManager::SearchForMime(GetStdName(kName_TextureMime), NULL), PixelBitSize));
	}
	catch (...)
	{
		GXDeleteGSPixMap(PixMap);
		throw;
	}

	// Delete the pixmap
	GXDeleteGSPixMap(PixMap);
}

END_NAMESPACE_UE_AC
