// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Elements/PCGVolumeSampler.h"
#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismSingleSameDataTest, FPCGTestBaseClass, "pcg.tests.VolumeSampler.Determinism.SingleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismSingleIdenticalDataTest, FPCGTestBaseClass, "pcg.tests.VolumeSampler.Determinism.SingleIdenticalData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismMultipleSameDataTest, FPCGTestBaseClass, "pcg.tests.VolumeSampler.Determinism.MultipleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismMultipleIdenticalDataTest, FPCGTestBaseClass, "pcg.tests.VolumeSampler.Determinism.MultipleIdenticalData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismOrderIndependenceTest, FPCGTestBaseClass, "pcg.tests.VolumeSampler.Determinism.OrderIndependence", PCGTestsCommon::TestFlags)

namespace
{
	void RandomizeVolumeSettingsVoxelSize(PCGDeterminismTests::FTestData& TestData)
	{
		CastChecked<UPCGVolumeSamplerSettings>(TestData.Settings)->VoxelSize = FVector::OneVector * 200.f + TestData.RandomStream.VRand() * 100.f;
	}
}

bool FPCGVolumeSamplerDeterminismSingleSameDataTest::RunTest(const FString& Parameters)
{
	// Test single same data
	PCGDeterminismTests::FTestData TestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(TestData, RandomizeVolumeSettingsVoxelSize);
	PCGDeterminismTests::AddRandomizedVolumeInputData(TestData);

	TestTrue("Same single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministicSameData(TestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismSingleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test single identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, RandomizeVolumeSettingsVoxelSize);
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(SecondTestData, RandomizeVolumeSettingsVoxelSize);

	PCGDeterminismTests::AddRandomizedVolumeInputData(FirstTestData);
	PCGDeterminismTests::AddRandomizedVolumeInputData(SecondTestData);

	TestTrue("Identical single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismMultipleSameDataTest::RunTest(const FString& Parameters)
{
	// Test multiple same data
	PCGDeterminismTests::FTestData TestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(TestData, RandomizeVolumeSettingsVoxelSize);

	// Add many random volumes
	for (int32 I = 0; I < 10; ++I)
	{
		PCGDeterminismTests::AddRandomizedVolumeInputData(TestData);
	}

	TestTrue("Same multiple input, same output", PCGDeterminismTests::ExecutionIsDeterministicSameData(TestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismMultipleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test multiple identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, RandomizeVolumeSettingsVoxelSize);
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(SecondTestData, RandomizeVolumeSettingsVoxelSize);

	// Add many random volumes
	for (int32 I = 0; I < 10; ++I)
	{
		PCGDeterminismTests::AddRandomizedVolumeInputData(FirstTestData);
		PCGDeterminismTests::AddRandomizedVolumeInputData(SecondTestData);
	}

	TestTrue("Identical multiple input, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismOrderIndependenceTest::RunTest(const FString& Parameters)
{
	// Test shuffled input data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, RandomizeVolumeSettingsVoxelSize);
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(SecondTestData, RandomizeVolumeSettingsVoxelSize);

	// Add many random volumes
	for (int32 I = 0; I < 10; ++I)
	{
		PCGDeterminismTests::AddRandomizedVolumeInputData(FirstTestData);
		PCGDeterminismTests::AddRandomizedVolumeInputData(SecondTestData);
	}

	PCGDeterminismTests::ShuffleInputOrder(SecondTestData);

	TestTrue("Shuffled input order, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));

	return true;
}

#endif