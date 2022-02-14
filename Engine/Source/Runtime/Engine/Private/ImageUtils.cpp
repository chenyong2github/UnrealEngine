// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
ImageUtils.cpp: Image utility functions.
=============================================================================*/

#include "ImageUtils.h"

#include "CubemapUnwrapUtils.h"
#include "DDSLoader.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Logging/MessageLog.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogImageUtils, Log, All);

#define LOCTEXT_NAMESPACE "ImageUtils"

/**
 * Returns data containing the pixmap of the passed in rendertarget.
 * @param TexRT - The 2D rendertarget from which to read pixmap data.
 * @param RawData - an array to be filled with pixel data.
 * @return true if RawData has been successfully filled.
 */
bool FImageUtils::GetRawData(UTextureRenderTarget2D* TexRT, TArray64<uint8>& RawData)
{
	FRenderTarget* RenderTarget = TexRT->GameThread_GetRenderTargetResource();
	EPixelFormat Format = TexRT->GetFormat();

	int32 ImageBytes = CalculateImageBytes(TexRT->SizeX, TexRT->SizeY, 0, Format);
	RawData.AddUninitialized(ImageBytes);
	bool bReadSuccess = false;
	switch (Format)
	{
	case PF_FloatRGBA:
	{
		TArray<FFloat16Color> FloatColors;
		bReadSuccess = RenderTarget->ReadFloat16Pixels(FloatColors);
		FMemory::Memcpy(RawData.GetData(), FloatColors.GetData(), ImageBytes);
	}
	break;
	case PF_B8G8R8A8:
		bReadSuccess = RenderTarget->ReadPixelsPtr((FColor*)RawData.GetData());
		break;
	}
	if (bReadSuccess == false)
	{
		RawData.Empty();
	}
	return bReadSuccess;
}

/**
 * Resizes the given image using a simple average filter and stores it in the destination array.
 *
 * @param SrcWidth		Source image width.
 * @param SrcHeight		Source image height.
 * @param SrcData		Source image data.
 * @param DstWidth		Destination image width.
 * @param DstHeight		Destination image height.
 * @param DstData		Destination image data.
 * @param bLinearSpace	If true, convert colors into linear space before interpolating (slower but more accurate)
 */
void FImageUtils::ImageResize(int32 SrcWidth, int32 SrcHeight, const TArray<FColor> &SrcData, int32 DstWidth, int32 DstHeight, TArray<FColor> &DstData, bool bLinearSpace, bool bForceOpaqueOutput)
{
	DstData.Empty(DstWidth*DstHeight);
	DstData.AddZeroed(DstWidth*DstHeight);

	ImageResize(SrcWidth, SrcHeight, TArrayView<const FColor>(SrcData), DstWidth, DstHeight, TArrayView<FColor>(DstData), bLinearSpace, bForceOpaqueOutput);
}

/**
 * Resizes the given image using a simple average filter and stores it in the destination array.  This version constrains aspect ratio.
 * Accepts TArrayViews but requires that DstData be pre-sized appropriately
 *
 * @param SrcWidth	Source image width.
 * @param SrcHeight	Source image height.
 * @param SrcData	Source image data.
 * @param DstWidth	Destination image width.
 * @param DstHeight Destination image height.
 * @param DstData	Destination image data. (must already be sized to DstWidth*DstHeight)
 */
void FImageUtils::ImageResize(int32 SrcWidth, int32 SrcHeight, const TArrayView<const FColor> &SrcData, int32 DstWidth, int32 DstHeight, const TArrayView<FColor> &DstData, bool bLinearSpace, bool bForceOpaqueOutput)
{
	check(SrcData.Num() >= SrcWidth * SrcHeight);
	check(DstData.Num() >= DstWidth * DstHeight);

	float SrcX = 0;
	float SrcY = 0;

	const float StepSizeX = SrcWidth / (float)DstWidth;
	const float StepSizeY = SrcHeight / (float)DstHeight;

	for(int32 Y=0; Y<DstHeight;Y++)
	{
		int32 PixelPos = Y * DstWidth;
		SrcX = 0.0f;	
	
		for(int32 X=0; X<DstWidth; X++)
		{
			int32 PixelCount = 0;
			float EndX = SrcX + StepSizeX;
			float EndY = SrcY + StepSizeY;
			
			// Generate a rectangular region of pixels and then find the average color of the region.
			int32 PosY = FMath::TruncToInt(SrcY+0.5f);
			PosY = FMath::Clamp<int32>(PosY, 0, (SrcHeight - 1));

			int32 PosX = FMath::TruncToInt(SrcX+0.5f);
			PosX = FMath::Clamp<int32>(PosX, 0, (SrcWidth - 1));

			int32 EndPosY = FMath::TruncToInt(EndY+0.5f);
			EndPosY = FMath::Clamp<int32>(EndPosY, 0, (SrcHeight - 1));

			int32 EndPosX = FMath::TruncToInt(EndX+0.5f);
			EndPosX = FMath::Clamp<int32>(EndPosX, 0, (SrcWidth - 1));

			FColor FinalColor;
			if(bLinearSpace)
			{
				FLinearColor LinearStepColor(0.0f,0.0f,0.0f,0.0f);
				for(int32 PixelX = PosX; PixelX <= EndPosX; PixelX++)
				{
					for(int32 PixelY = PosY; PixelY <= EndPosY; PixelY++)
					{
						int32 StartPixel =  PixelX + PixelY * SrcWidth;

						// Convert from gamma space to linear space before the addition.
						LinearStepColor += SrcData[StartPixel];
						PixelCount++;
					}
				}
				LinearStepColor /= (float)PixelCount;

				// Convert back from linear space to gamma space.
				FinalColor = LinearStepColor.ToFColor(true);
			}
			else
			{
				FVector4 StepColor(0,0,0,0);
				for(int32 PixelX = PosX; PixelX <= EndPosX; PixelX++)
				{
					for(int32 PixelY = PosY; PixelY <= EndPosY; PixelY++)
					{
						int32 StartPixel =  PixelX + PixelY * SrcWidth;
						StepColor.X += (float)SrcData[StartPixel].R;
						StepColor.Y += (float)SrcData[StartPixel].G;
						StepColor.Z += (float)SrcData[StartPixel].B;
						StepColor.W += (float)SrcData[StartPixel].A;
						PixelCount++;
					}
				}
				uint8 FinalR = FMath::Clamp(FMath::TruncToInt(StepColor.X / (float)PixelCount), 0, 255);
				uint8 FinalG = FMath::Clamp(FMath::TruncToInt(StepColor.Y / (float)PixelCount), 0, 255);
				uint8 FinalB = FMath::Clamp(FMath::TruncToInt(StepColor.Z / (float)PixelCount), 0, 255);
				uint8 FinalA = FMath::Clamp(FMath::TruncToInt(StepColor.W / (float)PixelCount), 0, 255);
				FinalColor = FColor(FinalR, FinalG, FinalB, FinalA);
			}

			if ( bForceOpaqueOutput )
			{
				FinalColor.A = 255;
			}

			// Store the final averaged pixel color value.
			DstData[PixelPos] = FinalColor;

			SrcX = EndX;	
			PixelPos++;
		}

		SrcY += StepSizeY;
	}
}

/**
 * Resizes the given image using a simple average filter and stores it in the destination array.
 *
 * @param SrcWidth		Source image width.
 * @param SrcHeight		Source image height.
 * @param SrcData		Source image data.
 * @param DstWidth		Destination image width.
 * @param DstHeight		Destination image height.
 * @param DstData		Destination image data.
 */
void FImageUtils::ImageResize(int32 SrcWidth, int32 SrcHeight, const TArray64<FLinearColor>& SrcData, int32 DstWidth, int32 DstHeight, TArray64<FLinearColor>& DstData)
{
	DstData.Empty(DstWidth * DstHeight);
	DstData.AddZeroed(DstWidth * DstHeight);

	ImageResize(SrcWidth, SrcHeight, TArrayView64<const FLinearColor>(SrcData), DstWidth, DstHeight, TArrayView64<FLinearColor>(DstData));
}

/**
 * Resizes the given image using a simple average filter and stores it in the destination array.  This version constrains aspect ratio.
 * Accepts TArrayViews but requires that DstData be pre-sized appropriately
 *
 * @param SrcWidth	Source image width.
 * @param SrcHeight	Source image height.
 * @param SrcData	Source image data.
 * @param DstWidth	Destination image width.
 * @param DstHeight Destination image height.
 * @param DstData	Destination image data. (must already be sized to DstWidth*DstHeight)
 */
void FImageUtils::ImageResize(int32 SrcWidth, int32 SrcHeight, const TArrayView64<const FLinearColor>& SrcData, int32 DstWidth, int32 DstHeight, const TArrayView64<FLinearColor>& DstData)
{
	check(SrcData.Num() >= SrcWidth * SrcHeight);
	check(DstData.Num() >= DstWidth * DstHeight);

	float SrcX = 0;
	float SrcY = 0;
	const float StepSizeX = SrcWidth / (float)DstWidth;
	const float StepSizeY = SrcHeight / (float)DstHeight;

	for (int32 Y = 0; Y < DstHeight; Y++)
	{
		int32 PixelPos = Y * DstWidth;
		SrcX = 0.0f;

		for (int32 X = 0; X < DstWidth; X++)
		{
			int32 PixelCount = 0;
			float EndX = SrcX + StepSizeX;
			float EndY = SrcY + StepSizeY;

			// Generate a rectangular region of pixels and then find the average color of the region.
			int32 PosY = FMath::TruncToInt(SrcY + 0.5f);
			PosY = FMath::Clamp<int32>(PosY, 0, (SrcHeight - 1));

			int32 PosX = FMath::TruncToInt(SrcX + 0.5f);
			PosX = FMath::Clamp<int32>(PosX, 0, (SrcWidth - 1));

			int32 EndPosY = FMath::TruncToInt(EndY + 0.5f);
			EndPosY = FMath::Clamp<int32>(EndPosY, 0, (SrcHeight - 1));

			int32 EndPosX = FMath::TruncToInt(EndX + 0.5f);
			EndPosX = FMath::Clamp<int32>(EndPosX, 0, (SrcWidth - 1));

			FLinearColor FinalColor(0.0f, 0.0f, 0.0f, 0.0f);
			for (int32 PixelX = PosX; PixelX <= EndPosX; PixelX++)
			{
				for (int32 PixelY = PosY; PixelY <= EndPosY; PixelY++)
				{
					int32 StartPixel = PixelX + PixelY * SrcWidth;
					FinalColor += SrcData[StartPixel];
					PixelCount++;
				}
			}
			FinalColor /= (float)PixelCount;

			// Store the final averaged pixel color value.
			DstData[PixelPos] = FinalColor;
			SrcX = EndX;
			PixelPos++;
		}
		SrcY += StepSizeY;
	}
}

/**
 * Creates a 2D texture from a array of raw color data.
 *
 * @param SrcWidth		Source image width.
 * @param SrcHeight		Source image height.
 * @param SrcData		Source image data.
 * @param Outer			Outer for the texture object.
 * @param Name			Name for the texture object.
 * @param Flags			Object flags for the texture object.
 * @param InParams		Params about how to set up the texture.
 * @return				Returns a pointer to the constructed 2D texture object.
 *
 */
UTexture2D* FImageUtils::CreateTexture2D(int32 SrcWidth, int32 SrcHeight, const TArray<FColor> &SrcData, UObject* Outer, const FString& Name, const EObjectFlags &Flags, const FCreateTexture2DParameters& InParams)
{
#if WITH_EDITOR
	UTexture2D* Tex2D;

	Tex2D = NewObject<UTexture2D>(Outer, FName(*Name), Flags);
	Tex2D->Source.Init(SrcWidth, SrcHeight, /*NumSlices=*/ 1, /*NumMips=*/ 1, TSF_BGRA8);
	
	// Create base mip for the texture we created.
	uint8* MipData = Tex2D->Source.LockMip(0);
	for( int32 y=0; y<SrcHeight; y++ )
	{
		uint8* DestPtr = &MipData[(SrcHeight - 1 - y) * SrcWidth * sizeof(FColor)];
		FColor* SrcPtr = const_cast<FColor*>(&SrcData[(SrcHeight - 1 - y) * SrcWidth]);
		for( int32 x=0; x<SrcWidth; x++ )
		{
			*DestPtr++ = SrcPtr->B;
			*DestPtr++ = SrcPtr->G;
			*DestPtr++ = SrcPtr->R;
			if( InParams.bUseAlpha )
			{
				*DestPtr++ = SrcPtr->A;
			}
			else
			{
				*DestPtr++ = 0xFF;
			}
			SrcPtr++;
		}
	}
	Tex2D->Source.UnlockMip(0);

	// Set the Source Guid/Hash if specified
	if (InParams.SourceGuidHash.IsValid())
	{
		Tex2D->Source.SetId(InParams.SourceGuidHash, true);
	}

	// Set compression options.
	Tex2D->SRGB = InParams.bSRGB;
	Tex2D->CompressionSettings = InParams.CompressionSettings;
	Tex2D->MipGenSettings = InParams.MipGenSettings;
	if( !InParams.bUseAlpha )
	{
		Tex2D->CompressionNoAlpha = true;
	}
	Tex2D->DeferCompression	= InParams.bDeferCompression;
	if (InParams.TextureGroup != TEXTUREGROUP_MAX)
	{
		Tex2D->LODGroup = InParams.TextureGroup;
	}

	Tex2D->VirtualTextureStreaming = InParams.bVirtualTexture;

	Tex2D->PostEditChange();
	return Tex2D;
#else
	UE_LOG(LogImageUtils, Fatal,TEXT("ConstructTexture2D not supported on console."));
	return NULL;
#endif
}

void FImageUtils::CropAndScaleImage( int32 SrcWidth, int32 SrcHeight, int32 DesiredWidth, int32 DesiredHeight, const TArray<FColor> &SrcData, TArray<FColor> &DstData  )
{
	// Get the aspect ratio, and calculate the dimension of the image to crop
	float DesiredAspectRatio = (float)DesiredWidth/(float)DesiredHeight;

	float MaxHeight = (float)SrcWidth / DesiredAspectRatio;
	float MaxWidth = (float)SrcWidth;

	if ( MaxHeight > (float)SrcHeight)
	{
		MaxHeight = (float)SrcHeight;
		MaxWidth = MaxHeight * DesiredAspectRatio;
	}

	// Store crop width and height as ints for convenience
	int32 CropWidth = FMath::FloorToInt(MaxWidth);
	int32 CropHeight = FMath::FloorToInt(MaxHeight);

	// Array holding the cropped image
	TArray<FColor> CroppedData;
	CroppedData.AddUninitialized(CropWidth*CropHeight);

	int32 CroppedSrcTop  = 0;
	int32 CroppedSrcLeft = 0;

	// Set top pixel if we are cropping height
	if ( CropHeight < SrcHeight )
	{
		CroppedSrcTop = ( SrcHeight - CropHeight ) / 2;
	}

	// Set width pixel if cropping width
	if ( CropWidth < SrcWidth )
	{
		CroppedSrcLeft = ( SrcWidth - CropWidth ) / 2;
	}

	const int32 DataSize = sizeof(FColor);

	//Crop the image
	for (int32 Row = 0; Row < CropHeight; Row++)
	{
	 	//Row*Side of a row*byte per color
	 	int32 SrcPixelIndex = (CroppedSrcTop+Row)*SrcWidth + CroppedSrcLeft;
	 	const void* SrcPtr = &(SrcData[SrcPixelIndex]);
	 	void* DstPtr = &(CroppedData[Row*CropWidth]);
	 	FMemory::Memcpy(DstPtr, SrcPtr, CropWidth*DataSize);
	}

	// Scale the image
	DstData.AddUninitialized(DesiredWidth*DesiredHeight);

	// Resize the image
	FImageUtils::ImageResize( MaxWidth, MaxHeight, CroppedData, DesiredWidth, DesiredHeight, DstData, true );
}

void FImageUtils::CompressImageArray( int32 ImageWidth, int32 ImageHeight, const TArray<FColor> &SrcData, TArray<uint8> &DstData )
{
	TArray<FColor> MutableSrcData = SrcData;

	// Thumbnails are saved as RGBA but FColors are stored as BGRA. An option to swap the order upon compression may be added at 
	// some point. At the moment, manually swapping Red and Blue 
	for ( int32 Index = 0; Index < ImageWidth*ImageHeight; Index++ )
	{
		uint8 TempRed = MutableSrcData[Index].R;
		MutableSrcData[Index].R = MutableSrcData[Index].B;
		MutableSrcData[Index].B = TempRed;
	}

	FObjectThumbnail TempThumbnail;
	TempThumbnail.SetImageSize( ImageWidth, ImageHeight );
	TArray<uint8>& ThumbnailByteArray = TempThumbnail.AccessImageData();

	// Copy scaled image into destination thumb
	int32 MemorySize = ImageWidth*ImageHeight*sizeof(FColor);
	ThumbnailByteArray.AddUninitialized(MemorySize);
	FMemory::Memcpy(ThumbnailByteArray.GetData(), MutableSrcData.GetData(), MemorySize);

	// Compress data - convert into thumbnail current format
	TempThumbnail.CompressImageData();
	DstData = TempThumbnail.AccessCompressedImageData();
}

void FImageUtils::PNGCompressImageArray(int32 ImageWidth, int32 ImageHeight, const TArrayView64<const FColor>& SrcData, TArray64<uint8>& DstData)
{
	const int64 PixelsNum = ImageWidth * ImageHeight;
	check(SrcData.Num() == PixelsNum);

	const uint8* SrcFirstByte = static_cast<const uint8*>(static_cast<const void*>(SrcData.GetData()));
	const int64 MemorySize = PixelsNum * sizeof(FColor);

	DstData.Reset();

	if (SrcData.Num() > 0 && ImageWidth > 0 && ImageHeight > 0)
	{
		if (DstData.Num() == 0)
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(SrcFirstByte, MemorySize, ImageWidth, ImageHeight, ERGBFormat::BGRA, 8))
			{
				DstData = ImageWrapper->GetCompressed();
			}
		}
	}
}

UTexture2D* FImageUtils::CreateCheckerboardTexture(FColor ColorOne, FColor ColorTwo, int32 CheckerSize)
{
	CheckerSize = FMath::Min<uint32>( FMath::RoundUpToPowerOfTwo(CheckerSize), 4096 );
	const int32 HalfPixelNum = CheckerSize >> 1;

	// Create the texture
	UTexture2D* CheckerboardTexture = UTexture2D::CreateTransient(CheckerSize, CheckerSize, PF_B8G8R8A8);

	// Lock the checkerboard texture so it can be modified
	FColor* MipData = reinterpret_cast<FColor*>( CheckerboardTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE) );

	// Fill in the colors in a checkerboard pattern
	for ( int32 RowNum = 0; RowNum < CheckerSize; ++RowNum )
	{
		for ( int32 ColNum = 0; ColNum < CheckerSize; ++ColNum )
		{
			FColor& CurColor = MipData[( ColNum + ( RowNum * CheckerSize ) )];

			if ( ColNum < HalfPixelNum )
			{
				CurColor = ( RowNum < HalfPixelNum ) ? ColorOne : ColorTwo;
			}
			else
			{
				CurColor = ( RowNum < HalfPixelNum ) ? ColorTwo : ColorOne;
			}
		}
	}

	// Unlock the texture
	CheckerboardTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	CheckerboardTexture->UpdateResource();

	return CheckerboardTexture;
}

UTextureCube* FImageUtils::CreateCheckerboardCubeTexture(FColor ColorOne, FColor ColorTwo, int32 CheckerSize)
{
	CheckerSize = FMath::Min<uint32>(FMath::RoundUpToPowerOfTwo(CheckerSize), 4096);
	const int32 HalfPixelNum = CheckerSize >> 1;

	// Create the texture
	UTextureCube* CheckerboardTexture = UTextureCube::CreateTransient(CheckerSize, CheckerSize, PF_B8G8R8A8);

	// Lock the checkerboard texture so it can be modified
	FColor* MipData = reinterpret_cast<FColor*>(CheckerboardTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));

	// Fill in the colors in a checkerboard pattern
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 RowNum = 0; RowNum < CheckerSize; ++RowNum)
		{
			for (int32 ColNum = 0; ColNum < CheckerSize; ++ColNum)
			{
				FColor& CurColor = MipData[(ColNum + (RowNum * CheckerSize))];

				if (ColNum < HalfPixelNum)
				{
					CurColor = (RowNum < HalfPixelNum) ? ColorOne : ColorTwo;
				}
				else
				{
					CurColor = (RowNum < HalfPixelNum) ? ColorTwo : ColorOne;
				}
			}
		}
		MipData += CheckerSize * CheckerSize;
	}

	// Unlock the texture
	CheckerboardTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	CheckerboardTexture->UpdateResource();

	return CheckerboardTexture;
}

/*------------------------------------------------------------------------------
HDR file format helper.
------------------------------------------------------------------------------*/
class FHDRExportHelper
{
public:
	/**
	* Writes HDR format image to an FArchive
	* @param TexRT - A 2D source render target to read from.
	* @param Ar - Archive object to write HDR data to.
	* @return true on successful export.
	*/
	bool ExportHDR(UTextureRenderTarget2D* TexRT, FArchive& Ar)
	{
		check(TexRT != nullptr);
		FRenderTarget* RenderTarget = TexRT->GameThread_GetRenderTargetResource();
		Size = RenderTarget->GetSizeXY();
		Format = TexRT->GetFormat();

		TArray64<uint8> RawData;
		bool bReadSuccess = FImageUtils::GetRawData(TexRT, RawData);
		if (bReadSuccess)
		{
			WriteHDRImage(RawData, Ar);
			return true;
		}
		return false;
	}

	/**
	* Writes HDR format image to an FArchive
	* @param TexRT - A 2D source render target to read from.
	* @param Ar - Archive object to write HDR data to.
	* @return true on successful export.
	*/
	bool ExportHDR(UTexture2D* Texture, FArchive& Ar)
	{
		check(Texture != nullptr);
		bool bReadSuccess = true;
		TArray64<uint8> RawData;

#if WITH_EDITORONLY_DATA
		Size = FIntPoint(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
		bReadSuccess = Texture->Source.GetMipData(RawData, 0);
		const ETextureSourceFormat NewFormat = Texture->Source.GetFormat();

		if (NewFormat == TSF_BGRA8)
		{
			Format = PF_B8G8R8A8;
		}
		else if (NewFormat == TSF_RGBA16F)
		{
			Format = PF_FloatRGBA;
		}
		else
		{
			bReadSuccess = false;			
			FMessageLog("ImageUtils").Warning(LOCTEXT("ExportHDRUnsupportedSourceTextureFormat", "Unsupported source texture format provided."));
		}
#else
		TArray<uint8*> RawData2;
		Size = Texture->GetImportedSize();
		RawData2.AddZeroed(Texture->GetNumMips());
		Texture->GetMipData(0, (void**)RawData2.GetData());
		const EPixelFormat NewFormat = Texture->GetPixelFormat();

		if (Texture->GetPlatformData()->Mips.Num() == 0)
		{
			bReadSuccess = false;
			FMessageLog("ImageUtils").Warning(FText::Format(LOCTEXT("ExportHDRFailedToReadMipData", "Failed to read Mip Data in: '{0}'"), FText::FromString(Texture->GetName())));
		}

		if (NewFormat == PF_B8G8R8A8)
		{
			Format = PF_B8G8R8A8;
		}
		else if (NewFormat == PF_FloatRGBA)
		{
			Format = PF_FloatRGBA;
		}
		else
		{
			bReadSuccess = false;
			FMessageLog("ImageUtils").Warning(LOCTEXT("ExportHDRUnsupportedTextureFormat", "Unsupported texture format provided."));
		}

		//Put first mip data into usable array
		if (bReadSuccess)
		{
			const uint32 TotalSize = Texture->GetPlatformData()->Mips[0].BulkData.GetBulkDataSize();
			RawData.AddZeroed(TotalSize);
			FMemory::Memcpy(RawData.GetData(), RawData2[0], TotalSize);
		}

		//Deallocate the mip data
		for (auto MipData : RawData2)
		{
			FMemory::Free(MipData);
		}

#endif // WITH_EDITORONLY_DATA

		if (bReadSuccess)
		{
			WriteHDRImage(RawData, Ar);
			return true;
		}

		return false;
	}

	/**
	* Writes HDR format image to an FArchive
	* This function unwraps the cube image on to a 2D surface.
	* @param TexCube - A cube source cube texture to read from.
	* @param Ar - Archive object to write HDR data to.
	* @return true on successful export.
	*/
	bool ExportHDR(UTextureCube* TexCube, FArchive& Ar)
	{
		check(TexCube != nullptr);

		// Generate 2D image.
		TArray64<uint8> RawData;
		bool bUnwrapSuccess = CubemapHelpers::GenerateLongLatUnwrap(TexCube, RawData, Size, Format);
		bool bAcceptableFormat = (Format == PF_B8G8R8A8 || Format == PF_FloatRGBA);
		if (bUnwrapSuccess == false || bAcceptableFormat == false)
		{
			return false;
		}

		WriteHDRImage(RawData, Ar);

		return true;
	}

	/**
	* Writes HDR format image to an FArchive
	* This function unwraps the cube image on to a 2D surface.
	* @param TexCube - A cube source render target cube texture to read from.
	* @param Ar - Archive object to write HDR data to.
	* @return true on successful export.
	*/
	bool ExportHDR(UTextureRenderTargetCube* TexCube, FArchive& Ar)
	{
		check(TexCube != nullptr);

		// Generate 2D image.
		TArray64<uint8> RawData;
		bool bUnwrapSuccess = CubemapHelpers::GenerateLongLatUnwrap(TexCube, RawData, Size, Format);
		bool bAcceptableFormat = (Format == PF_B8G8R8A8 || Format == PF_FloatRGBA);
		if (bUnwrapSuccess == false || bAcceptableFormat == false)
		{
			return false;
		}

		WriteHDRImage(RawData, Ar);

		return true;
	}

private:
	void WriteScanLine(FArchive& Ar, const TArray<uint8>& ScanLine)
	{
		const uint8* LineEnd = ScanLine.GetData() + ScanLine.Num();
		const uint8* LineSource = ScanLine.GetData();
		TArray<uint8> Output;
		Output.Reserve(ScanLine.Num() * 2);
		while (LineSource < LineEnd)
		{
			int32 CurrentPos = 0;
			int32 NextPos = 0;
			int32 CurrentRunLength = 0;
			while (CurrentRunLength <= 4 && NextPos < 128 && LineSource + NextPos < LineEnd)
			{
				CurrentPos = NextPos;
				CurrentRunLength = 0;
				while (CurrentRunLength < 127 && CurrentPos + CurrentRunLength < 128 && LineSource + NextPos < LineEnd && LineSource[CurrentPos] == LineSource[NextPos])
				{
					NextPos++;
					CurrentRunLength++;
				}
			}

			if (CurrentRunLength > 4)
			{
				// write a non run: LineSource[0] to LineSource[CurrentPos]
				if (CurrentPos > 0)
				{
					Output.Add(CurrentPos);
					for (int32 i = 0; i < CurrentPos; i++)
					{
						Output.Add(LineSource[i]);
					}
				}
				Output.Add((uint8)(128 + CurrentRunLength));
				Output.Add(LineSource[CurrentPos]);
			}
			else
			{
				// write a non run: LineSource[0] to LineSource[NextPos]
				Output.Add((uint8)(NextPos));
				for (int32 i = 0; i < NextPos; i++)
				{
					Output.Add((uint8)(LineSource[i]));
				}
			}
			LineSource += NextPos;
		}
		Ar.Serialize(Output.GetData(), Output.Num());
	}

	template<typename TSourceColorType>
	void WriteHDRBits(FArchive& Ar, TSourceColorType* SourceTexels)
	{
		const int32 NumChannels = 4;
		const int32 SizeX = Size.X;
		const int32 SizeY = Size.Y;
		TArray<uint8> ScanLine[NumChannels];
		for (int32 Channel = 0; Channel < NumChannels; Channel++)
		{
			ScanLine[Channel].Reserve(SizeX);
		}

		for (int32 y = 0; y < SizeY; y++)
		{
			// write RLE header
			uint8 RLEheader[4];
			RLEheader[0] = 2;
			RLEheader[1] = 2;
			RLEheader[2] = SizeX >> 8;
			RLEheader[3] = SizeX & 0xFF;
			Ar.Serialize(&RLEheader[0], sizeof(RLEheader));

			for (int32 Channel = 0; Channel < NumChannels; Channel++)
			{
				ScanLine[Channel].Reset();
			}

			for (int32 x = 0; x < SizeX; x++)
			{
				FLinearColor LinearColor(*SourceTexels);
				FColor RGBEColor = LinearColor.ToRGBE();

				ScanLine[0].Add(RGBEColor.R);
				ScanLine[1].Add(RGBEColor.G);
				ScanLine[2].Add(RGBEColor.B);
				ScanLine[3].Add(RGBEColor.A);
				SourceTexels++;
			}

			for (int32 Channel = 0; Channel < NumChannels; Channel++)
			{
				WriteScanLine(Ar, ScanLine[Channel]);
			}
		}
	}

	void WriteHDRHeader(FArchive& Ar)
	{
		const int32 MaxHeaderSize = 256;
		char Header[MAX_SPRINTF];
		FCStringAnsi::Sprintf(Header, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n", Size.Y, Size.X);
		Header[MaxHeaderSize - 1] = 0;
		int32 Len = FMath::Min(FCStringAnsi::Strlen(Header), MaxHeaderSize);
		Ar.Serialize(Header, Len);
	}

	void WriteHDRImage(const TArray64<uint8>& RawData, FArchive& Ar)
	{
		WriteHDRHeader(Ar);
		if (Format == PF_FloatRGBA)
		{
			WriteHDRBits(Ar, (FFloat16Color*)RawData.GetData());
		}
		else
		{
			WriteHDRBits(Ar, (FColor*)RawData.GetData());
		}
	}

	FIntPoint Size;
	EPixelFormat Format;
};

bool FImageUtils::ExportRenderTarget2DAsHDR(UTextureRenderTarget2D* TexRT, FArchive& Ar)
{
	FHDRExportHelper Exporter;
	return Exporter.ExportHDR(TexRT, Ar);
}

bool FImageUtils::ExportRenderTarget2DAsPNG(UTextureRenderTarget2D* TexRT, FArchive& Ar)
{
	bool bSuccess = false;
	if(TexRT->GetFormat() == PF_B8G8R8A8)
	{
		check(TexRT != nullptr);
		FRenderTarget* RenderTarget = TexRT->GameThread_GetRenderTargetResource();
		FIntPoint Size = RenderTarget->GetSizeXY();

		TArray64<uint8> RawData;
		bSuccess = GetRawData(TexRT, RawData);

		IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

		TSharedPtr<IImageWrapper> PNGImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

		PNGImageWrapper->SetRaw(RawData.GetData(), RawData.GetAllocatedSize(), Size.X, Size.Y, ERGBFormat::BGRA, 8);

		const TArray64<uint8> PNGData = PNGImageWrapper->GetCompressed(100);

		Ar.Serialize((void*)PNGData.GetData(), PNGData.GetAllocatedSize());
	}

	return bSuccess;
}

ENGINE_API bool FImageUtils::ExportRenderTarget2DAsEXR(UTextureRenderTarget2D* TexRT, FArchive& Ar)
{
	bool bSuccess = false;
	if(TexRT->GetFormat() == PF_B8G8R8A8 || TexRT->GetFormat() == PF_FloatRGBA)
	{
		check(TexRT != nullptr);
		FRenderTarget* RenderTarget = TexRT->GameThread_GetRenderTargetResource();
		FIntPoint Size = RenderTarget->GetSizeXY();

		TArray64<uint8> RawData;
		bSuccess = GetRawData(TexRT, RawData);

		int32 BitsPerPixel = TexRT->GetFormat() == PF_B8G8R8A8 ? 8 : (sizeof(FFloat16Color) / 4) * 8;
		ERGBFormat RGBFormat = TexRT->GetFormat() == PF_B8G8R8A8 ? ERGBFormat::BGRA : ERGBFormat::RGBAF;

		IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

		TSharedPtr<IImageWrapper> EXRImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);

		EXRImageWrapper->SetRaw(RawData.GetData(), RawData.GetAllocatedSize(), Size.X, Size.Y, RGBFormat, BitsPerPixel);

		const TArray64<uint8> Data = EXRImageWrapper->GetCompressed();

		Ar.Serialize((void*)Data.GetData(), Data.GetAllocatedSize());

		bSuccess = true;
	}

	return bSuccess;
}

bool FImageUtils::ExportTexture2DAsHDR(UTexture2D* TexRT, FArchive& Ar)
{
	FHDRExportHelper Exporter;
	return Exporter.ExportHDR(TexRT, Ar);
}

UTexture2D* FImageUtils::ImportFileAsTexture2D(const FString& Filename)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	UTexture2D* NewTexture = nullptr;
	TArray64<uint8> Buffer;
	if (FFileHelper::LoadFileToArray(Buffer, *Filename))
	{
		EPixelFormat PixelFormat = PF_Unknown;

		uint8* RawData = nullptr;
		int32 BitDepth = 0;
		int32 Width = 0;
		int32 Height = 0;

		if (FPaths::GetExtension(Filename) == TEXT("HDR"))
		{
			TSharedPtr<IImageWrapper> HdrImageWrapper =  ImageWrapperModule.CreateImageWrapper(EImageFormat::HDR);
	
			if(HdrImageWrapper->SetCompressed(Buffer.GetData(), Buffer.Num()))
			{
				PixelFormat = PF_FloatRGBA;
				Width = HdrImageWrapper->GetWidth();
				Height = HdrImageWrapper->GetHeight();

				TArray64<uint8> BGREImage;
				if (HdrImageWrapper->GetRaw(ERGBFormat::BGRE, 8, BGREImage))
				{
					NewTexture = UTexture2D::CreateTransient(Width, Height, PixelFormat);
					if (NewTexture)
					{
						uint8* MipData = static_cast<uint8*>(NewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));

						TArrayView64<FColor> SourceColors(reinterpret_cast<FColor*>(BGREImage.GetData()), BGREImage.Num() / sizeof(FColor));

						// Bulk data was already allocated for the correct size when we called CreateTransient above
						TArrayView64<FFloat16> Destination(reinterpret_cast<FFloat16*>(MipData), NewTexture->GetPlatformData()->Mips[0].BulkData.GetBulkDataSize() / sizeof(FFloat16));

						int64 DestinationIndex = 0;
						for (const FColor& Color: SourceColors)
						{
							FLinearColor LinearColor = Color.FromRGBE();
							Destination[DestinationIndex++].Set(LinearColor.R);
							Destination[DestinationIndex++].Set(LinearColor.G);
							Destination[DestinationIndex++].Set(LinearColor.B);
							Destination[DestinationIndex++].Set(LinearColor.A);
						}

						NewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();

						NewTexture->UpdateResource();
					}
				}
			}
		}
		else
		{
			NewTexture = FImageUtils::ImportBufferAsTexture2D(Buffer);
		}

		if(!NewTexture)
		{
			UE_LOG(LogImageUtils, Warning, TEXT("Error creating texture. %s is not a supported file format"), *Filename)
		}	
	}
	else
	{
		UE_LOG(LogImageUtils, Warning, TEXT("Error creating texture. %s could not be found"), *Filename)
	}

	return NewTexture;
}

UTexture2D* FImageUtils::ImportBufferAsTexture2D(TArrayView64<const uint8> Buffer)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat Format = ImageWrapperModule.DetectImageFormat(Buffer.GetData(), Buffer.Num());

	UTexture2D* NewTexture = nullptr;
	EPixelFormat PixelFormat = PF_Unknown;

	if (Format != EImageFormat::Invalid)
	{
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
		
		int32 BitDepth = 0;
		int32 Width = 0;
		int32 Height = 0;

		if (ImageWrapper->SetCompressed((void*)Buffer.GetData(), Buffer.Num()))
		{
			PixelFormat = PF_Unknown;
			
			ERGBFormat RGBFormat = ERGBFormat::Invalid;
			
			BitDepth = ImageWrapper->GetBitDepth();
			
			Width = ImageWrapper->GetWidth();
			Height = ImageWrapper->GetHeight();
			
			if (BitDepth == 16)
			{
				PixelFormat = PF_FloatRGBA;
				RGBFormat = ERGBFormat::RGBAF;
			}
			else if (BitDepth == 8)
			{
				PixelFormat = PF_B8G8R8A8;
				RGBFormat = ERGBFormat::BGRA;
			}
			else
			{
				UE_LOG(LogImageUtils, Warning, TEXT("Error creating texture. Bit depth is unsupported. (%d)"), BitDepth);
				return nullptr;
			}
			
			TArray64<uint8> UncompressedData;
			ImageWrapper->GetRaw(RGBFormat, BitDepth, UncompressedData);
			
			NewTexture = UTexture2D::CreateTransient(Width, Height, PixelFormat);
			if (NewTexture)
			{
				NewTexture->bNotOfflineProcessed = true;
				uint8* MipData = static_cast<uint8*>(NewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
				
				// Bulk data was already allocated for the correct size when we called CreateTransient above
				FMemory::Memcpy(MipData, UncompressedData.GetData(), NewTexture->GetPlatformData()->Mips[0].BulkData.GetBulkDataSize());
				
				NewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();

				NewTexture->UpdateResource();
			}
		}
	}
	else
	{
		UE_LOG(LogImageUtils, Warning, TEXT("Error creating texture. Couldn't determine the file format"));
	}

	return NewTexture;
}

UTexture2D* FImageUtils::ImportBufferAsTexture2D(const TArray<uint8>& Buffer)
{
	return ImportBufferAsTexture2D(TArrayView64<const uint8>(Buffer.GetData(), Buffer.Num()));
}

bool FImageUtils::ExportRenderTargetCubeAsHDR(UTextureRenderTargetCube* TexRT, FArchive& Ar)
{
	FHDRExportHelper Exporter;
	return Exporter.ExportHDR(TexRT, Ar);
}

bool FImageUtils::ExportTextureCubeAsHDR(UTextureCube* TexRT, FArchive& Ar)
{
	FHDRExportHelper Exporter;
	return Exporter.ExportHDR(TexRT, Ar);
}

#undef LOCTEXT_NAMESPACE
