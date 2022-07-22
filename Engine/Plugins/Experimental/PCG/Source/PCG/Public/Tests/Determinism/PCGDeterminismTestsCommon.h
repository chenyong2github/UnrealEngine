// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"
#include "PCGSettings.h"
#include "Tests/PCGTestsCommon.h"

#include "PCGDeterminismTestsCommon.generated.h"

class UPCGComponent;
class UPCGData;
class UPCGSpatialData;
class UPrimitiveComponent;
class USplineComponent;
struct FPCGDataCollection;

// This will include multiple values of different meanings, but we use an enum to facilitate data passing
UENUM()
enum class EDeterminismLevel : uint8
{
	None = 0u,
	NoDeterminism = None,
	Basic,
	OrderOrthogonal,
	OrderConsistent,
	OrderIndependent,
	Deterministic = OrderIndependent
};

USTRUCT(BlueprintType)
struct FDeterminismNodeTestResult
{
	GENERATED_BODY()

	/** The node's title */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Determinism)
	FName NodeTitle = TEXT("Untitled");

	/** The node's name */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Determinism)
	FString NodeName = TEXT("Unnamed");

	// TODO: Add the seed to the UI widget
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Determinism)
	int32 Seed = -1;

	/** BitFlags for which data types were tested */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Determinism)
	EPCGDataType DataTypesTested = EPCGDataType::None;

	/** A mapping of [test names : test results] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Determinism)
	TMap<FName, EDeterminismLevel> TestResults;

	/** A mapping of [test name : additional details] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Determinism)
	TArray<FString> AdditionalDetails;

	/** T/F whether a flag has been raised on this node's tests (for UI) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Determinism)
	bool bFlagRaised = false;

	// TODO: Chrono Duration of how long the tests took
};

namespace PCGDeterminismTests
{
	namespace Defaults
	{
		constexpr static int32 Seed = 42;
		constexpr static int32 NumPointsToGenerate = 1;
		constexpr static int32 NumTestPointsToGenerate = 100;
		constexpr static int32 NumPolyLinePointsToGenerate = 6;
		constexpr static int32 NumTestPolyLinePointsToGenerate = 6;
		constexpr static int32 NumSamplingStepsPerDimension = 100;
		constexpr static int32 NumTestInputsPerPin = 2;

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

	constexpr static EPCGDataType TestableDataTypes[6] =
	{
		EPCGDataType::None,
		EPCGDataType::Point,
		EPCGDataType::Volume,
		EPCGDataType::PolyLine,
		EPCGDataType::Primitive,
		EPCGDataType::Landscape
	};

	/** A default delegate to report an unset test */
	bool LogInvalidTest(const UPCGNode* InPCGNode, const FName& TestName, FDeterminismNodeTestResult& OutResult);

	typedef TFunction<bool(const UPCGNode* InPCGNode, const FName& TestName, FDeterminismNodeTestResult& OutResult)> TestFunction;

	struct FNodeTestInfo
	{
		FNodeTestInfo(FText Label, TestFunction Delegate, float LabelWidth = 140.f) :
			TestLabel(Label),
			TestName(Label.ToString()),
			TestDelegate(Delegate),
			TestLabelWidth(LabelWidth) {}

		FText TestLabel = NSLOCTEXT("PCGDeterminism", "UnnamedTest", "Unnamed Test");
		const FName TestName;
		TestFunction TestDelegate = LogInvalidTest;
		float TestLabelWidth = 140.f;
	};

	struct FNodeAndOptions
	{
		explicit FNodeAndOptions(const UPCGNode* PCGNode, const int32 Seed, bool bMultipleOptionsPerPin) :
			PCGNode(PCGNode),
			Seed(Seed),
			bMultipleOptionsPerPin(bMultipleOptionsPerPin) {}

		const UPCGNode* PCGNode;
		const int32 Seed;
		const bool bMultipleOptionsPerPin;
		TArray<TArray<EPCGDataType>> BaseOptionsByPin;
	};

	/** Validates if a PCGNode is deterministic */
	PCG_API void RunDeterminismTest(const UPCGNode* InPCGNode, FDeterminismNodeTestResult& OutResult, const FNodeTestInfo& TestToRun);

	/** Validates all the generic determinism tests for any given node */
	PCG_API bool RunBasicTestSuite(const UPCGNode* InPCGNode, const FName& TestName, FDeterminismNodeTestResult& OutResult);

	/** Validates the various levels of order independence for any given node */
	PCG_API bool RunOrderIndependenceSuite(const UPCGNode* InPCGNode, const FName& TestName, FDeterminismNodeTestResult& OutResult);

	/** Validates minimal node determinism against the same single test data */
	bool RunBasicSelfTest(const FNodeAndOptions& NodeAndOptions);
	/** Validates minimal node determinism against two identical single test data */
	bool RunBasicCopiedSelfTest(const FNodeAndOptions& NodeAndOptions);

	/** Conducts tests on all permutations and determines the highest level of determinism */
	EDeterminismLevel GetHighestDeterminismLevel(const FNodeAndOptions& NodeAndOptions,
		int32 NumInputsPerPin = Defaults::NumTestInputsPerPin,
		EDeterminismLevel MaxLevel = EDeterminismLevel::OrderIndependent);

	/** Adds input data to test data, based on input pins' allowed types */
	void AddRandomizedInputData(PCGTestsCommon::FTestData& TestData, EPCGDataType DataType, const FName& PinName = Defaults::TestPinName);

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
	/** Helper for adding LandscapeData to an InputData */
	void AddLandscapeInputData(FPCGDataCollection& InputData);

	/** Adds randomized PointData with a single Point */
	void AddRandomizedSinglePointInputData(PCGTestsCommon::FTestData& TestData, int32 PointNum = Defaults::NumPointsToGenerate, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized PointData with multiple Points */
	void AddRandomizedMultiplePointInputData(PCGTestsCommon::FTestData& TestData, int32 PointNum = Defaults::NumPointsToGenerate, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Volume Spatial Data */
	void AddRandomizedVolumeInputData(PCGTestsCommon::FTestData& TestData, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Surface Spatial Data */
	void AddRandomizedSurfaceInputData(PCGTestsCommon::FTestData& TestData, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized PolyLine Spatial Data */
	void AddRandomizedPolyLineInputData(PCGTestsCommon::FTestData& TestData, int32 PointNum = Defaults::NumPolyLinePointsToGenerate, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Primitive Spatial Data */
	void AddRandomizedPrimitiveInputData(PCGTestsCommon::FTestData& TestData, const FName& PinName = Defaults::TestPinName);
	/** Adds randomized Landscape Spatial Data */
	void AddRandomizedLandscapeInputData(PCGTestsCommon::FTestData& TestData, const FName& PinName = Defaults::TestPinName);

	/** Validates whether two DataCollection objects are exactly identical */
	bool DataCollectionsAreIdentical(const FPCGDataCollection& FirstCollection, const FPCGDataCollection& SecondCollection);
	/** Validates whether two DataCollection objects have data in order relative to their inputs */
	bool DataCollectionsAreConsistent(const FPCGDataCollection& FirstCollection, const FPCGDataCollection& SecondCollection, int32 NumInputs);
	/** Validates whether two DataCollection objects contain all the same data */
	bool DataCollectionsContainSameData(const FPCGDataCollection& FirstCollection, const FPCGDataCollection& SecondCollection);
	/** Validates the data collections match and delivers matched indices and offsets */
	bool DataCollectionsMatch(const FPCGDataCollection& FirstCollection,
		const FPCGDataCollection& SecondCollection,
		TArray<int32>& OutIndexOffsets);
	/** Validates whether the internal data within the two DataCollection objects are consistent */
	bool InternalDataMatches(const UPCGData* FirstData, const UPCGData* SecondData, TArray<int32>& OutIndexOffsets);

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
	bool SpatialBasicsAreIdentical(const UPCGData* FirstData, const UPCGData* SecondData);

	/** Validates whether two SpatialData objects are consistent */
	bool SpatialDataIsConsistent(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two PointData objects are consistent */
	bool PointDataIsConsistent(const UPCGData* FirstData, const UPCGData* SecondData);

	/** Validates whether two SpatialData objects contain the same spatial data */
	bool SpatialDataIsOrthogonal(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Validates whether two PointData objects contain the same points */
	bool PointDataIsOrthogonal(const UPCGData* FirstData, const UPCGData* SecondData);

	/** A catch function for unimplemented comparisons */
	bool ComparisonIsUnimplemented(const UPCGData* FirstData, const UPCGData* SecondData);
	/** Updates the test result output to reflect that the permutation limit has been exceeded */
	void UpdateTestResultForOverPermutationLimitError(FDeterminismNodeTestResult& OutResult);

	/** Determines if a PCG DataType is a type that should be compared */
	bool DataTypeIsComparable(EPCGDataType DataType);
	/** Validates PCGData and if its DataType is comparable */
	bool DataIsComparable(const UPCGData* Data);
	/** Validates that the data contains a shuffle-able array of data */
	bool DataCanBeShuffled(const UPCGData* Data);

	/** Randomizes the order of InputData collections */
	void ShuffleInputOrder(PCGTestsCommon::FTestData& TestData);
	/** Randomizes the order of OutputData collections */
	void ShuffleOutputOrder(PCGTestsCommon::FTestData& TestData);
	/** Randomizes the order of all internal data */
	void ShuffleAllInternalData(PCGTestsCommon::FTestData& TestData);
	/** Shifts the order of InputData collections */
	void ShiftInputOrder(PCGTestsCommon::FTestData& TestData, int32 NumShifts = 1);

	/** Gets an array of the union between an InputPin's allowed types and testable types */
	TArray<EPCGDataType> FilterTestableDataTypes(EPCGDataType AllowedDataTypes, int32 NumMultipleInputs = 1);
	/** Given input pins, updates an array with a permutation base of options */
	void RetrieveBaseOptionsPerPin(TArray<TArray<EPCGDataType>>& InBaseOptionsArray,
		const TArray<TObjectPtr<UPCGPin>>& InputPins,
		EPCGDataType& OutDataTypesTested,
		int32 NumMultipleInputs = 1);
	/** Gets the number of permutations from a base set of options */
	int32 GetNumPermutations(const TArray<TArray<EPCGDataType>>& BaseOptionsArray);
	/** Gets the index of a pin's permutation based on the permutation iteration */
	EPCGDataType GetPermutation(int32 PermutationIteration, int32 PinIndex, const TArray<TArray<EPCGDataType>>& BaseOptionsPerPin);

	/** Helper to update a tests' outgoing results */
	void UpdateTestResults(FName TestName, FDeterminismNodeTestResult& OutResult, EDeterminismLevel DeterminismLevel);

	/** Gets a comparison function to compare two data objects */
	TFunction<bool(const UPCGData*, const UPCGData*)> GetDataCompareFunction(EPCGDataType DataType, EDeterminismLevel DeterminismLevel);
	/** Gets a comparison function to compare two data collection entry basics */
	TFunction<bool(const UPCGData*, const UPCGData*)> GetDataCollectionCompareFunction(EPCGDataType DataType);

	/** Executes the element with given test data */
	void ExecuteWithTestData(PCGTestsCommon::FTestData& TestData, const UPCGNode* PCGNode);
	/** Executes the element with given test data against itself */
	void ExecuteWithSameTestData(const PCGTestsCommon::FTestData& TestData, const UPCGNode* PCGNode, FPCGDataCollection& OutFirstOutputData, FPCGDataCollection& OutSecondOutputData);
	/** Executes the element with given test data against itself, with the same element */
	void ExecuteWithSameTestDataSameElement(const PCGTestsCommon::FTestData& TestData, const UPCGNode* PCGNode, FPCGDataCollection& OutFirstOutputData, FPCGDataCollection& OutSecondOutputData);
	/** Execute the elements for each valid input and compare if all the outputs are at least orthogonally deterministic */
	bool ExecutionIsDeterministic(PCGTestsCommon::FTestData& FirstTestData, PCGTestsCommon::FTestData& SecondTestData, const UPCGNode* PCGNode = nullptr);
	/** Execute the same element twice compare if all the outputs are at least orthogonally deterministic */
	bool ExecutionIsDeterministicSameData(const PCGTestsCommon::FTestData& TestData, const UPCGNode* PCGNode = nullptr);

	/** Generates settings based upon a UPCGSettings subclass */
	template<typename SettingsType>
	SettingsType* GenerateSettings(PCGTestsCommon::FTestData& TestData, TFunction<void(PCGTestsCommon::FTestData&)> ExtraSettingsDelegate = nullptr)
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

		return Cast<const DataType>(FirstData) != nullptr && Cast<const DataType>(SecondData) != nullptr;
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

	template<typename DataType>
	void ShiftArrayElements(TArray<DataType>& Array, int32 NumShifts = 1)
	{
		if (Array.Num() < 2)
		{
			return;
		}

		int32 Count = Array.Num();
		NumShifts %= Count;
		if (NumShifts < 0)
		{
			NumShifts += Count;
		}

		TArray<DataType> TempArray;
		TempArray.SetNum(Count);

		for (int32 I = 0; I < NumShifts; ++I)
		{
			TempArray[I] = Array[I + Count - NumShifts];
		}

		for (int32 I = NumShifts; I < Count; ++I)
		{
			TempArray[I] = Array[I - NumShifts];
		}

		Array = MoveTemp(TempArray);
	}

	namespace Defaults
	{
		static const FNodeTestInfo DeterminismBasicTestInfo = FNodeTestInfo(NSLOCTEXT("PCGDeterminism", "BasicTest", "Basic Test"), RunBasicTestSuite, 90.f);
		static const FNodeTestInfo DeterminismOrderIndependenceInfo = FNodeTestInfo(NSLOCTEXT("PCGDeterminism", "OrderIndependenceTest", "Order Independence"), RunOrderIndependenceSuite, 140.f);
	}
}