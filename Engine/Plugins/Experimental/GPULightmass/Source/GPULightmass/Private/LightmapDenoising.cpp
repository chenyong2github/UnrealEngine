// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightmapDenoising.h"
#include "GPULightmassCommon.h"

FDenoiserFilterSet::FDenoiserFilterSet(FDenoiserContext& Context, FIntPoint NewSize, bool bSHDenoiser)
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
	filter.set("directional", bSHDenoiser);
	filter.set("inputScale", bSHDenoiser ? 0.5f : 1.0f);
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

	{

		// Resizing the filter is a super expensive operation
		// Round things into size bins to reduce number of resizes
		FDenoiserFilterSet& FilterSet = DenoiserContext.GetFilterForSize(FIntPoint(FMath::DivideAndRoundUp(Size.X, 64) * 64, FMath::DivideAndRoundUp(Size.Y, 64) * 64));

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

				// Copy to LQ coefficients
				LightSampleData[Y * Size.X + X].Coefficients[2][0] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				LightSampleData[Y * Size.X + X].Coefficients[2][1] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				LightSampleData[Y * Size.X + X].Coefficients[2][2] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}
	}

	{
		// Resizing the filter is a super expensive operation
		// Round things into size bins to reduce number of resizes
		FDenoiserFilterSet& FilterSet = DenoiserContext.GetFilterForSize(FIntPoint(FMath::DivideAndRoundUp(Size.X, 64) * 64, FMath::DivideAndRoundUp(Size.Y, 64) * 64), true);

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][0] = LightSampleData[Y * Size.X + X].Coefficients[1][0];
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][1] = LightSampleData[Y * Size.X + X].Coefficients[1][1];
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][2] = LightSampleData[Y * Size.X + X].Coefficients[1][2];
			}
		}

		FilterSet.Execute();

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				LightSampleData[Y * Size.X + X].Coefficients[1][0] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				LightSampleData[Y * Size.X + X].Coefficients[1][1] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				LightSampleData[Y * Size.X + X].Coefficients[1][2] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];

				// Copy to LQ coefficients
				LightSampleData[Y * Size.X + X].Coefficients[3][0] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				LightSampleData[Y * Size.X + X].Coefficients[3][1] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				LightSampleData[Y * Size.X + X].Coefficients[3][2] = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}
	}
}

void DenoiseRawData(
	FIntPoint Size,
	TArray<FLinearColor, FAnsiAllocator>& IncidentLighting,
	TArray<FLinearColor, FAnsiAllocator>& LuminanceSH,
	FDenoiserContext& DenoiserContext, 
	bool bPrepadTexels)
{
	for (int32 Y = 0; Y < Size.Y; Y++)
	{
		for (int32 X = 0; X < Size.X; X++)
		{
			if (IncidentLighting[Y * Size.X + X].A >= 0.0f)
			{
				float DirLuma[4];
				DirLuma[0] = LuminanceSH[Y * Size.X + X].A / 0.282095f; // Revert diffuse conv done in LightmapEncoding.ush for preview to get actual luma
				DirLuma[1] = LuminanceSH[Y * Size.X + X].R;	// Keep the rest as we need to diffuse conv them anyway
				DirLuma[2] = LuminanceSH[Y * Size.X + X].G;	// Keep the rest as we need to diffuse conv them anyway
				DirLuma[3] = LuminanceSH[Y * Size.X + X].B;	// Keep the rest as we need to diffuse conv them anyway

				float DirScale = 1.0f / FMath::Max(0.0001f, DirLuma[0]);
				float ColorScale = DirLuma[0];

				{
					IncidentLighting[Y * Size.X + X].R = ColorScale * IncidentLighting[Y * Size.X + X].R * IncidentLighting[Y * Size.X + X].R;
					IncidentLighting[Y * Size.X + X].G = ColorScale * IncidentLighting[Y * Size.X + X].G * IncidentLighting[Y * Size.X + X].G;
					IncidentLighting[Y * Size.X + X].B = ColorScale * IncidentLighting[Y * Size.X + X].B * IncidentLighting[Y * Size.X + X].B;

					LuminanceSH[Y * Size.X + X].A = 1.0f;
					LuminanceSH[Y * Size.X + X].R = DirLuma[1] * DirScale;
					LuminanceSH[Y * Size.X + X].G = DirLuma[2] * DirScale;
					LuminanceSH[Y * Size.X + X].B = DirLuma[3] * DirScale;
				}
			}
			else
			{
				IncidentLighting[Y * Size.X + X].R = 0;
				IncidentLighting[Y * Size.X + X].G = 0;
				IncidentLighting[Y * Size.X + X].B = 0;

				LuminanceSH[Y * Size.X + X].A = 0;
				LuminanceSH[Y * Size.X + X].R = 0;
				LuminanceSH[Y * Size.X + X].G = 0;
				LuminanceSH[Y * Size.X + X].B = 0;
			}
		}
	}

	if (bPrepadTexels)
	{
		TArray<float> DilationMask;
		DilationMask.AddZeroed(Size.X * Size.Y);

		const int32 TotalIteration = 2;

		for (int32 IterationIndex = 0; IterationIndex < TotalIteration; IterationIndex++)
		{
			for (int32 Y = 0; Y < Size.Y; Y++)
			{
				for (int32 X = 0; X < Size.X; X++)
				{
					if (!(IncidentLighting[Y * Size.X + X].A >= 0.0f))
					{
						for (int32 dx = -1; dx <= 1; dx++)
						{
							for (int32 dy = -1; dy <= 1; dy++)
							{
								if (Y + dy >= 0 && Y + dy < Size.Y && X + dx >= 0 && X + dx < Size.X)
								{
									if (IncidentLighting[(Y + dy) * Size.X + (X + dx)].A >= 0.0f)
									{
										IncidentLighting[Y * Size.X + X] = IncidentLighting[(Y + dy) * Size.X + (X + dx)];
										LuminanceSH[Y * Size.X + X] = LuminanceSH[(Y + dy) * Size.X + (X + dx)];
										DilationMask[Y * Size.X + X] = IncidentLighting[Y * Size.X + X].A;
										IncidentLighting[Y * Size.X + X].A = -1.0f;
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
					if (DilationMask[Y * Size.X + X] > 0.0f)
					{
						IncidentLighting[Y * Size.X + X].A = DilationMask[Y * Size.X + X];
						DilationMask[Y * Size.X + X] = 0;
					}
				}
			}
		}
	}

	{
		// Resizing the filter is a super expensive operation
		// Round things into size bins to reduce number of resizes
		FDenoiserFilterSet& FilterSet = DenoiserContext.GetFilterForSize(FIntPoint(FMath::DivideAndRoundUp(Size.X, 64) * 64, FMath::DivideAndRoundUp(Size.Y, 64) * 64));

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][0] = IncidentLighting[Y * Size.X + X].R;
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][1] = IncidentLighting[Y * Size.X + X].G;
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][2] = IncidentLighting[Y * Size.X + X].B;
			}
		}

		FilterSet.Execute();

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				IncidentLighting[Y * Size.X + X].R = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				IncidentLighting[Y * Size.X + X].G = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				IncidentLighting[Y * Size.X + X].B = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}
	}

	{
		// Resizing the filter is a super expensive operation
		// Round things into size bins to reduce number of resizes
		FDenoiserFilterSet& FilterSet = DenoiserContext.GetFilterForSize(FIntPoint(FMath::DivideAndRoundUp(Size.X, 64) * 64, FMath::DivideAndRoundUp(Size.Y, 64) * 64), true);

		FLinearColor MinValue(MAX_flt, MAX_flt, MAX_flt);

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][0] = LuminanceSH[Y * Size.X + X].R;
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][1] = LuminanceSH[Y * Size.X + X].G;
				FilterSet.InputBuffer[Y * FilterSet.Size.X + X][2] = LuminanceSH[Y * Size.X + X].B;
			}
		}

		FilterSet.Execute();

		for (int32 Y = 0; Y < Size.Y; Y++)
		{
			for (int32 X = 0; X < Size.X; X++)
			{
				LuminanceSH[Y * Size.X + X].R = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][0];
				LuminanceSH[Y * Size.X + X].G = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][1];
				LuminanceSH[Y * Size.X + X].B = FilterSet.OutputBuffer[Y * FilterSet.Size.X + X][2];
			}
		}
	}

	for (int32 Y = 0; Y < Size.Y; Y++)
	{
		for (int32 X = 0; X < Size.X; X++)
		{
			IncidentLighting[Y * Size.X + X].R = FMath::Sqrt(IncidentLighting[Y * Size.X + X].R);
			IncidentLighting[Y * Size.X + X].G = FMath::Sqrt(IncidentLighting[Y * Size.X + X].G);
			IncidentLighting[Y * Size.X + X].B = FMath::Sqrt(IncidentLighting[Y * Size.X + X].B);

			LuminanceSH[Y * Size.X + X].A *= 0.282095f;
		}
	}
}
