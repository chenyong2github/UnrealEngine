// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Elements/PCGDifferenceElement.h"
#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGDifferenceDeterminismSingleSameDataTest, FPCGTestBaseClass, "pcg.tests.Difference.Determinism.SingleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGDifferenceDeterminismSingleIdenticalDataTest, FPCGTestBaseClass, "pcg.tests.Difference.Determinism.SingleMultipleData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGDifferenceDeterminismMultipleSameDataTest, FPCGTestBaseClass, "pcg.tests.Difference.Determinism.MultipleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGDifferenceDeterminismMultipleIdenticalDataTest, FPCGTestBaseClass, "pcg.tests.Difference.Determinism.MultipleIdenticalData", PCGTestsCommon::TestFlags)
// TODO: Temporarily disabled while more testing into this is conducted
//IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGDifferenceDeterminismOrderIndependenceTest, FPCGTestBaseClass, "pcg.tests.Difference.Determinism.OrderIndependence", PCGTestsCommon::TestFlags)

namespace
{
	void DifferenceTestBase(PCGDeterminismTests::FTestData& TestData)
	{
		PCGDeterminismTests::GenerateSettings<UPCGDifferenceSettings>(TestData);
		// Source
		PCGDeterminismTests::AddVolumeInputData(TestData.InputData, FVector::ZeroVector, FVector::OneVector * 2000.f, FVector::OneVector * 200.f, PCGDifferenceConstants::SourceLabel);

		// Difference 
		PCGDeterminismTests::AddVolumeInputData(TestData.InputData, FVector::ZeroVector, FVector::OneVector * 500.f, FVector::OneVector * 200.f, PCGDifferenceConstants::DifferencesLabel);
	}

	void DifferenceTestMultiple(PCGDeterminismTests::FTestData& TestData)
	{
		DifferenceTestBase(TestData);

		// Randomized Sources
		PCGDeterminismTests::AddRandomizedVolumeInputData(TestData, PCGDifferenceConstants::SourceLabel);
		PCGDeterminismTests::AddRandomizedMultiplePointInputData(TestData, 20, PCGDifferenceConstants::SourceLabel);

		// Randomized Differences
		PCGDeterminismTests::AddRandomizedMultiplePointInputData(TestData, 20, PCGDifferenceConstants::DifferencesLabel);
	}

}

bool FPCGDifferenceDeterminismSingleSameDataTest::RunTest(const FString& Parameters)
{
	// Test single same data
	PCGDeterminismTests::FTestData TestData(PCGDeterminismTests::DefaultSeed);

	DifferenceTestBase(TestData);

	TestTrue("Same single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministicSameData(TestData));

	return true;
}

bool FPCGDifferenceDeterminismSingleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test single identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	DifferenceTestBase(FirstTestData);
	DifferenceTestBase(SecondTestData);

	TestTrue("Identical single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));

	return true;
}

bool FPCGDifferenceDeterminismMultipleSameDataTest::RunTest(const FString& Parameters)
{
	// Test multiple same data
	PCGDeterminismTests::FTestData TestData(PCGDeterminismTests::DefaultSeed);

	DifferenceTestMultiple(TestData);

	TestTrue("Identical multiple input, same output", PCGDeterminismTests::ExecutionIsDeterministicSameData(TestData));

	return true;
}

bool FPCGDifferenceDeterminismMultipleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test multiple identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	DifferenceTestMultiple(FirstTestData);
	DifferenceTestMultiple(SecondTestData);

	TestTrue("Identical single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));

	return true;
}

// TODO: Temporarily disabled while more testing into this is conducted
//bool FPCGDifferenceDeterminismOrderIndependenceTest::RunTest(const FString& Parameters)
//{
//	// Test multiple identical data
//	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
//	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);
//
//	DifferenceTestMultiple(FirstTestData);
//	DifferenceTestMultiple(SecondTestData);
//
//	PCGDeterminismTests::ShuffleInputOrder(SecondTestData);
//
//	TestTrue("Shuffled input order, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));
//
//	return true;
//}

#endif