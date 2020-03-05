// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineImageQuantization.h"
#include "ImagePixelData.h"

namespace UE
{
namespace MoviePipeline
{

static TUniquePtr<FImagePixelData> QuantizePixelDataTo8bpp(const FImagePixelData* InPixelData)
{
	TUniquePtr<FImagePixelData> QuantizedPixelData = nullptr;

	FIntPoint RawSize = InPixelData->GetSize();
	int32 RawNumChannels = InPixelData->GetNumChannels();

	// Look at our incoming bit depth
	switch (InPixelData->GetBitDepth())
	{
	case 8:
	{
		// No work actually needs to be done, hooray! We return a copy of the data for consistency
		QuantizedPixelData = InPixelData->CopyImageData();
		break;
	}
	case 16:
	{
		TArray<FColor> ClampedPixels;
		ClampedPixels.SetNum(RawSize.X * RawSize.Y);

		int64 SizeInBytes = 0;
		const void* SrcRawDataPtr = nullptr;
		InPixelData->GetRawData(SrcRawDataPtr, SizeInBytes);

		const uint16* RawDataPtr = static_cast<const uint16*>(SrcRawDataPtr);

		// Copy pixels to new array
		for (int32 Y = 0; Y < RawSize.Y; Y++)
		{
			for (int32 X = 0; X < RawSize.X; X++)
			{
				FColor* DestColor = &ClampedPixels.GetData()[Y*RawSize.X + X];
				FLinearColor SrcColor;
				for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
				{
					FFloat16 Value;
					Value.Encoded = RawDataPtr[(Y*RawSize.X + X)*RawNumChannels + ChanIter];

					switch (ChanIter)
					{
					case 0: SrcColor.R = Value; break;
					case 1: SrcColor.G = Value; break;
					case 2: SrcColor.B = Value; break;
					case 3: SrcColor.A = Value; break;
					}
				}
				// convert to FColor using sRGB conversion
				*DestColor = SrcColor.ToFColor(true);
			}
		}

		QuantizedPixelData = MakeUnique<TImagePixelData<FColor>>(RawSize, TArray64<FColor>(MoveTemp(ClampedPixels)));
	}
	case 32:
	{
		TArray<FColor> ClampedPixels;
		ClampedPixels.SetNum(RawSize.X * RawSize.Y);

		int64 SizeInBytes = 0;
		const void* SrcRawDataPtr = nullptr;
		InPixelData->GetRawData(SrcRawDataPtr, SizeInBytes);

		const float* RawDataPtr = static_cast<const float*>(SrcRawDataPtr);

		// Copy pixels to new array
		for (int32 Y = 0; Y < RawSize.Y; Y++)
		{
			for (int32 X = 0; X < RawSize.X; X++)
			{
				FColor* DestColor = &ClampedPixels.GetData()[Y*RawSize.X + X];
				FLinearColor SrcColor;
				for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
				{
					float Value = RawDataPtr[(Y*RawSize.X + X)*RawNumChannels + ChanIter];

					switch (ChanIter)
					{
					case 0: SrcColor.R = Value; break;
					case 1: SrcColor.G = Value; break;
					case 2: SrcColor.B = Value; break;
					case 3: SrcColor.A = Value; break;
					}
				}
				// convert to FColor using sRGB conversion
				*DestColor = SrcColor.ToFColor(true);
			}
		}

		QuantizedPixelData = MakeUnique<TImagePixelData<FColor>>(RawSize, TArray64<FColor>(MoveTemp(ClampedPixels)));
		break;
	}

	default:
		// Unsupported source bit-depth, consider adding it!
		check(false);
	}

	return QuantizedPixelData;
}

TUniquePtr<FImagePixelData> QuantizeImagePixelDataToBitDepth(const FImagePixelData* InData, const int32 TargetBitDepth)
{
	TUniquePtr<FImagePixelData> QuantizedPixelData = nullptr;
	switch (TargetBitDepth)
	{
	case 8:
		// Convert to 8 bit FColor
		QuantizedPixelData = UE::MoviePipeline::QuantizePixelDataTo8bpp(InData);
		break;
	case 10:
	case 12:
	case 16:
		// Convert to 16 bit FFloat16Color
	case 32:
		// Convert to 32 bit FLinearColor

	default:
		// Unsupported bit-depth to convert from, please consider implementing!
		check(false);
	}

	return QuantizedPixelData;
}
}
}
