// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXGemmCS.h"
#include "NNXCore.h"
#include "Algo/Accumulate.h"

namespace
{
	struct GemmMatrixParameters
	{
		uint32 M = 0;
		uint32 N = 0;
		uint32 K = 0;
		TArray<uint32> StackShapeA;
		TArray<uint32> StackShapeB;
		TArray<uint32> StackStrideA;
		TArray<uint32> StackStrideB;
	};

	static GemmMatrixParameters GetMatrixParameters(TArray<uint32> ShapeA, TArray<uint32> ShapeB)
	{
		check(!ShapeA.IsEmpty());
		check(!ShapeB.IsEmpty());

		GemmMatrixParameters Result;
		int32 NumStackDimensionsA = FMath::Max(ShapeA.Num() - 2, 0);
		int32 NumStackDimensionsB = FMath::Max(ShapeB.Num() - 2, 0);
		int32 NumStackDimensions = FMath::Max(NumStackDimensionsA, NumStackDimensionsB);

		check(NumStackDimensions <= NNXRT_GEMM_MAX_NUM_STACK_DIMENSIONS);

		// Check matrix stack dimensions
		if (NumStackDimensionsA > 0 && NumStackDimensionsB > 0)
		{
			const int32 NumDimToCheck = FMath::Min(NumStackDimensionsA, NumStackDimensionsB);
			for (int32 i = 0; i < NumDimToCheck; i++)
			{
				const uint32 VolumeA = ShapeA[NumStackDimensionsA - 1 - i];
				const uint32 VolumeB = ShapeB[NumStackDimensionsB - 1 - i];

				check(VolumeA == 1 || VolumeB == 1 || VolumeA == VolumeB);
			}
		}

		const bool IsVectorA = ShapeA.Num() == 1;
		const bool IsVectorB = ShapeB.Num() == 1;

		Result.M = IsVectorA ? 1 : ShapeA[ShapeA.Num() - 2];
		Result.N = IsVectorB ? 1 : ShapeB[ShapeB.Num() - 1];
		Result.K = ShapeA[IsVectorA ? 0 : ShapeA.Num() - 1];
		check(ShapeB[IsVectorB ? 0 : ShapeB.Num() - 2] == Result.K);

		Result.StackShapeA.Init(1, NumStackDimensions);
		Result.StackShapeB.Init(1, NumStackDimensions);

		for (int32 i = 0; i < NumStackDimensions; i++)
		{
			Result.StackShapeA[Result.StackShapeA.Num() - 1 - i] = i < NumStackDimensionsA ? ShapeA[ShapeA.Num() - 3 - i] : 1;
			Result.StackShapeB[Result.StackShapeB.Num() - 1 - i] = i < NumStackDimensionsB ? ShapeB[ShapeB.Num() - 3 - i] : 1;
		}

		Result.StackStrideA.Init(1, NumStackDimensions);
		Result.StackStrideB.Init(1, NumStackDimensions);

		for (int32 i = NumStackDimensions - 2; i >= 0; i--)
		{
			Result.StackStrideA[i] = Result.StackStrideA[i + 1] * Result.StackShapeA[i + 1];
			Result.StackStrideB[i] = Result.StackStrideB[i + 1] * Result.StackShapeB[i + 1];
		}

		return Result;
	}
} // namespace

void FMLGemmCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("MAX_NUM_STACK_DIMENSIONS"), NNXRT_GEMM_MAX_NUM_STACK_DIMENSIONS);
}

void FMLGemmCS::FillInParameters(float Alpha, float Beta, int32 TransA, int32 TransB, uint32 M, uint32 N, uint32 K, uint32 CWidth, uint32 CHeight, float CScalar, FMLGemmCS::FParameters& Parameters)
{
	Parameters.Alpha = Alpha;
	Parameters.Beta = Beta;
	Parameters.TransA = TransA;
	Parameters.TransB = TransB;
	Parameters.M = M;
	Parameters.N = N;
	Parameters.K = K;
	Parameters.MxK = M * K;
	Parameters.KxN = K * N;
	Parameters.MxN = M * N;
	Parameters.CWidth = CWidth;
	Parameters.CHeight = CHeight;
	Parameters.CScalar = CScalar;
}

uint32 FMLGemmCS::GetShapeSize(TArray<uint32> Shape)
{
	auto Product = [](uint32 A, uint32 B) { return A * B; };

	return Algo::Accumulate(Shape, 1, Product);
}

uint32 FMLGemmCS::GetMatMulOutputSize(TArray<uint32> ShapeA, TArray<uint32> ShapeB)
{
	GemmMatrixParameters Params = GetMatrixParameters(ShapeA, ShapeB);

	return FMath::Max(GetShapeSize(Params.StackShapeA), GetShapeSize(Params.StackShapeB)) * Params.M * Params.N;
}

void FMLGemmCS::FillInParametersMatMul(TArray<uint32> ShapeA, TArray<uint32> ShapeB, FMLGemmCS::FParameters& Parameters)
{
	GemmMatrixParameters Params = GetMatrixParameters(ShapeA, ShapeB);

	Parameters.Alpha = 1.0f;
	Parameters.Beta = 1.0f;
	Parameters.TransA = 0;
	Parameters.TransB = 0;
	Parameters.M = Params.M;
	Parameters.N = Params.N;
	Parameters.K = Params.K;
	Parameters.MxK = Params.M * Params.K;
	Parameters.KxN = Params.K * Params.N;
	Parameters.MxN = Params.M * Params.N;
	Parameters.CWidth = 0;
	Parameters.CHeight = 0;
	Parameters.CScalar = 0;

	for (int32 i = 0; i < Params.StackShapeA.Num(); i++)
	{
		Parameters.StackShapeA_StackShapeB_StackStrideA_StackStrideB[i] = FUint32Vector4(Params.StackShapeA[i], Params.StackShapeB[i], Params.StackStrideA[i], Params.StackStrideB[i]);
	}
}

FIntVector FMLGemmCS::GetGroupCount(const FMLGemmCS::FParameters& Parameters, EGemmAlgorithm Algorithm, int32 NumStackDimensions)
{
	int32 OutputStackSize = 1;
	for (int32 i = 0; i < NumStackDimensions; i++)
	{
		OutputStackSize *= FMath::Max(
			Parameters.StackShapeA_StackShapeB_StackStrideA_StackStrideB[i].X,
			Parameters.StackShapeA_StackShapeB_StackStrideA_StackStrideB[i].Y);
	}

	int32 ThreadGroupCountValueZ = FMath::DivideAndRoundUp(OutputStackSize, 1);
	int32 ThreadGroupCountValueY = FMath::DivideAndRoundUp((int32)Parameters.M, 8);
	int32 ThreadGroupCountValueX = FMath::DivideAndRoundUp((int32)Parameters.N, 8);
	switch (Algorithm)
	{
	case EGemmAlgorithm::Simple8x8:
	case EGemmAlgorithm::SharedMemory8x8:
		ThreadGroupCountValueX = FMath::DivideAndRoundUp((int32)Parameters.N, 8);
		ThreadGroupCountValueY = FMath::DivideAndRoundUp((int32)Parameters.M, 8);
		break;
	case EGemmAlgorithm::Simple16x16:
	case EGemmAlgorithm::SharedMemory16x16:
	case EGemmAlgorithm::MultiWrite1x16:
	case EGemmAlgorithm::MultiWrite2x16:
		ThreadGroupCountValueX = FMath::DivideAndRoundUp((int32)Parameters.N, 16);
		ThreadGroupCountValueY = FMath::DivideAndRoundUp((int32)Parameters.M, 16);
		break;
	case EGemmAlgorithm::Simple32x32:
	case EGemmAlgorithm::SharedMemory32x32:
	case EGemmAlgorithm::MultiWrite1x32:
	case EGemmAlgorithm::MultiWrite2x32:
	case EGemmAlgorithm::MultiWrite4x32:
		ThreadGroupCountValueX = FMath::DivideAndRoundUp((int32)Parameters.N, 32);
		ThreadGroupCountValueY = FMath::DivideAndRoundUp((int32)Parameters.M, 32);
		break;
	case EGemmAlgorithm::Simple256x1:
		ThreadGroupCountValueX = FMath::DivideAndRoundUp((int32)Parameters.N, 256);
		ThreadGroupCountValueY = Parameters.M;
		break;
	case EGemmAlgorithm::MultiWrite2x64:
	case EGemmAlgorithm::MultiWrite4x64:
	case EGemmAlgorithm::MultiWrite8x64:
		ThreadGroupCountValueX = FMath::DivideAndRoundUp((int32)Parameters.N, 64);
		ThreadGroupCountValueY = FMath::DivideAndRoundUp((int32)Parameters.M, 64);
		break;
	default:
		break;
	}

	return FIntVector(ThreadGroupCountValueX, ThreadGroupCountValueY, ThreadGroupCountValueZ);
}

EGemmAlgorithm FMLGemmCS::GetAlgorithm(const FMLGemmCS::FParameters& Parameters)
{
	return EGemmAlgorithm::MultiWrite1x32;
}

IMPLEMENT_GLOBAL_SHADER(FMLGemmCS, "/NNX/GemmOp.usf", "main", SF_Compute);