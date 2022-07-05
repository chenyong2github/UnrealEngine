// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Elements/PCGIntersectionElement.h"
#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionDeterminismSingleSameDataTest, FPCGTestBaseClass, "pcg.tests.Intersection.Determinism.SingleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionDeterminismSingleIdenticalDataTest, FPCGTestBaseClass, "pcg.tests.Intersection.Determinism.SingleMultipleData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionDeterminismMultipleSameDataTest, FPCGTestBaseClass, "pcg.tests.Intersection.Determinism.MultipleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionDeterminismMultipleIdenticalDataTest, FPCGTestBaseClass, "pcg.tests.Intersection.Determinism.MultipleIdenticalData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGIntersectionDeterminismOrderIndependenceTest, FPCGTestBaseClass, "pcg.tests.Intersection.Determinism.OrderIndependence", PCGTestsCommon::TestFlags)

namespace
{
	void IntersectionTestBase(PCGDeterminismTests::FTestData& TestData)
	{
		PCGDeterminismTests::GenerateSettings<UPCGIntersectionSettings>(TestData);
		// Source Volumes
		PCGDeterminismTests::AddVolumeInputData(TestData.InputData, PCGDeterminismTests::Defaults::SmallVector, PCGDeterminismTests::Defaults::MediumVector, PCGDeterminismTests::Defaults::MediumVector);
		PCGDeterminismTests::AddVolumeInputData(TestData.InputData, PCGDeterminismTests::Defaults::SmallVector * -1.f, PCGDeterminismTests::Defaults::MediumVector, PCGDeterminismTests::Defaults::MediumVector);
	}

	void IntersectionTestMultiple(PCGDeterminismTests::FTestData& TestData)
	{
		IntersectionTestBase(TestData);

		// Randomized Sources
		AddRandomizedVolumeInputData(TestData);
	}
}

bool FPCGIntersectionDeterminismSingleSameDataTest::RunTest(const FString& Parameters)
{
	// Test single same data
	PCGDeterminismTests::FTestData TestData(PCGDeterminismTests::Defaults::Seed);

	IntersectionTestBase(TestData);

	return TestTrue(TEXT("Same single input and settings, same output"), ExecutionIsDeterministicSameData(TestData));
}

bool FPCGIntersectionDeterminismSingleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test single identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::Defaults::Seed);

	IntersectionTestBase(FirstTestData);
	IntersectionTestBase(SecondTestData);

	return TestTrue(TEXT("Identical single input and settings, same output"), ExecutionIsDeterministic(FirstTestData, SecondTestData));
}

bool FPCGIntersectionDeterminismMultipleSameDataTest::RunTest(const FString& Parameters)
{
	// Test multiple same data
	PCGDeterminismTests::FTestData TestData(PCGDeterminismTests::Defaults::Seed);

	IntersectionTestMultiple(TestData);

	return TestTrue(TEXT("Identical multiple input, same output"), ExecutionIsDeterministicSameData(TestData));
}

bool FPCGIntersectionDeterminismMultipleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test multiple identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::Defaults::Seed);

	IntersectionTestMultiple(FirstTestData);
	IntersectionTestMultiple(SecondTestData);

	return TestTrue(TEXT("Identical single input and settings, same output"), ExecutionIsDeterministic(FirstTestData, SecondTestData));
}

bool FPCGIntersectionDeterminismOrderIndependenceTest::RunTest(const FString& Parameters)
{
	// Test multiple identical, shuffled data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::Defaults::Seed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::Defaults::Seed);

	IntersectionTestMultiple(FirstTestData);
	IntersectionTestMultiple(SecondTestData);

	ShuffleInputOrder(SecondTestData);

	return TestTrue(TEXT("Shuffled input order, same output"), ExecutionIsDeterministic(FirstTestData, SecondTestData));
}

#endif