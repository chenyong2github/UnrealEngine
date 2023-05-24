// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ImagePrivate.h"
#include "MuR/MutableMath.h"
#include "MuR/Platform.h"


namespace mu
{

	inline bool ImageCrop( Image* InCropped, int32 CompressionQuality, const Image* InBase, const box< vec2<int32> >& Rect )
	{
		Ptr<const Image> Base = InBase;
		Ptr<Image> Cropped = InCropped;

		EImageFormat BaseFormat = Base->GetFormat();
		EImageFormat UncompressedFormat = GetUncompressedFormat(BaseFormat);

		if (BaseFormat != UncompressedFormat)
		{
			// Compressed formats need decompression + compression after crop			
			// \TODO: This may use some additional untracked memory locally in this function.
			Base = ImagePixelFormat(CompressionQuality, Base.get(), UncompressedFormat);
			Cropped = new Image( InCropped->GetSizeX(), InCropped->GetSizeY(), InCropped->GetLODCount(), UncompressedFormat, EInitializationType::NotInitialized );
        }

		const FImageFormatData& finfo = GetImageFormatData(UncompressedFormat);

		check(Base && Cropped);
		check(Cropped->GetSizeX() == Rect.size[0]);
		check(Cropped->GetSizeY() == Rect.size[1]);

		// TODO: better error control. This happens if some layouts are corrupt.
		bool bCorrect =
				( Rect.min[0]>=0 && Rect.min[1]>=0 ) &&
				( Rect.size[0]>=0 && Rect.size[1]>=0 ) &&
				( Rect.min[0]+Rect.size[0]<=Base->GetSizeX() ) &&
				( Rect.min[1]+Rect.size[1]<=Base->GetSizeY() );
		if (!bCorrect)
		{
			return false;
		}

		// Block images are not supported for now
		check( finfo.m_pixelsPerBlockX == 1 );
		check( finfo.m_pixelsPerBlockY == 1 );

		checkf( Rect.min[0] % finfo.m_pixelsPerBlockX == 0, TEXT("Rect must snap to blocks.") );
		checkf( Rect.min[1] % finfo.m_pixelsPerBlockY == 0, TEXT("Rect must snap to blocks.") );
		checkf( Rect.size[0] % finfo.m_pixelsPerBlockX == 0, TEXT("Rect must snap to blocks.") );
		checkf( Rect.size[1] % finfo.m_pixelsPerBlockY == 0, TEXT("Rect must snap to blocks.") );

		int baseRowSize = finfo.m_bytesPerBlock * Base->GetSizeX() / finfo.m_pixelsPerBlockX;
		int cropRowSize = finfo.m_bytesPerBlock * Rect.size[0] / finfo.m_pixelsPerBlockX;

        const uint8_t* pBaseBuf = Base->GetData();
        uint8_t* pCropBuf = Cropped->GetData();

		int skipPixels = Base->GetSizeX() * Rect.min[1] + Rect.min[0];
		pBaseBuf += finfo.m_bytesPerBlock * skipPixels / finfo.m_pixelsPerBlockX;
		for ( int y=0; y<Rect.size[1]; ++y )
		{
			FMemory::Memcpy( pCropBuf, pBaseBuf, cropRowSize );
			pCropBuf += cropRowSize;
			pBaseBuf += baseRowSize;
		}

		if (BaseFormat != UncompressedFormat)
		{
			bool bSuccess = false;
			int32 DataSize = Cropped->m_data.Num();
			while (!bSuccess)
			{
				InCropped->m_data.SetNumUninitialized(DataSize);
				ImagePixelFormat(bSuccess, CompressionQuality, InCropped, Cropped.get());
				DataSize = (DataSize + 16) * 2;
			}
		}

		return true;
	}

}
