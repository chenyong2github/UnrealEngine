// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageCore.h"
#include "Modules/ModuleManager.h"
#include "Async/ParallelFor.h"
#include "TransferFunctions.h"
#include "ColorSpace.h"

DEFINE_LOG_CATEGORY_STATIC(LogImageCore, Log, All);

static constexpr float MAX_HALF_FLOAT16 = 65504.0f;

FORCEINLINE static void SaturateToHalfFloat(FLinearColor& LinearCol)
{
	LinearCol.R = FMath::Clamp(LinearCol.R, -MAX_HALF_FLOAT16, MAX_HALF_FLOAT16);
	LinearCol.G = FMath::Clamp(LinearCol.G, -MAX_HALF_FLOAT16, MAX_HALF_FLOAT16);
	LinearCol.B = FMath::Clamp(LinearCol.B, -MAX_HALF_FLOAT16, MAX_HALF_FLOAT16);
}

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
 * Compute number of jobs to use for ParallelFor
 *
 * @param OutNumItemsPerJob - filled with num items per job; NumJobs*OutNumItemsPerJob >= NumItems
 * @param NumItems - total num items
 * @param MinNumItemsPerJob - jobs will do at least this many items each
 * @param MinNumItemsForAnyJobs - (optional) if NumItems is less than this, no parallelism are used
 * @return NumJobs
 */
static inline int32 ParallelForComputeNumJobs(int64 & OutNumItemsPerJob,int64 NumItems,int64 MinNumItemsPerJob,int64 MinNumItemsForAnyJobs = 0)
{
	if ( NumItems <= FMath::Max(MinNumItemsPerJob,MinNumItemsForAnyJobs) )
	{
		OutNumItemsPerJob = NumItems;
		return 1;
	}
	
	// ParallelFor will actually make 6*NumWorkers batches and then make NumWorkers tasks that pop the batches
	//	this helps with mismatched thread runtime
	//	here we only make NumWorkers batches max
	//	but this is rarely a problem in image cook because it is paralellized already at a the higher level

	const int32 NumWorkers = FTaskGraphInterface::Get().GetNumWorkerThreads();

	int32 NumJobs = (int32)(NumItems / MinNumItemsPerJob); // round down
	NumJobs = FMath::Clamp(NumJobs, int32(1), NumWorkers); 

	OutNumItemsPerJob = (NumItems + NumJobs-1) / NumJobs; // round up
	check( NumJobs*OutNumItemsPerJob >= NumItems ); 

	return NumJobs;
}

static constexpr int64 MinPixelsPerJob = 16384;
// Surfaces of VT tile size or smaller will not parallelize at all :
static constexpr int64 MinPixelsForAnyJob = 136*136;

IMAGECORE_API int32 ImageParallelForComputeNumJobsForPixels(int64 & OutNumPixelsPerJob,int64 NumPixels)
{
	return ParallelForComputeNumJobs(OutNumPixelsPerJob,NumPixels,MinPixelsPerJob,MinPixelsForAnyJob);
}

IMAGECORE_API int32 ImageParallelForComputeNumJobsForRows(int32 & OutNumItemsPerJob,int32 SizeX,int32 SizeY)
{
	int64 NumPixels = int64(SizeX)*SizeY;
	int64 OutNumPixelsPerJob;
	int32 NumJobs = ParallelForComputeNumJobs(OutNumPixelsPerJob,NumPixels,MinPixelsPerJob,MinPixelsForAnyJob);
	OutNumItemsPerJob = (SizeY + NumJobs-1) / NumJobs; // round up;
	return NumJobs;
}

/**
 * Copies an image accounting for format differences. Sizes must match.
 *
 * @param SrcImage - The source image to copy from.
 * @param DestImage - The destination image to copy to.
 */
static void CopyImage(const FImage& SrcImage, FImage& DestImage)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage);

	check(SrcImage.SizeX == DestImage.SizeX);
	check(SrcImage.SizeY == DestImage.SizeY);
	check(SrcImage.NumSlices == DestImage.NumSlices);

	const bool bDestIsGammaCorrected = DestImage.IsGammaCorrected();
	const int64 NumTexels = int64(SrcImage.SizeX) * SrcImage.SizeY * SrcImage.NumSlices;
	int64 TexelsPerJob;
	int32 NumJobs = ImageParallelForComputeNumJobsForPixels(TexelsPerJob,NumTexels);

	if (SrcImage.Format == DestImage.Format &&
		SrcImage.GammaSpace == DestImage.GammaSpace)
	{
		DestImage.RawData = SrcImage.RawData;
	}
	else if (SrcImage.Format == ERawImageFormat::RGBA32F)
	{
		// Convert from 32-bit linear floating point.
		TArrayView64<const FLinearColor> SrcColors = SrcImage.AsRGBA32F();
	
		// if gamma correction is done, it's always to sRGB , not to Pow22
		// so if Pow22 was requested, change to sRGB
		// so that Float->int->Float roundtrips correctly
		if ( DestImage.GammaSpace == EGammaSpace::Pow22 )
		{
			DestImage.GammaSpace = EGammaSpace::sRGB;
		}

		switch (DestImage.Format)
		{
		case ERawImageFormat::G8:
			{
				TArrayView64<uint8> DestLum = DestImage.AsG8();
				ParallelFor(NumJobs, [NumJobs, DestLum, SrcColors, TexelsPerJob, NumTexels, bDestIsGammaCorrected](int64 JobIndex)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

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
				TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

				const int64 StartIndex = JobIndex * TexelsPerJob;
				const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
				for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
				{
					DestLum[TexelIndex] = FColor::QuantizeUNormFloatTo16( SrcColors[TexelIndex].R );
				}
			});
		}
		break;

		case ERawImageFormat::BGRA8:
			{
				TArrayView64<FColor> DestColors = DestImage.AsBGRA8();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels, bDestIsGammaCorrected](int64 JobIndex)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

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
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

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
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

					for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
					{
						int64 DestIndex = TexelIndex * 4;
						DestColors[DestIndex + 0] = FColor::QuantizeUNormFloatTo16( SrcColors[TexelIndex].R );
						DestColors[DestIndex + 1] = FColor::QuantizeUNormFloatTo16( SrcColors[TexelIndex].G );
						DestColors[DestIndex + 2] = FColor::QuantizeUNormFloatTo16( SrcColors[TexelIndex].B );
						DestColors[DestIndex + 3] = FColor::QuantizeUNormFloatTo16( SrcColors[TexelIndex].A );
					}
				});
			}
			break;
		
		case ERawImageFormat::RGBA16F:
			{
				TArrayView64<FFloat16Color> DestColors = DestImage.AsRGBA16F();
				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels](int64 JobIndex)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

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
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

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
				float Value = FColor::DequantizeUNorm16ToFloat( SrcLum[TexelIndex] );
				DestColors[TexelIndex] = FLinearColor(Value,Value,Value, 1.0f);
			}
		}
		break;

		case ERawImageFormat::BGRA8:
			{
				TArrayView64<const FColor> SrcColors = SrcImage.AsBGRA8();
				EGammaSpace SrcGamma = SrcImage.GammaSpace;

				ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels, SrcGamma](int64 JobIndex)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.PF);

					int64 StartIndex = JobIndex * TexelsPerJob;
					int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);

					switch ( SrcGamma )
					{
					case EGammaSpace::Linear:
						for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
						{
							DestColors[TexelIndex] = SrcColors[TexelIndex].ReinterpretAsLinear();
						}
					break;
					case EGammaSpace::sRGB:
						for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
						{
							DestColors[TexelIndex] = FLinearColor(SrcColors[TexelIndex]);
						}
					break;
					case EGammaSpace::Pow22:
						for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
						{
							DestColors[TexelIndex] = FLinearColor::FromPow22Color(SrcColors[TexelIndex]);
						}
					break;
					}
				});
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
					DestColors[TexelIndex] = SrcColors[TexelIndex].GetFloats();
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
		TRACE_CPUPROFILER_EVENT_SCOPE(CopyImage.TempLinear);

		// Arbitrary conversion, use 32-bit linear float as an intermediate format.
		FImage TempImage(SrcImage.SizeX, SrcImage.SizeY, ERawImageFormat::RGBA32F);
		CopyImage(SrcImage, TempImage);
		CopyImage(TempImage, DestImage);
	}
}

void FImage::TransformToWorkingColorSpace(const FVector2D& SourceRedChromaticity, const FVector2D& SourceGreenChromaticity, const FVector2D& SourceBlueChromaticity, const FVector2D& SourceWhiteChromaticity, UE::Color::EChromaticAdaptationMethod Method, double EqualityTolerance)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TransformToWorkingColorSpace);

	check(GammaSpace == EGammaSpace::Linear);

	const UE::Color::FColorSpace Source(SourceRedChromaticity, SourceGreenChromaticity, SourceBlueChromaticity, SourceWhiteChromaticity);
	const UE::Color::FColorSpace& Target = UE::Color::FColorSpace::GetWorking();

	if (Source.Equals(Target, EqualityTolerance))
	{
		UE_LOG(LogImageCore, VeryVerbose, TEXT("Source and working color spaces are equal within tolerance, bypass color space transformation."));
		return;
	}

	UE::Color::FColorSpaceTransform Transform(Source, Target, Method);

	TArrayView64<FLinearColor> ImageColors = AsRGBA32F();

	const int64 NumTexels = int64(SizeX) * SizeY * NumSlices;
	int64 TexelsPerJob;
	int32 NumJobs = ImageParallelForComputeNumJobsForPixels(TexelsPerJob,NumTexels);

	ParallelFor(NumJobs, [Transform, ImageColors, TexelsPerJob, NumTexels](int64 JobIndex)
		{
		TRACE_CPUPROFILER_EVENT_SCOPE(TransformToWorkingColorSpace.PF);

			const int64 StartIndex = JobIndex * TexelsPerJob;
			const int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
			for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
			{
				ImageColors[TexelIndex] = Transform.Apply(ImageColors[TexelIndex]);
				SaturateToHalfFloat(ImageColors[TexelIndex]);
			}
		});
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

void FImage::Linearize(uint8 SourceEncoding, FImage& DestImage) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Linearize);

	DestImage.SizeX = SizeX;
	DestImage.SizeY = SizeY;
	DestImage.NumSlices = NumSlices;
	DestImage.Format = ERawImageFormat::RGBA32F;
	DestImage.GammaSpace = EGammaSpace::Linear;
	InitImageStorage(DestImage);

	const FImage& SrcImage = *this;
	check(SrcImage.SizeX == DestImage.SizeX);
	check(SrcImage.SizeY == DestImage.SizeY);
	check(SrcImage.NumSlices == DestImage.NumSlices);

	UE::Color::EEncoding SourceEncodingType = static_cast<UE::Color::EEncoding>(SourceEncoding);

	if (SourceEncodingType == UE::Color::EEncoding::None)
	{
		CopyImage(SrcImage, DestImage);
		return;
	}
	else if (SourceEncodingType >= UE::Color::EEncoding::Max)
	{
		UE_LOG(LogImageCore, Warning, TEXT("Invalid encoding %d, falling back to linearization using CopyImage."), SourceEncoding);
		CopyImage(SrcImage, DestImage);
		return;
	}

	const bool bDestIsGammaCorrected = DestImage.IsGammaCorrected();
	const int64 NumTexels = int64(SrcImage.SizeX) * SrcImage.SizeY * SrcImage.NumSlices;

	int64 TexelsPerJob;
	int32 NumJobs = ParallelForComputeNumJobs(TexelsPerJob,NumTexels,MinPixelsPerJob,MinPixelsForAnyJob);

	TFunction<FLinearColor(const FLinearColor&)> DecodeFunction = UE::Color::GetColorDecodeFunction(SourceEncodingType);
	check(DecodeFunction != nullptr);

	// Convert to 32-bit linear floating point.
	TArrayView64<FLinearColor> DestColors = DestImage.AsRGBA32F();
	switch (SrcImage.Format)
	{
	case ERawImageFormat::G8:
	{
		TArrayView64<const uint8> SrcLum = SrcImage.AsG8();
		for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
		{
			FColor SrcColor(SrcLum[TexelIndex], SrcLum[TexelIndex], SrcLum[TexelIndex], 255);
			DestColors[TexelIndex] = SrcColor.ReinterpretAsLinear();
			DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
		}
	}
	break;

	case ERawImageFormat::G16:
	{
		TArrayView64<const uint16> SrcLum = SrcImage.AsG16();
		for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
		{
			float Value = FColor::DequantizeUNorm16ToFloat(SrcLum[TexelIndex]);
			DestColors[TexelIndex] = FLinearColor(Value, Value, Value, 1.0f);
			DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
		}
	}
	break;

	case ERawImageFormat::BGRA8:
	{
		TArrayView64<const FColor> SrcColors = SrcImage.AsBGRA8();
		EGammaSpace SrcGamma = SrcImage.GammaSpace;

		ParallelFor(NumJobs, [DestColors, SrcColors, TexelsPerJob, NumTexels, SrcGamma, DecodeFunction](int64 JobIndex)
			{
			TRACE_CPUPROFILER_EVENT_SCOPE(Linearize.PF);

				int64 StartIndex = JobIndex * TexelsPerJob;
				int64 EndIndex = FMath::Min(StartIndex + TexelsPerJob, NumTexels);
				for (int64 TexelIndex = StartIndex; TexelIndex < EndIndex; ++TexelIndex)
				{
					DestColors[TexelIndex] = SrcColors[TexelIndex].ReinterpretAsLinear();
					DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
					SaturateToHalfFloat(DestColors[TexelIndex]);
				}
			});
	}
	break;

	case ERawImageFormat::BGRE8:
	{
		TArrayView64<const FColor> SrcColors = SrcImage.AsBGRE8();
		for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
		{
			DestColors[TexelIndex] = SrcColors[TexelIndex].FromRGBE();
			DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
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
			DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
		}
	}
	break;

	case ERawImageFormat::RGBA16F:
	{
		TArrayView64<const FFloat16Color> SrcColors = SrcImage.AsRGBA16F();
		for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
		{
			DestColors[TexelIndex] = SrcColors[TexelIndex].GetFloats();
			DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
		}
	}
	break;

	case ERawImageFormat::RGBA32F:
	{
		TArrayView64<const FLinearColor> SrcColors = SrcImage.AsRGBA32F();
		for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
		{
			DestColors[TexelIndex] = DecodeFunction(SrcColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
		}
	}
	break;

	case ERawImageFormat::R16F:
	{
		TArrayView64<const FFloat16> SrcColors = SrcImage.AsR16F();
		for (int64 TexelIndex = 0; TexelIndex < NumTexels; ++TexelIndex)
		{
			DestColors[TexelIndex] = FLinearColor(SrcColors[TexelIndex].GetFloat(), 0, 0, 1);
			DestColors[TexelIndex] = DecodeFunction(DestColors[TexelIndex]);
			SaturateToHalfFloat(DestColors[TexelIndex]);
		}
	}
	break;
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
