// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"
#include "PCGSettings.h"

class AActor;
class UPCGComponent;
class UPCGData;
struct FPCGDataCollection;

namespace PCGDeterminismTests
{
	constexpr int32 DefaultSeed = 42;

	struct FTestData
	{
		explicit FTestData(int32 Seed = DefaultSeed);

		void Reset();

		AActor* TestActor;
		UPCGComponent* TestComponent;
		FPCGDataCollection InputData;
		UPCGSettings* Settings;
		int32 Seed;
		FRandomStream RandomStream;
	};

	/** Helper for adding VolumeData to an InputData */
	void AddVolumeInputData(FPCGDataCollection& InputData, const FVector& Location, const FVector& HalfSize, const FVector& VoxelSize);

	/** Adds generic Volume Spatial Data at the origin */
	void AddGenericVolumeInputData(FTestData& TestData);
	/** Adds randomized Volume Spatial Data at the origin */
	void AddRandomizedVolumeInputData(FTestData& TestData);

	/** Validates whether two DataCollection objects are identical */
	bool DataCollectionsAreIdentical(const FPCGDataCollection& FirstCollection, const FPCGDataCollection& SecondCollection);
	/** Validates whether two SpatialData objects are identical */
	bool SpatialDataIsIdentical(const UPCGData* FirstSpatialData, const UPCGData* SecondSpatialData);
	/** Validates whether two PointData objects are identical */
	bool PointDataIsIdentical(const UPCGData* FirstPointData, const UPCGData* SecondPointData);
	/** A catch function for unimplemented comparisons */
	bool ComparisonIsUnimplemented(const UPCGData* FirstPointData, const UPCGData* SecondPointData);

	/** Determines if a PCG DataType is a type that should be compared */
	bool DataIsComparable(EPCGDataType DataType);

	/** Randomizes the order of InputData */
	void ShuffleInputOrder(FTestData& TestData);

	/** Gets a comparison function to compare two of a specific DataType */
	TFunction<bool(const UPCGData*, const UPCGData*)> GetCompareFunction(EPCGDataType DataType);

	/** Execute the elements for each valid input and compare if all the outputs are identical */
	bool ExecutionIsDeterministic(FTestData& FirstTestData, FTestData& SecondTestData);

	/** Generates settings based upon a UPCGSettings subclass */
	template<typename SettingsType>
	void GenerateSettings(FTestData& TestData, TFunction<void(SettingsType*, FRandomStream&)> ExtraSettingsDelegate = nullptr)
	{
		TestData.Settings = NewObject<SettingsType>();
		check(TestData.Settings);
		TestData.Settings->Seed = TestData.Seed;

		TestData.InputData.TaggedData.Emplace_GetRef().Data = TestData.Settings;

		if (ExtraSettingsDelegate)
		{
			ExtraSettingsDelegate(Cast<SettingsType>(TestData.Settings), TestData.RandomStream);
		}
	}
}