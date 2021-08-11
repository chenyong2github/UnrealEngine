// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceQA.h"
#include "NeuralNetworkInferenceQAUtils.h"
#include "UnitTester.h"
#include "Misc/Paths.h"



/* UNeuralNetworkInferenceQA static functions
 *****************************************************************************/

void UNeuralNetworkInferenceQA::UnitTesting()
{
	const FString ModelsDirectory = FPaths::ProjectContentDir() / TEXT("Models/");
	const FString UnitTestingDirectory = FPaths::ProjectContentDir() / TEXT("UnitTesting/");
	FUnitTester::GlobalTest(UnitTestingDirectory, ModelsDirectory);
}
