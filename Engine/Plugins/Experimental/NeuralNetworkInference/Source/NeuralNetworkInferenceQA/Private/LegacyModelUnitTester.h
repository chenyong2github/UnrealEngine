// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralNetworkLegacy.h"

class FLegacyModelUnitTester
{
public:
	static void GlobalTest(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory);

private:
	/**
	 * It runs a full model test on the desired model.
	 */
	static void ModelLoadAccuracyAndSpeedTests(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory, const TArray<FString>& InModelNames,
		const TArray<float>& InInputArrayValues, const TArray<TArray<double>>& InCPUGroundTruths, const TArray<TArray<double>>& InGPUGroundTruths,
		const TArray<int32>& InCPURepetitions, const TArray<int32>& InGPURepetitions);
	/**
	 * Other auxiliary functions for GlobalTest().
	 */
	static FString GetONNXModelFilePath(const FString& InModelZooDirectory, const FString& InModelName);
	static FString GetOtxtModelFilePath(const FString& InModelZooDirectory, const FString& InModelName);
	static FString GetUAssetModelFilePath(const FString& InModelName, const FString& InModelZooRelativeDirectory);
	static UNeuralNetworkLegacy* NetworkUassetLoadTest(const FString& InUAssetPath);
	static UNeuralNetworkLegacy* NetworkONNXLoadTest(const FString& InONNXFilePath);
	static void ModelAccuracyTest(UNeuralNetworkLegacy* InOutNetwork, const TArray<float>& InInputArrayValues, const TArray<double>& InCPUGroundTruths,
		const TArray<double>& InGPUGroundTruths);
	static void ModelSpeedTest(const FString& InUAssetPath, const int32 InCPURepetitions, const int32 InGPURepetitions);
	static double GetAveragedL1Norm(const TArray<float>& InArray);
	static double GetAveragedL1NormDiff(const TArray<float>& InArray1, const TArray<float>& InArray2);
};
