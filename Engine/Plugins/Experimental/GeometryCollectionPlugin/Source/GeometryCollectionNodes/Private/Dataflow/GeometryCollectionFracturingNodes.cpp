// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionFracturingNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "Logging/LogMacros.h"
#include "Templates/SharedPointer.h"
#include "UObject/UnrealTypePrivate.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshEditor.h"
#include "Operations/MeshBoolean.h"

#include "EngineGlobals.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "FractureEngineClustering.h"
#include "FractureEngineSelection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionFracturingNodes)

namespace Dataflow
{

	void GeometryCollectionFracturingNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformScatterPointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadialScatterPointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVoronoiFractureDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPlaneCutterDataflowNode);

		// GeometryCollection|Fracture
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("GeometryCollection|Fracture", FLinearColor(1.f, 1.f, 0.8f), CDefaultNodeBodyTintColor);
	}
}

void FUniformScatterPointsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FBox& BBox = GetValue<FBox>(Context, &BoundingBox);
		if (BBox.GetVolume() > 0.f)
		{
			FRandomStream RandStream(GetValue<float>(Context, &RandomSeed));

			const FVector Extent(BBox.Max - BBox.Min);
			const int32 NumPoints = RandStream.RandRange(GetValue<int32>(Context, &MinNumberOfPoints), GetValue<int32>(Context, &MaxNumberOfPoints));

			TArray<FVector> PointsArr;
			PointsArr.Reserve(NumPoints);
			for (int32 Idx = 0; Idx < NumPoints; ++Idx)
			{
				PointsArr.Emplace(BBox.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
			}

			SetValue<TArray<FVector>>(Context, PointsArr, &Points);
		}
		else
		{
			// ERROR: Invalid BoundingBox input
			SetValue<TArray<FVector>>(Context, TArray<FVector>(), &Points);
		}
	}
}

void FRadialScatterPointsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FVector::FReal RadialStep = GetValue<float>(Context, &Radius) / GetValue<int32>(Context, &RadialSteps);
		const FVector::FReal AngularStep = 2 * PI / GetValue<int32>(Context, &AngularSteps);

		FRandomStream RandStream(GetValue<float>(Context, &RandomSeed));
		FVector UpVector(GetValue<FVector>(Context, &Normal));
		UpVector.Normalize();
		FVector BasisX, BasisY;
		UpVector.FindBestAxisVectors(BasisX, BasisY);

		TArray<FVector> PointsArr;

		FVector::FReal Len = RadialStep * .5;
		for (int32 ii = 0; ii < GetValue<int32>(Context, &RadialSteps); ++ii, Len += RadialStep)
		{
			FVector::FReal Angle = FMath::DegreesToRadians(GetValue<float>(Context, &AngleOffset));
			for (int32 kk = 0; kk < AngularSteps; ++kk, Angle += AngularStep)
			{
				FVector RotatingOffset = Len * (FMath::Cos(Angle) * BasisX + FMath::Sin(Angle) * BasisY);
				PointsArr.Emplace(GetValue<FVector>(Context, &Center) + RotatingOffset + (RandStream.VRand() * RandStream.FRand() * Variability));
			}
		}

		SetValue<TArray<FVector>>(Context, PointsArr, &Points);
	}
}


static float GetMaxVertexMovement(float Grout, float Amplitude, int OctaveNumber, float Persistence)
{
	float MaxDisp = Grout;
	float AmplitudeScaled = Amplitude;
	for (int32 OctaveIdx = 0; OctaveIdx < OctaveNumber; OctaveIdx++, AmplitudeScaled *= Persistence)
	{
		MaxDisp += FMath::Abs(AmplitudeScaled);
	}
	return MaxDisp;
}

void FVoronoiFractureDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			const TArray<FVector>& Sites = GetValue<TArray<FVector>>(Context, &Points);
			if (Sites.Num() > 0)
			{
				//
				// Compute BoundingBox for ManagedArrayIn
				//
				FBox BoundingBox(ForceInit);

				if (InCollection.HasAttribute("Transform", FGeometryCollection::TransformGroup) &&
					InCollection.HasAttribute("Parent", FGeometryCollection::TransformGroup) &&
					InCollection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup) &&
					InCollection.HasAttribute("BoundingBox", FGeometryCollection::GeometryGroup))
				{
					const TManagedArray<FTransform>& Transforms = InCollection.GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
					const TManagedArray<int32>& ParentIndices = InCollection.GetAttribute<int32>("Parent", FGeometryCollection::TransformGroup);
					const TManagedArray<int32>& TransformIndices = InCollection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);
					const TManagedArray<FBox>& BoundingBoxes = InCollection.GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);

					TArray<FMatrix> TmpGlobalMatrices;
					GeometryCollectionAlgo::GlobalMatrices(Transforms, ParentIndices, TmpGlobalMatrices);

					if (TmpGlobalMatrices.Num() > 0)
					{
						for (int32 BoxIdx = 0; BoxIdx < BoundingBoxes.Num(); ++BoxIdx)
						{
							const int32 TransformIndex = TransformIndices[BoxIdx];
							BoundingBox += BoundingBoxes[BoxIdx].TransformBy(TmpGlobalMatrices[TransformIndex]);
						}
					}

					//
					// Compute Voronoi Bounds
					//
					FBox VoronoiBounds = BoundingBox;
					VoronoiBounds += FBox(Sites);

					float GroutVal = GetValue<float>(Context, &Grout);
					float AmplitudeVal = GetValue<float>(Context, &Amplitude);
					int32 OctaveNumberVal = GetValue<int32>(Context, &OctaveNumber);
					float PersistenceVal = GetValue<float>(Context, &Persistence);

					VoronoiBounds = VoronoiBounds.ExpandBy(GetMaxVertexMovement(GroutVal, AmplitudeVal, OctaveNumberVal, PersistenceVal) + KINDA_SMALL_NUMBER);

					//
					// Voronoi Fracture
					//
					FNoiseSettings NoiseSettings;
					NoiseSettings.Amplitude = AmplitudeVal;
					NoiseSettings.Frequency = GetValue<float>(Context, &Frequency);
					NoiseSettings.Octaves = OctaveNumberVal;
					NoiseSettings.PointSpacing = GetValue<float>(Context, &PointSpacing);
					NoiseSettings.Lacunarity = GetValue<float>(Context, &Lacunarity);
					NoiseSettings.Persistence = GetValue<float>(Context, &Persistence);;

					FVoronoiDiagram Voronoi(Sites, VoronoiBounds, .1f);

					FPlanarCells VoronoiPlanarCells = FPlanarCells(Sites, Voronoi);
					VoronoiPlanarCells.InternalSurfaceMaterials.NoiseSettings = NoiseSettings;

					const TArrayView<const int32>& TransformIndicesArray(TransformIndices.GetConstArray());

					float CollisionSampleSpacingVal = GetValue<float>(Context, &CollisionSampleSpacing);
					float RandomSeedVal = GetValue<float>(Context, &RandomSeed);

					int ResultGeometryIndex = CutMultipleWithPlanarCells(VoronoiPlanarCells, *GeomCollection, TransformIndicesArray, GroutVal, CollisionSampleSpacingVal, RandomSeedVal, FTransform().Identity);

					SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
				}
			}
		}
	}
}


void FPlaneCutterDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			TArray<FPlane> CuttingPlanes;
			TArray<FTransform> CuttingPlaneTransforms;

			float RandomSeedVal = GetValue<float>(Context, &RandomSeed);
			FRandomStream RandStream(RandomSeedVal);

			FBox Bounds = GetValue<FBox>(Context, &BoundingBox);
			const FVector Extent(Bounds.Max - Bounds.Min);

			CuttingPlaneTransforms.Reserve(CuttingPlaneTransforms.Num() + NumPlanes);
			for (int32 ii = 0; ii < NumPlanes; ++ii)
			{
				FVector Position(Bounds.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
				CuttingPlaneTransforms.Emplace(FTransform(FRotator(RandStream.FRand() * 360.0f, RandStream.FRand() * 360.0f, 0.0f), Position));
			}

			for (const FTransform& Transform : CuttingPlaneTransforms)
			{
				CuttingPlanes.Add(FPlane(Transform.GetLocation(), Transform.GetUnitAxis(EAxis::Z)));
			}

			FInternalSurfaceMaterials InternalSurfaceMaterials;
			FNoiseSettings NoiseSettings;

			float AmplitudeVal = GetValue<float>(Context, &Amplitude);
			if (AmplitudeVal > 0.0f)
			{
				NoiseSettings.Amplitude = AmplitudeVal;
				NoiseSettings.Frequency = GetValue<float>(Context, &Frequency);
				NoiseSettings.Lacunarity = GetValue<float>(Context, &Lacunarity);
				NoiseSettings.Persistence = GetValue<float>(Context, &Persistence);
				NoiseSettings.Octaves = GetValue<int32>(Context, &OctaveNumber);
				NoiseSettings.PointSpacing = GetValue<float>(Context, &PointSpacing);

				InternalSurfaceMaterials.NoiseSettings = NoiseSettings;
			}

			if (GeomCollection->HasAttribute("TransformIndex", "Geometry"))
			{
				const TManagedArray<int32>& TransformIndices = GeomCollection->GetAttribute<int32>("TransformIndex", "Geometry");
				const TArrayView<const int32>& TransformIndicesArray(TransformIndices.GetConstArray());

				float CollisionSampleSpacingVal = GetValue<float>(Context, &CollisionSampleSpacing);
				float GroutVal = GetValue<float>(Context, &Grout);

				int ResultGeometryIndex = CutMultipleWithMultiplePlanes(CuttingPlanes, InternalSurfaceMaterials, *GeomCollection, TransformIndicesArray, GroutVal, CollisionSampleSpacingVal, RandomSeedVal, FTransform().Identity);

				SetValue<FManagedArrayCollection>(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
			}
		}
	}
}



