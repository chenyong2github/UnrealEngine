// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/Determinism/PCGDeterminismTestsCommon.h"

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGPoint.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGPrimitiveData.h"
#include "Data/PCGSurfaceData.h"
#include "Data/PCGVolumeData.h"
#include "Tests/PCGTestsCommon.h"

#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace PCGDeterminismTests
{
	FTestData::FTestData(int32 RandomSeed) :
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

		TestComponent = NewObject<UPCGComponent>(TestActor, FName(TEXT("PCG Test Component")), RF_Transient);
		check(TestComponent);
		TestActor->AddInstanceComponent(TestComponent);
		TestComponent->RegisterComponent();

		UPCGGraph* TestGraph = NewObject<UPCGGraph>(TestComponent, FName(TEXT("PCG Test Graph")), RF_Transient);
		check(TestGraph);
		TestComponent->SetGraph(TestGraph);
#else
		TestActor = nullptr;
		TestComponent = nullptr;
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

	void AddVolumeInputData(FPCGDataCollection& InputData, const FVector& Location, const FVector& HalfSize, const FVector& VoxelSize)
	{
		UPCGVolumeData* VolumeData = PCGTestsCommon::CreateVolumeData(FBox::BuildAABB(Location, HalfSize));
		VolumeData->VoxelSize = VoxelSize;
		InputData.TaggedData.Emplace_GetRef().Data = VolumeData;
	}

	void AddGenericVolumeInputData(FTestData& TestData)
	{
		AddVolumeInputData(TestData.InputData, FVector::ZeroVector, FVector::OneVector * 500.f, FVector::OneVector * 200.f);
	}

	void AddRandomizedVolumeInputData(FTestData& TestData)
	{
		AddVolumeInputData(TestData.InputData, TestData.RandomStream.VRand() * 500.f, FVector::OneVector * 500.f + TestData.RandomStream.VRand() * 200.f, FVector::OneVector * 200.f + TestData.RandomStream.VRand() * 100.f);
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
			if (DataIsComparable(FirstCollection.TaggedData[I].Data->GetDataType()))
			{
				FirstCollectionComparableIndices.Emplace(I);
			}
		}

		// Find comparable data from second collection
		for (int32 I = 0; I < SecondCollection.TaggedData.Num(); ++I)
		{
			// Check if its a type we care about comparing
			if (DataIsComparable(SecondCollection.TaggedData[I].Data->GetDataType()))
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

	bool SpatialDataIsIdentical(const UPCGData* FirstSpatialData, const UPCGData* SecondSpatialData)
	{
		check(FirstSpatialData);
		check(SecondSpatialData);

		if (Cast<UPCGPointData>(FirstSpatialData) != nullptr && Cast<UPCGPointData>(SecondSpatialData) != nullptr)
		{
			return PointDataIsIdentical(FirstSpatialData, SecondSpatialData);
		}
		else if (Cast<UPCGVolumeData>(FirstSpatialData) != nullptr && Cast<UPCGVolumeData>(SecondSpatialData) != nullptr)
		{
			// TODO: Implement Volume Data comparison
			UE_LOG(LogPCG, Warning, TEXT("Volume comparison unimplemented."));
			return ComparisonIsUnimplemented(FirstSpatialData, SecondSpatialData);
		}
		else if (Cast<UPCGSurfaceData>(FirstSpatialData) != nullptr && Cast<UPCGSurfaceData>(SecondSpatialData) != nullptr)
		{
			// TODO: Implement Surface Data comparison
			UE_LOG(LogPCG, Warning, TEXT("Surface comparison unimplemented."));
			return ComparisonIsUnimplemented(FirstSpatialData, SecondSpatialData);
		}
		else if (Cast<UPCGPolyLineData>(FirstSpatialData) != nullptr && Cast<UPCGPolyLineData>(SecondSpatialData) != nullptr)
		{
			// TODO: Implement PolyLine Data comparison
			UE_LOG(LogPCG, Warning, TEXT("PolyLine comparison unimplemented."));
			return ComparisonIsUnimplemented(FirstSpatialData, SecondSpatialData);
		}
		else if (Cast<UPCGPrimitiveData>(FirstSpatialData) != nullptr && Cast<UPCGPrimitiveData>(SecondSpatialData) != nullptr)
		{
			// TODO: Implement Primitive Data comparison
			UE_LOG(LogPCG, Warning, TEXT("Primitive comparison unimplemented."));
			return ComparisonIsUnimplemented(FirstSpatialData, SecondSpatialData);
		}

		return false;
	}

	bool PointDataIsIdentical(const UPCGData* FirstData, const UPCGData* SecondData)
	{
		const UPCGPointData* FirstPointData = Cast<const UPCGPointData>(FirstData);
		const UPCGPointData* SecondPointData = Cast<const UPCGPointData>(SecondData);
		check(FirstPointData);
		check(SecondPointData);

		const TArray<FPCGPoint>& FirstPoints = Cast<const UPCGPointData>(FirstPointData)->GetPoints();
		const TArray<FPCGPoint>& SecondPoints = Cast<const UPCGPointData>(SecondPointData)->GetPoints();

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

	bool ComparisonIsUnimplemented(const UPCGData* FirstPointData, const UPCGData* SecondPointData)
	{
		return false;
	}

	bool DataIsComparable(EPCGDataType DataType)
	{
		// Comparable data types
		if (DataType == EPCGDataType::Spatial)
		{
			return true;
		}

		// TODO: Data types that don't need to be compared
		if (DataType == EPCGDataType::None || DataType == EPCGDataType::Other || DataType == EPCGDataType::Settings)
		{
			return false;
		}

		UE_LOG(LogPCG, Warning, TEXT("Unknown data comparison type: %s"), *UEnum::GetValueAsString(DataType));
		return false;
	}

	void ShuffleInputOrder(FTestData& TestData)
	{
		const int32 LastIndex = TestData.InputData.TaggedData.Num() - 1;
		for (int32 I = 0; I <= LastIndex; ++I)
		{
			int32 Index = TestData.RandomStream.RandRange(I, LastIndex);

			if (I != Index)
			{
				TestData.InputData.TaggedData.Swap(I, Index);
			}
		}
	}

	TFunction<bool(const UPCGData*, const UPCGData*)> GetCompareFunction(EPCGDataType DataType)
	{
		if (!DataIsComparable(DataType))
		{
			// Should never reach here
			UE_LOG(LogPCG, Warning, TEXT("Attempting to compare incomparable data."));
			return ComparisonIsUnimplemented;
		}

		switch (DataType)
		{
			// TODO: Many more data type comparisons to be added
		case EPCGDataType::Spatial:
			return SpatialDataIsIdentical;
		default:
			UE_LOG(LogPCG, Warning, TEXT("Comparable PCG DataType has no 'comparison function'."));
			return ComparisonIsUnimplemented;
		}
	}

	bool ExecutionIsDeterministic(FTestData& FirstTestData, FTestData& SecondTestData)
	{
		FPCGElementPtr FirstElement = FirstTestData.Settings->GetElement();
		FPCGElementPtr SecondElement = SecondTestData.Settings->GetElement();

		TUniquePtr<FPCGContext> FirstContext(FirstElement->Initialize(FirstTestData.InputData, FirstTestData.TestComponent, nullptr));
		TUniquePtr<FPCGContext> SecondContext(SecondElement->Initialize(SecondTestData.InputData, SecondTestData.TestComponent, nullptr));

		FirstContext->NumAvailableTasks = 1;
		SecondContext->NumAvailableTasks = 1;

		// Execute both elements until complete
		while (!FirstElement->Execute(FirstContext.Get()))
		{
		}

		while (!SecondElement->Execute(SecondContext.Get()))
		{
		}

		return PCGDeterminismTests::DataCollectionsAreIdentical(FirstContext->OutputData, SecondContext->OutputData);
	}
}