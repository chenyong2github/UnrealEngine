// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"
#include "PCGSettings.h"
#include "Tests/PCGTestsCommon.h"

class AActor;
class UPCGComponent;
class UPCGData;
class UPCGSpatialData;
class UPrimitiveComponent;
class USplineComponent;
struct FPCGDataCollection;

namespace PCGDeterminismTests
{
	constexpr static int32 DefaultSeed = 42;
	constexpr static int32 DefaultNumPointsToGenerate = 1;
	constexpr static int32 DefaultNumPolyLinePointsToGenerate = 6;

	const FName DefaultTestPinName = TEXT("Test");

	struct FTestData
	{
		explicit FTestData(int32 Seed = DefaultSeed);

		void Reset();

		AActor* TestActor;
		UPCGComponent* TestPCGComponent;
		FPCGDataCollection InputData;
		UPCGSettings* Settings;
		int32 Seed;
		FRandomStream RandomStream;
	};

	struct FPCGDeterminismResult
	{
		int32 Index;
		bool bIsDeterministic;
		FName NodeTitle;
		FString NodeNameString;
		FString DataTestedString;
		FString AdditionalDetailString;
	};

	/** Validates if a PCGNode is deterministic, updating the passed in result accordingly */
	PCG_API void RunDeterminismTests(const UPCGNode* InPCGNode, FPCGDeterminismResult& OutResult);

	/** Adds input data to test data, based on input pins' allowed types */
	void AddInputDataBasedOnPins(FTestData& TestData, const UPCGNode* InPCGNode, FPCGDeterminismResult& OutResult);

	/** Helper for adding PointData with a single Point to an InputData */
	void AddSinglePointInputData(FPCGDataCollection& InputData, const FVector& Location, const FName& PinName = DefaultTestPinName);
	/** Helper for adding PointData with multiple Points to an InputData */
	void AddMultiplePointsInputData(FPCGDataCollection& InputData, const TArray<FPCGPoint>& Points, const FName& PinName = DefaultTestPinName);
	/** Helper for adding VolumeData to an InputData */
	void AddVolumeInputData(FPCGDataCollection& InputData, const FVector& Location, const FVector& HalfSize, const FVector& VoxelSize, const FName& PinName = DefaultTestPinName);
	/** Helper for adding PolyLineData to an InputData */
	void AddPolyLineInputData(FPCGDataCollection& InputData, USplineComponent* SplineComponent, const FName& PinName = DefaultTestPinName);
	/** Helper for adding PrimitiveData to an InputData */
	void AddPrimitiveInputData(FPCGDataCollection& InputData, UPrimitiveComponent* SplineComponent, const FVector& VoxelSize, const FName& PinName = DefaultTestPinName);

	/** Adds randomized PointData with a single Point */
	void AddRandomizedSinglePointInputData(FTestData& TestData, int32 PointNum = DefaultNumPointsToGenerate, const FName& PinName = DefaultTestPinName);
	/** Adds randomized PointData with multiple Points */
	void AddRandomizedMultiplePointInputData(FTestData& TestData, int32 PointNum = DefaultNumPointsToGenerate, const FName& PinName = DefaultTestPinName);
	/** Adds randomized Volume Spatial Data */
	void AddRandomizedVolumeInputData(FTestData& TestData, const FName& PinName = DefaultTestPinName);
	/** Adds randomized Surface Spatial Data */
	void AddRandomizedSurfaceInputData(FTestData& TestData, const FName& PinName = DefaultTestPinName);
	/** Adds randomized PolyLine Spatial Data */
	void AddRandomizedPolyLineInputData(FTestData& TestData, int32 PointNum = DefaultNumPolyLinePointsToGenerate, const FName& PinName = DefaultTestPinName);
	/** Adds randomized Primitive Spatial Data */
	void AddRandomizedPrimitiveInputData(FTestData& TestData, const FName& PinName = DefaultTestPinName);

	/** Validates whether two DataCollection objects are identical */
	bool DataCollectionsAreIdentical(const FPCGDataCollection& FirstCollection, const FPCGDataCollection& SecondCollection);

	/** Validates whether two SpatialData objects are identical */
	bool SpatialDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two PointData objects are identical */
	bool PointDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two VolumeData objects are identical */
	bool VolumeDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two SurfaceData objects are identical */
	bool SurfaceDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two PolyLineData objects are identical */
	bool PolyLineDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two PrimitiveData objects are identical */
	bool PrimitiveDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two DifferenceData objects are identical */
	bool DifferenceDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData);

	/** Validates the basics of SpatialData are identical */
	bool SpatialBasicsAreIdentical(const UPCGSpatialData* FirstSpatialData, const UPCGSpatialData* SecondSpatialData);

	/** A catch function for unimplemented comparisons */
	bool ComparisonIsUnimplemented(const UPCGData* FirstData, const UPCGData* SecondData);

	/** Determines if a PCG DataType is a type that should be compared */
	bool DataTypeIsComparable(EPCGDataType DataType);
	/** Validates PCGData and if its DataType is comparable */
	bool DataIsComparable(const UPCGData* Data);

	/** Randomizes the order of InputData */
	void ShuffleInputOrder(FTestData& TestData);

	/** Gets a comparison function to compare two of a specific DataType */
	TFunction<bool(const UPCGData*, const UPCGData*)> GetCompareFunction(EPCGDataType DataType);

	/** Execute the elements for each valid input and compare if all the outputs are identical */
	bool ExecutionIsDeterministic(FTestData& FirstTestData, FTestData& SecondTestData, const UPCGNode* PCGNode = nullptr);
	/** Execute the same element twice compare if all the outputs are identical */
	bool ExecutionIsDeterministicSameData(FTestData& TestData, const UPCGNode* PCGNode = nullptr);

	/** Generates settings based upon a UPCGSettings subclass */
	template<typename SettingsType>
	void GenerateSettings(FTestData& TestData, TFunction<void(FTestData& TestData)> ExtraSettingsDelegate = nullptr)
	{
		TestData.Settings = NewObject<SettingsType>();
		check(TestData.Settings);
		TestData.Settings->Seed = TestData.Seed;

		TestData.InputData.TaggedData.Emplace_GetRef().Data = TestData.Settings;
		TestData.InputData.TaggedData.Last().Pin = FName(TEXT("Settings"));

		if (ExtraSettingsDelegate)
		{
			ExtraSettingsDelegate(TestData);
		}
	}

	/** Validates whether both UPCGData can be cast to a specified subclass */
	template<typename DataType>
	bool BothDataCastsToDataType(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		check(FirstData && SecondData);

		return (Cast<const DataType>(FirstData) != nullptr && Cast<const DataType>(SecondData) != nullptr);
	}
}