// Copyright Epic Games, Inc. All Rights Reserved.

#include "GerstnerWaterWaveViewExtension.h"
#include "GerstnerWaterWaveSubsystem.h"
#include "WaterBodyActor.h"
#include "GerstnerWaterWaves.h"

FGerstnerWaterWaveViewExtension::FGerstnerWaterWaveViewExtension(const FAutoRegister& AutoReg) : FSceneViewExtensionBase(AutoReg)
{
}

FGerstnerWaterWaveViewExtension::~FGerstnerWaterWaveViewExtension()
{
}

void FGerstnerWaterWaveViewExtension::Initialize()
{
	if (UGerstnerWaterWaveSubsystem* GerstnerWaterWaveSubsystem = GEngine->GetEngineSubsystem<UGerstnerWaterWaveSubsystem>())
	{
		GerstnerWaterWaveSubsystem->Register(this);
	}
}

void FGerstnerWaterWaveViewExtension::Deinitialize()
{
	if (UGerstnerWaterWaveSubsystem* GerstnerWaterWaveSubsystem = GEngine->GetEngineSubsystem<UGerstnerWaterWaveSubsystem>())
	{
		GerstnerWaterWaveSubsystem->Unregister(this);
	}
}

void FGerstnerWaterWaveViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
	if (bRebuildGPUData && WaterBodies)
	{
		TResourceArray<FVector4> WaterIndirectionBuffer;
		TResourceArray<FVector4> WaterDataBuffer;

		// Some max value
		const int32 MaxWavesPerWaterBody = 4096;

		const int32 NumFloat4PerWave = 2;

		for (const AWaterBody* WaterBody : *WaterBodies)
		{
			WaterIndirectionBuffer.AddZeroed();

			if (WaterBody)
			{
				if (const UWaterWavesBase* WaterWavesBase = WaterBody->GetWaterWaves())
				{
					if (const UGerstnerWaterWaves* GerstnerWaves = Cast<const UGerstnerWaterWaves>(WaterWavesBase->GetWaterWaves()))
					{
						const TArray<FGerstnerWave>& Waves = GerstnerWaves->GetGerstnerWaves();

						// Where the data for this water body starts (including header)
						const int32 DataBaseIndex = WaterDataBuffer.Num();

						// Allocate for the waves in this water body
						const int32 NumWaves = FMath::Min(Waves.Num(), MaxWavesPerWaterBody);
						WaterDataBuffer.AddZeroed(NumWaves * NumFloat4PerWave);

						// The header is a vector4 and contains generic per-water body information
						// X: Index to the wave data
						// Y: Num waves
						// Z: TargetWaveMaskDepth
						// W: Unused
						FVector4& Header = WaterIndirectionBuffer.Last();
						Header.X = DataBaseIndex;
						Header.Y = NumWaves;
						Header.Z = WaterBody->TargetWaveMaskDepth;
						Header.W = 0.0f;

						for (int32 i = 0; i < NumWaves; i++)
						{
							const FGerstnerWave& Wave = Waves[i];

							const int32 WaveIndex = DataBaseIndex + (i * NumFloat4PerWave);

							WaterDataBuffer[WaveIndex] = FVector4(Wave.Direction.X, Wave.Direction.Y, Wave.WaveLength, Wave.Amplitude);
							WaterDataBuffer[WaveIndex + 1] = FVector4(Wave.Steepness, 0.0f, 0.0f, 0.0f);
						}
					}
				}
			}
		}

		if (WaterIndirectionBuffer.Num() == 0)
		{
			WaterIndirectionBuffer.AddZeroed();
		}
		
		if (WaterDataBuffer.Num() == 0)
		{
			WaterDataBuffer.AddZeroed();
		}

		ENQUEUE_RENDER_COMMAND(AllocateWaterInstanceDataBuffer)
		(
			[this, WaterDataBuffer, WaterIndirectionBuffer](FRHICommandListImmediate& RHICmdList) mutable
			{
				FRHIResourceCreateInfo CreateInfoData;
				CreateInfoData.ResourceArray = &WaterDataBuffer;
				CreateInfoData.DebugName = TEXT("WaterDataBuffer");
				DataBuffer = RHICreateStructuredBuffer(sizeof(FVector4), WaterDataBuffer.GetResourceDataSize(), BUF_StructuredBuffer | BUF_ShaderResource | BUF_Static, ERHIAccess::SRVMask, CreateInfoData);
				DataSRV = RHICreateShaderResourceView(DataBuffer);

				FRHIResourceCreateInfo CreateInfoIndirection;
				CreateInfoIndirection.ResourceArray = &WaterIndirectionBuffer;
				CreateInfoIndirection.DebugName = TEXT("WaterIndirectionBuffer");
				IndirectionBuffer = RHICreateStructuredBuffer(sizeof(FVector4), WaterIndirectionBuffer.GetResourceDataSize(), BUF_StructuredBuffer | BUF_ShaderResource | BUF_Static, ERHIAccess::SRVMask, CreateInfoIndirection);
				IndirectionSRV = RHICreateShaderResourceView(IndirectionBuffer);
			}
		);

		bRebuildGPUData = false;
	}
}

void FGerstnerWaterWaveViewExtension::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
	if (DataSRV && IndirectionSRV)
	{
		InView.WaterDataBuffer = DataSRV;
		InView.WaterIndirectionBuffer = IndirectionSRV;
	}
}