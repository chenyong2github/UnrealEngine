// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NeuralNetwork.h"

class FModelUnitTester
{
public:
	static bool GlobalTest(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory);

private:
	/**
	 * It runs a full model test on the desired model.
	 */
	static bool ModelLoadAccuracyAndSpeedTests(const FString& InProjectContentDir, const FString& InModelZooRelativeDirectory, const TArray<FString>& InModelNames,
		const TArray<float>& InInputArrayValues, const TArray<TArray<double>>& InCPUGroundTruths, const TArray<TArray<double>>& InGPUGroundTruths,
		const TArray<int32>& InCPURepetitions, const TArray<int32>& InGPURepetitions);
	/**
	 * Other auxiliary functions for GlobalTest().
	 */
	static FString GetONNXModelFilePath(const FString& InModelZooDirectory, const FString& InModelName);
	static FString GetORTModelFilePath(const FString& InModelZooDirectory, const FString& InModelName);
	static FString GetUAssetModelFilePath(const FString& InModelName, const FString& InModelZooRelativeDirectory);
	static UNeuralNetwork* NetworkUassetLoadTest(const FString& InUAssetPath);
	static UNeuralNetwork* NetworkONNXOrORTLoadTest(const FString& InModelFilePath);
	static bool ModelAccuracyTest(UNeuralNetwork* InOutNetwork, const TArray<float>& InInputArrayValues, const TArray<double>& InCPUGroundTruths,
		const TArray<double>& InGPUGroundTruths);
	static bool ModelSpeedTest(const FString& InUAssetPath, const int32 InCPURepetitions, const int32 InGPURepetitions);
	static double GetAveragedL1Norm(const TArray<float>& InArray);
	static double GetAveragedL1NormDiff(const TArray<float>& InArray1, const TArray<float>& InArray2);
};
