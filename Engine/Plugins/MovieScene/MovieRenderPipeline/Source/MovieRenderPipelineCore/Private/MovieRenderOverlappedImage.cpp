// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieRenderOverlappedImage.h"

#include "Math/Vector.h"
#include "Math/VectorRegister.h"

#include "MovieRenderPipelineCoreModule.h"
#include "Math/Float16.h"
#include "HAL/PlatformTime.h"

void FImageOverlappedPlane::Init(int32 InSizeX, int32 InSizeY)
{
	SizeX = InSizeX;
	SizeY = InSizeY;

	// leaves the data as is
	ChannelData.SetNumUninitialized(SizeX * SizeY);
}

void FImageOverlappedPlane::ZeroPlane()
{
	int32 Num = SizeX * SizeY;
	for (int32 Index = 0; Index < Num; Index++)
	{
		ChannelData[Index] = 0.0f;
	}
}

void FImageOverlappedPlane::Reset()
{
	SizeX = 0;
	SizeY = 0;
	ChannelData.Empty();
}

// a subpixel offset of (0.5,0.5) means that the raw data is exactly centered on destination pixels.
void FImageOverlappedPlane::AccumulateSinglePlane(const TArray64<float>& InRawData, const TArray64<float>& InWeightData, int32 InSizeX, int32 InSizeY, int32 InOffsetX, int32 InOffsetY,
								float SubpixelOffsetX, float SubpixelOffsetY,
								int32 SubRectOffsetX, int32 SubRectOffsetY,
								int32 SubRectSizeX, int32 SubRectSizeY)
{
	check(InRawData.Num() == InSizeX * InSizeY);
	check(InWeightData.Num() == InSizeX * InSizeY);

	check(SubpixelOffsetX >= 0.0f);
	check(SubpixelOffsetX <= 1.0f);
	check(SubpixelOffsetY >= 0.0f);
	check(SubpixelOffsetY <= 1.0f);

	// if the subpixel offset is less than 0.5, go back one pixel
	int32 StartX = (SubpixelOffsetX >= 0.5 ? InOffsetX : InOffsetX - 1);
	int32 StartY = (SubpixelOffsetY >= 0.5 ? InOffsetY : InOffsetY - 1);

	float PixelWeight[2][2];
	{
		// make sure that the equal sign is correct, if the subpixel offset is 0.5, the weightX is 0 and it starts on the center pixel
		float WeightX = FMath::Frac(SubpixelOffsetX + 0.5f);
		float WeightY = FMath::Frac(SubpixelOffsetY + 0.5f);

		// row, column
		PixelWeight[0][0] = (1.0f - WeightX) * (1.0f - WeightY);
		PixelWeight[0][1] = (       WeightX) * (1.0f - WeightY);
		PixelWeight[1][0] = (1.0f - WeightX) * (       WeightY);
		PixelWeight[1][1] = (       WeightX) * (       WeightY);
	}

	static bool bSlowReference = false;
	if (bSlowReference)
	{
		static bool bReversedReference = false;
		if (bReversedReference)
		{
			// This is the reversed reference. Instead of taking a tile and applying it to the accumulation, we start from the
			// accumulation point and figure out the source pixels that affect it. I.e. all the math is backwards, but it should
			// be faster.
			//
			// Given a position on the current tile (x,y), we apply the sum of the 4 points (x+0,y+0), (x+1,y+0), (x+0,y+1), (x+1,y+1) to the destination index.
			// So in reverse, given a destination position, our source sample positions are (x-1,y-1), (x+0,y-1), (x-1,y+0), (x+0,y+0).
			// That's why we make sure to start at a minimum of index 1, instead of 0.
			for (int CurrY = 1; CurrY < InSizeY; CurrY++)
			{
				for (int CurrX = 1; CurrX < InSizeX; CurrX++)
				{
					int DstY = StartY + CurrY;// +OffsetY;
					int DstX = StartX + CurrX;// +OffsetX;

					if (DstX >= 0 && DstY >= 0 &&
						DstX < SizeX && DstY < SizeY)
					{
						for (int OffsetY = 0; OffsetY < 2; OffsetY++)
						{
							for (int OffsetX = 0; OffsetX < 2; OffsetX++)
							{
								float Val = InRawData[(CurrY - 1 + OffsetY) * InSizeX + (CurrX - 1 + OffsetX)];
								float BaseWeight = InWeightData[(CurrY - 1 + OffsetY) * InSizeX + (CurrX - 1 + OffsetX)];

								float Weight = BaseWeight * PixelWeight[1-OffsetY][1-OffsetX];

								ChannelData[DstY * SizeX + DstX] += Weight * Val;
							}
						}
					}
				}
			}
		}
		else
		{
			// Slow, reference version. This is the main one.
			for (int CurrY = 0; CurrY < InSizeY; CurrY++)
			{
				for (int CurrX = 0; CurrX < InSizeX; CurrX++)
				{
					float Val = InRawData[CurrY * InSizeX + CurrX];
					float BaseWeight = InWeightData[CurrY * InSizeX + CurrX];

					for (int OffsetY = 0; OffsetY < 2; OffsetY++)
					{
						for (int OffsetX = 0; OffsetX < 2; OffsetX++)
						{
							int DstY = StartY + CurrY + OffsetY;
							int DstX = StartX + CurrX + OffsetX;

							float Weight = BaseWeight * PixelWeight[OffsetY][OffsetX];

							if (DstX >= 0 && DstY >= 0 &&
								DstX < SizeX && DstY < SizeY)
							{
								ChannelData[DstY * SizeX + DstX] += Weight * Val;
							}
						}
					}
				}
			}
		}
	}
	else
	{
		// The initial destination corners, may go off the destination window.
		int32 ActualDstX0 = StartX + SubRectOffsetX;
		int32 ActualDstY0 = StartY + SubRectOffsetY;
		int32 ActualDstX1 = StartX + SubRectOffsetX + SubRectSizeX;
		int32 ActualDstY1 = StartY + SubRectOffsetY + SubRectSizeY;

		ActualDstX0 = FMath::Clamp<int32>(ActualDstX0, 0, SizeX);
		ActualDstX1 = FMath::Clamp<int32>(ActualDstX1, 0, SizeX);

		ActualDstY0 = FMath::Clamp<int32>(ActualDstY0, 0, SizeY);
		ActualDstY1 = FMath::Clamp<int32>(ActualDstY1, 0, SizeY);

		const float PixelWeight00 = PixelWeight[0][0];
		const float PixelWeight01 = PixelWeight[0][1];
		const float PixelWeight10 = PixelWeight[1][0];
		const float PixelWeight11 = PixelWeight[1][1];

		static bool bRunVectorized = false;
		if (!bRunVectorized)
		{
			for (int32 DstY = ActualDstY0; DstY < ActualDstY1; DstY++)
			{
				int32 CurrY = DstY - StartY;
				int32 SrcY0 = CurrY + (0 - 1);
				int32 SrcY1 = CurrY + (1 - 1);

				const float * SrcLineRaw0 = &InRawData[SrcY0 * InSizeX];
				const float * SrcLineRaw1 = &InRawData[SrcY1 * InSizeX];

				const float * SrcLineWeight0 = &InWeightData[SrcY0 * InSizeX];
				const float * SrcLineWeight1 = &InWeightData[SrcY1 * InSizeX];

				float * DstLine = &ChannelData[DstY * SizeX];
				for (int32 DstX = ActualDstX0; DstX < ActualDstX1; DstX++)
				{
					int32 CurrX = DstX - StartX;
					int32 X0 = CurrX - 1 + 0;
					int32 X1 = CurrX - 1 + 1;

					float Sum = DstLine[DstX];

					Sum += SrcLineWeight0[X0] * PixelWeight[1][1] * SrcLineRaw0[X0];
					Sum += SrcLineWeight0[X1] * PixelWeight[1][0] * SrcLineRaw0[X1];
					Sum += SrcLineWeight1[X0] * PixelWeight[0][1] * SrcLineRaw1[X0];
					Sum += SrcLineWeight1[X1] * PixelWeight[0][0] * SrcLineRaw1[X1];

					DstLine[DstX] = Sum;
				}
			}
		}
		else
		{
			int32 ActualWidth = ActualDstX1 - ActualDstX0;

			// how many groups of 4
			int32 ActualWidthGroup4 = ActualWidth / 4;

			// extra 0-3
			int32 ActualWidthExtra4 = ActualWidth - 4 * ActualWidthGroup4;

			VectorRegister VecPixelWeight00 = VectorSetFloat1(PixelWeight[0][0]);
			VectorRegister VecPixelWeight01 = VectorSetFloat1(PixelWeight[0][1]);
			VectorRegister VecPixelWeight10 = VectorSetFloat1(PixelWeight[1][0]);
			VectorRegister VecPixelWeight11 = VectorSetFloat1(PixelWeight[1][1]);

			for (int32 DstY = ActualDstY0; DstY < ActualDstY1; DstY++)
			{
				int32 CurrY = DstY - StartY;
				int32 SrcY0 = CurrY + (0 - 1);
				int32 SrcY1 = CurrY + (1 - 1);

				const float * SrcLineRaw0 = &InRawData[SrcY0 * InSizeX];
				const float * SrcLineRaw1 = &InRawData[SrcY1 * InSizeX];

				const float * SrcLineWeight0 = &InWeightData[SrcY0 * InSizeX];
				const float * SrcLineWeight1 = &InWeightData[SrcY1 * InSizeX];

				float * DstLine = &ChannelData[DstY * SizeX];

				for (int32 GroupIter = 0; GroupIter < ActualWidthGroup4; GroupIter++)
				{
					{
						int32 BaseDstX = ActualDstX0 + GroupIter * 4;

						int32 BaseCurrX = BaseDstX - StartX;

						int32 BaseX0 = BaseCurrX - 1 + 0;
						int32 BaseX1 = BaseCurrX - 1 + 1;

						VectorRegister Sum = VectorLoad(&DstLine[BaseDstX]);
						
						Sum = VectorMultiplyAdd(VecPixelWeight11, VectorMultiply(VectorLoad(&SrcLineWeight0[BaseX0]), VectorLoad(&SrcLineRaw0[BaseX0])), Sum);
						Sum = VectorMultiplyAdd(VecPixelWeight10, VectorMultiply(VectorLoad(&SrcLineWeight0[BaseX1]), VectorLoad(&SrcLineRaw0[BaseX1])), Sum);
						Sum = VectorMultiplyAdd(VecPixelWeight01, VectorMultiply(VectorLoad(&SrcLineWeight1[BaseX0]), VectorLoad(&SrcLineRaw1[BaseX0])), Sum);
						Sum = VectorMultiplyAdd(VecPixelWeight00, VectorMultiply(VectorLoad(&SrcLineWeight1[BaseX1]), VectorLoad(&SrcLineRaw1[BaseX1])), Sum);

						VectorStore(Sum, &DstLine[BaseDstX]);
					}
#if 0
					// Scalar version of the same code for reference
					for (int32 Iter = 0; Iter < 4; Iter++)
					{
						int32 DstX = ActualDstX0 + GroupIter * 4 + Iter;

						int32 CurrX = DstX - StartX;
						int32 X0 = CurrX - 1 + 0;
						int32 X1 = CurrX - 1 + 1;

						float Sum = DstLine[DstX];

						Sum += PixelWeight11 * SrcLineWeight0[X0] * SrcLineRaw0[X0];
						Sum += PixelWeight10 * SrcLineWeight0[X1] * SrcLineRaw0[X1];
						Sum += PixelWeight01 * SrcLineWeight1[X0] * SrcLineRaw1[X0];
						Sum += PixelWeight00 * SrcLineWeight1[X1] * SrcLineRaw1[X1];

						DstLine[DstX] = Sum;
					}
#endif

				}

				for (int32 IterExtra = 0; IterExtra < ActualWidthExtra4; IterExtra++)
				{
					int32 DstX = ActualDstX0 + ActualWidthExtra4 * 4 + IterExtra;

					int32 CurrX = DstX - StartX;
					int32 X0 = CurrX - 1 + 0;
					int32 X1 = CurrX - 1 + 1;

					float Sum = DstLine[DstX];

					Sum += PixelWeight11 * SrcLineWeight0[X0] * SrcLineRaw0[X0];
					Sum += PixelWeight10 * SrcLineWeight0[X1] * SrcLineRaw0[X1];
					Sum += PixelWeight01 * SrcLineWeight1[X0] * SrcLineRaw1[X0];
					Sum += PixelWeight00 * SrcLineWeight1[X1] * SrcLineRaw1[X1];

					DstLine[DstX] = Sum;
				}

			}
		}
	}
}

void FImageOverlappedAccumulator::InitMemory(int InPlaneSizeX, int InPlaneSizeY, int InNumChannels)
{
	PlaneSizeX = InPlaneSizeX;
	PlaneSizeY = InPlaneSizeY;
	NumChannels = InNumChannels;

	ChannelPlanes.SetNum(NumChannels);

	for (int Channel = 0; Channel < NumChannels; Channel++)
	{
		ChannelPlanes[Channel].Init(PlaneSizeX, PlaneSizeY);
	}

	WeightPlane.Init(PlaneSizeX, PlaneSizeY);

	TileMaskData.Empty();
	TileMaskSizeX = 0;
	TileMaskSizeY = 0;
}

void FImageOverlappedAccumulator::ZeroPlanes()
{
	check(ChannelPlanes.Num() == NumChannels);

	for (int Channel = 0; Channel < NumChannels; Channel++)
	{
		ChannelPlanes[Channel].ZeroPlane();
	}

	WeightPlane.ZeroPlane();
}

void FImageOverlappedAccumulator::Reset()
{
	PlaneSizeX = 0;
	PlaneSizeY = 0;
	NumChannels = 0;

	// Let the desctructor clean up
	ChannelPlanes.Empty();
	WeightPlane.Reset();

	TileMaskData.Empty();
	TileMaskSizeX = 0;
	TileMaskSizeY = 0;
}


void FImageOverlappedAccumulator::GenerateTileWeight(TArray64<float>& Weights, int32 SizeX, int32 SizeY)
{
	Weights.SetNum(SizeX * SizeY);

	// we'll use a simple triangle filter, which goes from 1.0 at the center to 0.0 at 3/4 of the way to the tile edge.

	float ScaleX = 1.0f / (float(SizeX / 2) * .75f);
	float ScaleY = 1.0f / (float(SizeY / 2) * .75f);

	for (int PixY = 0; PixY < SizeY; PixY++)
	{
		for (int PixX = 0; PixX < SizeX; PixX++)
		{
			float Y = float(PixY) + .5f;
			float X = float(PixX) + .5f;

			float DistX = FMath::Abs(SizeX / 2 - X);
			float DistY = FMath::Abs(SizeY / 2 - Y);

			float WeightX = FMath::Clamp(1.0f - DistX * ScaleX, 0.0f, 1.0f);
			float WeightY = FMath::Clamp(1.0f - DistY * ScaleY, 0.0f, 1.0f);

			Weights[PixY * SizeX + PixX] = WeightX * WeightY;
		}
	}
}

// Any changes to GenerateTileWeight() need to propagate to this function. The subrect should be large enough to incorporate all nonzero values
void FImageOverlappedAccumulator::GetTileSubRect(int32 & SubRectOffsetX, int32 & SubRectOffsetY, int32 & SubRectSizeX, int32 & SubRectSizeY, const TArray64<float>& Weights, const int32 SizeX, const int32 SizeY)
{
	static bool bBruteForce = false;
	if (bBruteForce)
	{
		int32 MinX = SizeX-1;
		int32 MinY = SizeY-1;

		int32 MaxX = 0;
		int32 MaxY = 0;

		for (int32 Y = 0; Y < SizeY; Y++)
		{
			for (int32 X = 0; X < SizeX; X++)
			{
				const float Val = Weights[Y*SizeX + X];
				if (Val > 0.0f)
				{
					MinX = FMath::Min<int32>(MinX, X);
					MinY = FMath::Min<int32>(MinY, Y);

					MaxX = FMath::Max<int32>(MaxX, X);
					MaxY = FMath::Max<int32>(MaxY, Y);
				}
			}
		}

		SubRectOffsetX = MinX;
		SubRectOffsetY = MinY;
		SubRectSizeX = (MaxX - MinX) + 1;
		SubRectSizeY = (MaxY - MinY) + 1;
	}
	else
	{
		// We're using a traingle filter using the middle 75% of the screen. The filter starts at 12.5%, and ends at 87.5%. Then add one pixel
		// just in case for roundoff/precision error.
		int32 MinX = FMath::FloorToInt(0.125f * SizeX) - 1;
		int32 MinY = FMath::FloorToInt(0.125f * SizeY) - 1;
		int32 MaxX = FMath::CeilToInt(0.875f * SizeX) + 1;
		int32 MaxY = FMath::CeilToInt(0.875f * SizeY) + 1;

		SubRectOffsetX = MinX;
		SubRectOffsetY = MinY;
		SubRectSizeX = (MaxX - MinX) + 1;
		SubRectSizeY = (MaxY - MinY) + 1;
	}
}

void FImageOverlappedAccumulator::CheckTileSubRect(const TArray64<float>& Weights, const int32 SizeX, const int32 SizeY, int32 SubRectOffsetX, int32 SubRectOffsetY, int32 SubRectSizeX, int32 SubRectSizeY)
{
	for (int32 Y = 0; Y < SizeY; Y++)
	{
		for (int32 X = 0; X < SizeX; X++)
		{
			const float Val = Weights[Y*SizeX + X];
			if (Val > 0.0f)
			{
				check(SubRectOffsetX <= X);
				check(SubRectOffsetY <= Y);
				check(X < SubRectOffsetX + SubRectSizeX);
				check(Y < SubRectOffsetY + SubRectSizeY);
			}
		}
	}
}

void FImageOverlappedAccumulator::AccumulatePixelData(const FImagePixelData& InPixelData, int32 InTileOffsetX, int32 InTileOffsetY, FVector2D InSubpixelOffset)
{
	EImagePixelType Fmt = InPixelData.GetType();

	int32 RawNumChan = InPixelData.GetNumChannels();
	int32 RawBitDepth = InPixelData.GetBitDepth();
	FIntPoint RawSize = InPixelData.GetSize();

	int32 SizeInBytes = 0;
	const void* SrcRawDataPtr = nullptr;

	bool IsFetchOk = InPixelData.GetRawData(SrcRawDataPtr, SizeInBytes);
	check(IsFetchOk); // keeping the if below for now, but this really should always succeed

	if (IsFetchOk)
	{
		// hardcode to 4 channels (RGBA), even if we are only saving fewer channels
		TArray64<float> RawData[4];

		check(NumChannels >= 1);
		check(NumChannels <= 4);

		for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
		{
			RawData[ChanIter].SetNumUninitialized(RawSize.X * RawSize.Y);
		}

		const double AccumulateBeginTime = FPlatformTime::Seconds();
			
		// for now, only handle rgba8
		if (Fmt == EImagePixelType::Color && RawNumChan == 4 && RawBitDepth == 8)
		{
			const uint8* RawDataPtr = static_cast<const uint8*>(SrcRawDataPtr);

			static bool bIsReferenceUnpack = false;
			if (bIsReferenceUnpack)
			{
				// simple, slow unpack, takes about 10-15ms on a 1080p image
				for (int32 Y = 0; Y < RawSize.Y; Y++)
				{
					for (int32 X = 0; X < RawSize.X; X++)
					{
						int32 ChanReorder[4] = { 2, 1, 0, 3 };
						for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
						{
							int32 RawValue = RawDataPtr[(Y*RawSize.X + X)*RawNumChan + ChanIter];
							float Value = float(RawValue) / 255.0f;
							int32 Reorder = ChanReorder[ChanIter];
							RawData[Reorder][Y*RawSize.X + X] = Value;
						}
					}
				}
			}
			else
			{
				// slightly optimized, takes about 3-7ms on a 1080p image
				for (int32 Y = 0; Y < RawSize.Y; Y++)
				{
					const uint8* SrcRowDataPtr = &RawDataPtr[Y*RawSize.X*RawNumChan];

					float* DstRowDataR = &RawData[0][Y*RawSize.X];
					float* DstRowDataG = &RawData[1][Y*RawSize.X];
					float* DstRowDataB = &RawData[2][Y*RawSize.X];
					float* DstRowDataA = &RawData[3][Y*RawSize.X];

					VectorRegister ColorScale = MakeVectorRegister(1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f);

					// simple, one pixel at a time vectorized version, we could do better
					for (int32 X = 0; X < RawSize.X; X++)
					{
						VectorRegister Color = VectorLoadByte4(&SrcRowDataPtr[X*RawNumChan]);
						Color = VectorMultiply(Color, ColorScale); // multiply by 1/255

						const float* RawColorVec = reinterpret_cast<const float *>(&Color);
						DstRowDataR[X] = RawColorVec[2];
						DstRowDataG[X] = RawColorVec[1];
						DstRowDataB[X] = RawColorVec[0];
						DstRowDataA[X] = RawColorVec[3];
					}
				}
			}
		}
		else if (Fmt == EImagePixelType::Float16 && RawNumChan == 4 && RawBitDepth == 16)
		{
			const uint16* RawDataPtr = static_cast<const uint16*>(SrcRawDataPtr);

			// simple, slow unpack version for fp16, could make this faster
			for (int32 Y = 0; Y < RawSize.Y; Y++)
			{
				for (int32 X = 0; X < RawSize.X; X++)
				{
					for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
					{
						FFloat16 RawValue;
						RawValue.Encoded = RawDataPtr[(Y*RawSize.X + X)*RawNumChan + ChanIter];
						
						// c cast does the conversion from fp16 bits to float
						float Value = float(RawValue);
						RawData[ChanIter][Y*RawSize.X + X] = Value;
					}
				}
			}
		}
		else if (Fmt == EImagePixelType::Float32 && RawNumChan == 4 && RawBitDepth == 32)
		{
			const float* RawDataPtr = static_cast<const float*>(SrcRawDataPtr);

			// reference version for float
			for (int32 Y = 0; Y < RawSize.Y; Y++)
			{
				for (int32 X = 0; X < RawSize.X; X++)
				{
					for (int32 ChanIter = 0; ChanIter < 4; ChanIter++)
					{
						float Value = RawDataPtr[(Y*RawSize.X + X)*RawNumChan + ChanIter];
						RawData[ChanIter][Y*RawSize.X + X] = Value;
					}
				}
			}
		}
		else
		{
			check(0);
		}

		static bool bLogTiming = false;

		const double AccumulateEndTime = FPlatformTime::Seconds();
		if(bLogTiming)
		{
			const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime)*1000.0f);
			UE_LOG(LogTemp, Log, TEXT("    [%8.2f] Unpack time."), ElapsedMs);
		}
	
		if (AccumulationGamma != 1.0f)
		{
			// Unfortunately, we don't have an SSE optimized pow function. This function is quite slow (about 30-40ms).
			float Gamma = AccumulationGamma;

			for (int ChanIter = 0; ChanIter < NumChannels; ChanIter++)
			{
				float* DstData = RawData[ChanIter].GetData();
				int32 DstSize = RawSize.X * RawSize.Y;
				for (int32 Iter = 0; Iter < DstSize; Iter++)
				{
					DstData[Iter] = FMath::Pow(DstData[Iter], Gamma);
				}
			}
		} 

		const double GammaEndTime = FPlatformTime::Seconds();
		if (bLogTiming)
		{
			const float ElapsedMs = float((GammaEndTime - AccumulateEndTime)*1000.0f);
			UE_LOG(LogTemp, Log, TEXT("    [%8.2f] Gamma time."), ElapsedMs);
		}

		// Calculate weight data for this tile. If it is the same, use the cached size. In theory it is allowed
		// to have a bunch of tiles of different sizes, but so far we are only using the same size for all tiles.
		if (RawSize.X != TileMaskSizeX ||
			RawSize.Y != TileMaskSizeY ||
			TileMaskSizeX * TileMaskSizeY != TileMaskData.Num())
		{
			TileMaskSizeX = RawSize.X;
			TileMaskSizeY = RawSize.Y;

			GenerateTileWeight(TileMaskData, RawSize.X, RawSize.Y);
		}
		
		int SubRectOffsetX = 0;
		int SubRectOffsetY = 0;
		int SubRectSizeX = RawSize.X;
		int SubRectSizeY = RawSize.Y;
		GetTileSubRect(SubRectOffsetX, SubRectOffsetY, SubRectSizeX, SubRectSizeY, TileMaskData, RawSize.X, RawSize.Y);
		
		const bool bVerifySubRect = false;
		if (bVerifySubRect)
		{
			CheckTileSubRect(TileMaskData, RawSize.X, RawSize.Y, SubRectOffsetX, SubRectOffsetY, SubRectSizeX, SubRectSizeY);
		}

		const double TileEndTime = FPlatformTime::Seconds();
		if (bLogTiming)
		{
			const float ElapsedMs = float((TileEndTime - GammaEndTime)*1000.0f);
			UE_LOG(LogTemp, Log, TEXT("    [%8.2f] Tile time."), ElapsedMs);
		}


		for (int32 ChanIter = 0; ChanIter < NumChannels; ChanIter++)
		{
			ChannelPlanes[ChanIter].AccumulateSinglePlane(RawData[ChanIter], TileMaskData, RawSize.X, RawSize.Y, InTileOffsetX, InTileOffsetY, InSubpixelOffset.X, InSubpixelOffset.Y,
				SubRectOffsetX, SubRectOffsetY, SubRectSizeX, SubRectSizeY);
		}

		// Accumulate weights as well.
		{
			TArray64<float> VecOne;
			VecOne.SetNum(RawSize.X * RawSize.Y);
			for (int32 Index = 0; Index < VecOne.Num(); Index++)
			{
				VecOne[Index] = 1.0f;
			}

			WeightPlane.AccumulateSinglePlane(VecOne, TileMaskData, RawSize.X, RawSize.Y, InTileOffsetX, InTileOffsetY, InSubpixelOffset.X, InSubpixelOffset.Y,
				SubRectOffsetX, SubRectOffsetY, SubRectSizeX, SubRectSizeY);
		}

		const double PlaneEndTime = FPlatformTime::Seconds();
		if (bLogTiming)
		{
			const float ElapsedMs = float((PlaneEndTime - TileEndTime)*1000.0f);
			UE_LOG(LogTemp, Log, TEXT("    [%8.2f] Plane time."), ElapsedMs);
		}

	}
}

void FImageOverlappedAccumulator::FetchFullImageValue(float Rgba[4], int32 FullX, int32 FullY) const
{
	Rgba[0] = 0.0f;
	Rgba[1] = 0.0f;
	Rgba[2] = 0.0f;
	Rgba[3] = 1.0f;

	float RawWeight = WeightPlane.ChannelData[FullY * PlaneSizeX + FullX];

	float Scale = 1.0f / FMath::Max<float>(RawWeight, 0.0001f);

	for (int64 Chan = 0; Chan < NumChannels; Chan++)
	{
		float Val = ChannelPlanes[Chan].ChannelData[FullY * PlaneSizeX + FullX];

		Rgba[Chan] = Val * Scale;
	}

	if (AccumulationGamma != 1.0f)
	{
		float InvGamma = 1.0f / AccumulationGamma;
		Rgba[0] = FMath::Pow(Rgba[0], InvGamma);
		Rgba[1] = FMath::Pow(Rgba[1], InvGamma);
		Rgba[2] = FMath::Pow(Rgba[2], InvGamma);
		Rgba[3] = FMath::Pow(Rgba[3], InvGamma);
	}
}

void FImageOverlappedAccumulator::FetchFinalPixelDataByte(TArray<FColor> & OutPixelData) const
{
	int32 FullSizeX = PlaneSizeX;
	int32 FullSizeY = PlaneSizeY;
	OutPixelData.SetNumUninitialized(FullSizeX * FullSizeY);

	for (int32 FullY = 0L; FullY < FullSizeY; FullY++)
	{
		for (int32 FullX = 0L; FullX < FullSizeX; FullX++)
		{
			float Rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

			FetchFullImageValue(Rgba, FullX, FullY);
			FColor Color = FColor(Rgba[0]*255.0f,Rgba[1]*255.0f,Rgba[2]*255.0f,Rgba[3]*255.0f);

			// be careful with this index, make sure to use 64bit math, not 32bit
			int64 DstIndex = int64(FullY) * int64(FullSizeX) + int64(FullX);
			OutPixelData[DstIndex] = Color;
		}
	}
}

void FImageOverlappedAccumulator::FetchFinalPixelDataLinearColor(TArray<FLinearColor> & OutPixelData) const
{
	int32 FullSizeX = PlaneSizeX;
	int32 FullSizeY = PlaneSizeY;
	OutPixelData.SetNumUninitialized(FullSizeX * FullSizeY);

	for (int32 FullY = 0L; FullY < FullSizeY; FullY++)
	{
		for (int32 FullX = 0L; FullX < FullSizeX; FullX++)
		{
			float Rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

			FetchFullImageValue(Rgba, FullX, FullY);
			FLinearColor Color = FLinearColor(Rgba[0],Rgba[1],Rgba[2],Rgba[3]);

			// be careful with this index, make sure to use 64bit math, not 32bit
			int64 DstIndex = int64(FullY) * int64(FullSizeX) + int64(FullX);
			OutPixelData[DstIndex] = Color;
		}
	}
}

