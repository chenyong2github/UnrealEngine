// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGHelpers.h"
#include "PCGPoint.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGSurfaceData.h"
#include "Data/PCGVolumeData.h"
#include "Tests/PCGTestsCommon.h"

#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "PCGDeterminism"

namespace PCGDeterminismTests
{
	FTestData::FTestData(int32 RandomSeed, UPCGSettings* DefaultSettings) :
		Settings(DefaultSettings),
		Seed(RandomSeed),
		RandomStream(Seed)
	{
#if WITH_EDITOR
		check(GEditor);
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		check(EditorWorld);

		// No getting the level dirty
		FActorSpawnParameters TransientActorParameters;
		TransientActorParameters.bHideFromSceneOutliner = true;
		TransientActorParameters.bTemporaryEditorActor = true;
		TransientActorParameters.ObjectFlags = RF_Transient;
		TestActor = EditorWorld->SpawnActor<AActor>(AActor::StaticClass(), TransientActorParameters);
		check(TestActor);

		TestPCGComponent = NewObject<UPCGComponent>(TestActor, FName(TEXT("Test PCG Component")), RF_Transient);
		check(TestPCGComponent);
		TestActor->AddInstanceComponent(TestPCGComponent);
		TestPCGComponent->RegisterComponent();

		UPCGGraph* TestGraph = NewObject<UPCGGraph>(TestPCGComponent, FName(TEXT("Test PCG Graph")), RF_Transient);
		check(TestGraph);
		TestPCGComponent->SetGraph(TestGraph);
#else
		TestActor = nullptr;
		TestPCGComponent = nullptr;
		Settings = nullptr;
#endif
	}

	void FTestData::Reset()
	{
		// Clear all the data
		RandomStream.Reset();
		InputData.TaggedData.Empty();
		Settings = nullptr;
	}

	bool LogInvalidTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		UE_LOG(LogPCG, Warning, TEXT("Attempting to run an invalid determinism test"));
		OutAdditionalDetails.Add(TEXT("Invalid test"));
		return false;
	}

	PCG_API void RunDeterminismTest(const UPCGNode* InPCGNode, FNodeTestResult& OutResult, const FNodeTestInfo& TestToRun)
	{
		if (!InPCGNode || !InPCGNode->DefaultSettings)
		{
			OutResult.DataTypesTested = EPCGDataType::None;
			OutResult.bFlagRaised = true;
			OutResult.AdditionalDetails.Add(TEXT("Invalid Node or Settings"));
			return;
		}

		bool bTestWasSuccessful = TestToRun.TestDelegate(InPCGNode, InPCGNode->DefaultSettings->Seed, OutResult.DataTypesTested, OutResult.AdditionalDetails);
		OutResult.TestResults.FindOrAdd(FName(TestToRun.TestLabel.ToString()), bTestWasSuccessful);
		OutResult.bFlagRaised = OutResult.bFlagRaised && !bTestWasSuccessful;
	}

	PCG_API void RetrieveBasicTests(TArray<FNodeTestInfo>& OutBasicTests)
	{
		OutBasicTests.Emplace(LOCTEXT("SingleSameData", "Single Same Data"), RunSingleSameDataTest);
		OutBasicTests.Emplace(LOCTEXT("SingleIdenticalData", "Single Identical Data"), RunSingleIdenticalDataTest);
		OutBasicTests.Emplace(LOCTEXT("MultipleSameData", "Multiple Same Data"), RunMultipleSameDataTest);
		OutBasicTests.Emplace(LOCTEXT("MultipleIdenticalData", "Multiple Identical Data"), RunMultipleIdenticalDataTest);
		OutBasicTests.Emplace(LOCTEXT("DataCollectionOrderIndependence", "Shuffle Data Indices"),RunDataCollectionOrderIndependenceTest);
		OutBasicTests.Emplace(LOCTEXT("AllDataOrderIndependence", "Shuffle Internal Data"), RunAllDataOrderIndependenceTest);
	}

	bool RunSingleSameDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		FTestData TestData(Seed, InPCGNode->DefaultSettings);

		// Generate random input data
		AddInputDataBasedOnPins(TestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);

		return ExecutionIsDeterministicSameData(TestData, InPCGNode);
	}

	bool RunSingleIdenticalDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		FTestData FirstTestData(Seed, InPCGNode->DefaultSettings);
		FTestData SecondTestData(Seed, InPCGNode->DefaultSettings);

		AddInputDataBasedOnPins(FirstTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
		AddInputDataBasedOnPins(SecondTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);

		return ExecutionIsDeterministic(FirstTestData, SecondTestData, InPCGNode);
	}

	bool RunMultipleSameDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		FTestData TestData(Seed, InPCGNode->DefaultSettings);

		for (int32 I = 0; I < Defaults::NumMultipleTestDataSets; ++I)
		{
			AddInputDataBasedOnPins(TestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
		}

		return ExecutionIsDeterministicSameData(TestData, InPCGNode);
	}

	bool RunMultipleIdenticalDataTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		FTestData FirstTestData(Seed, InPCGNode->DefaultSettings);
		FTestData SecondTestData(Seed, InPCGNode->DefaultSettings);

		for (int32 I = 0; I < Defaults::NumMultipleTestDataSets; ++I)
		{
			AddInputDataBasedOnPins(FirstTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
			AddInputDataBasedOnPins(SecondTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
		}

		return ExecutionIsDeterministic(FirstTestData, SecondTestData, InPCGNode);
	}

	bool RunDataCollectionOrderIndependenceTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		FTestData FirstTestData(Seed, InPCGNode->DefaultSettings);
		FTestData SecondTestData(Seed, InPCGNode->DefaultSettings);

		for (int32 I = 0; I < Defaults::NumMultipleTestDataSets; ++I)
		{
			AddInputDataBasedOnPins(FirstTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
			AddInputDataBasedOnPins(SecondTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
		}

		ShuffleInputOrder(SecondTestData);

		return ExecutionIsDeterministic(FirstTestData, SecondTestData, InPCGNode);
	}

	bool RunAllDataOrderIndependenceTest(const UPCGNode* InPCGNode, int32 Seed, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		FTestData FirstTestData(Seed, InPCGNode->DefaultSettings);
		FTestData SecondTestData(Seed, InPCGNode->DefaultSettings);

		for (int32 I = 0; I < Defaults::NumMultipleTestDataSets; ++I)
		{
			AddInputDataBasedOnPins(FirstTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
			AddInputDataBasedOnPins(SecondTestData, InPCGNode, OutDataTypesTested, OutAdditionalDetails);
		}

		ShuffleInputOrder(SecondTestData);
		ShuffleAllInternalData(SecondTestData);

		return ExecutionIsDeterministic(FirstTestData, SecondTestData, InPCGNode);
	}

	void AddInputDataBasedOnPins(FTestData& TestData, const UPCGNode* PCGNode, EPCGDataType& OutDataTypesTested, TArray<FString>& OutAdditionalDetails)
	{
		check(PCGNode);

		for (UPCGPin* InputPin : PCGNode->GetInputPins())
		{
			check(InputPin);

			switch (InputPin->Properties.AllowedTypes)
			{
			case EPCGDataType::Point:
				AddRandomizedMultiplePointInputData(TestData);
				OutDataTypesTested |= EPCGDataType::Point;
				break;
			case EPCGDataType::Volume:
				AddRandomizedVolumeInputData(TestData);
				OutDataTypesTested |= EPCGDataType::Volume;
				break;
			case EPCGDataType::PolyLine:
				AddRandomizedPolyLineInputData(TestData);
				OutDataTypesTested |= EPCGDataType::PolyLine;
				break;
			case EPCGDataType::Primitive:
				AddRandomizedPrimitiveInputData(TestData);
				OutDataTypesTested |= EPCGDataType::Primitive;
				break;
			case EPCGDataType::Surface:
				AddRandomizedSurfaceInputData(TestData);
				OutDataTypesTested |= EPCGDataType::Surface;
				break;
			case EPCGDataType::Any:
			case EPCGDataType::Spatial:
				AddRandomizedMultiplePointInputData(TestData, Defaults::NumTestPointsToGenerate);
				AddRandomizedVolumeInputData(TestData);
				AddRandomizedPolyLineInputData(TestData, Defaults::NumTestPolyLinePointsToGenerate);
				AddRandomizedPrimitiveInputData(TestData);
				AddRandomizedSurfaceInputData(TestData);
				OutDataTypesTested |= EPCGDataType::Spatial;
				break;
			default:
				OutDataTypesTested = EPCGDataType::None;
				OutAdditionalDetails.Add(TEXT("Unknown InputPin data type to test"));
				break;
			}
		}
	}

	void AddSinglePointInputData(FPCGDataCollection& InputData, const FVector& Location, const FName& PinName)
	{
		UPCGPointData* PointData = PCGTestsCommon::CreatePointData(Location);

		FPCGTaggedData& TaggedData = InputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		TaggedData.Pin = PinName;
	}

	void AddMultiplePointsInputData(FPCGDataCollection& InputData, const TArray<FPCGPoint>& Points, const FName& PinName)
	{
		UPCGPointData* PointData = PCGTestsCommon::CreateEmptyPointData();
		PointData->SetPoints(Points);

		FPCGTaggedData& TaggedData = InputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PointData;
		TaggedData.Pin = PinName;
	}

	void AddVolumeInputData(FPCGDataCollection& InputData, const FVector& Location, const FVector& HalfSize, const FVector& VoxelSize, const FName& PinName)
	{
		UPCGVolumeData* VolumeData = PCGTestsCommon::CreateVolumeData(FBox::BuildAABB(Location, HalfSize));
		VolumeData->VoxelSize = VoxelSize;

		FPCGTaggedData& TaggedData = InputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = VolumeData;
		TaggedData.Pin = PinName;
	}

	void AddPolyLineInputData(FPCGDataCollection& InputData, USplineComponent* SplineComponent, const FName& PinName)
	{
		UPCGSplineData* SplineData = NewObject<UPCGSplineData>();
		SplineData->Initialize(SplineComponent);

		FPCGTaggedData& TaggedData = InputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = SplineData;
		TaggedData.Pin = PinName;
	}

	void AddPrimitiveInputData(FPCGDataCollection& InputData, UPrimitiveComponent* PrimitiveComponent, const FVector& VoxelSize, const FName& PinName)
	{
		UPCGPrimitiveData* PrimitiveData = NewObject<UPCGPrimitiveData>();
		PrimitiveData->Initialize(PrimitiveComponent);
		PrimitiveData->VoxelSize = VoxelSize;

		FPCGTaggedData& TaggedData = InputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PrimitiveData;
		TaggedData.Pin = PinName;
	}

	void AddRandomizedSinglePointInputData(FTestData& TestData, int32 PointNum, const FName& PinName)
	{
		check(PointNum > 0);
		for (int32 I = 0; I < PointNum; ++I)
		{
			AddSinglePointInputData(TestData.InputData, TestData.RandomStream.VRand() * Defaults::LargeDistance, PinName);
		}
	}

	void AddRandomizedMultiplePointInputData(FTestData& TestData, int32 PointNum, const FName& PinName)
	{
		check(PointNum > 0);

		TArray<FPCGPoint> Points;
		Points.SetNumUninitialized(PointNum);
		for (int32 I = 0; I < PointNum; ++I)
		{
			FVector NewLocation = TestData.RandomStream.VRand() * Defaults::LargeDistance;
			FTransform NewTransform(FRotator::ZeroRotator, NewLocation, FVector::OneVector * TestData.RandomStream.FRandRange(0.5, 1.5));
			int PointSeed = PCGHelpers::ComputeSeed(static_cast<int>(NewLocation.X), static_cast<int>(NewLocation.Y), static_cast<int>(NewLocation.Z));
			Points[I] = FPCGPoint(NewTransform, 1.f, PCGHelpers::ComputeSeed(PointSeed, TestData.Seed));
		}

		AddMultiplePointsInputData(TestData.InputData, Points, PinName);
	}

	void AddRandomizedVolumeInputData(FTestData& TestData, const FName& PinName)
	{
		AddVolumeInputData(TestData.InputData,
			TestData.RandomStream.VRand() * Defaults::MediumDistance,
			Defaults::MediumVector + TestData.RandomStream.VRand() * 0.5f * Defaults::MediumDistance,
			Defaults::SmallVector + TestData.RandomStream.VRand() * 0.5f * Defaults::SmallDistance,
			PinName);
	}

	void AddRandomizedSurfaceInputData(FTestData& TestData, const FName& PinName)
	{
		// TODO: PCG doesn't currently generate Surface data; function remains for future scalability
	}

	void AddRandomizedPolyLineInputData(FTestData& TestData, int32 PointNum, const FName& PinName)
	{
		check(TestData.TestActor);
		USplineComponent* TestSplineComponent = Cast<USplineComponent>(TestData.TestActor->GetComponentByClass(USplineComponent::StaticClass()));

		if (TestSplineComponent == nullptr)
		{
			TestSplineComponent = NewObject<USplineComponent>(TestData.TestActor, FName(TEXT("Test Spline Component")), RF_Transient);
		}

		check(PointNum > 1);
		for (int32 I = 0; I < PointNum; ++I)
		{
			TestSplineComponent->AddSplinePoint(TestData.RandomStream.VRand() * Defaults::LargeDistance, ESplineCoordinateSpace::Type::World, false);
			TestSplineComponent->AddRelativeRotation(FRotator(
				TestData.RandomStream.FRandRange(-90.0, 90.0),
				TestData.RandomStream.FRandRange(-90.0, 90.0),
				TestData.RandomStream.FRandRange(-90.0, 90.0)));
		}
		TestSplineComponent->UpdateSpline();

		AddPolyLineInputData(TestData.InputData, TestSplineComponent, PinName);
	}

	void AddRandomizedPrimitiveInputData(FTestData& TestData, const FName& PinName)
	{
		check(TestData.TestActor);
		UPrimitiveComponent* TestPrimitiveComponent = Cast<UPrimitiveComponent>(TestData.TestActor->GetComponentByClass(UPrimitiveComponent::StaticClass()));

		if (TestPrimitiveComponent == nullptr)
		{
			// TODO: If it reaches here, this will break, as it has no bounds. Please suggest the best way to give it some bounds?
			TestPrimitiveComponent = NewObject<UStaticMeshComponent>(TestData.TestActor, FName(TEXT("Test Primitive Component")), RF_Transient);
		}

		TestPrimitiveComponent->SetWorldTransform(FTransform(
			FRotator(TestData.RandomStream.FRandRange(0.0, 90.0), TestData.RandomStream.FRandRange(0.0, 90.0), TestData.RandomStream.FRandRange(0.0, 90.0)),
			TestData.RandomStream.VRand() * Defaults::LargeDistance,
			FVector::OneVector * TestData.RandomStream.FRandRange(0.5, 1.5)));

		// TODO: Probably more varieties to add in the future
		AddPrimitiveInputData(TestData.InputData, TestPrimitiveComponent, Defaults::MediumVector + TestData.RandomStream.VRand() * 0.5f * Defaults::MediumDistance, PinName);
	}

	bool DataCollectionsAreIdentical(const FPCGDataCollection& FirstCollection, const FPCGDataCollection& SecondCollection)
	{
		if (FirstCollection.TaggedData.Num() != SecondCollection.TaggedData.Num())
		{
			return false;
		}

		TArray<int32> FirstCollectionComparableIndices;
		TArray<int32> SecondCollectionComparableIndices;

		// Find comparable data from first collection
		for (int32 I = 0; I < FirstCollection.TaggedData.Num(); ++I)
		{
			if (DataIsComparable(FirstCollection.TaggedData[I].Data))
			{
				FirstCollectionComparableIndices.Emplace(I);
			}
		}

		// Find comparable data from second collection
		for (int32 I = 0; I < SecondCollection.TaggedData.Num(); ++I)
		{
			// Check if its a type we care about comparing
			if (DataIsComparable(SecondCollection.TaggedData[I].Data))
			{
				SecondCollectionComparableIndices.Emplace(I);
			}
		}

		TArray<int32> MatchedIndices;

		// Find matches for each TaggedData array
		for (int32 I : FirstCollectionComparableIndices)
		{
			bool bFound = false;
			for (int32 J : SecondCollectionComparableIndices)
			{
				if (MatchedIndices.Contains(J))
				{
					continue;
				}

				EPCGDataType FirstDataType = FirstCollection.TaggedData[I].Data->GetDataType();
				EPCGDataType SecondDataType = SecondCollection.TaggedData[I].Data->GetDataType();

				// Only compare if they are the same type and same pin label
				if (FirstDataType == SecondDataType && FirstCollection.TaggedData[I].Pin == SecondCollection.TaggedData[I].Pin)
				{
					// TODO: Compare in cascading order:
					// Order independent
					// Order consistent
					// Deterministic but not having a predictable order
					// Non-deterministic
					if (GetCompareFunction(FirstDataType)(FirstCollection.TaggedData[I].Data, SecondCollection.TaggedData[J].Data))
					{
						MatchedIndices.Emplace(J);
						bFound = true;
						break;
					}
				}
			}

			if (!bFound)
			{
				return false;
			}
		}

		return true;
	}

	bool SpatialDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		check(FirstData);
		check(SecondData);

		if (BothDataCastsToDataType<const UPCGPointData>(FirstData, SecondData))
		{
			return PointDataIsIdentical(FirstData, SecondData);
		}
		else if (BothDataCastsToDataType<const UPCGVolumeData>(FirstData, SecondData))
		{
			return VolumeDataIsIdentical(FirstData, SecondData);
		}
		else if (BothDataCastsToDataType<const UPCGSurfaceData>(FirstData, SecondData))
		{
			return SurfaceDataIsIdentical(FirstData, SecondData);
		}
		else if (BothDataCastsToDataType<const UPCGPolyLineData>(FirstData, SecondData))
		{
			return PolyLineDataIsIdentical(FirstData, SecondData);
		}
		else if (BothDataCastsToDataType<const UPCGPrimitiveData>(FirstData, SecondData))
		{
			return PrimitiveDataIsIdentical(FirstData, SecondData);
		}
		else if (BothDataCastsToDataType<const UPCGSpatialData>(FirstData, SecondData))
		{
			return SampledSpatialDataIsIdentical(Cast<const UPCGSpatialData>(FirstData), Cast<const UPCGSpatialData>(SecondData));
		}

		return false;
	}

	bool PointDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		const UPCGPointData* FirstPointData = CastChecked<const UPCGPointData>(FirstData);
		const UPCGPointData* SecondPointData = CastChecked<const UPCGPointData>(SecondData);

		if (!SpatialBasicsAreIdentical(FirstPointData, SecondPointData))
		{
			return false;
		}

		const TArray<FPCGPoint>& FirstPoints = FirstPointData->GetPoints();
		const TArray<FPCGPoint>& SecondPoints = SecondPointData->GetPoints();

		if (FirstPoints.Num() != SecondPoints.Num())
		{
			return false;
		}

		// Match points based on indices
		TSet<int32> RemainingIndices;
		for (int32 i = 0; i < SecondPoints.Num(); ++i)
		{
			RemainingIndices.Add(i);
		}

		for (int32 i = 0; i < FirstPoints.Num(); ++i)
		{
			bool bFound = false;

			// TODO: Maybe use Octree to prune. Is the saved computation worth the complexity of more complex index tracking?
			for (int32 j : RemainingIndices)
			{
				if (PCGTestsCommon::PointsAreIdentical(FirstPoints[i], SecondPoints[j]))
				{
					RemainingIndices.Remove(j);
					bFound = true;
					break;
				}
			}

			// Couldn't find the matching point
			if (!bFound)
			{
				return false;
			}
		}

		return true;
	}

	bool VolumeDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		const UPCGVolumeData* FirstVolumeData = CastChecked<const UPCGVolumeData>(FirstData);
		const UPCGVolumeData* SecondVolumeData = CastChecked<const UPCGVolumeData>(SecondData);

		return FirstVolumeData->VoxelSize == SecondVolumeData->VoxelSize && SpatialBasicsAreIdentical(FirstVolumeData, SecondVolumeData);
	}

	bool SurfaceDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		const UPCGSurfaceData* FirstSurfaceData = CastChecked<const UPCGSurfaceData>(FirstData);
		const UPCGSurfaceData* SecondSurfaceData = CastChecked<const UPCGSurfaceData>(SecondData);

		// TODO: Implement Surface Data comparison as needed in the future
		UE_LOG(LogPCG, Warning, TEXT("Surface comparison not fully implemented."));

		return SpatialBasicsAreIdentical(FirstSurfaceData, SecondSurfaceData);
	}

	bool PolyLineDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		const UPCGPolyLineData* FirstPolyLineData = CastChecked<const UPCGPolyLineData>(FirstData);
		const UPCGPolyLineData* SecondPolyLineData = CastChecked<const UPCGPolyLineData>(SecondData);

		if (!SpatialBasicsAreIdentical(FirstPolyLineData, SecondPolyLineData))
		{
			return false;
		}

		int NumSegments = FirstPolyLineData->GetNumSegments();
		for (int32 I = 0; I < NumSegments; ++I)
		{
			// TODO: Needs more robust checking for straight line vs spline tangents, etc
			if (FirstPolyLineData->GetSegmentLength(I) != SecondPolyLineData->GetSegmentLength(I) ||
				!FirstPolyLineData->GetTransformAtDistance(I, 0.0).Equals(SecondPolyLineData->GetTransformAtDistance(I, 0.0)))
			{
				return false;
			}
		}

		return true;
	}

	bool PrimitiveDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		const UPCGPrimitiveData* FirstPrimitiveData = CastChecked<const UPCGPrimitiveData>(FirstData);
		const UPCGPrimitiveData* SecondPrimitiveData = CastChecked<const UPCGPrimitiveData>(SecondData);

		if (FirstPrimitiveData->VoxelSize != SecondPrimitiveData->VoxelSize || !SpatialBasicsAreIdentical(FirstPrimitiveData, SecondPrimitiveData))
		{
			return false;
		}

		// TODO: Compare the ToPointData, which will require the Context
		return false;
	}

	bool SampledSpatialDataIsIdentical(const UPCGSpatialData* FirstSpatialData, const UPCGSpatialData* SecondSpatialData)
	{
		if (!SpatialBasicsAreIdentical(FirstSpatialData, SecondSpatialData))
		{
			return false;
		}

		// At this point, bounds has already been checked for equality
		const FBox SampleBounds = Defaults::TestingVolume;
		const FVector SampleExtent = SampleBounds.GetExtent();

		FPCGPoint FirstPoint;
		FPCGPoint SecondPoint;
		FVector StepInterval = SampleExtent * 2.0 / FMath::Max(Defaults::NumSamplingStepsPerDimension, 1);
		FVector StartingOffset = SampleBounds.Min + StepInterval * 0.5;

		// Sample points across the 3D volume
		for (FVector::FReal X = StartingOffset.X; X < SampleBounds.Max.X; X += StepInterval.X)
		{
			for (FVector::FReal Y = StartingOffset.Y; Y < SampleBounds.Max.Y; Y += StepInterval.Y)
			{
				for (FVector::FReal Z = StartingOffset.Z; Z < SampleBounds.Max.Z; Z += StepInterval.Z)
				{
					FTransform PointTransform(FVector(X, Y, Z));
					bool bFirstPointWasSampled = FirstSpatialData->SamplePoint(PointTransform, SampleBounds, FirstPoint, nullptr);
					bool bSecondPointWasSampled = SecondSpatialData->SamplePoint(PointTransform, SampleBounds, SecondPoint, nullptr);

					if (bFirstPointWasSampled != bSecondPointWasSampled)
					{
						return false;
					}

					// Only compare if both points were sampled
					if (bFirstPointWasSampled && bSecondPointWasSampled && !PCGTestsCommon::PointsAreIdentical(FirstPoint, SecondPoint))
					{
						return false;
					}
				}
			}
		}

		return true;
	}

	bool SpatialBasicsAreIdentical(const UPCGSpatialData* FirstSpatialData, const UPCGSpatialData* SecondSpatialData)
	{
		return (FirstSpatialData->GetDataType() == SecondSpatialData->GetDataType() &&
			FirstSpatialData->GetDimension() == SecondSpatialData->GetDimension() &&
			FirstSpatialData->GetBounds() == SecondSpatialData->GetBounds() &&
			FirstSpatialData->GetStrictBounds() == SecondSpatialData->GetStrictBounds());
	}

	bool ComparisonIsUnimplemented(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		return false;
	}

	bool DataTypeIsComparable(EPCGDataType DataType)
	{
		// Comparable data types
		if (!!(DataType & EPCGDataType::Spatial))
		{
			return true;
		}

		// Data types that don't need to be compared
		if (DataType == EPCGDataType::None || DataType == EPCGDataType::Other || DataType == EPCGDataType::Settings)
		{
			return false;
		}

		UE_LOG(LogPCG, Warning, TEXT("Unknown data comparison type: %s"), *UEnum::GetValueAsString(DataType));
		return false;
	}

	bool DataIsComparable(const UPCGData* Data)
	{
		return Data && DataTypeIsComparable(Data->GetDataType());
	}

	bool DataCanBeShuffled(const UPCGData* Data)
	{
		// TODO: Revisit for other data types that can be shuffled
		return Data && Data->IsA<UPCGPointData>();
	}

	void ShuffleInputOrder(FTestData& TestData)
	{
		ShuffleArray<FPCGTaggedData>(TestData.InputData.TaggedData, TestData.RandomStream);
	}

	void ShuffleAllInternalData(FTestData& TestData)
	{
		for (FPCGTaggedData& TaggedData : TestData.InputData.TaggedData)
		{
			if (DataCanBeShuffled(TaggedData.Data))
			{
				UPCGPointData* PointData = CastChecked<UPCGPointData>(TaggedData.Data);
				ShuffleArray<FPCGPoint>(PointData->GetMutablePoints(), TestData.RandomStream);
			}
		}
	}

	TFunction<bool(const UPCGData*, const UPCGData*)> GetCompareFunction(EPCGDataType DataType)
	{
		if (!DataTypeIsComparable(DataType))
		{
			// Should never reach here
			UE_LOG(LogPCG, Warning, TEXT("Attempting to compare incomparable data."));
			return ComparisonIsUnimplemented;
		}

		switch (DataType)
		{
		case EPCGDataType::Spatial:
			return SpatialDataIsIdentical;
		default:
			UE_LOG(LogPCG, Warning, TEXT("Comparable PCG DataType has no 'comparison function'."));
			return ComparisonIsUnimplemented;
		}
	}

	bool ExecutionIsDeterministic(const FTestData& FirstTestData, const FTestData& SecondTestData, const UPCGNode* PCGNode)
	{
		FPCGElementPtr FirstElement = FirstTestData.Settings->GetElement();
		FPCGElementPtr SecondElement = SecondTestData.Settings->GetElement();

		TUniquePtr<FPCGContext> FirstContext(FirstElement->Initialize(FirstTestData.InputData, FirstTestData.TestPCGComponent, PCGNode));
		TUniquePtr<FPCGContext> SecondContext(SecondElement->Initialize(SecondTestData.InputData, SecondTestData.TestPCGComponent, PCGNode));

		FirstContext->NumAvailableTasks = 1;
		SecondContext->NumAvailableTasks = 1;

		// Execute both elements until complete
		while (!FirstElement->Execute(FirstContext.Get()))
		{
		}

		while (!SecondElement->Execute(SecondContext.Get()))
		{
		}

		return DataCollectionsAreIdentical(FirstContext->OutputData, SecondContext->OutputData);
	}

	bool ExecutionIsDeterministicSameData(FTestData& TestData, const UPCGNode* PCGNode)
	{
		return ExecutionIsDeterministic(TestData, TestData, PCGNode);
	}
}

#undef LOCTEXT_NAMESPACE