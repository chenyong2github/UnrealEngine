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



#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNeuralNetworkInferenceTest, "System.Engine.MachineLearning.NeuralNetworkInference (NNI)",
	EAutomationTestFlags::ApplicationContextMask // = EditorContext | ClientContext | ServerContext | CommandletContext
	| EAutomationTestFlags::FeatureMask // = NonNullRHI | RequiresUser
	| EAutomationTestFlags::EngineFilter)

bool FNeuralNetworkInferenceTest::RunTest(const FString& Parameters)
{
	UNeuralNetworkInferenceQA::UnitTesting();
UE_LOG(LogNeuralNetworkInferenceQA, Warning, TEXT("FNeuralNetworkInferenceTest::RunTest(): Warning with parameters = %s."), *Parameters);
ensureMsgf(false, TEXT("FNeuralNetworkInferenceTest::RunTest(): Failure with parameters = %s."), *Parameters);
	return true;
}


#endif //WITH_DEV_AUTOMATION_TESTS
