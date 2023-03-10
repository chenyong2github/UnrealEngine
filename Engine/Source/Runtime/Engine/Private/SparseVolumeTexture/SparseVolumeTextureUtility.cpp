// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTextureUtility.h"

namespace UE
{
namespace SVT
{
namespace Private
{
	float F16ToF32(uint16 Packed)
	{
		FFloat16 F16{};
		F16.Encoded = Packed;
		return F16.GetFloat();
	};
} // Private
} // SVT
} // UE

uint32 UE::SVT::PackPageTableEntry(const FIntVector3& Coord)
{
	// A page encodes the physical tile coord as unsigned int of 11 11 10 bits
	// This means a page coord cannot be larger than 2047 for x and y and 1023 for z
	// which mean we cannot have more than 2048*2048*1024 = 4 Giga tiles of 16^3 tiles.
	uint32 Result = (Coord.X & 0x7FF) | ((Coord.Y & 0x7FF) << 11) | ((Coord.Z & 0x3FF) << 22);
	return Result;
}

FIntVector3 UE::SVT::UnpackPageTableEntry(uint32 Packed)
{
	FIntVector3 Result;
	Result.X = Packed & 0x7FF;
	Result.Y = (Packed >> 11) & 0x7FF;
	Result.Z = (Packed >> 22) & 0x3FF;
	return Result;
}

FVector4f UE::SVT::ReadVoxel(int64 VoxelIndex, const uint8* TileData, EPixelFormat Format)
{
	using namespace UE::SVT::Private;
	if (Format == PF_Unknown)
	{
		return FVector4f();
	}
	switch (Format)
	{
	case PF_R8:
		return FVector4f(TileData[VoxelIndex] / 255.0f, 0.0f, 0.0f, 0.0f);
	case PF_R8G8:
		return FVector4f(TileData[VoxelIndex * 2 + 0] / 255.0f, TileData[VoxelIndex * 2 + 1] / 255.0f, 0.0f, 0.0f);
	case PF_R8G8B8A8:
		return FVector4f(TileData[VoxelIndex * 4 + 0] / 255.0f, TileData[VoxelIndex * 4 + 1] / 255.0f, TileData[VoxelIndex * 4 + 2] / 255.0f, TileData[VoxelIndex * 4 + 3] / 255.0f);
	case PF_R16F:
		return FVector4f(F16ToF32(((const uint16*)TileData)[VoxelIndex]), 0.0f, 0.0f, 0.0f);
	case PF_G16R16F:
		return FVector4f(F16ToF32(((const uint16*)TileData)[VoxelIndex * 2 + 0]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 2 + 1]), 0.0f, 0.0f);
	case PF_FloatRGBA:
		return FVector4f(F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 0]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 1]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 2]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 3]));
	case PF_R32_FLOAT:
		return FVector4f(((const float*)TileData)[VoxelIndex], 0.0f, 0.0f, 0.0f);
	case PF_G32R32F:
		return FVector4f(((const float*)TileData)[VoxelIndex * 2 + 0], ((const float*)TileData)[VoxelIndex * 2 + 1], 0.0f, 0.0f);
	case PF_A32B32G32R32F:
		return FVector4f(((const float*)TileData)[VoxelIndex * 4 + 0], ((const float*)TileData)[VoxelIndex * 4 + 1], ((const float*)TileData)[VoxelIndex * 4 + 2], ((const float*)TileData)[VoxelIndex * 4 + 3]);
	default:
		checkNoEntry();
		return FVector4f();
	}
}

void UE::SVT::WriteVoxel(int64 VoxelIndex, uint8* TileData, EPixelFormat Format, const FVector4f& Value, int32 DstComponent)
{
	if (Format == PF_Unknown)
	{
		return;
	}
	switch (Format)
	{
	case PF_R8:
		if (DstComponent == -1 || DstComponent == 0) TileData[VoxelIndex] = uint8(FMath::Clamp(Value.X, 0.0f, 1.0f) * 255.0f);
		break;
	case PF_R8G8:
		if (DstComponent == -1 || DstComponent == 0) TileData[VoxelIndex * 2 + 0] = uint8(FMath::Clamp(Value.X, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 1) TileData[VoxelIndex * 2 + 1] = uint8(FMath::Clamp(Value.Y, 0.0f, 1.0f) * 255.0f);
		break;
	case PF_R8G8B8A8:
		if (DstComponent == -1 || DstComponent == 0) TileData[VoxelIndex * 4 + 0] = uint8(FMath::Clamp(Value.X, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 1) TileData[VoxelIndex * 4 + 1] = uint8(FMath::Clamp(Value.Y, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 2) TileData[VoxelIndex * 4 + 2] = uint8(FMath::Clamp(Value.Z, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 3) TileData[VoxelIndex * 4 + 3] = uint8(FMath::Clamp(Value.W, 0.0f, 1.0f) * 255.0f);
		break;
	case PF_R16F:
		if (DstComponent == -1 || DstComponent == 0) ((uint16*)TileData)[VoxelIndex] = FFloat16(Value.X).Encoded;
		break;
	case PF_G16R16F:
		if (DstComponent == -1 || DstComponent == 0) ((uint16*)TileData)[VoxelIndex * 2 + 0] = FFloat16(Value.X).Encoded;
		if (DstComponent == -1 || DstComponent == 1) ((uint16*)TileData)[VoxelIndex * 2 + 1] = FFloat16(Value.Y).Encoded;
		break;
	case PF_FloatRGBA:
		if (DstComponent == -1 || DstComponent == 0) ((uint16*)TileData)[VoxelIndex * 4 + 0] = FFloat16(Value.X).Encoded;
		if (DstComponent == -1 || DstComponent == 1) ((uint16*)TileData)[VoxelIndex * 4 + 1] = FFloat16(Value.Y).Encoded;
		if (DstComponent == -1 || DstComponent == 2) ((uint16*)TileData)[VoxelIndex * 4 + 2] = FFloat16(Value.Z).Encoded;
		if (DstComponent == -1 || DstComponent == 3) ((uint16*)TileData)[VoxelIndex * 4 + 3] = FFloat16(Value.W).Encoded;
		break;
	case PF_R32_FLOAT:
		if (DstComponent == -1 || DstComponent == 0) ((float*)TileData)[VoxelIndex] = Value.X;
		break;
	case PF_G32R32F:
		if (DstComponent == -1 || DstComponent == 0) ((float*)TileData)[VoxelIndex * 2 + 0] = Value.X;
		if (DstComponent == -1 || DstComponent == 1) ((float*)TileData)[VoxelIndex * 2 + 1] = Value.Y;
		break;
	case PF_A32B32G32R32F:
		if (DstComponent == -1 || DstComponent == 0) ((float*)TileData)[VoxelIndex * 4 + 0] = Value.X;
		if (DstComponent == -1 || DstComponent == 1) ((float*)TileData)[VoxelIndex * 4 + 1] = Value.Y;
		if (DstComponent == -1 || DstComponent == 2) ((float*)TileData)[VoxelIndex * 4 + 2] = Value.Z;
		if (DstComponent == -1 || DstComponent == 3) ((float*)TileData)[VoxelIndex * 4 + 3] = Value.W;
		break;
	default:
		checkNoEntry();
	}
}