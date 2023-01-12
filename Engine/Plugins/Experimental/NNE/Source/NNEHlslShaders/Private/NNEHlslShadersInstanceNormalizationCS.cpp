// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEHlslShadersInstanceNormalizationCS.h"
#include "NNXCore.h"
#include "NNECoreTensor.h"

namespace UE::NNEHlslShaders::Internal
{
	namespace
	{
		FIntVector GetGroupSize(EInstanceNormalizationAlgorithm Algorithm)
		{
			switch (Algorithm)
			{
			case EInstanceNormalizationAlgorithm::Simple1x265: 			return { 1, 256, 1};
			case EInstanceNormalizationAlgorithm::SharedMemory8x32:		return { 8,  32, 1};
			case EInstanceNormalizationAlgorithm::SharedMemory16x16:	return {16,  16, 1};
			case EInstanceNormalizationAlgorithm::SharedMemory32x8:		return {32,   8, 1};
			}

			check(false);
			return {1, 256, 1};
		}
	}

	void TInstanceNormalizationCS::FillInParameters(float Epsilon, const  UE::NNECore::Internal::FTensor& Input, TInstanceNormalizationCS::FParameters& Parameters)
	{
		check(Input.GetShape().Rank() >= 3);
		
		Parameters.Epsilon = Epsilon;
		const int32 N = Input.GetShape().GetData()[0];
		Parameters.C = Input.GetShape().GetData()[1];
		Parameters.NxC = N * Parameters.C;
		Parameters.W = Input.GetShape().Volume() / (Parameters.NxC);
	}

	FIntVector TInstanceNormalizationCS::GetGroupCount(const TInstanceNormalizationCS::FParameters& Parameters, EInstanceNormalizationAlgorithm Algorithm)
	{
		int ThreadGroupCountValueY = FMath::DivideAndRoundUp((int32)Parameters.NxC, GetGroupSize(Algorithm).Y);

		return {1, ThreadGroupCountValueY, 1};
	}

	EInstanceNormalizationAlgorithm TInstanceNormalizationCS::GetAlgorithm(const TInstanceNormalizationCS::FParameters& Parameters)
	{
		return EInstanceNormalizationAlgorithm::SharedMemory16x16;
	}

	void TInstanceNormalizationCS::LexFromString(EInstanceNormalizationAlgorithm& OutValue, const TCHAR* StringVal)
	{
		OutValue = EInstanceNormalizationAlgorithm::MAX;
		if (FCString::Stricmp(StringVal, TEXT("Simple1x265")) == 0) OutValue = EInstanceNormalizationAlgorithm::Simple1x265;
		else if (FCString::Stricmp(StringVal, TEXT("SharedMemory8x32")) == 0) OutValue = EInstanceNormalizationAlgorithm::SharedMemory8x32;
		else if (FCString::Stricmp(StringVal, TEXT("SharedMemory16x16")) == 0) OutValue = EInstanceNormalizationAlgorithm::SharedMemory16x16;
		else if (FCString::Stricmp(StringVal, TEXT("SharedMemory32x8")) == 0) OutValue = EInstanceNormalizationAlgorithm::SharedMemory32x8;
	}

	IMPLEMENT_GLOBAL_SHADER(TInstanceNormalizationCS, "/NNE/NNEHlslShadersInstanceNormalization.usf", "InstanceNormalization", SF_Compute);
} // UE::NNEHlslShaders::Internal