// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceQA.h"
#include "NeuralNetworkInferenceQAUtils.h"
#include "UnitTester.h"
#include "Misc/Paths.h"



/* UNeuralNetworkInferenceQA static functions
 *****************************************************************************/

void UNeuralNetworkInferenceQA::UnitTesting()
{
	const FString ProjectContentDir = FPaths::ProjectContentDir();
	const FString MachineLearningTestsRelativeDirectory = TEXT("Tests/MachineLearning/");
	const FString ModelZooRelativeDirectory = MachineLearningTestsRelativeDirectory / TEXT("Models/"); // Eg "Tests/MachineLearning/Models/"
	const FString UnitTestRelativeDirectory = MachineLearningTestsRelativeDirectory / TEXT("UnitTests/"); // Eg "Tests/MachineLearning/UnitTests/"
	FUnitTester::GlobalTest(ProjectContentDir, ModelZooRelativeDirectory, UnitTestRelativeDirectory);
}
