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
		TResourceArray<FVector4> WaterDataBuffer;

		const int32 TempMaxNumWaves = 32; // Has to match MAX_NUM_WAVES in GersnerWaveFunctions.ush
		// Two FVector4 per wave plus one FVector4 for other info
		const int32 NumVec4 = 2 * TempMaxNumWaves + 1; 

		for (const AWaterBody* WaterBody : *WaterBodies)
		{
			WaterDataBuffer.AddZeroed(NumVec4);
			
			if (WaterBody)
			{
				if (const UWaterWavesBase* WaterWavesBase = WaterBody->GetWaterWaves())
				{
					if (const UGerstnerWaterWaves* GerstnerWaves = Cast<const UGerstnerWaterWaves>(WaterWavesBase->GetWaterWaves()))
					{
						const TArray<FGerstnerWave>& Waves = GerstnerWaves->GetGerstnerWaves();

						const int32 BaseIndex = WaterBody->WaterBodyIndex * NumVec4;
						const int32 WaveDataBaseIndex = BaseIndex + 1;

						const int32 NumWaves = FMath::Min(Waves.Num(), TempMaxNumWaves);

						// The first vector4 of the buffer contains generic per-water body information :
						WaterDataBuffer[BaseIndex].X = NumWaves;
						WaterDataBuffer[BaseIndex].Y = WaterBody->TargetWaveMaskDepth;

						for (int32 i = 0; i < NumWaves; i++)
						{
							const FGerstnerWave& Wave = Waves[i];

							const int32 WaveIndex = WaveDataBaseIndex + (i * 2);

							WaterDataBuffer[WaveIndex] = FVector4(Wave.Direction.X, Wave.Direction.Y, Wave.WaveLength, Wave.Amplitude);
							WaterDataBuffer[WaveIndex + 1] = FVector4(Wave.Steepness, 0.0f, 0.0f, 0.0f);
						}
					}
				}
			}
		}

		if (WaterDataBuffer.Num() == 0)
		{
			WaterDataBuffer.AddZeroed();
		}

		ENQUEUE_RENDER_COMMAND(AllocateWaterInstanceDataBuffer)
		(
			[this, WaterDataBuffer](FRHICommandListImmediate& RHICmdList) mutable
			{
				FRHIResourceCreateInfo CreateInfo;
				CreateInfo.ResourceArray = &WaterDataBuffer;
				CreateInfo.DebugName = TEXT("WaterBuffer");
				Buffer = RHICreateStructuredBuffer(sizeof(FVector4), WaterDataBuffer.GetResourceDataSize(), BUF_StructuredBuffer | BUF_ShaderResource | BUF_Dynamic, ERHIAccess::SRVMask, CreateInfo);
				SRV = RHICreateShaderResourceView(Buffer);
			}
		);

		bRebuildGPUData = false;
	}
}

void FGerstnerWaterWaveViewExtension::PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
	if (SRV)
	{
		InView.WaterDataBuffer = SRV;
	}
}