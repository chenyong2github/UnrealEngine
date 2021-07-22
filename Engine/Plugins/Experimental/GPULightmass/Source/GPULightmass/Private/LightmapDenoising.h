// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LightmapEncoding.h"

#ifndef WITH_INTELOIDN
	#define WITH_INTELOIDN 0
#endif

#if WITH_INTELOIDN
#include "OpenImageDenoise/oidn.hpp"
#endif

// TBB suffers from extreme fragmentation problem in editor
#include "Core/Private/HAL/Allocators/AnsiAllocator.h"

struct FDenoiserFilterSet
{
	struct FDenoiserContext& Context;

#if WITH_INTELOIDN
	oidn::FilterRef filter;
#endif

	FIntPoint Size;
	TArray<FVector> InputBuffer;
	TArray<FVector> OutputBuffer;

	FDenoiserFilterSet(FDenoiserContext& Context, FIntPoint NewSize, bool bSHDenoiser = false);

	void Execute();

	void Clear();
};

struct FDenoiserContext
{
	int32 NumFilterInit = 0;
	int32 NumFilterExecution = 0;
	double FilterInitTime = 0.0;
	double FilterExecutionTime = 0.0;

#if WITH_INTELOIDN
	oidn::DeviceRef OIDNDevice;
#endif

	TMap<FIntPoint, FDenoiserFilterSet> Filters;
	TMap<FIntPoint, FDenoiserFilterSet> SHFilters;

	FDenoiserContext()
	{
#if WITH_INTELOIDN
		OIDNDevice = oidn::newDevice();
		OIDNDevice.commit();
#endif
	}

	~FDenoiserContext()
	{
		UE_LOG(LogTemp, Log, TEXT("Denoising: %.2lfs initializing filters (%d), %.2lfs executing filters (%d)"), FilterInitTime, NumFilterInit, FilterExecutionTime, NumFilterExecution);
	}

	FDenoiserFilterSet& GetFilterForSize(FIntPoint Size, bool bSHDenoiser = false)
	{
		if (!bSHDenoiser)
		{
			if (!Filters.Contains(Size))
			{
				Filters.Add(Size, FDenoiserFilterSet(*this, Size));
			}

			Filters[Size].Clear();

			return Filters[Size];
		}
		else
		{
			if (!SHFilters.Contains(Size))
			{
				SHFilters.Add(Size, FDenoiserFilterSet(*this, Size, true));
			}

			SHFilters[Size].Clear();

			return SHFilters[Size];
		}
	}
};

void DenoiseLightSampleData(FIntPoint Size, TArray<FLightSampleData>& LightSampleData, FDenoiserContext& DenoiserContext, bool bPrepadTexels = true);

void DenoiseRawData(
	FIntPoint Size,
	TArray<FLinearColor, FAnsiAllocator>& IncidentLighting,
	TArray<FLinearColor, FAnsiAllocator>& LuminanceSH,
	FDenoiserContext& DenoiserContext,
	bool bPrepadTexels = true);