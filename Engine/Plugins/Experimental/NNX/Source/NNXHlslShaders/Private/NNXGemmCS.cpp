// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXGemmCS.h"
#include "NNXCore.h"

EGemmAlgorithm FMLGemmCS::GetAlgorithm(const FMLGemmCS::FParameters& Parameters)
{
	return EGemmAlgorithm::MultiWrite1x32;
}

FIntVector FMLGemmCS::GetGroupCount(const FMLGemmCS::FParameters& Parameters, EGemmAlgorithm Algorithm)
{
	uint32 ThreadGroupCountValueY = FMath::DivideAndRoundUp((uint32)Parameters.M, (uint32)8);
	uint32 ThreadGroupCountValueX = FMath::DivideAndRoundUp((uint32)Parameters.N, (uint32)8);
	switch (Algorithm)
	{
		case EGemmAlgorithm::Simple8x8:
		case EGemmAlgorithm::SharedMemory8x8:
			ThreadGroupCountValueX = FMath::DivideAndRoundUp((uint32)Parameters.N, (uint32)8);
			ThreadGroupCountValueY = FMath::DivideAndRoundUp((uint32)Parameters.M, (uint32)8);
			break;
		case EGemmAlgorithm::Simple16x16:
		case EGemmAlgorithm::SharedMemory16x16:
		case EGemmAlgorithm::MultiWrite1x16:
		case EGemmAlgorithm::MultiWrite2x16:
			ThreadGroupCountValueX = FMath::DivideAndRoundUp((uint32)Parameters.N, (uint32)16);
			ThreadGroupCountValueY = FMath::DivideAndRoundUp((uint32)Parameters.M, (uint32)16);
			break;
		case EGemmAlgorithm::Simple32x32:
		case EGemmAlgorithm::SharedMemory32x32:
		case EGemmAlgorithm::MultiWrite1x32:
		case EGemmAlgorithm::MultiWrite2x32:
		case EGemmAlgorithm::MultiWrite4x32:
			ThreadGroupCountValueX = FMath::DivideAndRoundUp((uint32)Parameters.N, (uint32)32);
			ThreadGroupCountValueY = FMath::DivideAndRoundUp((uint32)Parameters.M, (uint32)32);
			break;
		case EGemmAlgorithm::Simple256x1:
			ThreadGroupCountValueX = FMath::DivideAndRoundUp((uint32)Parameters.N, (uint32)256);
			ThreadGroupCountValueY = (uint32)Parameters.M;
			break;
		case EGemmAlgorithm::MultiWrite2x64:
		case EGemmAlgorithm::MultiWrite4x64:
		case EGemmAlgorithm::MultiWrite8x64:
			ThreadGroupCountValueX = FMath::DivideAndRoundUp((uint32)Parameters.N, (uint32)64);
			ThreadGroupCountValueY = FMath::DivideAndRoundUp((uint32)Parameters.M, (uint32)64);
			break;
		default:
			break;
	}
	return FIntVector(ThreadGroupCountValueX, ThreadGroupCountValueY, 1);
}

IMPLEMENT_GLOBAL_SHADER(FMLGemmCS, "/NNX/GemmOp.usf", "main", SF_Compute);