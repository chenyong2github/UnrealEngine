// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightmapDenoising.h"

FDenoiserFilterSet::FDenoiserFilterSet(FDenoiserContext& Context, FIntPoint NewSize)
	: Context(Context)
{
	double Start = FPlatformTime::Seconds();

	Size = NewSize;
	InputBuffer.Empty(Size.X * Size.Y);
	OutputBuffer.Empty(Size.X * Size.Y);

	InputBuffer.AddZeroed(Size.X * Size.Y);
	OutputBuffer.AddZeroed(Size.X * Size.Y);

#if WITH_INTELOIDN
	filter = Context.OIDNDevice.newFilter("RTLightmap");
	filter.setImage("color", InputBuffer.GetData(), oidn::Format::Float3, Size.X, Size.Y);
	filter.setImage("output", OutputBuffer.GetData(), oidn::Format::Float3, Size.X, Size.Y);
	filter.set("hdr", true);
	filter.commit();
#endif

	Context.FilterInitTime += FPlatformTime::Seconds() - Start;
	Context.NumFilterInit++;
}

void FDenoiserFilterSet::Execute()
{
	double Start = FPlatformTime::Seconds();

#if WITH_INTELOIDN
	filter.execute();
#endif

	Context.FilterExecutionTime += FPlatformTime::Seconds() - Start;
	Context.NumFilterExecution++;
}

void FDenoiserFilterSet::Clear()
{
	FMemory::Memset(InputBuffer.GetData(), 0, InputBuffer.GetTypeSize() * InputBuffer.Num());
	FMemory::Memset(OutputBuffer.GetData(), 0, OutputBuffer.GetTypeSize() * OutputBuffer.Num());
}

void DenoiseLightSampleData(FIntPoint Size, TArray<FLightSampleData>& LightSampleData, FDenoiserContext& DenoiserContext, bool bPrepadTexels)
{
	if (bPrepadTexels)
	{
		TArray<int32> DilationMask;
		DilationMask.AddZeroed(Size.X * Size.Y);

		const int32 TotalIteration = 2;

		for (int32 IterationIndex = 0; IterationIndex < TotalIteration; IterationIndex++)
		{
			for (int32 Y = 0; Y < Size.Y; Y++)
			{
				for (int32 X = 0; X < Size.X; X++)
				{
					if (!LightSampleData[Y * Size.X + X].bIsMapped)
					{
						for (int32 dx = -1; dx <= 1; dx++)
						{
							for (int32 dy = -1; dy <= 1; dy++)
							{
								if (Y + dy >= 0 && Y + dy < Size.Y && X + dx >= 0 && X + dx < Size.X)
								{
									if (LightSampleData[(Y + dy) * Size.X + (X + dx)].bIsMapped)
									{
										LightSampleData[Y * Size.X + X] = LightSampleData[(Y + dy) * Size.X + (X + dx)];
										LightSampleData[Y * Size.X + X].bIsMapped = false;
										DilationMask[Y * Size.X + X] = 1;
									}
								}
							}
						}
					}
				}
			}

			for (int32 Y = 0; Y < Size.Y; Y++)
			{
				for (int32 X = 0; X < Size.X; X++)
				{
					if (DilationMask[Y * Size.X + X] == 1)
					{
						DilationMask[Y * Size.X + X] = 0;
						LightSampleData[Y * Size.X + X].bIsMapped = true;
					}
				}
			}
		}
	}

	// Resizing the filter is a super expensive operation
	// Round things into size bins to reduce number of resizes
	FDenoiserFilterSet& FilterSet = DenoiserContext.GetFilterForSize(FIntPoint(FMath::DivideAndRoundUp(Size.X, 64) * 64, FMath::DivideAndRoundUp(Size.Y, 64) * 64));

	{
		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][0] = LightSampleData[Y * Size.X + X].Coefficients[0][0];
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][1] = LightSampleData[Y * Size.X + X].Coefficients[0][1];
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][2] = LightSampleData[Y * Size.X + X].Coefficients[0][2];
			}
		}

		FilterSet.Execute();

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				LightSampleData[Y * Size.X + X].Coefficients[0][0] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				LightSampleData[Y * Size.X + X].Coefficients[0][1] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				LightSampleData[Y * Size.X + X].Coefficients[0][2] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}
	}

	{
		TArray<FVector> SHPositiveOutput;
		TArray<FVector> SHNegativeOutput;
		SHPositiveOutput.AddZeroed(Size.X * Size.Y);
		SHNegativeOutput.AddZeroed(Size.X * Size.Y);

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][0] = FMath::Max(0.0f, LightSampleData[Y * Size.X + X].Coefficients[1][0]);
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][1] = FMath::Max(0.0f, LightSampleData[Y * Size.X + X].Coefficients[1][1]);
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][2] = FMath::Max(0.0f, LightSampleData[Y * Size.X + X].Coefficients[1][2]);
			}
		}

		FilterSet.Execute();

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				 SHPositiveOutput[Y * Size.X + X][0] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				 SHPositiveOutput[Y * Size.X + X][1] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				 SHPositiveOutput[Y * Size.X + X][2] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][0] = FMath::Max(0.0f, -LightSampleData[Y * Size.X + X].Coefficients[1][0]);
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][1] = FMath::Max(0.0f, -LightSampleData[Y * Size.X + X].Coefficients[1][1]);
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][2] = FMath::Max(0.0f, -LightSampleData[Y * Size.X + X].Coefficients[1][2]);
			}
		}

		FilterSet.Execute();

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				SHNegativeOutput[Y * Size.X + X][0] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				SHNegativeOutput[Y * Size.X + X][1] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				SHNegativeOutput[Y * Size.X + X][2] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				LightSampleData[Y * Size.X + X].Coefficients[1][0] = SHPositiveOutput[Y * Size.X + X][0] - SHNegativeOutput[Y * Size.X + X][0];
				LightSampleData[Y * Size.X + X].Coefficients[1][1] = SHPositiveOutput[Y * Size.X + X][1] - SHNegativeOutput[Y * Size.X + X][1];
				LightSampleData[Y * Size.X + X].Coefficients[1][2] = SHPositiveOutput[Y * Size.X + X][2] - SHNegativeOutput[Y * Size.X + X][2];
			}
		}
	}
}
