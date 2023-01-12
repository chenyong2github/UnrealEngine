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

enum class EOpenVDBGridType : uint8
{
	Unknown = 0,
	Float = 1,
	Float2 = 2,
	Float3 = 3,
	Float4 = 4,
	Double = 5,
	Double2 = 6,
	Double3 = 7,
	Double4 = 8,
};

struct FOpenVDBGridInfo
{
	uint32 Index;
	uint32 NumComponents;
	EOpenVDBGridType Type;
	FString Name;
	FString DisplayString; // Contains Index (into source file grids), Type and Name
	FOpenVDBData OpenVDBData;
};

bool IsOpenVDBDataValid(const FOpenVDBData& OpenVDBData, const FString& Filename);

bool FindDensityGridIndex(TArray<uint8>& SourceFile, const FString& Filename, uint32* OutGridIndex, FOpenVDBData* OutOVDBData);

bool GetOpenVDBGridInfo(TArray<uint8>& SourceFile, TArray<FOpenVDBGridInfo>* OutGridInfo);

bool ConvertOpenVDBToSparseVolumeTexture(
	TArray<uint8>& SourceFile,
	struct FSparseVolumeRawSourcePackedData& PackedDataA,
	struct FSparseVolumeRawSourcePackedData& PackedDataB,
	struct FOpenVDBToSVTConversionResult* OutResult,
	bool bOverrideActiveMinMax,
	FVector ActiveMin,
	FVector ActiveMax);

const TCHAR* OpenVDBGridTypeToString(EOpenVDBGridType Type);

#endif // WITH_EDITOR