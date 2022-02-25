// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureCompressorModule.h"
#include "Math/RandomStream.h"
#include "Containers/IndirectArray.h"
#include "Stats/Stats.h"
#include "Async/AsyncWork.h"
#include "Async/ParallelFor.h"
#include "Modules/ModuleManager.h"
#include "Engine/TextureDefines.h"
#include "TextureFormatManager.h"
#include "Interfaces/ITextureFormat.h"
#include "Misc/Paths.h"
#include "ImageCore.h"
#include <cmath>

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogTextureCompressor, Log, All);

/*------------------------------------------------------------------------------
	Mip-Map Generation
------------------------------------------------------------------------------*/

enum EMipGenAddressMode
{
	MGTAM_Wrap,
	MGTAM_Clamp,
	MGTAM_BorderBlack,
};

/**
 * 2D view into one slice of an image.
 */
struct FImageView2D
{
	/** Pointer to colors in the slice. */
	FLinearColor* SliceColors;
	/** Width of the slice. */
	int32 SizeX;
	/** Height of the slice. */
	int32 SizeY;

	FImageView2D() : SliceColors(nullptr), SizeX(0), SizeY(0) {}

	/** Initialization constructor. */
	FImageView2D(FImage& Image, int32 SliceIndex)
	{
		SizeX = Image.SizeX;
		SizeY = Image.SizeY;
		SliceColors = (&Image.AsRGBA32F()[0]) + SliceIndex * SizeY * SizeX;
	}

	/** Access a single texel. */
	FLinearColor& Access(int32 X, int32 Y)
	{
		return SliceColors[X + Y * SizeX];
	}

	/** Const access to a single texel. */
	const FLinearColor& Access(int32 X, int32 Y) const
	{
		return SliceColors[X + Y * SizeX];
	}

	bool IsValid() const { return SliceColors != nullptr; }

	static const FImageView2D ConstructConst(const FImage& Image, int32 SliceIndex)
	{
		return FImageView2D(const_cast<FImage&>(Image), SliceIndex);
	}

};

// 2D sample lookup with input conversion
// requires SourceImageData.SizeX and SourceImageData.SizeY to be power of two
template <EMipGenAddressMode AddressMode>
FLinearColor LookupSourceMip(const FImageView2D& SourceImageData, int32 X, int32 Y)
{
	if(AddressMode == MGTAM_Wrap)
	{
		// wrap
		X = (int32)((uint32)X) & (SourceImageData.SizeX - 1);
		Y = (int32)((uint32)Y) & (SourceImageData.SizeY - 1);
	}
	else if(AddressMode == MGTAM_Clamp)
	{
		// clamp
		X = FMath::Clamp(X, 0, SourceImageData.SizeX - 1);
		Y = FMath::Clamp(Y, 0, SourceImageData.SizeY - 1);
	}
	else if(AddressMode == MGTAM_BorderBlack)
	{
		// border color 0
		if((uint32)X >= (uint32)SourceImageData.SizeX
			|| (uint32)Y >= (uint32)SourceImageData.SizeY)
		{
			return FLinearColor(0, 0, 0, 0);
		}
	}
	else
	{
		check(0);
	}
	//return *(SourceImageData.AsRGBA32F() + X + Y * SourceImageData.SizeX);
	return SourceImageData.Access(X,Y);
}

// Kernel class for image filtering operations like image downsampling
// at max MaxKernelExtend x MaxKernelExtend
class FImageKernel2D
{
public:
	FImageKernel2D() :FilterTableSize(0)
	{
	}

	// @param TableSize1D 2 for 2x2, 4 for 4x4, 6 for 6x6, 8 for 8x8
	// @param SharpenFactor can be negative to blur
	// generate normalized 2D Kernel with sharpening
	void BuildSeparatableGaussWithSharpen(uint32 TableSize1D, float SharpenFactor = 0.0f)
	{
		if(TableSize1D > MaxKernelExtend)
		{
			TableSize1D = MaxKernelExtend;
		}

		float Table1D[MaxKernelExtend];
		float NegativeTable1D[MaxKernelExtend];

		FilterTableSize = TableSize1D;
		
		if(TableSize1D == 2)
		{
			// 2x2 kernel: simple average
			// SharpenFactor is ignored
			// this is TMGS_SimpleAverage
			KernelWeights[0] = KernelWeights[1] = KernelWeights[2] = KernelWeights[3] = 0.25f;
			return;
		}
		else if(SharpenFactor < 0.0f)
		{
			// blur only
			// this is TMGS_Blur

			// TMGS_Blur will always give us TableSize > 2
			check( TableSize1D > 2 );

			BuildGaussian1D(Table1D, TableSize1D, 1.0f, -SharpenFactor);
			BuildFilterTable2DFrom1D(KernelWeights, Table1D, TableSize1D);
			return;
		}
		else if(TableSize1D == 4)
		{
			// 4x4 kernel with sharpen or blur: can alias a bit
			// this is not used by standard TMGS_ mip options
			//  one thing that can get you in here is GenerateTopMip
			//	because it takes the standard 8 size and does /2
			BuildFilterTable1DBase(Table1D, TableSize1D, 1.0f + SharpenFactor);
			BuildFilterTable1DBase(NegativeTable1D, TableSize1D, -SharpenFactor);
			BlurFilterTable1D(NegativeTable1D, TableSize1D, 1);
		}
		else if(TableSize1D == 6)
		{
			// 6x6 kernel with sharpen or blur: still can alias
			// this is not used by standard TMGS_ mip options
			BuildFilterTable1DBase(Table1D, TableSize1D, 1.0f + SharpenFactor);
			BuildFilterTable1DBase(NegativeTable1D, TableSize1D, -SharpenFactor);
			BlurFilterTable1D(NegativeTable1D, TableSize1D, 2);
		}
		else if(TableSize1D == 8)
		{
			//8x8 kernel with sharpen
			// these are the TMGS_Sharpen filters

			// * 2 to get similar appearance as for TableSize 6
			SharpenFactor = SharpenFactor * 2.0f;

			BuildFilterTable1DBase(Table1D, TableSize1D, 1.0f + SharpenFactor);
			// positive lobe is blurred a bit for better quality
			BlurFilterTable1D(Table1D, TableSize1D, 1);
			BuildFilterTable1DBase(NegativeTable1D, TableSize1D, -SharpenFactor);
			BlurFilterTable1D(NegativeTable1D, TableSize1D, 3);
		}
		else 
		{
			// not yet supported
			check(0);
		}

		AddFilterTable1D(Table1D, NegativeTable1D, TableSize1D);
		BuildFilterTable2DFrom1D(KernelWeights, Table1D, TableSize1D);
	}

	inline uint32 GetFilterTableSize() const
	{
		return FilterTableSize;
	}

	inline float GetAt(uint32 X, uint32 Y) const
	{
		checkSlow(X < FilterTableSize);
		checkSlow(Y < FilterTableSize);
		return KernelWeights[X + Y * FilterTableSize];
	}

	inline float& GetRefAt(uint32 X, uint32 Y)
	{
		checkSlow(X < FilterTableSize);
		checkSlow(Y < FilterTableSize);
		return KernelWeights[X + Y * FilterTableSize];
	}

private:

	inline static float NormalDistribution(float X, float Variance)
	{
		const float StandardDeviation = FMath::Sqrt(Variance);
		return FMath::Exp(-FMath::Square(X) / (2.0f * Variance)) / (StandardDeviation * FMath::Sqrt(2.0f * (float)PI));
	}

	// support even and non even sized filters
	static void BuildGaussian1D(float *InOutTable, uint32 TableSize, float Sum, float Variance)
	{
		float Center = TableSize * 0.5f - 0.5f;
		float CurrentSum = 0;
		for(uint32 i = 0; i < TableSize; ++i)
		{
			float Actual = NormalDistribution(i - Center, Variance);
			InOutTable[i] = Actual;
			CurrentSum += Actual;
		}
		// Normalize
		float InvSum = Sum / CurrentSum;
		for(uint32 i = 0; i < TableSize; ++i)
		{
			InOutTable[i] *= InvSum;
		}
	}

	//
	static void BuildFilterTable1DBase(float *InOutTable, uint32 TableSize, float Sum )
	{
		// we require a even sized filter
		check(TableSize % 2 == 0);

		float Inner = 0.5f * Sum;

		uint32 Center = TableSize / 2;
		for(uint32 x = 0; x < TableSize; ++x)
		{
			if(x == Center || x == Center - 1)
			{
				// center elements
				InOutTable[x] = Inner;
			}
			else
			{
				// outer elements
				InOutTable[x] = 0.0f;
			}
		}
	}

	// InOutTable += InTable
	static void AddFilterTable1D( float *InOutTable, float *InTable, uint32 TableSize )
	{
		for(uint32 x = 0; x < TableSize; ++x)
		{
			InOutTable[x] += InTable[x];
		}
	}

	// @param Times 1:box, 2:triangle, 3:pow2, 4:pow3, ...
	// can be optimized with double buffering but doesn't need to be fast
	static void BlurFilterTable1D( float *InOutTable, uint32 TableSize, uint32 Times )
	{
		check(Times>0);
		check(TableSize<32);

		float Intermediate[32];

		for(uint32 Pass = 0; Pass < Times; ++Pass)
		{
			for(uint32 x = 0; x < TableSize; ++x)
			{
				Intermediate[x] = InOutTable[x];
			}

			for(uint32 x = 0; x < TableSize; ++x)
			{
				float sum = Intermediate[x];

				if(x)
				{
					sum += Intermediate[x-1];	
				}
				if(x < TableSize - 1)
				{
					sum += Intermediate[x+1];	
				}

				InOutTable[x] = sum / 3.0f;
			}
		}
	}

	static void BuildFilterTable2DFrom1D( float *OutTable2D, float *InTable1D, uint32 TableSize )
	{
		for(uint32 y = 0; y < TableSize; ++y)
		{
			for(uint32 x = 0; x < TableSize; ++x)
			{
				OutTable2D[x + y * TableSize] = InTable1D[y] * InTable1D[x];
			}
		}
	}

	// at max we support MaxKernelExtend x MaxKernelExtend kernels
	const static uint32 MaxKernelExtend = 12;
	// 0 if no kernel was setup yet
	uint32 FilterTableSize;
	// normalized, means the sum of it should be 1.0f
	float KernelWeights[MaxKernelExtend * MaxKernelExtend];
};

static float DetermineScaledThreshold(float Threshold, float Scale)
{
	check(Threshold > 0.f && Scale > 0.f);

	// Assuming Scale > 0 and Threshold > 0, find ScaledThreshold such that
	//	 x * Scale >= Threshold
	// is exactly equivalent to
	//	 x >= ScaledThreshold.
	//
	// This is for a test that was originally written in the first form that we want to
	// transform to the second form without changing results (which would in turn change
	// texture cooks).
	//
	// In exact arithmetic, this is just ScaledThreshold = Threshold / Scale.
	//
	// In floating point, we need to consider rounding. Computed in floating point
	// and assuming round-to-nearest (breaking ties towards even), we get 
	//
	//	 RN(x * Scale) >= Threshold
	//
	// The smallest conceivable x that passes RN(x * Scale) >= Threshold is
	// x = (Threshold - 0.5u) / Scale, landing exactly halfway with the rounding
	// going up; this is slightly less than an exact Threshold/Scale.
	//
	// For regular floating point division, we get
	//	 RN(Threshold / Scale)
	// = (Threshold / Scale) * (1 + e),  |e| < 0.5u (the inequality is strict for divisions)
	//
	// That gets us relatively close to the target value, but we have no guarantee that rounding
	// on the division was in the direction we wanted. Just check whether our target inequality
	// is satisfied and bump up or down to the next representable float as required.
	float ScaledThreshold = Threshold / Scale;
	float SteppedDown = std::nextafter(ScaledThreshold, 0.f);

	// We want ScaledThreshold to be the smallest float such that
	//	 ScaledThreshold * Scale >= Threshold
	// meaning the next-smaller float below ScaledThreshold (which is SteppedDown)
	// should not be >=Threshold. 

	if (SteppedDown * Scale >= Threshold)
	{
		// We were too large, go down by 1 ulp
		ScaledThreshold = SteppedDown;
	}
	else if (ScaledThreshold * Scale < Threshold)
	{
		// We were too small, go up by 1 ulp
		ScaledThreshold = std::nextafter(ScaledThreshold, 2.f * ScaledThreshold);
	}

	// We should now have the right threshold:
	check(ScaledThreshold * Scale >= Threshold); // ScaledThreshold is large enough
	check(std::nextafter(ScaledThreshold, 0.f) * Scale < Threshold); // next below is too small

	return ScaledThreshold;
}


static FVector4f ComputeAlphaCoverage(const FVector4f Thresholds, const FVector4f Scales, const FImageView2D& SourceImageData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.ComputeAlphaCoverage);

	FVector4f Coverage(0, 0, 0, 0);

	int32 NumRowsEachJob;
	int32 NumJobs = ImageParallelForComputeNumJobsForRows(NumRowsEachJob,SourceImageData.SizeX,SourceImageData.SizeY);

	if ( Thresholds[0] == 0.f && Thresholds[1] == 0.f && Thresholds[2] == 0.f )
	{
		// common case that only channel 3 (A) is used for alpha coverage :
		
		check( Thresholds[3] != 0.f );

		const float ThresholdScaled = DetermineScaledThreshold(Thresholds[3] , Scales[3]);
		
		int32 CommonResult = 0;
		ParallelFor( TEXT("Texture.ComputeAlphaCoverage.PF"),NumJobs,1, [&](int32 Index)
		{
			int32 StartIndex = Index * NumRowsEachJob;
			int32 EndIndex = FMath::Min(StartIndex + NumRowsEachJob, SourceImageData.SizeY);
			int32 LocalCoverage = 0;
			for (int32 y = StartIndex; y < EndIndex; ++y)
			{
				const FLinearColor * RowPixels = &SourceImageData.Access(0,y);

				for (int32 x = 0; x < SourceImageData.SizeX; ++x)
				{
					LocalCoverage += (RowPixels[x].A >= ThresholdScaled);
				}
			}

			FPlatformAtomics::InterlockedAdd(&CommonResult, LocalCoverage);
		});

		Coverage[3] = float(CommonResult) / float(SourceImageData.SizeX * SourceImageData.SizeY);
		
		UE_LOG(LogTextureCompressor, VeryVerbose, TEXT("Thresholds = 000 %f Coverage = 000 %f"),  \
			Thresholds[3], Coverage[3] );
	}
	else
	{
		FVector4f ThresholdsScaled;

		for (int32 i = 0; i < 4; ++i)
		{
			// Skip channel if Threshold is 0
			if (Thresholds[i] == 0)
			{
				// stuff a value that we will always be less than
				ThresholdsScaled[i] = FLT_MAX;
			}
			else
			{
				check( Scales[i] != 0.f );
				ThresholdsScaled[i] = DetermineScaledThreshold( Thresholds[i] , Scales[i] );
			}
		}

		int32 CommonResults[4] = { 0, 0, 0, 0 };
		ParallelFor( TEXT("Texture.ComputeAlphaCoverage.PF"),NumJobs,1, [&](int32 Index)
		{
			int32 StartIndex = Index * NumRowsEachJob;
			int32 EndIndex = FMath::Min(StartIndex + NumRowsEachJob, SourceImageData.SizeY);
			int32 LocalCoverage[4] = { 0, 0, 0, 0 };
			for (int32 y = StartIndex; y < EndIndex; ++y)
			{
				const FLinearColor * RowPixels = &SourceImageData.Access(0,y);

				for (int32 x = 0; x < SourceImageData.SizeX; ++x)
				{
					const FLinearColor & PixelValue = RowPixels[x];

					// Calculate coverage for each channel
					for (int32 i = 0; i < 4; ++i)
					{
						LocalCoverage[i] += ( PixelValue.Component(i) >= ThresholdsScaled[i] );
					}
				}
			}

			for (int32 i = 0; i < 4; ++i)
			{
				FPlatformAtomics::InterlockedAdd(&CommonResults[i], LocalCoverage[i]);
			}
		});

		for (int32 i = 0; i < 4; ++i)
		{
			Coverage[i] = float(CommonResults[i]) / float(SourceImageData.SizeX * SourceImageData.SizeY);
		}
		
		UE_LOG(LogTextureCompressor, VeryVerbose, TEXT("Thresholds = %f %f %f %f Coverage = %f %f %f %f"),  \
			Thresholds[0], Thresholds[1], Thresholds[2], Thresholds[3], \
			Coverage[0], Coverage[1], Coverage[2], Coverage[3] );
	}
	
	return Coverage;
}

static FVector4f ComputeAlphaScale(const FVector4f Coverages, const FVector4f AlphaThresholds, const FImageView2D& SourceImageData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.ComputeAlphaScale);

	// This function is not a good way to do this
	// but we cannot change it without changing output pixels
	// A better method would be to histogram the channel and scale the histogram to meet the desired threshold
	// even if using this binary search method, you should remember which value gave the closest result
	//	 don't assume that each binary search step is an improvement
	// 

	FVector4f MinAlphaScales (0, 0, 0, 0);
	FVector4f MaxAlphaScales (4, 4, 4, 4);
	FVector4f AlphaScales (1, 1, 1, 1);

	//Binary Search to find Alpha Scale
	// limit binary search to 8 steps
	for (int32 i = 0; i < 8; ++i)
	{
		FVector4f ComputedCoverages = ComputeAlphaCoverage(AlphaThresholds, AlphaScales, SourceImageData);
		
		UE_LOG(LogTextureCompressor, VeryVerbose, TEXT("Tried AlphaScale = %f ComputedCoverage = %f Goal = %f"), AlphaScales[3], ComputedCoverages[3], Coverages[3] );

		for (int32 j = 0; j < 4; ++j)
		{
			if (AlphaThresholds[j] == 0 || fabsf(ComputedCoverages[j] - Coverages[j]) < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			if (ComputedCoverages[j] < Coverages[j])
			{
				MinAlphaScales[j] = AlphaScales[j];
			}
			else if (ComputedCoverages[j] > Coverages[j])
			{
				MaxAlphaScales[j] = AlphaScales[j];
			}

			// guess alphascale is best at next midpoint :
			//  this means we wind up returning an alphascale value we have never tested
			AlphaScales[j] = (MinAlphaScales[j] + MaxAlphaScales[j]) * 0.5f;
		}

		// Equals default tolerance is KINDA_SMALL_NUMBER so it checks the same condition as above
		if (ComputedCoverages.Equals(Coverages))
		{
			break;
		}
	}

	UE_LOG(LogTextureCompressor, VeryVerbose, TEXT("Final AlphaScales = %f %f %f %f"), AlphaScales[0], AlphaScales[1], AlphaScales[2], AlphaScales[3] );

	return AlphaScales;
}


static void GenerateMip2x2Simple(
	const FImageView2D& SourceImageData, 
	FImageView2D& DestImageData)
{
	int32 NumRowsEachJob;
	int32 NumJobs = ImageParallelForComputeNumJobsForRows(NumRowsEachJob,DestImageData.SizeX,DestImageData.SizeY);

	ParallelFor( TEXT("Texture.GenerateMip2x2Simple.PF"),NumJobs,1, [&](int32 Index)
	{
		int32 StartIndex = Index * NumRowsEachJob;
		int32 EndIndex = FMath::Min(StartIndex + NumRowsEachJob, DestImageData.SizeY);
		for (int32 DestY = StartIndex; DestY < EndIndex; ++DestY)
		{
			FLinearColor * DestRow = &DestImageData.Access(0,DestY);
			const FLinearColor * SourceRow0 = &SourceImageData.Access(0,2*DestY);
			const FLinearColor * SourceRow1 = &SourceImageData.Access(0,2*DestY+1);

			for ( int32 DestX = 0;DestX < DestImageData.SizeX; DestX++ )
			{
				DestRow[DestX] = (SourceRow0[0] + SourceRow0[1] + SourceRow1[0] + SourceRow1[1]) * 0.25f;
				SourceRow0 += 2;
				SourceRow1 += 2;
			}
		}
	});
}

/**
* Generates a mip-map for an 2D B8G8R8A8 image using a 4x4 filter with sharpening
* @param SourceImageData - The source image's data.
* @param DestImageData - The destination image's data.
* @param ImageFormat - The format of both the source and destination images.
* @param FilterTable2D - [FilterTableSize * FilterTableSize]
* @param FilterTableSize - >= 2
* @param ScaleFactor 1 / 2:for downsampling
*/
template <EMipGenAddressMode AddressMode>
static void GenerateSharpenedMipB8G8R8A8Templ(
	const FImageView2D& SourceImageData, 
	FImageView2D& DestImageData, 
	bool bDoScaleMipsForAlphaCoverage,
	const FVector4f AlphaCoverages,
	const FVector4f AlphaThresholds,
	const FImageKernel2D& Kernel,
	uint32 ScaleFactor,
	bool bSharpenWithoutColorShift,
	bool bUnfiltered)
{
	check( SourceImageData.SizeX == ScaleFactor * DestImageData.SizeX || DestImageData.SizeX == 1 );
	check( SourceImageData.SizeY == ScaleFactor * DestImageData.SizeY || DestImageData.SizeY == 1 );
	
	int32 KernelFilterTableSize = (int32) Kernel.GetFilterTableSize();

	checkf( KernelFilterTableSize >= 2, TEXT("Kernel table size %d, expected at least 2!"), KernelFilterTableSize);
	if ( KernelFilterTableSize == 2 )
	{
		// 2x2 is always box filter
		check( Kernel.GetAt(0,0) == 0.25f );
	}
	
	if ( KernelFilterTableSize == 2 &&
		ScaleFactor == 2 &&
		DestImageData.SizeX*2 == SourceImageData.SizeX &&
		DestImageData.SizeY*2 == SourceImageData.SizeY &&
		! bDoScaleMipsForAlphaCoverage &&
		! bUnfiltered )
	{
		// bSharpenWithoutColorShift is ignored for 2x2 filter
		GenerateMip2x2Simple(SourceImageData,DestImageData);
		return;
	}

	// if KernelFilterTableSize is odd, centered in-place filter can be applied
	// KernelFilterTableSize should be even for standard down-sampling
	const int32 KernelCenter = (KernelFilterTableSize - 1) / 2;

	FVector4f AlphaScale(1, 1, 1, 1);
	if (bDoScaleMipsForAlphaCoverage)
	{
		AlphaScale = ComputeAlphaScale(AlphaCoverages, AlphaThresholds, SourceImageData);
	}
	
	int32 NumRowsEachJob;
	int32 NumJobs = ImageParallelForComputeNumJobsForRows(NumRowsEachJob,DestImageData.SizeX,DestImageData.SizeY);

	ParallelFor( TEXT("Texture.GenerateSharpenedMip.PF"),NumJobs,1, [&](int32 Index)
	{
		int32 StartIndex = Index * NumRowsEachJob;
		int32 EndIndex = FMath::Min(StartIndex + NumRowsEachJob, DestImageData.SizeY);
		for (int32 DestY = StartIndex; DestY < EndIndex; ++DestY)
		{
			for ( int32 DestX = 0;DestX < DestImageData.SizeX; DestX++ )
			{
				const int32 SourceX = DestX * ScaleFactor;
				const int32 SourceY = DestY * ScaleFactor;

				FLinearColor FilteredColor(0, 0, 0, 0);

				if ( bUnfiltered )
				{
					FilteredColor = LookupSourceMip<AddressMode>(SourceImageData, SourceX + 0, SourceY + 0);
				}
				else if ( KernelFilterTableSize == 2 )
				{
					// simple 2x2 kernel to compute the color
					// check for this case early so that bSharpenWithoutColorShift isn't run on 2x2

					FilteredColor =
						( LookupSourceMip<AddressMode>( SourceImageData, SourceX + 0, SourceY + 0 )
						+ LookupSourceMip<AddressMode>( SourceImageData, SourceX + 1, SourceY + 0 )
						+ LookupSourceMip<AddressMode>( SourceImageData, SourceX + 0, SourceY + 1 )
						+ LookupSourceMip<AddressMode>( SourceImageData, SourceX + 1, SourceY + 1 ) ) * 0.25f;
				}
				else if ( bSharpenWithoutColorShift )
				{
					// don't use this if KernelFilterTableSize == 2 (box)
					//	because it just does the same thing twice

					FLinearColor SharpenedColor(0, 0, 0, 0);

					for ( int32 KernelY = 0; KernelY < KernelFilterTableSize;  ++KernelY )
					{
						for ( int32 KernelX = 0; KernelX < KernelFilterTableSize;  ++KernelX )
						{
							float Weight = Kernel.GetAt( KernelX, KernelY );
							FLinearColor Sample = LookupSourceMip<AddressMode>( SourceImageData, SourceX + KernelX - KernelCenter, SourceY + KernelY - KernelCenter );
							SharpenedColor += Weight * Sample;
						}
					}

					float NewLuminance = SharpenedColor.GetLuminance();

					// simple 2x2 kernel to compute the color
					FilteredColor =
						( LookupSourceMip<AddressMode>( SourceImageData, SourceX + 0, SourceY + 0 )
						+ LookupSourceMip<AddressMode>( SourceImageData, SourceX + 1, SourceY + 0 )
						+ LookupSourceMip<AddressMode>( SourceImageData, SourceX + 0, SourceY + 1 )
						+ LookupSourceMip<AddressMode>( SourceImageData, SourceX + 1, SourceY + 1 ) ) * 0.25f;

					float OldLuminance = FilteredColor.GetLuminance();

					if ( OldLuminance > 0.001f )
					{
						float Factor = NewLuminance / OldLuminance;
						FilteredColor.R *= Factor;
						FilteredColor.G *= Factor;
						FilteredColor.B *= Factor;
					}

					// We also want to sharpen the alpha channel (was missing before)
					FilteredColor.A = SharpenedColor.A;
				}
				else
				{
					for ( int32 KernelY = 0; KernelY < KernelFilterTableSize;  ++KernelY )
					{
						for ( int32 KernelX = 0; KernelX < KernelFilterTableSize;  ++KernelX )
						{
							float Weight = Kernel.GetAt( KernelX, KernelY );
							FLinearColor Sample = LookupSourceMip<AddressMode>( SourceImageData, SourceX + KernelX - KernelCenter, SourceY + KernelY - KernelCenter );
							FilteredColor += Weight	* Sample;
						}
					}
				}

				// Apply computed alpha scales to each channel		
				FilteredColor.R *= AlphaScale.X;
				FilteredColor.G *= AlphaScale.Y;
				FilteredColor.B *= AlphaScale.Z;
				FilteredColor.A *= AlphaScale.W;

				// Set the destination pixel.
				DestImageData.Access(DestX, DestY) = FilteredColor;
			}
		}
	});
}

// to switch conveniently between different texture wrapping modes for the mip map generation
// the template can optimize the inner loop using a constant AddressMode
static void GenerateSharpenedMipB8G8R8A8(
	const FImageView2D& SourceImageData, 
	const FImageView2D& SourceImageData2, // Only used with volume texture.
	FImageView2D& DestImageData, 
	EMipGenAddressMode AddressMode, 
	bool bDoScaleMipsForAlphaCoverage,
	FVector4f AlphaCoverages,
	FVector4f AlphaThresholds,
	const FImageKernel2D &Kernel,
	uint32 ScaleFactor,
	bool bSharpenWithoutColorShift,
	bool bUnfiltered
	)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.GenerateSharpenedMip);

	switch(AddressMode)
	{
	case MGTAM_Wrap:
		GenerateSharpenedMipB8G8R8A8Templ<MGTAM_Wrap>(SourceImageData, DestImageData, bDoScaleMipsForAlphaCoverage, AlphaCoverages, AlphaThresholds, Kernel, ScaleFactor, bSharpenWithoutColorShift, bUnfiltered);
		break;
	case MGTAM_Clamp:
		GenerateSharpenedMipB8G8R8A8Templ<MGTAM_Clamp>(SourceImageData, DestImageData, bDoScaleMipsForAlphaCoverage, AlphaCoverages, AlphaThresholds, Kernel, ScaleFactor, bSharpenWithoutColorShift, bUnfiltered);
		break;
	case MGTAM_BorderBlack:
		GenerateSharpenedMipB8G8R8A8Templ<MGTAM_BorderBlack>(SourceImageData, DestImageData, bDoScaleMipsForAlphaCoverage, AlphaCoverages, AlphaThresholds, Kernel, ScaleFactor, bSharpenWithoutColorShift, bUnfiltered);
		break;
	default:
		check(0);
	}

	// For volume texture, do the average between the 2.
	if (SourceImageData2.IsValid() && !bUnfiltered)
	{
		FImage Temp(DestImageData.SizeX, DestImageData.SizeY, 1, ERawImageFormat::RGBA32F);
		FImageView2D TempImageData (Temp, 0);

		switch(AddressMode)
		{
		case MGTAM_Wrap:
			GenerateSharpenedMipB8G8R8A8Templ<MGTAM_Wrap>(SourceImageData2, TempImageData, bDoScaleMipsForAlphaCoverage, AlphaCoverages, AlphaThresholds, Kernel, ScaleFactor, bSharpenWithoutColorShift, bUnfiltered);
			break;
		case MGTAM_Clamp:
			GenerateSharpenedMipB8G8R8A8Templ<MGTAM_Clamp>(SourceImageData2, TempImageData, bDoScaleMipsForAlphaCoverage, AlphaCoverages, AlphaThresholds, Kernel, ScaleFactor, bSharpenWithoutColorShift, bUnfiltered);
			break;
		case MGTAM_BorderBlack:
			GenerateSharpenedMipB8G8R8A8Templ<MGTAM_BorderBlack>(SourceImageData2, TempImageData, bDoScaleMipsForAlphaCoverage, AlphaCoverages, AlphaThresholds, Kernel, ScaleFactor, bSharpenWithoutColorShift, bUnfiltered);
			break;
		default:
			check(0);
		}

		const int32 NumColors = DestImageData.SizeX * DestImageData.SizeY;
		for (int32 ColorIndex = 0; ColorIndex < NumColors; ++ColorIndex)
		{
			DestImageData.SliceColors[ColorIndex] =
				(DestImageData.SliceColors[ColorIndex] + TempImageData.SliceColors[ColorIndex]) * 0.5f;
		}
	}
}

// Update border texels after normal mip map generation to preserve the colors there (useful for particles and decals).
static void GenerateMipBorder(
	const FImageView2D& SrcImageData, 
	FImageView2D& DestImageData
	)
{
	check( SrcImageData.SizeX == 2 * DestImageData.SizeX || DestImageData.SizeX == 1 );
	check( SrcImageData.SizeY == 2 * DestImageData.SizeY || DestImageData.SizeY == 1 );

	for ( int32 DestY = 0; DestY < DestImageData.SizeY; DestY++ )
	{
		for ( int32 DestX = 0; DestX < DestImageData.SizeX; )
		{
			FLinearColor FilteredColor(0, 0, 0, 0);
			{
				float WeightSum = 0.0f;
				for ( int32 KernelY = 0; KernelY < 2;  ++KernelY )
				{
					for ( int32 KernelX = 0; KernelX < 2;  ++KernelX )
					{
						const int32 SourceX = DestX * 2 + KernelX;
						const int32 SourceY = DestY * 2 + KernelY;

						// only average the source border
						if ( SourceX == 0 ||
							SourceX == SrcImageData.SizeX - 1 ||
							SourceY == 0 ||
							SourceY == SrcImageData.SizeY - 1 )
						{
							FLinearColor Sample = LookupSourceMip<MGTAM_Wrap>( SrcImageData, SourceX, SourceY );
							FilteredColor += Sample;
							WeightSum += 1.0f;
						}
					}
				}
				FilteredColor /= WeightSum;
			}

			// Set the destination pixel.
			//FLinearColor& DestColor = *(DestImageData.AsRGBA32F() + DestX + DestY * DestImageData.SizeX);
			FLinearColor& DestColor = DestImageData.Access(DestX, DestY);
			DestColor = FilteredColor;

			++DestX;

			if ( DestY > 0 &&
				DestY < DestImageData.SizeY - 1 &&
				DestX > 0 &&
				DestX < DestImageData.SizeX - 1 )
			{
				// jump over the non border area
				DestX += FMath::Max( 1, DestImageData.SizeX - 2 );
			}
		}
	}
}

// how should be treat lookups outside of the image
static EMipGenAddressMode ComputeAdressMode(const FTextureBuildSettings& Settings)
{
	EMipGenAddressMode AddressMode = MGTAM_Wrap;

	if(Settings.bPreserveBorder)
	{
		AddressMode = Settings.bBorderColorBlack ? MGTAM_BorderBlack : MGTAM_Clamp;
	}

	return AddressMode;
}

static void GenerateTopMip(const FImage& SrcImage, FImage& DestImage, const FTextureBuildSettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.GenerateTopMip);

	// GenerateTopMip is only used for ApplyCompositeTexture
	// bApplyKernelToTopMip is not exposed to Texture GUI

	EMipGenAddressMode AddressMode = ComputeAdressMode(Settings);

	FImageKernel2D KernelDownsample;

	if ( Settings.MipSharpening < 0.f )
	{
		// negative Sharpening is a Gaussian
		//  this can make centered ("odd") filters, so the image doesn't shift
		int32 OddMipKernelSize = Settings.SharpenMipKernelSize | 1;
		KernelDownsample.BuildSeparatableGaussWithSharpen( OddMipKernelSize, Settings.MipSharpening );
	}
	else
	{	
		// non-Gaussians only support "even" filters
		//	this causes a half-pixel shift of the top mip
		// warn but then go ahead and do as requested
		UE_LOG(LogTextureCompressor, Warning, TEXT("GenerateTopMip used with non-Gaussian blur filter will cause half pixel shift"));

		KernelDownsample.BuildSeparatableGaussWithSharpen( Settings.SharpenMipKernelSize, Settings.MipSharpening );
	}

	DestImage.Init(SrcImage.SizeX, SrcImage.SizeY, SrcImage.NumSlices, SrcImage.Format, SrcImage.GammaSpace);

	for (int32 SliceIndex = 0; SliceIndex < SrcImage.NumSlices; ++SliceIndex)
	{
		FImageView2D SrcView((FImage&)SrcImage, SliceIndex);
		FImageView2D DestView(DestImage, SliceIndex);

		// generate DestImage: down sample with sharpening
		GenerateSharpenedMipB8G8R8A8(
			SrcView, 
			FImageView2D(),
			DestView,
			AddressMode,
			false,
			FVector4f(0, 0, 0, 0),
			FVector4f(0, 0, 0, 0),
			KernelDownsample,
			1,
			Settings.bSharpenWithoutColorShift,
			Settings.MipGenSettings == TMGS_Unfiltered);
	}
}

static FLinearColor LookupSourceMipBilinear(const FImageView2D& SourceImageData, float X, float Y)
{
	X = FMath::Clamp(X, 0.f, SourceImageData.SizeX - 1.f);
	Y = FMath::Clamp(Y, 0.f, SourceImageData.SizeY - 1.f);
	int32 IntX0 = FMath::FloorToInt(X);
	int32 IntY0 = FMath::FloorToInt(Y);
	float FractX = X - IntX0;
	float FractY = Y - IntY0;
	int32 IntX1 = FMath::Min(IntX0+1, SourceImageData.SizeX-1);
	int32 IntY1 = FMath::Min(IntY0+1, SourceImageData.SizeY-1);
	
	FLinearColor Sample00 = SourceImageData.Access(IntX0,IntY0);
	FLinearColor Sample10 = SourceImageData.Access(IntX1,IntY0);
	FLinearColor Sample01 = SourceImageData.Access(IntX0,IntY1);
	FLinearColor Sample11 = SourceImageData.Access(IntX1,IntY1);
	FLinearColor Sample0 = FMath::Lerp(Sample00, Sample10, FractX);
	FLinearColor Sample1 = FMath::Lerp(Sample01, Sample11, FractX);
		
	return FMath::Lerp(Sample0, Sample1, FractY);
}

struct FTextureDownscaleSettings
{
	int32 BlockSize;
	float Downscale;
	uint8 DownscaleOptions;
};

static void DownscaleImage(const FImage& SrcImage, FImage& DstImage, const FTextureDownscaleSettings& Settings)
{
	if (Settings.Downscale <= 1.f)
	{
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.DownscaleImage);
		
	float Downscale = FMath::Clamp(Settings.Downscale, 1.f, 8.f);
	int32 FinalSizeX = FMath::CeilToInt(SrcImage.SizeX / Downscale);
	int32 FinalSizeY = FMath::CeilToInt(SrcImage.SizeY / Downscale);

	// compute final size respecting image block size
	if (Settings.BlockSize > 1 
		&& SrcImage.SizeX % Settings.BlockSize == 0 
		&& SrcImage.SizeY % Settings.BlockSize == 0)
	{
		int32 NumBlocksX = SrcImage.SizeX / Settings.BlockSize;
		int32 NumBlocksY = SrcImage.SizeY / Settings.BlockSize;
		int32 GCD = FMath::GreatestCommonDivisor(NumBlocksX, NumBlocksY);
		int32 RatioX = NumBlocksX/GCD;
		int32 RatioY = NumBlocksY/GCD;
		int32 FinalNumBlocksX = (int32)FMath::GridSnap((float)FinalSizeX/Settings.BlockSize, (float)RatioX);
		int32 FinalNumBlocksY = FinalNumBlocksX/RatioX*RatioY;
		FinalSizeX = FinalNumBlocksX*Settings.BlockSize;
		FinalSizeY = FinalNumBlocksY*Settings.BlockSize;
	}

	Downscale = (float)SrcImage.SizeX / FinalSizeX;
		
	FImage Image0;
	FImage Image1;
	FImage* ImageChain[2] = {&const_cast<FImage&>(SrcImage), &Image1};
	bool bUnfiltered = Settings.DownscaleOptions == (uint8)ETextureDownscaleOptions::Unfiltered;
	
	// Scaledown using 2x2 average, use user specified filtering only for last iteration
	FImageKernel2D AvgKernel;
	AvgKernel.BuildSeparatableGaussWithSharpen(2);
	int32 NumIterations = 0;
	while(Downscale > 2.0f)
	{
		int32 DstSizeX = ImageChain[0]->SizeX / 2;
		int32 DstSizeY = ImageChain[0]->SizeY / 2;
		ImageChain[1]->Init(DstSizeX, DstSizeY, ImageChain[0]->NumSlices, ImageChain[0]->Format, ImageChain[0]->GammaSpace);

		FImageView2D SrcImageData(*ImageChain[0], 0);
		FImageView2D DstImageData(*ImageChain[1], 0);
		GenerateSharpenedMipB8G8R8A8Templ<MGTAM_Clamp>(
			SrcImageData, 
			DstImageData, 
			false,
			FVector4f(0, 0, 0, 0),
			FVector4f(0, 0, 0, 0), 
			AvgKernel, 
			2, 
			false,
			bUnfiltered);

		if (NumIterations == 0)
		{
			ImageChain[0] = &Image0;
		}
		Swap(ImageChain[0], ImageChain[1]);
		
		NumIterations++;
		Downscale/= 2.f;
	}

	if (ImageChain[0]->SizeX == FinalSizeX &&
		ImageChain[0]->SizeY == FinalSizeY)
	{
		ImageChain[0]->CopyTo(DstImage, ImageChain[0]->Format, ImageChain[0]->GammaSpace);
		return;
	}
	
	int32 KernelSize = 2;
	float Sharpening = 0.0f;
	if (Settings.DownscaleOptions >= (uint8)ETextureDownscaleOptions::Sharpen0 && Settings.DownscaleOptions <= (uint8)ETextureDownscaleOptions::Sharpen10)
	{
		// 0 .. 2.0f
		Sharpening = ((int32)Settings.DownscaleOptions - (int32)ETextureDownscaleOptions::Sharpen0) * 0.2f;
		KernelSize = 8;
	}
	
	bool bBilinear = Settings.DownscaleOptions == (uint8)ETextureDownscaleOptions::SimpleAverage;
	
	FImageKernel2D KernelSharpen;
	KernelSharpen.BuildSeparatableGaussWithSharpen(KernelSize, Sharpening);
	const int32 KernelCenter = (int32)KernelSharpen.GetFilterTableSize() / 2 - 1;
		
	ImageChain[1] = &DstImage;
	if (ImageChain[0] == ImageChain[1])
	{
		ImageChain[0]->CopyTo(Image0, ImageChain[0]->Format, ImageChain[0]->GammaSpace);
		ImageChain[0] = &Image0;
	}
	
	ImageChain[1]->Init(FinalSizeX, FinalSizeY, ImageChain[0]->NumSlices, ImageChain[0]->Format, ImageChain[0]->GammaSpace);
	Downscale = (float)ImageChain[0]->SizeX / FinalSizeX;

	FImageView2D SrcImageData(*ImageChain[0], 0);
	FImageView2D DstImageData(*ImageChain[1], 0);
					
	for (int32 Y = 0; Y < FinalSizeY; ++Y)
	{
		float SourceY = Y * Downscale;
		int32 IntSourceY = FMath::RoundToInt(SourceY);
		
		for (int32 X = 0; X < FinalSizeX; ++X)
		{
			float SourceX = X * Downscale;
			int32 IntSourceX = FMath::RoundToInt(SourceX);

			FLinearColor FilteredColor(0,0,0,0);

			if (bUnfiltered)
			{
				FilteredColor = LookupSourceMip<MGTAM_Clamp>(SrcImageData, IntSourceX, IntSourceY);
			}
			else if(bBilinear)
			{
				FilteredColor = LookupSourceMipBilinear(SrcImageData, SourceX, SourceY);
			}
			else
			{
				for (uint32 KernelY = 0; KernelY < KernelSharpen.GetFilterTableSize();  ++KernelY)
				{
					for (uint32 KernelX = 0; KernelX < KernelSharpen.GetFilterTableSize();  ++KernelX)
					{
						float Weight = KernelSharpen.GetAt(KernelX, KernelY);
						FLinearColor Sample = LookupSourceMipBilinear(SrcImageData, SourceX + KernelX - KernelCenter, SourceY + KernelY - KernelCenter);
						FilteredColor += Weight	* Sample;
					}
				}
			}

			// Set the destination pixel.
			FLinearColor& DestColor = DstImageData.Access(X, Y);
			DestColor = FilteredColor;
		}
	}
}

void ITextureCompressorModule::GenerateMipChain(
	const FTextureBuildSettings& Settings,
	const FImage& BaseImage,
	TArray<FImage> &OutMipChain,
	uint32 MipChainDepth 
	)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.GenerateMipChain);

	check(BaseImage.Format == ERawImageFormat::RGBA32F);

	const FImage& BaseMip = BaseImage;
	const int32 SrcWidth = BaseMip.SizeX;
	const int32 SrcHeight= BaseMip.SizeY;
	const int32 SrcNumSlices = BaseMip.NumSlices;
	const ERawImageFormat::Type ImageFormat = ERawImageFormat::RGBA32F;

	const FImage* IntermediateSrcPtr;
	FImage* IntermediateDstPtr;

	// This will be used as a buffer for the mip processing
	FImage FirstTempImage;

	if (BaseMip.GammaSpace != EGammaSpace::Linear)
	{
		// copy base mip
		BaseMip.CopyTo(FirstTempImage, ERawImageFormat::RGBA32F, EGammaSpace::Linear);

		IntermediateSrcPtr = &FirstTempImage;
	}
	else
	{
		// It looks like the BaseMip can be reused for the intermediate source of the second Mip (assuming that the format was check earlier to be RGBA32F)
		IntermediateSrcPtr = &BaseMip;

		// This temp image will be first used as an intermediate destination for the third mip in the chain
		FirstTempImage.Init( FMath::Max<uint32>( 1, SrcWidth >> 2 ), FMath::Max<uint32>( 1, SrcHeight >> 2 ), Settings.bVolume ? FMath::Max<uint32>( 1, SrcNumSlices >> 2 ) : SrcNumSlices, ImageFormat );
	}

	// The image for the first destination
	FImage SecondTempImage(FMath::Max<uint32>( 1, SrcWidth >> 1 ), FMath::Max<uint32>( 1, SrcHeight >> 1 ), Settings.bVolume ? FMath::Max<uint32>( 1, SrcNumSlices >> 1 ) : SrcNumSlices, ImageFormat);
	IntermediateDstPtr = &SecondTempImage;

	// Filtering kernels.
	FImageKernel2D KernelSimpleAverage;
	FImageKernel2D KernelDownsample;
	KernelSimpleAverage.BuildSeparatableGaussWithSharpen( 2 );
	KernelDownsample.BuildSeparatableGaussWithSharpen( Settings.SharpenMipKernelSize, Settings.MipSharpening );

	//@TODO : add a true 3D kernel.

	EMipGenAddressMode AddressMode = ComputeAdressMode(Settings);
	bool bReDrawBorder = false;
	if( Settings.bPreserveBorder )
	{
		bReDrawBorder = !Settings.bBorderColorBlack;
	}

	// Calculate alpha coverage value to preserve along mip chain
	FVector4f AlphaCoverages(0, 0, 0, 0);
	if ( Settings.bDoScaleMipsForAlphaCoverage )
	{
		check(Settings.AlphaCoverageThresholds != FVector4f(0,0,0,0));
		check(IntermediateSrcPtr);
		const FImageView2D IntermediateSrcView = FImageView2D::ConstructConst(*IntermediateSrcPtr, 0);

		const FVector4f AlphaScales(1, 1, 1, 1);		
		AlphaCoverages = ComputeAlphaCoverage(Settings.AlphaCoverageThresholds, AlphaScales, IntermediateSrcView);
	}

	// Generate mips
	//  default value of MipChainDepth is MAX_uint32, means generate all mips down to 1x1
	//	(break inside the loop)
	for (; MipChainDepth != 0 ; --MipChainDepth)
	{
		check(IntermediateSrcPtr && IntermediateDstPtr);
		const FImage& IntermediateSrc = *IntermediateSrcPtr;
		FImage& IntermediateDst = *IntermediateDstPtr;

		// add new mip to TArray<FImage> &OutMipChain :
		//	placement new on TArray does AddUninitialized then constructs in the last element
		FImage& DestImage = *new(OutMipChain) FImage(IntermediateDst.SizeX, IntermediateDst.SizeY, IntermediateDst.NumSlices, ImageFormat);
		
		for (int32 SliceIndex = 0; SliceIndex < IntermediateDst.NumSlices; ++SliceIndex)
		{
			const int32 SrcSliceIndex = Settings.bVolume ? (SliceIndex * 2) : SliceIndex;
			const FImageView2D IntermediateSrcView = FImageView2D::ConstructConst(IntermediateSrc, SrcSliceIndex);
			const FImageView2D IntermediateSrcView2 = Settings.bVolume ?  FImageView2D::ConstructConst(IntermediateSrc, SrcSliceIndex + 1) : FImageView2D(); // Volume texture mips take 2 slices
			FImageView2D DestView(DestImage, SliceIndex);
			FImageView2D IntermediateDstView(IntermediateDst, SliceIndex);

			GenerateSharpenedMipB8G8R8A8(
				IntermediateSrcView, 
				IntermediateSrcView2,
				DestView,
				AddressMode,
				Settings.bDoScaleMipsForAlphaCoverage,
				AlphaCoverages,
				Settings.AlphaCoverageThresholds,
				KernelDownsample,
				2,
				Settings.bSharpenWithoutColorShift,
				Settings.MipGenSettings == TMGS_Unfiltered);

			// generate IntermediateDstImage:
			if ( Settings.bDownsampleWithAverage )
			{
				// down sample without sharpening for the next iteration
				GenerateSharpenedMipB8G8R8A8(
					IntermediateSrcView,
					IntermediateSrcView2,
					IntermediateDstView,
					AddressMode,
					Settings.bDoScaleMipsForAlphaCoverage,
					AlphaCoverages,
					Settings.AlphaCoverageThresholds,
					KernelSimpleAverage,
					2,
					Settings.bSharpenWithoutColorShift,
					Settings.MipGenSettings == TMGS_Unfiltered);
			}
		}

		if ( Settings.bDownsampleWithAverage == false )
		{
			FMemory::Memcpy( (&IntermediateDst.AsRGBA32F()[0]), (&DestImage.AsRGBA32F()[0]),
				IntermediateDst.SizeX * IntermediateDst.SizeY * IntermediateDst.NumSlices * sizeof(FLinearColor) );
		}

		if ( bReDrawBorder )
		{
			for (int32 SliceIndex = 0; SliceIndex < IntermediateDst.NumSlices; ++SliceIndex)
			{
				const FImageView2D IntermediateSrcView = FImageView2D::ConstructConst(IntermediateSrc, SliceIndex);
				FImageView2D DestView(DestImage, SliceIndex);
				FImageView2D IntermediateDstView(IntermediateDst, SliceIndex);
				GenerateMipBorder( IntermediateSrcView, DestView );
				GenerateMipBorder( IntermediateSrcView, IntermediateDstView );
			}
		}

		// Once we've created mip-maps down to 1x1, we're done.
		if ( IntermediateDst.SizeX == 1 && IntermediateDst.SizeY == 1 && (!Settings.bVolume || IntermediateDst.NumSlices == 1))
		{
			break;
		}

		// last destination becomes next source
		if (IntermediateDstPtr == &SecondTempImage)
		{
			IntermediateDstPtr = &FirstTempImage;
			IntermediateSrcPtr = &SecondTempImage;
		}
		else
		{
			IntermediateDstPtr = &SecondTempImage;
			IntermediateSrcPtr = &FirstTempImage;
		}

		// Update the destination size for the next iteration.
		IntermediateDstPtr->SizeX = FMath::Max<uint32>( 1, IntermediateSrcPtr->SizeX >> 1 );
		IntermediateDstPtr->SizeY = FMath::Max<uint32>( 1, IntermediateSrcPtr->SizeY >> 1 );
		IntermediateDstPtr->NumSlices = Settings.bVolume ? FMath::Max<uint32>( 1, IntermediateSrcPtr->NumSlices >> 1 ) : SrcNumSlices;
	}
}

/*------------------------------------------------------------------------------
	Angular Filtering for HDR Cubemaps.
------------------------------------------------------------------------------*/

/**
 * View in to an image that allows access by converting a direction to longitude and latitude.
 */
struct FImageViewLongLat
{
	/** Image colors. */
	FLinearColor* ImageColors;
	/** Width of the image. */
	int32 SizeX;
	/** Height of the image. */
	int32 SizeY;

	/** Initialization constructor. */
	explicit FImageViewLongLat(FImage& Image, int32 SliceIndex)
	{
		SizeX = Image.SizeX;
		SizeY = Image.SizeY;
		ImageColors = (&Image.AsRGBA32F()[0]) + SliceIndex * SizeY * SizeX;
	}

	/** Wraps X around W. */
	static void WrapTo(int32& X, int32 W)
	{
		X = X % W;

		if(X < 0)
		{
			X += W;
		}
	}

	/** Const access to a texel. */
	FLinearColor Access(int32 X, int32 Y) const
	{
		return ImageColors[X + Y * SizeX];
	}

	/** Makes a filtered lookup. */
	FLinearColor LookupFiltered(float X, float Y) const
	{
		int32 X0 = (int32)floor(X);
		int32 Y0 = (int32)floor(Y);

		float FracX = X - X0;
		float FracY = Y - Y0;

		int32 X1 = X0 + 1;
		int32 Y1 = Y0 + 1;

		WrapTo(X0, SizeX);
		WrapTo(X1, SizeX);
		Y0 = FMath::Clamp(Y0, 0, (int32)(SizeY - 1));
		Y1 = FMath::Clamp(Y1, 0, (int32)(SizeY - 1));

		FLinearColor CornerRGB00 = Access(X0, Y0);
		FLinearColor CornerRGB10 = Access(X1, Y0);
		FLinearColor CornerRGB01 = Access(X0, Y1);
		FLinearColor CornerRGB11 = Access(X1, Y1);

		FLinearColor CornerRGB0 = FMath::Lerp(CornerRGB00, CornerRGB10, FracX);
		FLinearColor CornerRGB1 = FMath::Lerp(CornerRGB01, CornerRGB11, FracX);

		return FMath::Lerp(CornerRGB0, CornerRGB1, FracY);
	}

	/** Makes a filtered lookup using a direction. */
	FLinearColor LookupLongLat(FVector NormalizedDirection) const
	{
		// see http://gl.ict.usc.edu/Data/HighResProbes
		// latitude-longitude panoramic format = equirectangular mapping

		float X = (1 + atan2(NormalizedDirection.X, - NormalizedDirection.Z) / PI) / 2 * SizeX;
		float Y = acos(NormalizedDirection.Y) / PI * SizeY;

		return LookupFiltered(X, Y);
	}
};

// transform world space vector to a space relative to the face
static FVector TransformSideToWorldSpace(uint32 CubemapFace, FVector InDirection)
{
	float x = InDirection.X, y = InDirection.Y, z = InDirection.Z;

	FVector Ret = FVector(0, 0, 0);

	// see http://msdn.microsoft.com/en-us/library/bb204881(v=vs.85).aspx
	switch(CubemapFace)
	{
		case 0: Ret = FVector(+z, -y, -x); break;
		case 1: Ret = FVector(-z, -y, +x); break;
		case 2: Ret = FVector(+x, +z, +y); break;
		case 3: Ret = FVector(+x, -z, -y); break;
		case 4: Ret = FVector(+x, -y, +z); break;
		case 5: Ret = FVector(-x, -y, -z); break;
		default:
			checkSlow(0);
	}

	// this makes it with the Unreal way (z and y are flipped)
	return FVector(Ret.X, Ret.Z, Ret.Y);
}

// transform vector relative to the face to world space
static FVector TransformWorldToSideSpace(uint32 CubemapFace, FVector InDirection)
{
	// undo Unreal way (z and y are flipped)
	float x = InDirection.X, y = InDirection.Z, z = InDirection.Y;

	FVector Ret = FVector(0, 0, 0); 

	// see http://msdn.microsoft.com/en-us/library/bb204881(v=vs.85).aspx
	switch(CubemapFace)
	{
		case 0: Ret = FVector(-z, -y, +x); break;
		case 1: Ret = FVector(+z, -y, -x); break;
		case 2: Ret = FVector(+x, +z, +y); break;
		case 3: Ret = FVector(+x, -z, -y); break;
		case 4: Ret = FVector(+x, -y, +z); break;
		case 5: Ret = FVector(-x, -y, -z); break;
		default:
			checkSlow(0);
	}

	return Ret;
}

FVector ComputeSSCubeDirectionAtTexelCenter(uint32 x, uint32 y, float InvSideExtent)
{
	// center of the texels
	FVector DirectionSS((x + 0.5f) * InvSideExtent * 2 - 1, (y + 0.5f) * InvSideExtent * 2 - 1, 1);
	DirectionSS.Normalize();
	return DirectionSS;
}

static FVector ComputeWSCubeDirectionAtTexelCenter(uint32 CubemapFace, uint32 x, uint32 y, float InvSideExtent)
{
	FVector DirectionSS = ComputeSSCubeDirectionAtTexelCenter(x, y, InvSideExtent);
	FVector DirectionWS = TransformSideToWorldSpace(CubemapFace, DirectionSS);
	return DirectionWS;
}

static uint32 ComputeLongLatCubemapExtents(const FImage& SrcImage, const uint32 MaxCubemapTextureResolution)
{
	return FMath::Clamp(1U << FMath::FloorLog2(SrcImage.SizeX / 2), 32U, MaxCubemapTextureResolution);
}

void ITextureCompressorModule::GenerateBaseCubeMipFromLongitudeLatitude2D(FImage* OutMip, const FImage& SrcImage, const uint32 MaxCubemapTextureResolution, uint8 SourceEncodingOverride)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.GenerateBaseCubeMipFromLongitudeLatitude2D);

	FImage LongLatImage;
	SrcImage.Linearize(SourceEncodingOverride, LongLatImage);

	// TODO_TEXTURE: Expose target size to user.
	uint32 Extent = ComputeLongLatCubemapExtents(LongLatImage, MaxCubemapTextureResolution);
	float InvExtent = 1.0f / Extent;
	OutMip->Init(Extent, Extent, SrcImage.NumSlices * 6, ERawImageFormat::RGBA32F, EGammaSpace::Linear);

	for (int32 Slice = 0; Slice < SrcImage.NumSlices; ++Slice)
	{
		FImageViewLongLat LongLatView(LongLatImage, Slice);
		for (uint32 Face = 0; Face < 6; ++Face)
		{
			FImageView2D MipView(*OutMip, Slice * 6 + Face);
			for (uint32 y = 0; y < Extent; ++y)
			{
				for (uint32 x = 0; x < Extent; ++x)
				{
					FVector DirectionWS = ComputeWSCubeDirectionAtTexelCenter(Face, x, y, InvExtent);
					MipView.Access(x, y) = LongLatView.LookupLongLat(DirectionWS);
				}
			}
		}
	}
}

class FTexelProcessor
{
public:
	// @param InConeAxisSS - normalized, in side space
	// @param TexelAreaArray - precomputed area of each texel for correct weighting
	FTexelProcessor(const FVector& InConeAxisSS, float ConeAngle, const FLinearColor* InSideData, const float* InTexelAreaArray, uint32 InFullExtent)
		: ConeAxisSS(InConeAxisSS)
		, AccumulatedColor(0, 0, 0, 0)
		, SideData(InSideData)
		, TexelAreaArray(InTexelAreaArray)
		, FullExtent(InFullExtent)
	{
		ConeAngleSin = sinf(ConeAngle);
		ConeAngleCos = cosf(ConeAngle);

		// *2 as the position is from -1 to 1
		// / InFullExtent as x and y is in the range 0..InFullExtent-1
		PositionToWorldScale = 2.0f / InFullExtent;
		InvFullExtent = 1.0f / FullExtent;

		// examples: 0 to diffuse convolution, 0.95f for glossy
		DirDot = FMath::Min(FMath::Cos(ConeAngle), 0.9999f);

		InvDirOneMinusDot = 1.0f / (1.0f - DirDot);

		// precomputed sqrt(2.0f * 2.0f + 2.0f * 2.0f)
		float Sqrt8 = 2.8284271f;
		RadiusToWorldScale = Sqrt8 / (float)InFullExtent;
	}

	// @return true: yes, traverse deeper, false: not relevant
	bool TestIfRelevant(uint32 x, uint32 y, uint32 LocalExtent) const
	{
		float HalfExtent = LocalExtent * 0.5f; 
		float U = (x + HalfExtent) * PositionToWorldScale - 1.0f;
		float V = (y + HalfExtent) * PositionToWorldScale - 1.0f;

		float SphereRadius = RadiusToWorldScale * LocalExtent;

		FVector SpherePos(U, V, 1);

		return FMath::SphereConeIntersection(SpherePos, SphereRadius, ConeAxisSS, ConeAngleSin, ConeAngleCos);
	}

	void Process(uint32 x, uint32 y)
	{
		const FLinearColor* In = &SideData[x + y * FullExtent];
		
		FVector DirectionSS = ComputeSSCubeDirectionAtTexelCenter(x, y, InvFullExtent);

		float DotValue = ConeAxisSS | DirectionSS;

		if(DotValue > DirDot)
		{
			// 0..1, 0=at kernel border..1=at kernel center
			float KernelWeight = 1.0f - (1.0f - DotValue) * InvDirOneMinusDot;

			// apply smoothstep function (softer, less linear result)
			KernelWeight = KernelWeight * KernelWeight * (3 - 2 * KernelWeight);

			float AreaCompensation = TexelAreaArray[x + y * FullExtent];
			// AreaCompensation would be need for correctness but seems it has a but
			// as it looks much better (no seam) without, the effect is minor so it's deactivated for now.
//			float Weight = KernelWeight * AreaCompensation;
			float Weight = KernelWeight;

			AccumulatedColor.R += Weight * In->R;
			AccumulatedColor.G += Weight * In->G;
			AccumulatedColor.B += Weight * In->B;
			AccumulatedColor.A += Weight;
		}
	}

	// normalized, in side space
	FVector ConeAxisSS;

	FLinearColor AccumulatedColor;

	// cached for better performance
	float ConeAngleSin;
	float ConeAngleCos;
	float PositionToWorldScale;
	float RadiusToWorldScale;
	float InvFullExtent;
	// 0 to diffuse convolution, 0.95f for glossy
	float DirDot;
	float InvDirOneMinusDot;

	/** [x + y * FullExtent] */
	const FLinearColor* SideData;
	const float* TexelAreaArray;
	uint32 FullExtent;
};

template <class TVisitor>
void TCubemapSideRasterizer(TVisitor &TexelProcessor, int32 x, uint32 y, uint32 Extent)
{
	if(Extent > 1)
	{
		if(!TexelProcessor.TestIfRelevant(x, y, Extent))
		{
			return;
		}
		Extent /= 2;

		TCubemapSideRasterizer(TexelProcessor, x, y, Extent);
		TCubemapSideRasterizer(TexelProcessor, x + Extent, y, Extent);
		TCubemapSideRasterizer(TexelProcessor, x, y + Extent, Extent);
		TCubemapSideRasterizer(TexelProcessor, x + Extent, y + Extent, Extent);
	}
	else
	{
		TexelProcessor.Process(x, y);
	}
}

static FLinearColor IntegrateAngularArea(FImage& Image, FVector FilterDirectionWS, float ConeAngle, const float* TexelAreaArray)
{
	// Alpha channel is used to renormalize later
	FLinearColor ret(0, 0, 0, 0);
	int32 Extent = Image.SizeX;

	for(uint32 Face = 0; Face < 6; ++Face)
	{
		FImageView2D ImageView(Image, Face);
		FVector FilterDirectionSS = TransformWorldToSideSpace(Face, FilterDirectionWS);
		FTexelProcessor Processor(FilterDirectionSS, ConeAngle, &ImageView.Access(0,0), TexelAreaArray, Extent);

		// recursively split the (0,0)-(Extent-1,Extent-1), tests for intersection and processes only colors inside
		TCubemapSideRasterizer(Processor, 0, 0, Extent);
		ret += Processor.AccumulatedColor;
	}
	
	if(ret.A != 0)
	{
		float Inv = 1.0f / ret.A;

		ret.R *= Inv;
		ret.G *= Inv;
		ret.B *= Inv;
	}
	else
	{
		// should not happen
//		checkSlow(0);
	}

	ret.A = 0;

	return ret;
}

// @return 2 * computed triangle area 
static inline float TriangleArea2_3D(FVector A, FVector B, FVector C)
{
	return ((A-B) ^ (C-B)).Size();
}

static inline float ComputeTexelArea(uint32 x, uint32 y, float InvSideExtentMul2)
{
	float fU = x * InvSideExtentMul2 - 1;
	float fV = y * InvSideExtentMul2 - 1;

	FVector CornerA = FVector(fU, fV, 1);
	FVector CornerB = FVector(fU + InvSideExtentMul2, fV, 1);
	FVector CornerC = FVector(fU, fV + InvSideExtentMul2, 1);
	FVector CornerD = FVector(fU + InvSideExtentMul2, fV + InvSideExtentMul2, 1);

	CornerA.Normalize();
	CornerB.Normalize();
	CornerC.Normalize();
	CornerD.Normalize();

	return TriangleArea2_3D(CornerA, CornerB, CornerC) + TriangleArea2_3D(CornerC, CornerB, CornerD) * 0.5f;
}

/**
 * Generate a mip using angular filtering.
 * @param DestMip - The filtered mip.
 * @param SrcMip - The source mip which will be filtered.
 * @param ConeAngle - The cone angle with which to filter.
 */
static void GenerateAngularFilteredMip(FImage* DestMip, FImage& SrcMip, float ConeAngle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.GenerateAngularFilteredMip);

	int32 MipExtent = DestMip->SizeX;
	float MipInvSideExtent = 1.0f / MipExtent;

	TArray<float> TexelAreaArray;
	TexelAreaArray.AddUninitialized(SrcMip.SizeX * SrcMip.SizeY);

	// precompute the area size for one face (is the same for each face)
	for(int32 y = 0; y < SrcMip.SizeY; ++y)
	{
		for(int32 x = 0; x < SrcMip.SizeX; ++x)
		{
			TexelAreaArray[x + y * SrcMip.SizeX] = ComputeTexelArea(x, y, MipInvSideExtent * 2);
		}
	}

	// We start getting gains running threaded upwards of sizes >= 128
	if (SrcMip.SizeX >= 128)
	{
		// Quick workaround: Do a thread per mip
		struct FAsyncGenerateMipsPerFaceWorker : public FNonAbandonableTask
		{
			int32 Face;
			FImage* DestMip;
			int32 Extent;
			float ConeAngle;
			const float* TexelAreaArray;
			FImage* SrcMip;
			FAsyncGenerateMipsPerFaceWorker(int32 InFace, FImage* InDestMip, int32 InExtent, float InConeAngle, const float* InTexelAreaArray, FImage* InSrcMip) :
				Face(InFace),
				DestMip(InDestMip),
				Extent(InExtent),
				ConeAngle(InConeAngle),
				TexelAreaArray(InTexelAreaArray),
				SrcMip(InSrcMip)
			{
			}

			void DoWork()
			{
				const float InvSideExtent = 1.0f / Extent;
				FImageView2D DestMipView(*DestMip, Face);
				for (int32 y = 0; y < Extent; ++y)
				{
					for (int32 x = 0; x < Extent; ++x)
					{
						FVector DirectionWS = ComputeWSCubeDirectionAtTexelCenter(Face, x, y, InvSideExtent);
						DestMipView.Access(x, y) = IntegrateAngularArea(*SrcMip, DirectionWS, ConeAngle, TexelAreaArray);
					}
				}
			}

			FORCEINLINE TStatId GetStatId() const
			{
				RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncGenerateMipsPerFaceWorker, STATGROUP_ThreadPoolAsyncTasks);
			}
		};

		typedef FAsyncTask<FAsyncGenerateMipsPerFaceWorker> FAsyncGenerateMipsPerFaceTask;
		TIndirectArray<FAsyncGenerateMipsPerFaceTask> AsyncTasks;

		for (int32 Face = 0; Face < 6; ++Face)
		{
			auto* AsyncTask = new FAsyncGenerateMipsPerFaceTask(Face, DestMip, MipExtent, ConeAngle, TexelAreaArray.GetData(), &SrcMip);
			AsyncTasks.Add(AsyncTask);
			AsyncTask->StartBackgroundTask();
		}

		for (int32 TaskIndex = 0; TaskIndex < AsyncTasks.Num(); ++TaskIndex)
		{
			auto& AsyncTask = AsyncTasks[TaskIndex];
			AsyncTask.EnsureCompletion();
		}
	}
	else
	{
		for (int32 Face = 0; Face < 6; ++Face)
		{
			FImageView2D DestMipView(*DestMip, Face);
			for (int32 y = 0; y < MipExtent; ++y)
			{
				for (int32 x = 0; x < MipExtent; ++x)
				{
					FVector DirectionWS = ComputeWSCubeDirectionAtTexelCenter(Face, x, y, MipInvSideExtent);
					DestMipView.Access(x, y) = IntegrateAngularArea(SrcMip, DirectionWS, ConeAngle, TexelAreaArray.GetData());
				}
			}
		}
	}
}

void ITextureCompressorModule::GenerateAngularFilteredMips(TArray<FImage>& InOutMipChain, int32 NumMips, uint32 DiffuseConvolveMipLevel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.GenerateAngularFilteredMips);

	TArray<FImage> SrcMipChain;
	Exchange(SrcMipChain, InOutMipChain);
	InOutMipChain.Empty(NumMips);

	// Generate simple averaged mips to accelerate angular filtering.
	for (int32 MipIndex = SrcMipChain.Num(); MipIndex < NumMips; ++MipIndex)
	{
		FImage& BaseMip = SrcMipChain[MipIndex - 1];
		int32 BaseExtent = BaseMip.SizeX;
		int32 MipExtent = FMath::Max(BaseExtent >> 1, 1);
		FImage* Mip = new(SrcMipChain) FImage(MipExtent, MipExtent, BaseMip.NumSlices, BaseMip.Format);

		for(int32 Face = 0; Face < 6; ++Face)
		{
			FImageView2D BaseMipView(BaseMip, Face);
			FImageView2D MipView(*Mip, Face);

			for(int32 y = 0; y < MipExtent; ++y)
			{
				for(int32 x = 0; x < MipExtent; ++x)
				{		
					FLinearColor Sum = (
						BaseMipView.Access(x*2, y*2) +
						BaseMipView.Access(x*2+1, y*2) +
						BaseMipView.Access(x*2, y*2+1) +
						BaseMipView.Access(x*2+1, y*2+1)
						) * 0.25f;
					MipView.Access(x,y) = Sum;
				}
			}
		}
	}

	int32 Extent = 1 << (NumMips - 1);
	int32 BaseExtent = Extent;
	for (int32 i = 0; i < NumMips; ++i)
	{
		// 0:top mip 1:lowest mip = diffuse convolve
		float NormalizedMipLevel = i / (float)(NumMips - DiffuseConvolveMipLevel);
		float AdjustedMipLevel = NormalizedMipLevel * NumMips;
		float NormalizedWidth = BaseExtent * FMath::Pow(2.0f, -AdjustedMipLevel);
		float TexelSize = 1.0f / NormalizedWidth;

		// 0.001f:sharp  .. PI/2: diffuse convolve
		// all lower mips are used for diffuse convolve
		// above that the angle blends from sharp to diffuse convolved version
		float ConeAngle = PI / 2.0f * TexelSize;

		// restrict to reasonable range
		ConeAngle = FMath::Clamp(ConeAngle, 0.002f, (float)PI / 2.0f);

		UE_LOG(LogTextureCompressor, Verbose, TEXT("GenerateAngularFilteredMips  %f %f %f %f %f"), NormalizedMipLevel, AdjustedMipLevel, NormalizedWidth, TexelSize, ConeAngle * 180 / PI);

		// 0:normal, -1:4x faster, +1:4 times slower but more precise, -2, 2 ...
		float QualityBias = 3.0f;

		// defined to result in a area of 1.0f (NormalizedArea)
		// optimized = 0.5f * FMath::Sqrt(1.0f / PI);
		float SphereRadius = 0.28209478f;
		float SegmentHeight = SphereRadius * (1.0f - FMath::Cos(ConeAngle));
		// compute SphereSegmentArea
		float AreaCoveredInNormalizedArea = 2 * PI * SphereRadius * SegmentHeight;
		checkSlow(AreaCoveredInNormalizedArea <= 0.5f);

		// unoptimized
		//	float FloatInputMip = FMath::Log2(FMath::Sqrt(AreaCoveredInNormalizedArea)) + InputMipCount - QualityBias;
		// optimized
		float FloatInputMip = 0.5f * FMath::Log2(AreaCoveredInNormalizedArea) + NumMips - QualityBias;
		uint32 InputMip = FMath::Clamp(FMath::TruncToInt(FloatInputMip), 0, NumMips - 1);

		FImage* Mip = new(InOutMipChain) FImage(Extent, Extent, 6, ERawImageFormat::RGBA32F);
		GenerateAngularFilteredMip(Mip, SrcMipChain[InputMip], ConeAngle);
		Extent = FMath::Max(Extent >> 1, 1);
	}
}

static inline void AdjustColors(FLinearColor * Colors,int64 Count,
		const FTextureBuildSettings& InBuildSettings)
{
	for(int64 i=0;i<Count;i++)
	{
		FLinearColor OriginalColor = Colors[i];
	
		#if 0
		if ( ! InBuildSettings.bHDRSource )
		{
			// @todo Oodle: for non-HDR source ensure we are clamped as expected
			//	(can drift out of clamp due to previous processing)
			//  if you wind up even very slightly out of [0,1] range this function does bad things
			OriginalColor.R = FMath::Clamp(OriginalColor.R, 0.0f, 1.f);
			OriginalColor.G = FMath::Clamp(OriginalColor.G, 0.0f, 1.f);
			OriginalColor.B = FMath::Clamp(OriginalColor.B, 0.0f, 1.f);
		}
		#endif

		const FLinearColor & ChromaKeyTarget = InBuildSettings.ChromaKeyColor;
		const float ChromaKeyThreshold = InBuildSettings.ChromaKeyThreshold + SMALL_NUMBER;

		if (InBuildSettings.bChromaKeyTexture && (OriginalColor.Equals(ChromaKeyTarget, ChromaKeyThreshold)))
		{
			OriginalColor = FLinearColor::Transparent;

			//@todo Oodle: strange: no return? processing continues on the transparent color...
			//	  this was likely unintentional
		}

		// NOTE: if OriginalColor has HDR/floats in it, this does not handle it well
		//	it implicitly discards negatives (and negatives cause color shifts)
		//	for values > 1 the clamp behavior is very strange

		// Convert to HSV
		FLinearColor HSVColor = OriginalColor.LinearRGBToHSV();
		float& PixelHue = HSVColor.R;
		float& PixelSaturation = HSVColor.G;
		float& PixelValue = HSVColor.B;

		float OriginalLuminance = PixelValue;

		const FColorAdjustmentParameters& InParams = InBuildSettings.ColorAdjustment;

		// Apply brightness adjustment
		PixelValue *= InParams.AdjustBrightness;

		// Apply brightness power adjustment
		if (!FMath::IsNearlyEqual(InParams.AdjustBrightnessCurve, 1.0f, (float)KINDA_SMALL_NUMBER) && InParams.AdjustBrightnessCurve != 0.0f)
		{
			// Raise HSV.V to the specified power
			PixelValue = FMath::Pow(PixelValue, InParams.AdjustBrightnessCurve);
		}

		// Apply "vibrance" adjustment
		if (!FMath::IsNearlyZero(InParams.AdjustVibrance, (float)KINDA_SMALL_NUMBER))
		{
			const float SatRaisePow = 5.0f;
			const float InvSatRaised = FMath::Pow(1.0f - PixelSaturation, SatRaisePow);

			const float ClampedVibrance = FMath::Clamp(InParams.AdjustVibrance, 0.0f, 1.0f);
			const float HalfVibrance = ClampedVibrance * 0.5f;

			const float SatProduct = HalfVibrance * InvSatRaised;

			PixelSaturation += SatProduct;
		}

		// Apply saturation adjustment
		PixelSaturation *= InParams.AdjustSaturation;

		// Apply hue adjustment
		PixelHue += InParams.AdjustHue;

		// Clamp HSV values
		PixelHue = FMath::Fmod(PixelHue, 360.0f);
		if (PixelHue < 0.0f)
		{
			// Keep the hue value positive as HSVToLinearRGB prefers that
			PixelHue += 360.0f;
		}
		PixelSaturation = FMath::Clamp(PixelSaturation, 0.0f, 1.0f);

		// Clamp brightness if non-HDR
		if (!InBuildSettings.bHDRSource)
		{
			PixelValue = FMath::Clamp(PixelValue, 0.0f, 1.0f);
		}

		// Convert back to a linear color
		FLinearColor LinearColor = HSVColor.HSVToLinearRGB();

		// Apply RGB curve adjustment (linear space)
		if (!FMath::IsNearlyEqual(InParams.AdjustRGBCurve, 1.0f, (float)KINDA_SMALL_NUMBER) && InParams.AdjustRGBCurve != 0.0f)
		{
			LinearColor.R = FMath::Pow(LinearColor.R, InParams.AdjustRGBCurve);
			LinearColor.G = FMath::Pow(LinearColor.G, InParams.AdjustRGBCurve);
			LinearColor.B = FMath::Pow(LinearColor.B, InParams.AdjustRGBCurve);
		}

		// Clamp HDR RGB channels to 1 or the original luminance (max original RGB channel value), whichever is greater
		// @todo Oodle: this is a very odd thing to do
		//		clamping at OriginalLuminance if you do AdjustBrightness or AdjustBrightnessCurve ?
		//	    that would keep values brighter than 1.f unchanged, but bring up lower ones to 1.f
		//	  I question whether this should just be completely removed
		if (InBuildSettings.bHDRSource)
		{
			LinearColor.R = FMath::Clamp(LinearColor.R, 0.0f, (OriginalLuminance > 1.0f ? OriginalLuminance : 1.0f));
			LinearColor.G = FMath::Clamp(LinearColor.G, 0.0f, (OriginalLuminance > 1.0f ? OriginalLuminance : 1.0f));
			LinearColor.B = FMath::Clamp(LinearColor.B, 0.0f, (OriginalLuminance > 1.0f ? OriginalLuminance : 1.0f));
		}

		// Remap the alpha channel
		// @todo Oodle: clamp Original A in [0,1] ?
		LinearColor.A = FMath::Lerp(InParams.AdjustMinAlpha, InParams.AdjustMaxAlpha, OriginalColor.A);

		Colors[i] = LinearColor;
	}
}

void ITextureCompressorModule::AdjustImageColors(FImage& Image, const FTextureBuildSettings& InBuildSettings)
{
	const FColorAdjustmentParameters& InParams = InBuildSettings.ColorAdjustment;
	check( Image.SizeX > 0 && Image.SizeY > 0 );

	bool bAdjustNeeded =
		!FMath::IsNearlyEqual( InParams.AdjustBrightness, 1.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustBrightnessCurve, 1.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustSaturation, 1.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustVibrance, 0.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustRGBCurve, 1.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustHue, 0.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustMinAlpha, 0.0f, (float)KINDA_SMALL_NUMBER ) ||
		!FMath::IsNearlyEqual( InParams.AdjustMaxAlpha, 1.0f, (float)KINDA_SMALL_NUMBER ) ||
		InBuildSettings.bChromaKeyTexture;
		
	if ( bAdjustNeeded )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Texture.AdjustImageColors);

		const int64 NumPixels = (int64)Image.SizeX * Image.SizeY * Image.NumSlices;
		TArrayView64<FLinearColor> ImageColors = Image.AsRGBA32F();

		int64 NumPixelsEachJob;
		int32 NumJobs = ImageParallelForComputeNumJobsForPixels(NumPixelsEachJob,NumPixels);

		// bForceSingleThread is set to true when: 
		// editor or cooker is loading as this is when the derived data cache is rebuilt as it will already be limited to a single thread 
		//     and thus overhead of multithreading will simply make it slower
		// @todo Oodle - this is done here and not in other similar ParallelFor places here.  It should either be done everywhere or nowhere.
		bool bForceSingleThread = GIsEditorLoadingPackage || GIsCookerLoadingPackage || IsInAsyncLoadingThread();

		ParallelFor( TEXT("Texture.AdjustImageColorsFunc.PF"),NumJobs,1, [&](int32 Index)
		{
			int64 StartIndex = Index * NumPixelsEachJob;
			int64 EndIndex = FMath::Min(StartIndex + NumPixelsEachJob, NumPixels);

			AdjustColors(&ImageColors[StartIndex],EndIndex-StartIndex,InBuildSettings);
		}
		, (bForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None) );
	}
}

/**
 * Compute the alpha channel how BokehDOF needs it setup
 *
 * @param	Image	Image to adjust
 */
static void ComputeBokehAlpha(FImage& Image)
{
	check( Image.SizeX > 0 && Image.SizeY > 0 );

	const int32 NumPixels = Image.SizeX * Image.SizeY * Image.NumSlices;
	TArrayView64<FLinearColor> ImageColors = Image.AsRGBA32F();

	// compute LinearAverage
	FLinearColor LinearAverage;
	{
		FLinearColor LinearSum(0, 0, 0, 0);
		for( int32 CurPixelIndex = 0; CurPixelIndex < NumPixels; ++CurPixelIndex )
		{
			LinearSum += ImageColors[ CurPixelIndex ];
		}
		LinearAverage = LinearSum / (float)NumPixels;
	}

	FLinearColor Scale(1, 1, 1, 1);

	// we want to normalize the image to have 0.5 as average luminance, this is assuming clamping doesn't happen (can happen when using a very small Bokeh shape)
	{
		float RGBLum = (LinearAverage.R + LinearAverage.G + LinearAverage.B) / 3.0f;

		// ideally this would be 1 but then some pixels would need to be >1 which is not supported for the textureformat we want to use.
		// The value affects the occlusion computation of the BokehDOF
		const float LumGoal = 0.25f;

		// clamp to avoid division by 0
		Scale *= LumGoal / FMath::Max(RGBLum, 0.001f);
	}

	{
		for( int32 CurPixelIndex = 0; CurPixelIndex < NumPixels; ++CurPixelIndex )
		{
			const FLinearColor OriginalColor = ImageColors[ CurPixelIndex ];

			// Convert to a linear color
			FLinearColor LinearColor = OriginalColor * Scale;
			float RGBLum = (LinearColor.R + LinearColor.G + LinearColor.B) / 3.0f;
			LinearColor.A = FMath::Clamp(RGBLum, 0.0f, 1.0f);
			ImageColors[ CurPixelIndex ] = LinearColor;
		}
	}
}

/**
 * Replicates the contents of the red channel to the green, blue, and alpha channels.
 */
static void ReplicateRedChannel( TArray<FImage>& InOutMipChain )
{
	const uint32 MipCount = InOutMipChain.Num();
	for ( uint32 MipIndex = 0; MipIndex < MipCount; ++MipIndex )
	{
		FImage& SrcMip = InOutMipChain[MipIndex];
		FLinearColor* FirstColor = (&SrcMip.AsRGBA32F()[0]);
		FLinearColor* LastColor = FirstColor + (SrcMip.SizeX * SrcMip.SizeY * SrcMip.NumSlices);
		for ( FLinearColor* Color = FirstColor; Color < LastColor; ++Color )
		{
			*Color = FLinearColor( Color->R, Color->R, Color->R, Color->R );
		}
	}
}

/**
 * Replicates the contents of the alpha channel to the red, green, and blue channels.
 */
static void ReplicateAlphaChannel( TArray<FImage>& InOutMipChain )
{
	const uint32 MipCount = InOutMipChain.Num();
	for ( uint32 MipIndex = 0; MipIndex < MipCount; ++MipIndex )
	{
		FImage& SrcMip = InOutMipChain[MipIndex];
		FLinearColor* FirstColor = (&SrcMip.AsRGBA32F()[0]);
		FLinearColor* LastColor = FirstColor + (SrcMip.SizeX * SrcMip.SizeY * SrcMip.NumSlices);
		for ( FLinearColor* Color = FirstColor; Color < LastColor; ++Color )
		{
			*Color = FLinearColor( Color->A, Color->A, Color->A, Color->A );
		}
	}
}

/**
 * Flips the contents of the green channel.
 * @param InOutMipChain - The mip chain on which the green channel shall be flipped.
 */
static void FlipGreenChannel( FImage& Image )
{
	FLinearColor* FirstColor = (&Image.AsRGBA32F()[0]);
	FLinearColor* LastColor = FirstColor + (Image.SizeX * Image.SizeY * Image.NumSlices);
	for ( FLinearColor* Color = FirstColor; Color < LastColor; ++Color )
	{
		Color->G = 1.0f - FMath::Clamp(Color->G, 0.0f, 1.0f);
	}
}

/**
 * Detects whether or not the image contains an alpha channel where at least one texel is != 255.
 */
static bool DetectAlphaChannel(const FImage& InImage)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.DetectAlphaChannel);
	
	// previous :
	// "SMALL_NUMBER" is quite small, this provides almost no tolerance
	// #define SMALL_NUMBER		(1.e-8f)
	//const float FloatNonOpaqueAlpha = 1.0f - SMALL_NUMBER;
	// 
	// opaque alpha threshold where we'd quantize to < 255 in U8
	//  images with only alpha larger than this are treated as opaque
	const float FloatNonOpaqueAlpha = 254.5f / 255.f; // the U8 alpha threshold
		
	int64 NumPixels = (int64)InImage.SizeX * InImage.SizeY * InImage.NumSlices;

	if ( InImage.Format == ERawImageFormat::BGRA8 )
	{
		TArrayView64<const FColor> SrcColorArray = InImage.AsBGRA8();
		check( SrcColorArray.Num() == NumPixels );

		const FColor * ColorPtr = &SrcColorArray[0];
		const FColor * EndPtr = ColorPtr +  SrcColorArray.Num();

		for(;ColorPtr<EndPtr;++ColorPtr)
		{
			if ( ColorPtr->A != 255 )
			{
				return true;
			}
		}
	}
	else if ( InImage.Format == ERawImageFormat::RGBA32F )
	{
		TArrayView64<const FLinearColor> SrcColorArray = InImage.AsRGBA32F();
		check( SrcColorArray.Num() == NumPixels );

		const FLinearColor * ColorPtr = &SrcColorArray[0];
		const FLinearColor * EndPtr = ColorPtr +  SrcColorArray.Num();

		for(;ColorPtr<EndPtr;++ColorPtr)
		{
			if (ColorPtr->A <= FloatNonOpaqueAlpha )
			{
				return true;
			}
		}
	}
	else if ( InImage.Format == ERawImageFormat::RGBA16 )
	{
		TArrayView64<const uint16> SrcChannelArray = InImage.AsRGBA16();
		check( SrcChannelArray.Num() == NumPixels*4 );

		const uint16 * ChannelPtr = &SrcChannelArray[0];
		const uint16 * EndPtr = ChannelPtr +  SrcChannelArray.Num();

		for(;ChannelPtr<EndPtr;ChannelPtr += 4)
		{
			if ( ChannelPtr[3] != 0xFFFF )
			{
				return true;
			}
		}
	}
	else if ( InImage.Format == ERawImageFormat::RGBA16F )
	{
		TArrayView64<const FFloat16Color> SrcColorArray = InImage.AsRGBA16F();
		check( SrcColorArray.Num() == NumPixels );

		const FFloat16Color * ColorPtr = &SrcColorArray[0];
		const FFloat16Color * EndPtr = ColorPtr +  SrcColorArray.Num();
		
		for(;ColorPtr<EndPtr;++ColorPtr)
		{
			// 16F closest to 1.0 is 0.99951172
			// use the float tolerance here? or check exactly ?
			//if ( ColorPtr->A.GetFloat() < 1.f )
			// use the same FloatNonOpaqueAlpha tolerance for consistency ?
			if ( ColorPtr->A.GetFloat() < FloatNonOpaqueAlpha )
			{
				return true;
			}
		}
	}
	else if ( InImage.Format == ERawImageFormat::G8 ||
		InImage.Format == ERawImageFormat::BGRE8 ||
		InImage.Format == ERawImageFormat::G16 ||
		InImage.Format == ERawImageFormat::R16F )
	{
		// source image formats don't have alpha
	}
	else
	{
		// new format ?
		check(0);
	}

	return false;
}

/** Calculate a scale per 4x4 block of each image, and apply it to the red/green channels. Store scale in the blue channel. */
static void ApplyYCoCgBlockScale(TArray<FImage>& InOutMipChain)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.ApplyYCoCgBlockScale);

	const uint32 MipCount = InOutMipChain.Num();
	for (uint32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
	{
		FImage& SrcMip = InOutMipChain[MipIndex];
		FLinearColor* FirstColor = (&SrcMip.AsRGBA32F()[0]);

		int32 BlockWidthX = SrcMip.SizeX / 4;
		int32 BlockWidthY = SrcMip.SizeY / 4;

		for (int32 Slice = 0; Slice < SrcMip.NumSlices; ++Slice)
		{
			FLinearColor* SliceFirstColor = FirstColor + (SrcMip.SizeX * SrcMip.SizeY * Slice);

			for (int32 Y = 0; Y < BlockWidthY; ++Y)
			{
				FLinearColor* RowFirstColor = SliceFirstColor + (Y * 4 * SrcMip.SizeY);

				for (int32 X = 0; X < BlockWidthX; ++X)
				{
					FLinearColor* BlockFirstColor = RowFirstColor + (X * 4);

					// Iterate block to find MaxComponent
					float MaxComponent = 0.f;
					for (int32 BlockY = 0; BlockY < 4; ++BlockY)
					{
						FLinearColor* Color = BlockFirstColor + (BlockY * SrcMip.SizeY);
						for (int32 BlockX = 0; BlockX < 4; ++BlockX, ++Color)
						{
							MaxComponent = FMath::Max(FMath::Abs(Color->R - 128.f / 255.f), MaxComponent);
							MaxComponent = FMath::Max(FMath::Abs(Color->G - 128.f / 255.f), MaxComponent);
						}
					}

					const float Scale = (MaxComponent < 32.f / 255.f) ? 4.f : (MaxComponent < 64.f / 255.f) ? 2.f : 1.f;
					const float OutB = (Scale - 1.f) * 8.f / 255.f;

					// Iterate block to modify for scale
					for (int32 BlockY = 0; BlockY < 4; ++BlockY)
					{
						FLinearColor* Color = BlockFirstColor + (BlockY * SrcMip.SizeY);
						for (int32 BlockX = 0; BlockX < 4; ++BlockX, ++Color)
						{
							const float OutR = (Color->R - 128.f / 255.f) * Scale + 128.f / 255.f;
							const float OutG = (Color->G - 128.f / 255.f) * Scale + 128.f / 255.f;

							*Color = FLinearColor(OutR, OutG, OutB, Color->A);
						}
					}
				}
			}
		}
	}
}

static float RoughnessToSpecularPower(float Roughness)
{
	float Div = FMath::Pow(Roughness, 4);

	// Roughness of 0 should result in a high specular power
	float MaxSpecPower = 10000000000.0f;
	Div = FMath::Max(Div, 2.0f / (MaxSpecPower + 2.0f));

	return 2.0f / Div - 2.0f;
}

static float SpecularPowerToRoughness(float SpecularPower)
{
	float Out = FMath::Pow( SpecularPower * 0.5f + 1.0f, -0.25f );

	return Out;
}

// @param CompositeTextureMode original type ECompositeTextureMode
static void ApplyCompositeTexture(FImage& RoughnessSourceMips, const FImage& NormalSourceMips, uint8 CompositeTextureMode, float CompositePower)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.ApplyCompositeTexture);

	check(RoughnessSourceMips.SizeX == NormalSourceMips.SizeX);
	check(RoughnessSourceMips.SizeY == NormalSourceMips.SizeY);

	FLinearColor* FirstColor = (&RoughnessSourceMips.AsRGBA32F()[0]);
	const FLinearColor* NormalColors = (&NormalSourceMips.AsRGBA32F()[0]);

	int64 Count = (int64) RoughnessSourceMips.SizeX * RoughnessSourceMips.SizeY * RoughnessSourceMips.NumSlices;
	
	float* TargetValuePtr;

	switch((ECompositeTextureMode)CompositeTextureMode)
	{
		case CTM_NormalRoughnessToRed:
			TargetValuePtr = &FirstColor->R;
			break;
		case CTM_NormalRoughnessToGreen:
			TargetValuePtr = &FirstColor->G;
			break;
		case CTM_NormalRoughnessToBlue:
			TargetValuePtr = &FirstColor->B;
			break;
		case CTM_NormalRoughnessToAlpha:
			TargetValuePtr = &FirstColor->A;
			break;
		default:
			UE_LOG(LogTextureCompressor, Error, TEXT("Invalid CompositeTextureMode"));
			return;
	}

	for ( int64 i=0; i<Count; i++ )
	{
		const FLinearColor & NormalColor = NormalColors[i];
		FVector3f Normal = FVector3f(NormalColor.R * 2.0f - 1.0f, NormalColor.G * 2.0f - 1.0f, NormalColor.B * 2.0f - 1.0f);
				
		// Toksvig estimation of variance
		float LengthN = FMath::Min( Normal.Size(), 1.0f );
		float Variance = ( 1.0f - LengthN ) / LengthN;
		Variance = FMath::Max( 0.0f, Variance - 0.00004f );

		Variance *= CompositePower;
		
		float Roughness = TargetValuePtr[i*4];

#if 0
		float Power = RoughnessToSpecularPower( Roughness );
		Power = Power / ( 1.0f + Variance * Power );
		Roughness = SpecularPowerToRoughness( Power );
#else
		// Refactored above to avoid divide by zero
		float a = Roughness * Roughness;
		float a2 = a * a;
		float B = 2.0f * Variance * (a2 - 1.0f);
		a2 = ( B - a2 ) / ( B - 1.0f );
		Roughness = FMath::Pow( a2, 0.25f );
#endif

		TargetValuePtr[i*4] = Roughness;
	}
}

/*------------------------------------------------------------------------------
	Image Compression.
------------------------------------------------------------------------------*/

/**
 * Asynchronous compression, used for compressing mips simultaneously.
 */
class FAsyncCompressionWorker
{
public:
	/**
	 * Initializes the data and creates the async compression task.
	 */
	FAsyncCompressionWorker(const ITextureFormat* InTextureFormat, const FImage* InImages, uint32 InNumImages, const FTextureBuildSettings& InBuildSettings, FStringView InDebugTexturePathName, bool bInImageHasAlphaChannel, uint32 InExtData)
		: TextureFormat(*InTextureFormat)
		, SourceImages(InImages)
		, BuildSettings(InBuildSettings)
		, bImageHasAlphaChannel(bInImageHasAlphaChannel)
		, ExtData(InExtData)
		, NumImages(InNumImages)
		, bCompressionResults(false)
		, DebugTexturePathName(InDebugTexturePathName)
	{
	}

	/**
	 * Compresses the texture
	 */
	void DoWork()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Texture.CompressImage);

		bCompressionResults = TextureFormat.CompressImageEx(
			SourceImages,
			NumImages,
			BuildSettings,
			DebugTexturePathName,
			bImageHasAlphaChannel,
			ExtData,
			CompressedImage
			);
	}

	/**
	 * Transfer the result of the compression to the OutCompressedImage
	 * Can only be called once
	 */
	bool ConsumeCompressionResults(FCompressedImage2D& OutCompressedImage)
	{
		OutCompressedImage = MoveTemp(CompressedImage);
		return bCompressionResults;
	}

private:

	/** Texture format interface with which to compress. */
	const ITextureFormat& TextureFormat;
	/** The image(s) to compress. */
	const FImage* SourceImages;
	/** The resulting compressed image. */
	FCompressedImage2D CompressedImage;
	/** Build settings. */
	FTextureBuildSettings BuildSettings;
	/** true if the image has a non-white alpha channel. */
	bool bImageHasAlphaChannel;
	/** Extra data that the format may want to pass to each Compress call */
	uint32 ExtData;
	/** For miptails with multiple images going in to one, this is the number of them */
	uint32 NumImages;
	/** true if compression was successful. */
	bool bCompressionResults;
	FStringView DebugTexturePathName;
};

// compress mip-maps in InMipChain and add mips to Texture, might alter the source content
static bool CompressMipChain(
	const ITextureFormat* TextureFormat,
	const TArray<FImage>& MipChain,
	const FTextureBuildSettings& Settings,
	const bool bImageHasAlphaChannel,
	FStringView DebugTexturePathName,
	TArray<FCompressedImage2D>& OutMips,
	uint32& OutNumMipsInTail,
	uint32& OutExtData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Texture.CompressMipChain)

	// now call the Ex version now that we have the proper MipChain
	const FTextureFormatCompressorCaps CompressorCaps = TextureFormat->GetFormatCapabilitiesEx(Settings, MipChain.Num(), MipChain[0], bImageHasAlphaChannel);
	OutNumMipsInTail = CompressorCaps.NumMipsInTail;
	OutExtData = CompressorCaps.ExtData;

	int32 MipCount = MipChain.Num();
	check(MipCount >= (int32)CompressorCaps.NumMipsInTail);
	// This number was too small (128) for current hardware and caused too many
	// context switch for work taking < 1ms. Bump the value for 2020 CPUs.
	const int32 MinAsyncCompressionSize = 512;
	const bool bAllowParallelBuild = TextureFormat->AllowParallelBuild();
	bool bCompressionSucceeded = true;
	int32 FirstMipTailIndex = MipCount;
	uint32 StartCycles = FPlatformTime::Cycles();

	// check if we need to merge mips together into tail
	if (CompressorCaps.NumMipsInTail > 1)
	{
		FirstMipTailIndex = MipCount - CompressorCaps.NumMipsInTail;
	}

	OutMips.Empty(MipCount);
	TArray<FAsyncCompressionWorker> AsyncCompressionTasks;
	AsyncCompressionTasks.Reserve(MipCount);

	struct PreWork
	{
		int32 MipIndex;
		const FImage& SrcMip;
		FCompressedImage2D& DestMip;
	};
	TArray<PreWork> PreWorkTasks;
	PreWorkTasks.Reserve(MipCount);

	for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
	{
		const FImage& SrcMip = MipChain[MipIndex];
		FCompressedImage2D& DestMip = *new(OutMips) FCompressedImage2D;

		if (MipIndex > FirstMipTailIndex)
		{
			continue;
		}
		else if (bAllowParallelBuild && FMath::Min(SrcMip.SizeX, SrcMip.SizeY) >= MinAsyncCompressionSize)
		{
			AsyncCompressionTasks.Emplace(
				TextureFormat,
				&SrcMip,
				MipIndex == FirstMipTailIndex ? CompressorCaps.NumMipsInTail : 1, // number of mips pointed to by SrcMip
				Settings,
				DebugTexturePathName,
				bImageHasAlphaChannel,
				CompressorCaps.ExtData
			);
		}
		else
		{
			PreWorkTasks.Emplace(PreWork { MipIndex, SrcMip, DestMip });
		}
	}

	ParallelForWithPreWork( TEXT("Texture.CompressMipChain.PF"),AsyncCompressionTasks.Num(),1,
		[&AsyncCompressionTasks](int32 TaskIndex)
		{
			AsyncCompressionTasks[TaskIndex].DoWork();
		},
		[&PreWorkTasks, &TextureFormat, &OutMips, &bCompressionSucceeded, &CompressorCaps, &Settings, DebugTexturePathName, FirstMipTailIndex, bImageHasAlphaChannel]()
		{
			for (PreWork& Work : PreWorkTasks)
			{
				bCompressionSucceeded = bCompressionSucceeded && TextureFormat->CompressImageEx(
					&Work.SrcMip,
					Work.MipIndex == FirstMipTailIndex ? CompressorCaps.NumMipsInTail : 1, // number of mips pointed to by SrcMip
					Settings,
					DebugTexturePathName,
					bImageHasAlphaChannel,
					CompressorCaps.ExtData,
					Work.DestMip
				);
			}
		}, 
		EParallelForFlags::Unbalanced);

	for (int32 TaskIndex = 0; TaskIndex < AsyncCompressionTasks.Num(); ++TaskIndex)
	{
		FAsyncCompressionWorker& AsynTask = AsyncCompressionTasks[TaskIndex];
		FCompressedImage2D& DestMip = OutMips[TaskIndex];
		bCompressionSucceeded = bCompressionSucceeded && AsynTask.ConsumeCompressionResults(DestMip);
	}

	for (int32 MipIndex = FirstMipTailIndex + 1; MipIndex < MipCount; ++MipIndex)
	{
		FCompressedImage2D& PrevMip = OutMips[MipIndex - 1];
		FCompressedImage2D& DestMip = OutMips[MipIndex];
		DestMip.SizeX = FMath::Max(1, PrevMip.SizeX >> 1);
		DestMip.SizeY = FMath::Max(1, PrevMip.SizeY >> 1);
		DestMip.SizeZ = Settings.bVolume ? FMath::Max(1, PrevMip.SizeZ >> 1) : PrevMip.SizeZ;
		DestMip.PixelFormat = PrevMip.PixelFormat;
	}

	if (!bCompressionSucceeded)
	{
		OutMips.Empty();
	}

	uint32 EndCycles = FPlatformTime::Cycles();
	UE_LOG(LogTextureCompressor,Verbose,TEXT("Compressed %dx%dx%d %s in %fms"),
		MipChain[0].SizeX,
		MipChain[0].SizeY,
		MipChain[0].NumSlices,
		*Settings.TextureFormatName.ToString(),
		FPlatformTime::ToMilliseconds( EndCycles-StartCycles )
		);

	return bCompressionSucceeded;
}

// only useful for normal maps, fixed bad input (denormalized normals) and improved quality (quantization artifacts)
static void NormalizeMip(FImage& InOutMip)
{
	const uint32 NumPixels = InOutMip.SizeX * InOutMip.SizeY * InOutMip.NumSlices;
	TArrayView64<FLinearColor> ImageColors = InOutMip.AsRGBA32F();
	for(uint32 CurPixelIndex = 0; CurPixelIndex < NumPixels; ++CurPixelIndex)
	{
		FLinearColor& Color = ImageColors[CurPixelIndex];

		FVector Normal = FVector(Color.R * 2.0f - 1.0f, Color.G * 2.0f - 1.0f, Color.B * 2.0f - 1.0f);

		Normal = Normal.GetSafeNormal();

		Color = FLinearColor(Normal.X * 0.5f + 0.5f, Normal.Y * 0.5f + 0.5f, Normal.Z * 0.5f + 0.5f, Color.A);
	}
}

/**
 * Texture compression module
 */
class FTextureCompressorModule : public ITextureCompressorModule
{
public:
	FTextureCompressorModule()
#if PLATFORM_WINDOWS
		:	nvTextureToolsHandle(0)
#endif	//PLATFORM_WINDOWS
	{
	}

	virtual bool BuildTexture(
		const TArray<FImage>& SourceMips,
		const TArray<FImage>& AssociatedNormalSourceMips,
		const FTextureBuildSettings& BuildSettings,
		FStringView DebugTexturePathName,
		TArray<FCompressedImage2D>& OutTextureMips,
		uint32& OutNumMipsInTail,
		uint32& OutExtData
	)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(Texture.BuildTexture);

		const ITextureFormat* TextureFormat = nullptr;

		ITextureFormatManagerModule* TFM = GetTextureFormatManager();
		if (TFM)
		{
			TextureFormat = TFM->FindTextureFormat(BuildSettings.TextureFormatName);
		}
		if (TextureFormat == nullptr)
		{
			UE_LOG(LogTextureCompressor, Warning,
				TEXT("Failed to find compressor for texture format '%s'."),
				*BuildSettings.TextureFormatName.ToString()
			);

			return false;
		}
		
		// @todo Oodle: option to dump the Source image here
		//		we have dump in TextureFormatOodle for the after-processing (before encoding) image
		//		get a dump spot for before-processing as well


		#if 0
		// @todo Oodle : use or remove me
		// experimental : detect if alpha is wanted in the mip filter process
		bool bAlpha_Before = false;
		bool bAlpha_Programmatic = false;
		if ( ! BuildSettings.bForceNoAlphaChannel && ! BuildSettings.bForceAlphaChannel )
		{
			// at this point SourceMips is still in original format (eg. BGRA8 not yet promoted to linear float)
			// bAlpha_Before can be accelerated from the source image in many cases (if it was 3-channel RGB source)
			bAlpha_Before = DetectAlphaChannel(SourceMips[0]);

			// can mip processing add alpha ?
			//  does not gaurantee it definitely does add alpha, it just might
			bAlpha_Programmatic =
				BuildSettings.bComputeBokehAlpha ||
				BuildSettings.bReplicateRed ||
				BuildSettings.bChromaKeyTexture ||
				( BuildSettings.CompositeTextureMode == CTM_NormalRoughnessToAlpha && AssociatedNormalSourceMips.Num() > 0 );
			// (note bLongLatCubemap skips these processings, may be wrong)
		}
		// if bFilterAlpha is off, we can make mips in RGB 3-channel and skip A processing
		bool bFilterAlpha = (! BuildSettings.bForceNoAlphaChannel) && ( BuildSettings.bForceAlphaChannel || bAlpha_Before || bAlpha_Programmatic );
		#endif

		TArray<FImage> IntermediateMipChain;

		// we can't use the Ex version here because it needs an FImage, which needs BuildTextureMips to be called
		const FTextureFormatCompressorCaps CompressorCaps = TextureFormat->GetFormatCapabilities();

		if (!BuildTextureMips(SourceMips, BuildSettings, CompressorCaps, IntermediateMipChain))
		{
			return false;
		}

		// apply roughness adjustment depending on normal map variation
		if (AssociatedNormalSourceMips.Num())
		{
			// check AssociatedNormalSourceMips.Format; 
			// ECompositeTextureMode is only NormalRoughness
			//  composite texture should be a normal map

			TArray<FImage> IntermediateAssociatedNormalSourceMipChain;

			FTextureBuildSettings DefaultSettings;

			// apply a smooth Gaussian filter to the top level of the normal map
			// the original comment says :
			// "helps to reduce aliasing further"
			DefaultSettings.MipSharpening = -3.5f;
			DefaultSettings.SharpenMipKernelSize = 6;
			DefaultSettings.bApplyKernelToTopMip = true;

			// important to make accurate computation with normal length
			DefaultSettings.bRenormalizeTopMip = true;

			// @todo Oodle : filtering the normal map then computing roughness is fundamentally wrong
			//  we should instead compute the roughness scalar first on the original normap map
			//  then filter on the roughness scalar

			if (!BuildTextureMips(AssociatedNormalSourceMips, DefaultSettings, CompressorCaps, IntermediateAssociatedNormalSourceMipChain))
			{
				UE_LOG(LogTextureCompressor, Warning, TEXT("Failed to generate texture mips for composite texture"));
			}

			if (!ApplyCompositeTexture(IntermediateMipChain, IntermediateAssociatedNormalSourceMipChain, BuildSettings.CompositeTextureMode, BuildSettings.CompositePower))
			{
				UE_LOG(LogTextureCompressor, Warning, TEXT("Failed to apply composite texture"));
			}
		}

		
		// DetectAlphaChannel on the top mip of the generated mip chain
		//	it is now always Linear F32
		//	BuildSettings could have programatically introduced alpha that was not in the source
		// note the order of operations in bForceAlphaChannel and bForceNoAlphaChannel (ambiguity if both are on)
		const bool bImageHasAlphaChannel = !BuildSettings.bForceNoAlphaChannel  && (BuildSettings.bForceAlphaChannel || DetectAlphaChannel(IntermediateMipChain[0]));
	
		#if 0
		// @todo Oodle : experimental : verify bFilterAlpha was right
		// if bAlpha_Programmatic, we have to recheck if alpha got made or not
		check( bImageHasAlphaChannel == bAlpha_Before || bAlpha_Programmatic );
		// if bFilterAlpha is off, we should not have alpha out
		check( bFilterAlpha || !bImageHasAlphaChannel );
		#endif

		// Set the correct biased texture size so that the compressor understands the original source image size
		// This is requires for platforms that may need to tile based on the original source texture size
		BuildSettings.TopMipSize.X = IntermediateMipChain[0].SizeX;
		BuildSettings.TopMipSize.Y = IntermediateMipChain[0].SizeY;
		BuildSettings.VolumeSizeZ = BuildSettings.bVolume ? IntermediateMipChain[0].NumSlices : 1;
		if (BuildSettings.bTextureArray)
		{
			if (BuildSettings.bCubemap)
			{
				BuildSettings.ArraySlices = IntermediateMipChain[0].NumSlices / 6;
			}
			else
			{
				BuildSettings.ArraySlices = IntermediateMipChain[0].NumSlices;
			}
		}
		else
		{
			BuildSettings.ArraySlices = 1;
		}
		
		return CompressMipChain(TextureFormat, IntermediateMipChain, BuildSettings, bImageHasAlphaChannel, DebugTexturePathName,
					OutTextureMips, OutNumMipsInTail, OutExtData);
	}

	// IModuleInterface implementation.
	void StartupModule()
	{
#if PLATFORM_WINDOWS
	#if PLATFORM_64BITS
		if (FWindowsPlatformMisc::HasAVX2InstructionSupport())
		{
			nvTextureToolsHandle = FPlatformProcess::GetDllHandle(*(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/nvTextureTools/Win64/AVX2/nvtt_64.dll")));
		}
		else
		{
			nvTextureToolsHandle = FPlatformProcess::GetDllHandle(*(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/nvTextureTools/Win64/nvtt_64.dll")));
		}
	#else	//32-bit platform
		nvTextureToolsHandle = FPlatformProcess::GetDllHandle(*(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/nvTextureTools/Win32/nvtt_.dll")));
	#endif
#endif	//PLATFORM_WINDOWS
	}

	void ShutdownModule()
	{
#if PLATFORM_WINDOWS
		FPlatformProcess::FreeDllHandle(nvTextureToolsHandle);
		nvTextureToolsHandle = 0;
#endif
	}

private:
#if PLATFORM_WINDOWS
	// Handle to the nvtt dll
	void* nvTextureToolsHandle;
#endif	//PLATFORM_WINDOWS

	bool BuildTextureMips(
		const TArray<FImage>& InSourceMips,
		const FTextureBuildSettings& BuildSettings,
		const FTextureFormatCompressorCaps& CompressorCaps,
		TArray<FImage>& OutMipChain)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Texture.BuildTextureMips);

		check(InSourceMips.Num());
		check(InSourceMips[0].SizeX > 0 && InSourceMips[0].SizeY > 0 && InSourceMips[0].NumSlices > 0);

		// Identify long-lat cubemaps.
		const bool bLongLatCubemap = BuildSettings.bLongLatSource;
		if (BuildSettings.bCubemap && !bLongLatCubemap)
		{
			if (BuildSettings.bTextureArray && (InSourceMips[0].NumSlices % 6) != 0)
			{
				// Cube array must have multiiple of 6 slices
				return false;
			}
			if (!BuildSettings.bTextureArray && InSourceMips[0].NumSlices != 6)
			{
				// Non-array cube must have exactly 6 slices
				return false;
			}
		}

		// Determine the maximum possible mip counts for source and dest.
		const int32 MaxSourceMipCount = bLongLatCubemap ?
			1 + FMath::CeilLogTwo(ComputeLongLatCubemapExtents(InSourceMips[0], BuildSettings.MaxTextureResolution)) :
			1 + FMath::CeilLogTwo(FMath::Max3(InSourceMips[0].SizeX, InSourceMips[0].SizeY, BuildSettings.bVolume ? InSourceMips[0].NumSlices : 1));
		const int32 MaxDestMipCount = 1 + FMath::CeilLogTwo(FMath::Min(CompressorCaps.MaxTextureDimension, BuildSettings.MaxTextureResolution));

		// Determine the number of mips required by BuildSettings.
		int32 NumOutputMips = (BuildSettings.MipGenSettings == TMGS_NoMipmaps) ? 1 : MaxSourceMipCount;

		int32 NumSourceMips = InSourceMips.Num();

		// See if the smallest provided mip image is still too large for the current compressor.
		int32 LevelsToUsableSource = FMath::Max(0, MaxSourceMipCount - MaxDestMipCount);
		int32 StartMip = FMath::Max(0, LevelsToUsableSource);

		if (BuildSettings.MipGenSettings == TMGS_LeaveExistingMips)
		{
			NumOutputMips = InSourceMips.Num() - StartMip;
			if (NumOutputMips <= 0)
			{
				// We can't generate 0 mip maps
				UE_LOG(LogTextureCompressor, Warning,
					TEXT("The source image has %d mips while the first mip would be %d. Please verify the maximun texture size or change the mips gen settings."),
					NumSourceMips,
					StartMip);
				return false;
			}
		}

		NumOutputMips = FMath::Min(NumOutputMips, MaxDestMipCount);


		if (BuildSettings.MipGenSettings != TMGS_LeaveExistingMips || bLongLatCubemap)
		{
			NumSourceMips = 1;
		}

		TArray<FImage> PaddedSourceMips;

		{
			const FImage& FirstSourceMipImage = InSourceMips[0];
			int32 TargetTextureSizeX = FirstSourceMipImage.SizeX;
			int32 TargetTextureSizeY = FirstSourceMipImage.SizeY;
			int32 TargetTextureSizeZ = BuildSettings.bVolume ? FirstSourceMipImage.NumSlices : 1; // Only used for volume texture.
			bool bPadOrStretchTexture = false;

			const int32 PowerOfTwoTextureSizeX = FMath::RoundUpToPowerOfTwo(TargetTextureSizeX);
			const int32 PowerOfTwoTextureSizeY = FMath::RoundUpToPowerOfTwo(TargetTextureSizeY);
			const int32 PowerOfTwoTextureSizeZ = FMath::RoundUpToPowerOfTwo(TargetTextureSizeZ);
			switch (static_cast<const ETexturePowerOfTwoSetting::Type>(BuildSettings.PowerOfTwoMode))
			{
			case ETexturePowerOfTwoSetting::None:
				break;

			case ETexturePowerOfTwoSetting::PadToPowerOfTwo:
				bPadOrStretchTexture = true;
				TargetTextureSizeX = PowerOfTwoTextureSizeX;
				TargetTextureSizeY = PowerOfTwoTextureSizeY;
				TargetTextureSizeZ = PowerOfTwoTextureSizeZ;
				break;

			case ETexturePowerOfTwoSetting::PadToSquarePowerOfTwo:
				bPadOrStretchTexture = true;
				TargetTextureSizeX = TargetTextureSizeY = FMath::Max3<int32>(PowerOfTwoTextureSizeX, PowerOfTwoTextureSizeY, PowerOfTwoTextureSizeZ);
				break;

			default:
				checkf(false, TEXT("Unknown entry in ETexturePowerOfTwoSetting::Type"));
				break;
			}

			if (bPadOrStretchTexture)
			{
				// Want to stretch or pad the texture
				bool bSuitableFormat = FirstSourceMipImage.Format == ERawImageFormat::RGBA32F;

				FImage Temp;
				if (!bSuitableFormat)
				{
					// convert to RGBA32F
					FirstSourceMipImage.CopyTo(Temp, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
				}

				// space for one source mip and one destination mip
				const FImage& SourceImage = bSuitableFormat ? FirstSourceMipImage : Temp;
				FImage& TargetImage = *new (PaddedSourceMips) FImage(TargetTextureSizeX, TargetTextureSizeY, BuildSettings.bVolume ? TargetTextureSizeZ : SourceImage.NumSlices, SourceImage.Format);
				FLinearColor FillColor = BuildSettings.PaddingColor;

				FLinearColor* TargetPtr = (FLinearColor*)TargetImage.RawData.GetData();
				FLinearColor* SourcePtr = (FLinearColor*)SourceImage.RawData.GetData();
				check(SourceImage.GetBytesPerPixel() == sizeof(FLinearColor));
				check(TargetImage.GetBytesPerPixel() == sizeof(FLinearColor));

				const int32 SourceBytesPerLine = SourceImage.SizeX * SourceImage.GetBytesPerPixel();
				const int32 DestBytesPerLine = TargetImage.SizeX * TargetImage.GetBytesPerPixel();
				for (int32 SliceIndex = 0; SliceIndex < SourceImage.NumSlices; ++SliceIndex)
				{
					for (int32 Y = 0; Y < TargetTextureSizeY; ++Y)
					{
						int32 XStart = 0;
						if (Y < SourceImage.SizeY)
						{
							XStart = SourceImage.SizeX;
							FMemory::Memcpy(TargetPtr, SourcePtr, SourceImage.SizeX * sizeof(FLinearColor));
							SourcePtr += SourceImage.SizeX;
							TargetPtr += SourceImage.SizeX;
						}

						for (int32 XPad = XStart; XPad < TargetImage.SizeX; ++XPad)
						{
							*TargetPtr++ = FillColor;
						}
					}
				}
				// Pad new slices for volume texture
				for (int32 SliceIndex = SourceImage.NumSlices; SliceIndex < TargetImage.NumSlices; ++SliceIndex)
				{
					for (int32 Y = 0; Y < TargetImage.SizeY; ++Y)
					{
						for (int32 X = 0; X< TargetImage.SizeX; ++X)
						{
							*TargetPtr++ = FillColor;
						}
					}
				}
			}
		}

		const TArray<FImage>& PostOptionalUpscaleSourceMips = (PaddedSourceMips.Num() > 0) ? PaddedSourceMips : InSourceMips;

		bool bBuildSourceImage = StartMip > (NumSourceMips - 1);

		TArray<FImage> GeneratedSourceMips;
		if (bBuildSourceImage)
		{
			// the source is larger than the compressor allows and no mip image exists to act as a smaller source.
			// We must generate a suitable source image:
			bool bSuitableFormat = PostOptionalUpscaleSourceMips.Last().Format == ERawImageFormat::RGBA32F;
			const FImage& BaseImage = PostOptionalUpscaleSourceMips.Last();

			if (BaseImage.SizeX != FMath::RoundUpToPowerOfTwo(BaseImage.SizeX) || BaseImage.SizeY != FMath::RoundUpToPowerOfTwo(BaseImage.SizeY))
			{
				UE_LOG(LogTextureCompressor, Warning,
					TEXT("Source image %dx%d (npot) prevents resizing and is too large for compressors max dimension (%d)."),
					BaseImage.SizeX,
					BaseImage.SizeY,
					CompressorCaps.MaxTextureDimension
					);
				return false;
			}

			FImage Temp;
			if (!bSuitableFormat)
			{
				// convert to RGBA32F
				BaseImage.CopyTo(Temp, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
			}

			UE_LOG(LogTextureCompressor, Verbose,
				TEXT("Source image %dx%d too large for compressors max dimension (%d). Resizing."),
				BaseImage.SizeX,
				BaseImage.SizeY,
				CompressorCaps.MaxTextureDimension
				);
			// Max Texture Size resizing happens here :
			GenerateMipChain(BuildSettings, bSuitableFormat ? BaseImage : Temp, GeneratedSourceMips, LevelsToUsableSource);

			check(GeneratedSourceMips.Num() != 0);
			// Note: The newly generated mip chain does not include the original top level mip.
			StartMip--;
		}

		const TArray<FImage>& SourceMips = bBuildSourceImage ? GeneratedSourceMips : PostOptionalUpscaleSourceMips;

		OutMipChain.Empty(NumOutputMips);
		// Copy over base mips.
		check(StartMip < SourceMips.Num());
		int32 CopyCount = SourceMips.Num() - StartMip;

		for (int32 MipIndex = StartMip; MipIndex < StartMip + CopyCount; ++MipIndex)
		{
			const FImage& Image = SourceMips[MipIndex];

			// create base for the mip chain
			FImage* Mip = new(OutMipChain) FImage();

			if (bLongLatCubemap)
			{
				// Generate the base mip from the long-lat source image.
				GenerateBaseCubeMipFromLongitudeLatitude2D(Mip, Image, BuildSettings.MaxTextureResolution, BuildSettings.SourceEncodingOverride);
	
				// note the break here skips other adjustments:
				break;
			}
			else
			{
				// copy base source content to the base of the mip chain
				if(BuildSettings.bApplyKernelToTopMip)
				{
					FImage Temp;
					Image.Linearize(BuildSettings.SourceEncodingOverride, Temp);
					if(BuildSettings.bRenormalizeTopMip)
					{
						NormalizeMip(Temp);
					}

					GenerateTopMip(Temp, *Mip, BuildSettings);
				}
				else
				{
					Image.Linearize(BuildSettings.SourceEncodingOverride, *Mip);

					if(BuildSettings.bRenormalizeTopMip)
					{
						NormalizeMip(*Mip);
					}
				}				
			}

			if (BuildSettings.Downscale > 1.f)
			{
				FTextureDownscaleSettings DownscaleSettings;
				DownscaleSettings.Downscale = BuildSettings.Downscale;
				DownscaleSettings.DownscaleOptions = BuildSettings.DownscaleOptions;
				DownscaleSettings.BlockSize = 4;
		
				DownscaleImage(*Mip, *Mip, DownscaleSettings);
			}

			if (BuildSettings.bHasColorSpaceDefinition)
			{
				Mip->TransformToWorkingColorSpace(
					FVector2D(BuildSettings.RedChromaticityCoordinate),
					FVector2D(BuildSettings.GreenChromaticityCoordinate),
					FVector2D(BuildSettings.BlueChromaticityCoordinate),
					FVector2D(BuildSettings.WhiteChromaticityCoordinate),
					static_cast<UE::Color::EChromaticAdaptationMethod>(BuildSettings.ChromaticAdaptationMethod));
			}

			// Apply color adjustments
			AdjustImageColors(*Mip, BuildSettings);

			if (BuildSettings.bComputeBokehAlpha)
			{
				// To get the occlusion in the BokehDOF shader working for all Bokeh textures.
				ComputeBokehAlpha(*Mip);
			}
			if (BuildSettings.bFlipGreenChannel)
			{
				FlipGreenChannel(*Mip);
			}
		}

		// Generate any missing mips in the chain.
		if (NumOutputMips > OutMipChain.Num())
		{
			// Do angular filtering of cubemaps if requested.
			if (BuildSettings.MipGenSettings == TMGS_Angular)
			{
				GenerateAngularFilteredMips(OutMipChain, NumOutputMips, BuildSettings.DiffuseConvolveMipLevel);
			}
			else
			{
				GenerateMipChain(BuildSettings, OutMipChain.Last(), OutMipChain);
			}
		}
		check(OutMipChain.Num() == NumOutputMips);

		// Apply post-mip generation adjustments.
		if (BuildSettings.bReplicateRed)
		{
			ReplicateRedChannel(OutMipChain);
		}
		else if (BuildSettings.bReplicateAlpha)
		{
			ReplicateAlphaChannel(OutMipChain);
		}
		if (BuildSettings.bApplyYCoCgBlockScale)
		{
			ApplyYCoCgBlockScale(OutMipChain);
		}

		return true;
	}

	// @param CompositeTextureMode original type ECompositeTextureMode
	// @return true on success, false on failure. Can fail due to bad mismatched dimensions of incomplete mip chains.
	bool ApplyCompositeTexture(TArray<FImage>& RoughnessSourceMips, const TArray<FImage>& NormalSourceMips, uint8 CompositeTextureMode, float CompositePower)
	{
		uint32 MinLevel = FMath::Min(RoughnessSourceMips.Num(), NormalSourceMips.Num());

		if( RoughnessSourceMips[RoughnessSourceMips.Num() - MinLevel].SizeX != NormalSourceMips[NormalSourceMips.Num() - MinLevel].SizeX || 
			RoughnessSourceMips[RoughnessSourceMips.Num() - MinLevel].SizeY != NormalSourceMips[NormalSourceMips.Num() - MinLevel].SizeY )
		{
			UE_LOG(LogTextureCompressor, Warning, TEXT("Couldn't apply composite texture as RoughnessSourceMips (mip %d, %d x %d) doesn't match NormalSourceMips (mip %d, %d x %d); mipchain might be mismatched/incomplete"),
				RoughnessSourceMips.Num() - MinLevel,
				RoughnessSourceMips[RoughnessSourceMips.Num() - MinLevel].SizeX,
				RoughnessSourceMips[RoughnessSourceMips.Num() - MinLevel].SizeY,
				NormalSourceMips.Num() - MinLevel,
				NormalSourceMips[NormalSourceMips.Num() - MinLevel].SizeX,
				NormalSourceMips[NormalSourceMips.Num() - MinLevel].SizeY
				);
			return false;
		}

		for(uint32 Level = 0; Level < MinLevel; ++Level)
		{
			::ApplyCompositeTexture(RoughnessSourceMips[RoughnessSourceMips.Num() - 1 - Level], NormalSourceMips[NormalSourceMips.Num() - 1 - Level], CompositeTextureMode, CompositePower);
		}

		return true;
	}
};

IMPLEMENT_MODULE(FTextureCompressorModule, TextureCompressor)
