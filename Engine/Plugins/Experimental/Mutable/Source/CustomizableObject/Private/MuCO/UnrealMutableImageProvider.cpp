// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealMutableImageProvider.h"

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectSystem.h"


//-------------------------------------------------------------------------------------------------
namespace
{

	mu::ImagePtr ConvertTextureUnrealToMutable(UTexture2D* Texture)
	{
		mu::ImagePtr pResult;

#if WITH_EDITOR
		int LODs = 1;
		int SizeX = Texture->Source.GetSizeX();
		int SizeY = Texture->Source.GetSizeY();
		ETextureSourceFormat Format = Texture->Source.GetFormat();
		mu::EImageFormat MutableFormat = mu::EImageFormat::IF_NONE;

		switch (Format)
		{
		case ETextureSourceFormat::TSF_BGRA8: MutableFormat = mu::EImageFormat::IF_BGRA_UBYTE; break;
		// This format is deprecated ans using the enum fails to compile in some cases.
		//case ETextureSourceFormat::TSF_RGBA8: MutableFormat = mu::EImageFormat::IF_RGBA_UBYTE; break;
		case ETextureSourceFormat::TSF_G8: MutableFormat = mu::EImageFormat::IF_L_UBYTE; break;
		default:
			break;
		}

		pResult = new mu::Image(SizeX, SizeY, LODs, MutableFormat);

		// If not locked ReadOnly the Texture Source's FGuid can change, invalidating the texture's caching/shaders
		// making shader compile and cook times increase
		const uint8* pSource = Texture->Source.LockMipReadOnly(0);
		if (pSource)
		{
			FMemory::Memcpy(pResult->GetData(), pSource, pResult->GetDataSize());
		}

		Texture->Source.UnlockMip(0);
#endif

		return pResult;
	}
}


//-------------------------------------------------------------------------------------------------

mu::ImagePtr FUnrealMutableImageProvider::GetImage(mu::EXTERNAL_IMAGE_ID id)
{
	// Thread: Mutable worker

	check(ExternalImagesForCurrentInstance.Contains(id));
	return ExternalImagesForCurrentInstance[id];
}


void FUnrealMutableImageProvider::CacheImage(mu::EXTERNAL_IMAGE_ID id)
{
	check( IsInGameThread() );

	mu::ImagePtr pResult;

	// See if any providere provides this id.
	for (int p = 0; !pResult && p<ImageProviders.Num(); ++p)
	{
		TWeakObjectPtr<UCustomizableSystemImageProvider> Provider = ImageProviders[p];

		if (Provider.IsValid())
		{
			switch (Provider->HasTextureParameterValue(id))
			{

			case UCustomizableSystemImageProvider::ValueType::Raw:
			{
				FIntVector desc = Provider->GetTextureParameterValueSize(id);
				pResult = new mu::Image(desc[0], desc[1], 1, mu::EImageFormat::IF_RGBA_UBYTE);
				Provider->GetTextureParameterValueData(id, pResult->GetData());
				break;
			}

			case UCustomizableSystemImageProvider::ValueType::Unreal:
			{
				UTexture2D* UnrealTexture = Provider->GetTextureParameterValue(id);
				pResult = ConvertTextureUnrealToMutable(UnrealTexture);
				break;
			}

			default:
				break;
			}
		}
	}

	if (!pResult)
	{
		pResult = CreateDummy();
	}

	ExternalImagesForCurrentInstance.Add(id,pResult);
}


void FUnrealMutableImageProvider::ClearCache()
{
	check(IsInGameThread());

	ExternalImagesForCurrentInstance.Empty();
}


mu::ImagePtr FUnrealMutableImageProvider::CreateDummy()
{
	// Create a dummy image
	const int size = 32;
	const int checkerSize = 4;
	constexpr int checkerTileCount = 2;
	uint8_t colours[checkerTileCount][4] = { { 255,255,0,255 },{ 0,0,255,255 } };

	mu::ImagePtr pResult = new mu::Image(size, size, 1, mu::EImageFormat::IF_RGBA_UBYTE);

	uint8_t* pData = pResult->GetData();
	for (int x = 0; x < size; ++x)
	{
		for (int y = 0; y < size; ++y)
		{
			int checkerIndex = ((x / checkerSize) + (y / checkerSize)) % checkerTileCount;
			pData[0] = colours[checkerIndex][0];
			pData[1] = colours[checkerIndex][1];
			pData[2] = colours[checkerIndex][2];
			pData[3] = colours[checkerIndex][3];
			pData += 4;
		}
	}

	return pResult;
}


