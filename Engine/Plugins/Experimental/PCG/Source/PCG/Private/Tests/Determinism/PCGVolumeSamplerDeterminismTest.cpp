// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGPointData.h"
#include "Elements/PCGVolumeSampler.h"
#include "Tests/PCGTestsCommon.h"
#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismSingleSameDataTest, FPCGTestBaseClass, "pcg.determinism.VolumeSampler.SingleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismSingleIdenticalDataTest, FPCGTestBaseClass, "pcg.determinism.VolumeSampler.SingleIdenticalData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismMultipleSameDataTest, FPCGTestBaseClass, "pcg.determinism.VolumeSampler.MultipleSameData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismMultipleIdenticalDataTest, FPCGTestBaseClass, "pcg.determinism.VolumeSampler.MultipleIdenticalData", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGVolumeSamplerDeterminismOrderIndependenceTest, FPCGTestBaseClass, "pcg.determinism.VolumeSampler.OrderIndependence", PCGTestsCommon::TestFlags)

namespace
{
	void VolumeSettingsDelegate(UPCGVolumeSamplerSettings* Settings, FRandomStream& RandomStream)
	{
		Settings->VoxelSize = FVector::OneVector * 200.f + RandomStream.VRand() * 100.f;
	}
}

bool FPCGVolumeSamplerDeterminismSingleSameDataTest::RunTest(const FString& Parameters)
{
	// Test single same data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, VolumeSettingsDelegate);
	PCGDeterminismTests::AddRandomizedVolumeInputData(FirstTestData);

	TestTrue("Same single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, FirstTestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismSingleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test single identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, VolumeSettingsDelegate);
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(SecondTestData, VolumeSettingsDelegate);

	PCGDeterminismTests::AddRandomizedVolumeInputData(FirstTestData);
	PCGDeterminismTests::AddRandomizedVolumeInputData(SecondTestData);

	TestTrue("Identical single input and settings, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, SecondTestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismMultipleSameDataTest::RunTest(const FString& Parameters)
{
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);

	// Test multiple same data
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, VolumeSettingsDelegate);

	// Add many random volumes
	for (int32 I = 0; I < 10; ++I)
	{
		PCGDeterminismTests::AddRandomizedVolumeInputData(FirstTestData);
	}

	TestTrue("Same multiple input, same output", PCGDeterminismTests::ExecutionIsDeterministic(FirstTestData, FirstTestData));

	return true;
}

bool FPCGVolumeSamplerDeterminismMultipleIdenticalDataTest::RunTest(const FString& Parameters)
{
	// Test multiple identical data
	PCGDeterminismTests::FTestData FirstTestData(PCGDeterminismTests::DefaultSeed);
	PCGDeterminismTests::FTestData SecondTestData(PCGDeterminismTests::DefaultSeed);

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, VolumeSettingsDelegate);
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(SecondTestData, VolumeSettingsDelegate);

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

	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(FirstTestData, VolumeSettingsDelegate);
	PCGDeterminismTests::GenerateSettings<UPCGVolumeSamplerSettings>(SecondTestData, VolumeSettingsDelegate);

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
