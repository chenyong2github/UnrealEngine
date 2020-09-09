// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageCore.h"
#include "Modules/ModuleManager.h"
#include "Async/ParallelFor.h"


IMPLEMENT_MODULE(FDefaultModuleImpl, ImageCore);


/* Local helper functions
 *****************************************************************************/

/**
 * Initializes storage for an image.
 *
 * @param Image - The image to initialize storage for.
 */
static void InitImageStorage(FImage& Image)
{
	int64 NumBytes = int64(Image.SizeX) * Image.SizeY * Image.NumSlices * Image.GetBytesPerPixel();
	Image.RawData.Empty(NumBytes);
	Image.RawData.AddUninitialized(NumBytes);
}


/**
 * Copies an image accounting for format differences. Sizes must match.
 *
 * @param SrcImage - The source image to copy from.
 * @param DestImage - The destination image to copy to.
 */
static void CopyImage(const FImage& SrcImage, FImage& DestImage)
{
	check(SrcImage.SizeX == DestImage.SizeX);
	check(SrcImage.SizeY == DestImage.SizeY);
	check(SrcImage.NumSlices == DestImage.NumSlices);

	const bool bDestIsGammaCorrected = DestImage.IsGammaCorrected();
	const int64 NumTexels = int64(SrcImage.SizeX) * SrcImage.SizeY * SrcImage.NumSlices;
	const int64 NumJobs = FTaskGraphInterface::Get().GetNumWorkerThreads();
	int64 TexelsPerJob = NumTexels / NumJobs;
	if (TexelsPerJob * NumJobs < NumTexels)
	{
		++TexelsPerJob;
	}

	if (SrcImage.Format == DestImage.Format &&
		SrcImage.GammaSpace == DestImage.GammaSpace)
	{
		DestImage.RawData = SrcImage.RawData;
	}
	else if (SrcImage.Format == ERawImageFormat::RGBA32F)
	{
		// Convert from 32-bit linear floating point.
		TArrayView64<const FLinearColor> SrcColors = SrcImage.AsRGBA32F();
	
		switch (DestImage.Format)
		{
		case ERawImageFormat::G8:
			{
				TArrayView64<uint8> DestLum = DestImage.AsG8();
				ParallelFor(NumJobs, [NumJobs, DestLum, SrcColors, TexelsPerJob, NumTexels, bDestIsGammaCorrected](int64 JobIndex)
				{
					const int64 StartIndex = JobIndex * TexelsPerJob;
					const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
					for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
					{
						DestLum[TexelIndex] = SrcColors[TexelIndex].ToFColor(bDestIsGammaCorrected).R;
					}
				});
			}
			break;

		case ERawImageFormat::G16:
		{
			TArrayView64<uint16>DestLum = DestImage.AsG16();
			ParallelFor(NumJobs, [NumJobs, DestLum, SrcColors, TexelsPerJob, NumTexels, bDestIsGammaCorrected](int64 JobIndex)
			{
				const int64 StartIndex = JobIndex * TexelsPerJob;
				const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
				for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
				{
					DestLum[TexelIndex] = FMath::Clamp(FMath::FloorToInt(SrcColors[TexelIndex].R * 65535.999f), 0, 65535);
				}
			});
		}
		break;

		case ERawImageFormat::BGRA8:
			{
				TArrayView64<FColor> DestColors = DestImage.AsBGRA8();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels, bDestIsGammaCorrected](int64 JobIndex)
				{
					const int64 StartIndex = JobIndex * TexelsPerJob;
					const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
					for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
					{
						DestColors[TexelIndex] = SrcColors[TexelIndex].ToFColor(bDestIsGammaCorrected);
					}
				});
			}
			break;
		
		case ERawImageFormat::BGRE8:
			{
				TArrayView64<FColor> DestColors = DestImage.AsBGRE8();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels, bDestIsGammaCorrected](int64 JobIndex)
				{
					const int64 StartIndex = JobIndex * TexelsPerJob;
					const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
					for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
					{
						DestColors[TexelIndex] = SrcColors[TexelIndex].ToRGBE();
					}
				});
			}
			break;
		
		case ERawImageFormat::RGBA16:
			{
				TArrayView64<uint16> DestColors = DestImage.AsRGBA16();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels](int64 JobIndex)
				{
					for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
					{
						int64 DestIndex = TexelIndex * 4;
						DestColors[DestIndex + 0] = FMath::Clamp(FMath::FloorToInt(SrcColors[TexelIndex].R * 65535.999f), 0, 65535);
						DestColors[DestIndex + 1] = FMath::Clamp(FMath::FloorToInt(SrcColors[TexelIndex].G * 65535.999f), 0, 65535);
						DestColors[DestIndex + 2] = FMath::Clamp(FMath::FloorToInt(SrcColors[TexelIndex].B * 65535.999f), 0, 65535);
						DestColors[DestIndex + 3] = FMath::Clamp(FMath::FloorToInt(SrcColors[TexelIndex].A * 65535.999f), 0, 65535);
					}
				});
			}
			break;
		
		case ERawImageFormat::RGBA16F:
			{
				TArrayView64<FFloat16Color> DestColors = DestImage.AsRGBA16F();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels](int64 JobIndex)
				{
					for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
					{
						DestColors[TexelIndex] = FFloat16Color(SrcColors[TexelIndex]);
					}
				});
			}
			break;

		case ERawImageFormat::R16F:
			{
				TArrayView64<FFloat16> DestColors = DestImage.AsR16F();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels](int64 JobIndex)
				{
					for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
					{
						DestColors[TexelIndex] = FFloat16(SrcColors[TexelIndex].R);
					}
				});
			}
			break;
		}
	}
	else if (DestImage.Format == ERawImageFormat::RGBA32F)
	{
		// Convert to 32-bit linear floating point.
		TArrayView64<FLinearColor> DestColors = DestImage.AsRGBA32F();
		switch (SrcImage.Format)
		{
		case ERawImageFormat::G8:
			{
				TArrayView64<const uint8> SrcLum = SrcImage.AsG8();
				for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
				{
					FColor SrcColor(SrcLum[TexelIndex],SrcLum[TexelIndex],SrcLum[TexelIndex],255);
					
					switch ( SrcImage.GammaSpace )
					{
					case EGammaSpace::Linear:
						DestColors[TexelIndex] = SrcColor.ReinterpretAsLinear();
						break;
					case EGammaSpace::sRGB:
						DestColors[TexelIndex] = FLinearColor(SrcColor);
						break;
					case EGammaSpace::Pow22:
						DestColors[TexelIndex] = FLinearColor::FromPow22Color(SrcColor);
						break;
					}
				}
			}
			break;

		case ERawImageFormat::G16:
		{
			TArrayView64<const uint16> SrcLum = SrcImage.AsG16();
			for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
			{
				DestColors[TexelIndex] = FLinearColor(SrcLum[TexelIndex] / 65535.0f, SrcLum[TexelIndex] / 65535.0f, SrcLum[TexelIndex] / 65535.0f, 1.0f);
			}
		}
		break;

		case ERawImageFormat::BGRA8:
			{
				TArrayView64<const FColor> SrcColors = SrcImage.AsBGRA8();
				switch ( SrcImage.GammaSpace )
				{
				case EGammaSpace::Linear:
					ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels](int64 JobIndex)
					{
						int64 StartIndex = JobIndex * TexelsPerJob;
						int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
						for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
						{
							DestColors[TexelIndex] = SrcColors[TexelIndex].ReinterpretAsLinear();
						}
					});
					break;
				case EGammaSpace::sRGB:
					ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels](int64 JobIndex)
					{
						int64 StartIndex = JobIndex * TexelsPerJob;
						int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
						for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
						{
							DestColors[TexelIndex] = FLinearColor(SrcColors[TexelIndex]);
						}
					});
					break;
				case EGammaSpace::Pow22:
					for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
					{
						DestColors[TexelIndex] = FLinearColor::FromPow22Color(SrcColors[TexelIndex]);
					}
					break;
				}
			}
			break;

		case ERawImageFormat::BGRE8:
			{
				TArrayView64<const FColor> SrcColors = SrcImage.AsBGRE8();
				for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
				{
					DestColors[TexelIndex] = SrcColors[TexelIndex].FromRGBE();
				}
			}
			break;

		case ERawImageFormat::RGBA16:
			{
				TArrayView64<const uint16> SrcColors = SrcImage.AsRGBA16();
				for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
				{
					int64 SrcIndex = TexelIndex * 4;
					DestColors[TexelIndex] = FLinearColor(
						SrcColors[SrcIndex + 0] / 65535.0f,
						SrcColors[SrcIndex + 1] / 65535.0f,
						SrcColors[SrcIndex + 2] / 65535.0f,
						SrcColors[SrcIndex + 3] / 65535.0f
						);
				}
			}
			break;

		case ERawImageFormat::RGBA16F:
			{
				TArrayView64<const FFloat16Color> SrcColors = SrcImage.AsRGBA16F();
				for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
				{
					DestColors[TexelIndex] = FLinearColor(SrcColors[TexelIndex]);
				}
			}
			break;

		case ERawImageFormat::R16F:
		{
			TArrayView64<const FFloat16> SrcColors = SrcImage.AsR16F();
			for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
			{
				DestColors[TexelIndex] = FLinearColor(SrcColors[TexelIndex].GetFloat(), 0, 0, 1);
			}
		}
		break;
		}
	}
	else
	{
		// Arbitrary conversion, use 32-bit linear float as an intermediate format.
		FImage TempImage(SrcImage.SizeX, SrcImage.SizeY, ERawImageFormat::RGBA32F);
		CopyImage(SrcImage, TempImage);
		CopyImage(TempImage, DestImage);
	}
}

static FLinearColor SampleImage(TArrayView64<const FLinearColor> Pixels, int Width, int Height, float X, float Y)
{
	const int64 TexelX0 = FMath::FloorToInt(X);
	const int64 TexelY0 = FMath::FloorToInt(Y);
	const int64 TexelX1 = FMath::Min<int64>(TexelX0 + 1, Width - 1);
	const int64 TexelY1 = FMath::Min<int64>(TexelY0 + 1, Height - 1);
	checkSlow(TexelX0 >= 0 && TexelX0 < Width);
	checkSlow(TexelY0 >= 0 && TexelY0 < Width);

	const float FracX1 = FMath::Frac(X);
	const float FracY1 = FMath::Frac(Y);
	const float FracX0 = 1.0f - FracX1;
	const float FracY0 = 1.0f - FracY1;
	const FLinearColor& Color00 = Pixels[TexelY0 * Width + TexelX0];
	const FLinearColor& Color01 = Pixels[TexelY1 * Width + TexelX0];
	const FLinearColor& Color10 = Pixels[TexelY0 * Width + TexelX1];
	const FLinearColor& Color11 = Pixels[TexelY1 * Width + TexelX1];
	return
		Color00 * (FracX0 * FracY0) +
		Color01 * (FracX0 * FracY1) +
		Color10 * (FracX1 * FracY0) +
		Color11 * (FracX1 * FracY1);
}

static void ResizeImage(const FImage& SrcImage, FImage& DestImage)
{
	TArrayView64<const FLinearColor> SrcPixels = SrcImage.AsRGBA32F();
	TArrayView64<FLinearColor> DestPixels = DestImage.AsRGBA32F();
	const float DestToSrcScaleX = (float)SrcImage.SizeX / (float)DestImage.SizeX;
	const float DestToSrcScaleY = (float)SrcImage.SizeY / (float)DestImage.SizeY;

	for (int64 DestY = 0; DestY < DestImage.SizeY; ++DestY)
	{
		const float SrcY = (float)DestY * DestToSrcScaleY;
		for (int64 DestX = 0; DestX < DestImage.SizeX; ++DestX)
		{
			const float SrcX = (float)DestX * DestToSrcScaleX;
			const FLinearColor Color = SampleImage(SrcPixels, SrcImage.SizeX, SrcImage.SizeY, SrcX, SrcY);
			DestPixels[DestY * DestImage.SizeX + DestX] = Color;
		}
	}
}

/* FImage constructors
 *****************************************************************************/

FImage::FImage(int32 InSizeX, int32 InSizeY, int32 InNumSlices, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace)
	: SizeX(InSizeX)
	, SizeY(InSizeY)
	, NumSlices(InNumSlices)
	, Format(InFormat)
	, GammaSpace(InGammaSpace)
{
	InitImageStorage(*this);
}


FImage::FImage(int32 InSizeX, int32 InSizeY, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace)
	: SizeX(InSizeX)
	, SizeY(InSizeY)
	, NumSlices(1)
	, Format(InFormat)
	, GammaSpace(InGammaSpace)
{
	InitImageStorage(*this);
}


void FImage::Init(int32 InSizeX, int32 InSizeY, int32 InNumSlices, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumSlices = InNumSlices;
	Format = InFormat;
	GammaSpace = InGammaSpace;
	InitImageStorage(*this);
}


void FImage::Init(int32 InSizeX, int32 InSizeY, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumSlices = 1;
	Format = InFormat;
	GammaSpace = InGammaSpace;
	InitImageStorage(*this);
}


/* FImage interface
 *****************************************************************************/

void FImage::CopyTo(FImage& DestImage, ERawImageFormat::Type DestFormat, EGammaSpace DestGammaSpace) const
{
	DestImage.SizeX = SizeX;
	DestImage.SizeY = SizeY;
	DestImage.NumSlices = NumSlices;
	DestImage.Format = DestFormat;
	DestImage.GammaSpace = DestGammaSpace;
	InitImageStorage(DestImage);
	CopyImage(*this, DestImage);
}

void FImage::ResizeTo(FImage& DestImage, int32 DestSizeX, int32 DestSizeY, ERawImageFormat::Type DestFormat, EGammaSpace DestGammaSpace) const
{
	check(NumSlices == 1); // only support 1 slice for now

	FImage TempSrcImage;
	const FImage* SrcImagePtr = this;
	if (Format != ERawImageFormat::RGBA32F)
	{
		CopyTo(TempSrcImage, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		SrcImagePtr = &TempSrcImage;
	}

	if (DestFormat == ERawImageFormat::RGBA32F)
	{
		DestImage.SizeX = DestSizeX;
		DestImage.SizeY = DestSizeY;
		DestImage.NumSlices = 1;
		DestImage.Format = DestFormat;
		DestImage.GammaSpace = DestGammaSpace;
		InitImageStorage(DestImage);
		ResizeImage(*SrcImagePtr, DestImage);
	}
	else
	{
		FImage TempDestImage;
		TempDestImage.SizeX = DestSizeX;
		TempDestImage.SizeY = DestSizeY;
		TempDestImage.NumSlices = 1;
		TempDestImage.Format = ERawImageFormat::RGBA32F;
		TempDestImage.GammaSpace = DestGammaSpace;
		InitImageStorage(TempDestImage);
		ResizeImage(*SrcImagePtr, TempDestImage);
		TempDestImage.CopyTo(DestImage, DestFormat, DestGammaSpace);
	}
}

int32 FImage::GetBytesPerPixel() const
{
	int32 BytesPerPixel = 0;
	switch (Format)
	{
	case ERawImageFormat::G8:
		BytesPerPixel = 1;
		break;
	
	case ERawImageFormat::G16:
	case ERawImageFormat::R16F:
		BytesPerPixel = 2;
		break;

	case ERawImageFormat::BGRA8:
	case ERawImageFormat::BGRE8:
		BytesPerPixel = 4;
		break;
			
	case ERawImageFormat::RGBA16:
	case ERawImageFormat::RGBA16F:
		BytesPerPixel = 8;
		break;

	case ERawImageFormat::RGBA32F:
		BytesPerPixel = 16;
		break;
	}
	return BytesPerPixel;
}
