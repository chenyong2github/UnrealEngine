// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/OpImageResize.h"

namespace mu
{

	Ptr<Image> ImageResizeLinear(int32 imageCompressionQuality, const Image* pBasePtr, FImageSize destSize)
	{
		MUTABLE_CPUPROFILER_SCOPE(ImageResizeLinear);

		if (pBasePtr->GetSize() == destSize)
		{
			return pBasePtr->Clone();
		}

		check(!(pBasePtr->m_flags & Image::IF_CANNOT_BE_SCALED));

		ImagePtrConst pBase = pBasePtr;

		// Shouldn't happen! But if it does...
		EImageFormat sourceFormat = pBase->GetFormat();
		EImageFormat uncompressedFormat = GetUncompressedFormat(sourceFormat);
		if (sourceFormat != uncompressedFormat)
		{
			pBase = ImagePixelFormat(imageCompressionQuality, pBasePtr, uncompressedFormat);
		}

		FImageSize baseSize = FImageSize(pBase->GetSizeX(), pBase->GetSizeY());

		ImagePtr pDest = new Image(destSize[0], destSize[1], 1, pBase->GetFormat());
		if (!destSize[0] || !destSize[1] || !baseSize[0] || !baseSize[1])
		{
			return pDest;
		}

		// First resize X
		ImagePtr pTemp;
		if (destSize[0] > baseSize[0])
		{
			pTemp = new Image(destSize[0], baseSize[1], 1, pBase->GetFormat());
			ImageMagnifyX(pTemp.get(), pBase.get());
		}
		else if (destSize[0] < baseSize[0])
		{
			pTemp = new Image(destSize[0], baseSize[1], 1, pBase->GetFormat());
			ImageMinifyX(pTemp.get(), pBase.get());
		}
		else
		{
			pTemp = pBase->Clone();
		}

		// Now resize Y
		if (destSize[1] > baseSize[1])
		{
			ImageMagnifyY(pDest.get(), pTemp.get());
		}
		else if (destSize[1] < baseSize[1])
		{
			ImageMinifyY(pDest.get(), pTemp.get());
		}
		else
		{
			pDest = pTemp;
		}


		// Reset format if it was changed to scale
		if (sourceFormat != uncompressedFormat)
		{
			pDest = ImagePixelFormat(imageCompressionQuality, pDest.get(), sourceFormat);
		}

		// Update the relevancy data of the image.
		if (pBase->m_flags & Image::EImageFlags::IF_HAS_RELEVANCY_MAP)
		{
			pDest->m_flags |= Image::EImageFlags::IF_HAS_RELEVANCY_MAP;

			float FactorY = float(destSize[1]) / float(baseSize[1]);

			pDest->RelevancyMinY = uint16(FMath::FloorToFloat(pBase->RelevancyMinY * FactorY));
			pDest->RelevancyMaxY = uint16(FMath::Min((int32)FMath::CeilToFloat(pBase->RelevancyMinY * FactorY), pDest->GetSizeY() - 1));
		}

		return pDest;
	}


	void ImageResizeLinear(Image* pDest, int32 imageCompressionQuality, const Image* pBasePtr)
	{
		MUTABLE_CPUPROFILER_SCOPE(ImageResizeLinear);

		check(!(pBasePtr->m_flags & Image::IF_CANNOT_BE_SCALED));

		ImagePtrConst pBase = pBasePtr;

		// Shouldn't happen! But if it does...
		EImageFormat sourceFormat = pBase->GetFormat();
		EImageFormat uncompressedFormat = GetUncompressedFormat(sourceFormat);
		if (sourceFormat != uncompressedFormat)
		{
			pBase = ImagePixelFormat(imageCompressionQuality, pBasePtr, uncompressedFormat);
		}

		FImageSize baseSize = FImageSize(pBase->GetSizeX(), pBase->GetSizeY());
		FImageSize destSize = FImageSize(pDest->GetSizeX(), pDest->GetSizeY());
		if (!destSize[0] || !destSize[1] || !baseSize[0] || !baseSize[1])
		{
			return;
		}

		// First resize X
		ImagePtr pTemp;
		if (destSize[0] > baseSize[0])
		{
			pTemp = new Image(destSize[0], baseSize[1], 1, pBase->GetFormat());
			ImageMagnifyX(pTemp.get(), pBase.get());
		}
		else if (destSize[0] < baseSize[0])
		{
			pTemp = new Image(destSize[0], baseSize[1], 1, pBase->GetFormat());
			ImageMinifyX(pTemp.get(), pBase.get());
		}
		else
		{
			pTemp = pBase->Clone();
		}

		// Now resize Y
		ImagePtr pTemp2;
		if (destSize[1] > baseSize[1])
		{
			pTemp2 = new Image(destSize[0], destSize[1], 1, pBase->GetFormat());
			ImageMagnifyY(pTemp2.get(), pTemp.get());
		}
		else if (destSize[1] < baseSize[1])
		{
			pTemp2 = new Image(destSize[0], destSize[1], 1, pBase->GetFormat());
			ImageMinifyY(pTemp2.get(), pTemp.get());
		}
		else
		{
			pTemp2 = pTemp;
		}
		pTemp = nullptr;


		// Reset format if it was changed to scale
		if (sourceFormat != uncompressedFormat)
		{
			pTemp2 = ImagePixelFormat(imageCompressionQuality, pTemp2.get(), sourceFormat);
		}

		pDest->CopyMove(pTemp2.get());
		pTemp2 = nullptr;

		// Update the relevancy data of the image.
		if (pBase->m_flags & Image::EImageFlags::IF_HAS_RELEVANCY_MAP)
		{
			pDest->m_flags |= Image::EImageFlags::IF_HAS_RELEVANCY_MAP;

			float FactorY = float(destSize[1]) / float(baseSize[1]);

			pDest->RelevancyMinY = uint16(FMath::FloorToFloat(pBase->RelevancyMinY * FactorY));
			pDest->RelevancyMaxY = uint16(FMath::Min((int32)FMath::CeilToFloat(pBase->RelevancyMinY * FactorY), pDest->GetSizeY() - 1));
		}
	}

}