// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Determinism/PCGDeterminismTestBlueprintBase.h"

void UPCGDeterminismTestBlueprintBase::ExecuteTest_Implementation(const UPCGNode* InPCGNode, FDeterminismNodeTestResult& InOutTestResult)
{
	// The default case if the user tries to run the blueprint test unimplemented
	InOutTestResult.NodeTitle = InPCGNode->GetNodeTitle();
	InOutTestResult.NodeName = InPCGNode->GetName();
	InOutTestResult.Seed = InPCGNode->DefaultSettings->Seed;
	PCGDeterminismTests::RunDeterminismTest(InPCGNode, InOutTestResult, PCGDeterminismTests::Defaults::DeterminismBasicTestInfo);
}