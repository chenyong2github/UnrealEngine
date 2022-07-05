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
	namespace Defaults
	{
		constexpr static int32 Seed = 42;
		constexpr static int32 NumPointsToGenerate = 1;
		constexpr static int32 NumTestPointsToGenerate = 1000;
		constexpr static int32 NumPolyLinePointsToGenerate = 6;
		constexpr static int32 NumTestPolyLinePointsToGenerate = 12;
		constexpr static int32 NumSamplingStepsPerDimension = 100;
		constexpr static int32 NumMultipleTestDataSets = 2;

		constexpr static FVector::FReal SmallDistance = 50.0;
		constexpr static FVector::FReal MediumDistance = 200.0;
		constexpr static FVector::FReal LargeDistance = 500.0;

		const FVector SmallVector = FVector::OneVector * SmallDistance;
		const FVector MediumVector = FVector::OneVector * MediumDistance;
		const FVector LargeVector = FVector::OneVector * LargeDistance;

		const FName TestPinName = TEXT("Test");
		/** A little bigger than the typical largest volumes */
		const FBox TestingVolume = FBox(-1.2 * LargeVector, 1.2 * LargeVector);
	}

	struct FTestData
	{
		explicit FTestData(int32 Seed = Defaults::Seed, UPCGSettings* DefaultSettings = nullptr);

		void Reset();

		AActor* TestActor;
		UPCGComponent* TestPCGComponent;
		FPCGDataCollection InputData;
		UPCGSettings* Settings;
		int32 Seed;
		FRandomStream RandomStream;
	};

	struct FNodeTestResult
	{
		int32 Index = -1;
		EPCGDataType DataTypesTested = EPCGDataType::None;
		FName NodeTitle = TEXT("Untitled");
		FString NodeNameString = TEXT("Unnamed");
		TMap<FName, bool> TestResults;
		TArray<FString> AdditionalDetails;
		bool bFlagRaised = false;
		// TODO: Chrono Duration of how long the tests took
	};

	/** A default delegate to report an unset test */
	bool LogInvalidTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);

	typedef TFunction<bool(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)> TestFunction;

	struct FNodeTestInfo
	{
		FNodeTestInfo(FText Label, TestFunction Delegate, float LabelWidth = 140.f) :
			TestLabel(Label),
			TestDelegate(Delegate),
			TestLabelWidth(LabelWidth) {}

		FText TestLabel = NSLOCTEXT("PCGDeterminism", "UnnamedTest", "Unnamed Test");
		TestFunction TestDelegate = LogInvalidTest;
		float TestLabelWidth = 140.f;
	};

	/** Validates if a PCGNode is deterministic */
	PCG_API void RunDeterminismTest(const UPCGNode* InPCGNode, FNodeTestResult& OutResult, const FNodeTestInfo& TestToRun);

	/** Adds the basic set of determinism tests to the passed in array */
	PCG_API void RetrieveBasicTests(TArray<FNodeTestInfo>& OutBasicTests);

	/** Validates node determinism against the same single test data */
	bool RunSingleSameDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);
	/** Validates node determinism against two identical single test data */
	bool RunSingleIdenticalDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);
	/** Validates node determinism with the same multiple sets of test data */
	bool RunMultipleSameDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);
	/** Validates node determinism with two identical multiple sets of test data */
	bool RunMultipleIdenticalDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);
	/** Validates node determinism with multiple sets of test data, shuffling the order of the second set's data collection */
	bool RunDataCollectionOrderIndependenceTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);
	/** Validates node determinism with multiple sets of test data, shuffling all internal data */
	bool RunAllDataOrderIndependenceTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);

	/** Adds input data to test data, based on input pins' allowed types */
	void AddInputDataBasedOnPins(FTestData& TestData, const UPCGNode* InPCGNode, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails);

	/** Helper for adding PointData with a single Point to an InputData */
	void AddSinglePointInputData(FPCGDataCollection& InputData, const FVector& Location, const FName& PinName = Defaults::TestPinName);
	/** Helper for adding PointData with multiple Points to an InputData */
	void AddMultiplePointsInputData(FPCGDataCollection& InputData, const TArray<FPCGPoint>& Points, const FName& PinName = Defaults::TestPinName);
	/** Helper for adding VolumeData to an InputData */
	void AddVolumeInputData(FPCGDataCollection& InputData, const FVector& Location, const FVector& HalfSize, const FVector& VoxelSize, const FName& PinName = Defaults::TestPinName);
	/** Helper for adding PolyLineData to an InputData */
	void AddPolyLineInputData(FPCGDataCollection& InputData, USplineComponent* SplineComponent, const FName& PinName = Defaults::TestPinName);
	/** Helper for adding PrimitiveData to an InputData */
	void AddPrimitiveInputData(FPCGDataCollection& InputData, UPrimitiveComponent* SplineComponent, const FVector& VoxelSize, const FName& PinName = Defaults::TestPinName);

	/** Adds randomized PointData with a single Point */
	void AddRandomizedSinglePointInputData(FTestData& TestData, int32 PointNum = Defaults::NumPointsToGenerate, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized PointData with multiple Points */
	void AddRandomizedMultiplePointInputData(FTestData& TestData, int32 PointNum = Defaults::NumPointsToGenerate, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Volume Spatial Data */
	void AddRandomizedVolumeInputData(FTestData& TestData, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Surface Spatial Data */
	void AddRandomizedSurfaceInputData(FTestData& TestData, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized PolyLine Spatial Data */
	void AddRandomizedPolyLineInputData(FTestData& TestData, int32 PointNum = Defaults::NumPolyLinePointsToGenerate, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Primitive Spatial Data */
	void AddRandomizedPrimitiveInputData(FTestData& TestData, const FName& PinName = Defaults::TestPinName);

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
	/** Validates whether two SpatialData objects are identical via Point Sampling */
	bool SampledSpatialDataIsIdentical(const UPCGSpatialData* FirstSpatialData, const UPCGSpatialData* SecondSpatialData);

	/** Validates the basics of SpatialData are identical */
	bool SpatialBasicsAreIdentical(const UPCGSpatialData* FirstSpatialData, const UPCGSpatialData* SecondSpatialData);

	/** A catch function for unimplemented comparisons */
	bool ComparisonIsUnimplemented(const UPCGData* FirstData, const UPCGData* SecondData);

	/** Determines if a PCG DataType is a type that should be compared */
	bool DataTypeIsComparable(EPCGDataType DataType);
	/** Validates PCGData and if its DataType is comparable */
	bool DataIsComparable(const UPCGData* Data);
	/** Validates that the data contains a shuffle-able array of data */
	bool DataCanBeShuffled(const UPCGData* Data);

	/** Randomizes the order of InputData collections */
	void ShuffleInputOrder(FTestData& TestData);
	/** Randomizes the order of all internal data */
	void ShuffleAllInternalData(FTestData& TestData);

	/** Gets a comparison function to compare two of a specific DataType */
	TFunction<bool(const UPCGData*, const UPCGData*)> GetCompareFunction(EPCGDataType DataType);

	/** Execute the elements for each valid input and compare if all the outputs are identical */
	bool ExecutionIsDeterministic(const FTestData& FirstTestData, const FTestData& SecondTestData, const UPCGNode* PCGNode = nullptr);
	/** Execute the same element twice compare if all the outputs are identical */
	bool ExecutionIsDeterministicSameData(FTestData& TestData, const UPCGNode* PCGNode = nullptr);

	/** Generates settings based upon a UPCGSettings subclass */
	template<typename SettingsType>
	SettingsType* GenerateSettings(FTestData& TestData, TFunction<void(FTestData&)> ExtraSettingsDelegate = nullptr)
	{
		SettingsType* TypedSettings = NewObject<SettingsType>();
		check(TypedSettings);

		TestData.Settings = TypedSettings;
		TestData.Settings->Seed = TestData.Seed;

		TestData.InputData.TaggedData.Emplace_GetRef().Data = TestData.Settings;
		TestData.InputData.TaggedData.Last().Pin = FName(TEXT("Settings"));

		if (ExtraSettingsDelegate)
		{
			ExtraSettingsDelegate(TestData);
		}

		return TypedSettings;
	}

	/** Validates whether both UPCGData can be cast to a specified subclass */
	template<typename DataType>
	bool BothDataCastsToDataType(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		check(FirstData && SecondData);

		return (Cast<const DataType>(FirstData) != nullptr && Cast<const DataType>(SecondData) != nullptr);
	}

	template<typename DataType>
	void ShuffleArray(TArray<DataType>& Array, FRandomStream& RandomStream)
	{
		const int32 LastIndex = Array.Num() - 1;
		for (int32 I = 0; I <= LastIndex; ++I)
		{
			int32 Index = RandomStream.RandRange(I, LastIndex);

			if (I != Index)
			{
				Array.Swap(I, Index);
			}
		}
	}
}