// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

struct FOpenVDBData
{
	FVector VolumeActiveAABBMin;
	FVector VolumeActiveAABBMax;
	FVector VolumeActiveDim;
	FVector VolumeVoxelSize;
	bool bIsInWorldSpace;
	bool bHasUniformVoxels;
};

enum class EOpenVDBGridFormat : uint8
{
	Float = 0,
};

struct FOpenVDBGridInfo
{
	uint32 Index;
	EOpenVDBGridFormat Format;
	FString Name;
	FString DisplayString; // Contains both Name and Format
};

bool IsOpenVDBDataValid(FOpenVDBData& OpenVDBData, const FString& Filename);

bool FindDensityGridIndex(TArray<uint8>& SourceFile, const FString& Filename, uint32* OutGridIndex, FOpenVDBData* OutOVDBData);

bool GetOpenVDBGridInfo(TArray<uint8>& SourceFile, const FString& Filename, TArray<FOpenVDBGridInfo>* OutGridInfo);

bool ConvertOpenVDBToSparseVolumeTexture(
	TArray<uint8>& SourceFile,
	uint32 GridIndex,
	struct FSparseVolumeAssetHeader* OutHeader,
	TArray<uint32>* OutDensityPage,
	TArray<uint8>* OutDensityData,
	bool bOverrideActiveMinMax,
	FVector ActiveMin,
	FVector ActiveMax);

const TCHAR* OpenVDBGridFormatToString(EOpenVDBGridFormat Format);

#endif // WITH_EDITOR