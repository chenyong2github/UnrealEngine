// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryProcessing/CombineMeshInstancesImpl.h"

#include "Async/ParallelFor.h"
#include "Tasks/Task.h"

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "DynamicMeshEditor.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"

#include "ShapeApproximation/ShapeDetection3.h"
#include "ShapeApproximation/MeshSimpleShapeApproximation.h"
#include "Generators/GridBoxMeshGenerator.h"

#include "MeshSimplification.h"
#include "DynamicMesh/ColliderMesh.h"
#include "MeshConstraintsUtil.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Operations/RemoveOccludedTriangles.h"

#include "Physics/CollisionGeometryConversion.h"
#include "Physics/PhysicsDataCollection.h"

#include "TransformSequence.h"
#include "Sampling/SphericalFibonacci.h"
#include "Util/IteratorUtil.h"

#include "Implicit/Morphology.h"
#include "ProjectionTargets.h"


using namespace UE::Geometry;



static TAutoConsoleVariable<int> CVarGeometryCombineMeshInstancesRemoveHidden(
	TEXT("geometry.CombineInstances.DebugRemoveHiddenStrategy"),
	1,
	TEXT("Configure hidden-removal strategy via (temporary debug)"));


enum class EMeshDetailLevel
{
	Base = 0,
	Standard = 1,
	Small = 2,
	Decorative = 3
};


struct FMeshInstance
{
	UE::Geometry::FTransformSequence3d WorldTransform;
	TArray<UMaterialInterface*> Materials;

	UPrimitiveComponent* SourceComponent;
	int32 SourceInstanceIndex;

	EMeshDetailLevel DetailLevel = EMeshDetailLevel::Standard;

	// allow FMeshInstance to maintain link to external representation of instance
	FIndex3i ExternalInstanceIndex = FIndex3i::Invalid();
};


struct FMeshInstanceSet
{
	UStaticMesh* SourceAsset;
	TArray<FMeshInstance> Instances;
};


struct FSourceGeometry
{
	TArray<UE::Geometry::FDynamicMesh3> SourceMeshLODs;
	UE::Geometry::FSimpleShapeSet3d CollisionShapes;
};

struct FOptimizedGeometry
{
	TArray<UE::Geometry::FDynamicMesh3> SimplifiedMeshLODs;

	TArray<UE::Geometry::FDynamicMesh3> ApproximateMeshLODs;

	//UE::Geometry::FSimpleShapeSet3d CollisionShapes;
};


class FMeshInstanceAssembly
{
public:
	// this is necessary due to TArray<TUniquePtr> below
	FMeshInstanceAssembly() = default;
	FMeshInstanceAssembly(FMeshInstanceAssembly&) = delete;
	FMeshInstanceAssembly& operator=(const FMeshInstanceAssembly&) = delete;


	TArray<TUniquePtr<FMeshInstanceSet>> InstanceSets;

	TArray<UMaterialInterface*> UniqueMaterials;
	TMap<UMaterialInterface*, int32> MaterialMap;

	TArray<FSourceGeometry> SourceMeshGeometry;
	TArray<FOptimizedGeometry> OptimizedMeshGeometry;

	TArray<FDynamicMeshAABBTree3> SourceMeshSpatials;

	// allow external code to preprocess dynamic mesh for a specific instance
	TFunction<void(FDynamicMesh3&, const FMeshInstance&)> PreProcessInstanceMeshFunc;
};






void InitializeMeshInstanceAssembly(
	const IGeometryProcessing_CombineMeshInstances::FInstanceSet& SourceInstanceSet,
	FMeshInstanceAssembly& AssemblyOut)
{
	TMap<UStaticMesh*, FMeshInstanceSet*> MeshToInstanceMap;

	int32 NumInstances = SourceInstanceSet.StaticMeshInstances.Num();
	for ( int32 Index = 0; Index < NumInstances; ++Index)
	{
		const IGeometryProcessing_CombineMeshInstances::FStaticMeshInstance& SourceMeshInstance = SourceInstanceSet.StaticMeshInstances[Index];

		UStaticMesh* StaticMesh = SourceMeshInstance.SourceMesh;
		FMeshInstanceSet** FoundInstanceSet = MeshToInstanceMap.Find(StaticMesh);
		if (FoundInstanceSet == nullptr)
		{
			TUniquePtr<FMeshInstanceSet> NewInstanceSet = MakeUnique<FMeshInstanceSet>();
			NewInstanceSet->SourceAsset = StaticMesh;
			FMeshInstanceSet* Ptr = NewInstanceSet.Get();
					
			AssemblyOut.InstanceSets.Add(MoveTemp(NewInstanceSet));
			// store source model?

			MeshToInstanceMap.Add(StaticMesh, Ptr);
			FoundInstanceSet = &Ptr;
		}

		FMeshInstance NewInstance;
		NewInstance.ExternalInstanceIndex = FIndex3i(Index, -1,-1);

		if ( SourceMeshInstance.GroupDataIndex >= 0 && SourceMeshInstance.GroupDataIndex < SourceInstanceSet.InstanceGroupDatas.Num() )
		{
			const IGeometryProcessing_CombineMeshInstances::FMeshInstanceGroupData& GroupData = 
				SourceInstanceSet.InstanceGroupDatas[SourceMeshInstance.GroupDataIndex];
			NewInstance.Materials = GroupData.MaterialSet;
		}

		NewInstance.SourceComponent = SourceMeshInstance.SourceComponent;
		NewInstance.SourceInstanceIndex = SourceMeshInstance.SourceInstanceIndex;
		NewInstance.DetailLevel = static_cast<EMeshDetailLevel>( static_cast<int32>(SourceMeshInstance.DetailLevel) );
		for ( FTransform3d Transform : SourceMeshInstance.TransformSequence )
		{
			NewInstance.WorldTransform.Append( Transform );
		}
		(*FoundInstanceSet)->Instances.Add(NewInstance);
	}


	// collect unique materials
	for (TPair<UStaticMesh*, FMeshInstanceSet*>& Pair : MeshToInstanceMap)
	{
		UStaticMesh* StaticMesh = Pair.Key;
		FMeshInstanceSet& InstanceSet = *(Pair.Value);

		for (FMeshInstance& Instance : InstanceSet.Instances)
		{
			for (UMaterialInterface* Material : Instance.Materials)
			{
				if ( AssemblyOut.MaterialMap.Contains(Material) == false)
				{
					int32 NewIndex = AssemblyOut.UniqueMaterials.Num();
					AssemblyOut.UniqueMaterials.Add(Material);
					AssemblyOut.MaterialMap.Add(Material, NewIndex);
				}
			}
		}
	}

}


void InitializeAssemblySourceMeshesFromLOD(
	FMeshInstanceAssembly& Assembly,
	int32 SourceAssetBaseLOD,
	int32 NumSourceLODs)
{
	using namespace UE::Geometry;

	check(NumSourceLODs > 0);

	int32 NumSets = Assembly.InstanceSets.Num();
	Assembly.SourceMeshGeometry.SetNum(NumSets);

	// collect mesh for each assembly item
	ParallelFor(NumSets, [&](int32 Index)
	{
		TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];
		Target.SourceMeshLODs.SetNum(NumSourceLODs);

		UStaticMesh* StaticMesh = InstanceSet->SourceAsset;

		for (int32 k = 0; k < NumSourceLODs; ++k)
		{
			int32 LODIndex = SourceAssetBaseLOD + k;
			if (LODIndex < StaticMesh->GetNumSourceModels())
			{
				FMeshDescription* UseMeshDescription = StaticMesh->GetMeshDescription(LODIndex);
				if (UseMeshDescription != nullptr)
				{
					FMeshDescriptionToDynamicMesh Converter;
					Converter.bEnableOutputGroups = true; 
					Converter.bTransformVertexColorsLinearToSRGB = true;
					Converter.Convert(UseMeshDescription, Target.SourceMeshLODs[k]);
				}
			}
		}

		// if first LOD is missing try getting LOD0 again
		if (Target.SourceMeshLODs[0].TriangleCount() == 0)
		{
			if (FMeshDescription* UseMeshDescription = StaticMesh->GetMeshDescription(0))
			{
				FMeshDescriptionToDynamicMesh Converter;
				Converter.bEnableOutputGroups = true; 
				Converter.bTransformVertexColorsLinearToSRGB = true;
				Converter.Convert(UseMeshDescription, Target.SourceMeshLODs[0]);
			}
		}

		// now if first LOD is missing, just fall back to a box
		if (Target.SourceMeshLODs[0].TriangleCount() == 0)
		{
			FGridBoxMeshGenerator BoxGen;
			Target.SourceMeshLODs[0].Copy(&BoxGen.Generate());
		}

		// now make sure every one of our Source LODs has a mesh by copying from N-1
		for (int32 k = 1; k < NumSourceLODs; ++k)
		{
			if (Target.SourceMeshLODs[k].TriangleCount() == 0)
			{
				Target.SourceMeshLODs[k] = Target.SourceMeshLODs[k-1];
			}
		}

	});


	// not clear that it is safe to do this in parallel...
	for (int32 Index = 0; Index < NumSets; ++Index)
	{
		TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];

		UStaticMesh* StaticMesh = InstanceSet->SourceAsset;
		UBodySetup* BodySetup = StaticMesh->GetBodySetup();
		if (BodySetup)
		{
			UE::Geometry::GetShapeSet(BodySetup->AggGeom, Target.CollisionShapes);
			// todo: detect boxes?
		}
	}



	//ParallelFor(NumSets, [&](int32 Index)
	//{
	//	TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[Index];
	//	UStaticMesh* StaticMesh = InstanceSet->SourceAsset;

	//	FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];

	//	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	//	if (BodySetup)
	//	{
	//		UE::Geometry::GetShapeSet(BodySetup->AggGeom, Target.CollisionShapes);
	//	}

	//	// todo:
	//	//if (bDetectBoxes)
	//	//{
	//	//	DetectBoxesFromConvexes(CollisionShapes);
	//	//}

	//});
}










/**
 * @return ( Sqrt(Sum-of-squared-distances) / NumPoints , Max(distance)  )
 * 
 */
static FVector2d DeviationMetric(const FDynamicMesh3& MeasureMesh, const FDynamicMeshAABBTree3& SourceBVH)
{
	// todo: could consider normal deviation?
	int PointCount = 0;
	double SumDistanceSqr = 0;
	double MaxDistanceSqr = 0;
	auto TestPointFunc = [&SumDistanceSqr, &MaxDistanceSqr, &PointCount, &SourceBVH](FVector3d Point)
	{
		double NearDistSqr;
		SourceBVH.FindNearestTriangle(Point, NearDistSqr);
		if (NearDistSqr > MaxDistanceSqr)
		{
			MaxDistanceSqr = NearDistSqr;
		}
		SumDistanceSqr += NearDistSqr;
		PointCount++;
	};

	for (int32 vid : MeasureMesh.VertexIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetVertex(vid));
	}

	for (int32 tid : MeasureMesh.TriangleIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetTriCentroid(tid));
	}

	for (int32 eid : MeasureMesh.EdgeIndicesItr())
	{
		TestPointFunc(MeasureMesh.GetEdgePoint(eid, 0.5));
	}

	return FVector2d(
		FMathd::Sqrt(SumDistanceSqr) / (double)PointCount,
		FMathd::Sqrt(MaxDistanceSqr) );
}



class FPartApproxSelector
{
public:
	double TriangleCost = 0.7;

	struct FResultOption
	{
		FVector2d DeviationMetric;
		double CostMetric;
		TSharedPtr<FDynamicMesh3> Mesh;
		int32 MethodID;
	};
	TArray<FResultOption> Options;

	const FDynamicMesh3* SourceMesh;
	const FDynamicMeshAABBTree3* Spatial;

	void Initialize(const FDynamicMesh3* SourceMeshIn, const FDynamicMeshAABBTree3* SpatialIn)
	{
		SourceMesh = SourceMeshIn;
		Spatial = SpatialIn;
	}

	void AddGeneratedMesh(
		const FDynamicMesh3& ExternalMesh,
		int32 MethodID )
	{
		FResultOption Option;
		Option.MethodID = MethodID;
		Option.Mesh = MakeShared<FDynamicMesh3>(ExternalMesh);
		ComputeMetric(Option);
		Options.Add(Option);
	}

	void AddGeneratedMesh(
		TFunctionRef<void(FDynamicMesh3&)> GeneratorFunc,
		int32 MethodID )
	{
		FResultOption Option;
		Option.MethodID = MethodID;
		Option.Mesh = MakeShared<FDynamicMesh3>(*SourceMesh);
		GeneratorFunc(*Option.Mesh);
		ComputeMetric(Option);
		Options.Add(Option);
	}

	void ComputeMetric(FResultOption& Option)
	{
		Option.DeviationMetric = DeviationMetric(*Option.Mesh, *Spatial);
		int32 TriCount = Option.Mesh->TriangleCount();
		int32 BaseTriCount = 12;		// 2 tris for each face of box
		Option.CostMetric = 
			Option.DeviationMetric[0] * FMathd::Pow( (double)TriCount / (double)BaseTriCount, TriangleCost );
	}

	void SelectBestOption(
		FDynamicMesh3& ResultMesh)
	{
		Options.StableSort( [&](const FResultOption& A, const FResultOption& B) { return A.CostMetric < B.CostMetric; } );
		ResultMesh = MoveTemp(*Options[0].Mesh);
	}
};










void InitializeInstanceAssemblySpatials(FMeshInstanceAssembly& Assembly)
{
	int32 NumSets = Assembly.InstanceSets.Num();
	Assembly.SourceMeshSpatials.SetNum(NumSets);
	
	ParallelFor(NumSets, [&](int32 Index)
	{
		TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];
		FDynamicMeshAABBTree3& Spatial = Assembly.SourceMeshSpatials[Index];
		Spatial.SetMesh(&Target.SourceMeshLODs[0], true);
	});
}



/**
 * Simplification can make a mess on low-poly shapes and sometimes just using a simple
 * approximation would be better, use our metric to make this decision.
 * (todo: this could maybe be folded into simplified-mesh computations...)
 */
void ReplaceBadSimplifiedLODs(FMeshInstanceAssembly& Assembly)
{
	int32 NumSets = Assembly.InstanceSets.Num();
	
	ParallelFor(NumSets, [&](int32 Index)
	{
		TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[Index];
		FSourceGeometry& Target = Assembly.SourceMeshGeometry[Index];
		FDynamicMeshAABBTree3& Spatial = Assembly.SourceMeshSpatials[Index];
		FOptimizedGeometry& OptimizedTargets = Assembly.OptimizedMeshGeometry[Index];

		for ( int32 k = OptimizedTargets.SimplifiedMeshLODs.Num()-1; k >= 0; --k )
		{
			FPartApproxSelector Selector;
			Selector.Initialize(Spatial.GetMesh(), &Spatial);
			if ( k == OptimizedTargets.SimplifiedMeshLODs.Num()-1 )
			{
				Selector.AddGeneratedMesh(OptimizedTargets.ApproximateMeshLODs[0], 2);
			}
			else
			{
				Selector.AddGeneratedMesh(OptimizedTargets.SimplifiedMeshLODs[k+1], 1);
			}
			Selector.AddGeneratedMesh(OptimizedTargets.SimplifiedMeshLODs[k], 0);

			// either keep current mesh or replace w/ simplified version
			Selector.SelectBestOption(OptimizedTargets.SimplifiedMeshLODs[k]);
		}
	});
}



static void SimplifyPartMesh(
	FDynamicMesh3& EditMesh, 
	double Tolerance, 
	double RecomputeNormalsAngleThreshold)
{
	// weld edges in case input was unwelded...
	FMergeCoincidentMeshEdges Welder(&EditMesh);
	Welder.MergeVertexTolerance = Tolerance * 0.001;
	Welder.OnlyUniquePairs = false;
	Welder.Apply();

	// Skip out for very low-poly parts, they are unlikely to simplify very nicely.
	if (EditMesh.VertexCount() < 16)
	{
		return;
	}

	FVolPresMeshSimplification Simplifier(&EditMesh);

	// clear out attributes so it doesn't affect simplification
	//EditMesh.DiscardAttributes();
	EditMesh.Attributes()->SetNumUVLayers(0);
	EditMesh.Attributes()->DisableTangents();
	EditMesh.Attributes()->DisablePrimaryColors();
	FMeshNormals::InitializeOverlayToPerVertexNormals(EditMesh.Attributes()->PrimaryNormals(), false);

	Simplifier.ProjectionMode = FVolPresMeshSimplification::ETargetProjectionMode::NoProjection;

	FColliderMesh ColliderMesh;
	ColliderMesh.Initialize(EditMesh);
	FColliderMeshProjectionTarget ProjectionTarget(&ColliderMesh);
	Simplifier.SetProjectionTarget(&ProjectionTarget);

	Simplifier.DEBUG_CHECK_LEVEL = 0;
	Simplifier.bRetainQuadricMemory = false; 
	// currenly no need for this path, may need to resurrect it in the future
	//if ( bNoSplitAttributes == false )
	//{
	//	Simplifier.bAllowSeamCollapse = true;
	//	Simplifier.SetEdgeFlipTolerance(1.e-5);
	//	if (EditMesh.HasAttributes())
	//	{
	//		EditMesh.Attributes()->SplitAllBowties();	// eliminate any bowties that might have formed on attribute seams.
	//	}
	//}

	// this should preserve part shape better but it completely fails currently =\
	//Simplifier.CollapseMode = FVolPresMeshSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;

	// do these flags matter here since we are not flipping??
	EEdgeRefineFlags MeshBoundaryConstraints = EEdgeRefineFlags::NoFlip;
	EEdgeRefineFlags GroupBorderConstraints = EEdgeRefineFlags::NoConstraint;
	EEdgeRefineFlags MaterialBorderConstraints = EEdgeRefineFlags::NoConstraint;

	FMeshConstraints Constraints;
	FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, EditMesh,
		MeshBoundaryConstraints, GroupBorderConstraints, MaterialBorderConstraints, true, false, true);
	Simplifier.SetExternalConstraints(MoveTemp(Constraints));

	Simplifier.GeometricErrorConstraint = FVolPresMeshSimplification::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
	Simplifier.GeometricErrorTolerance = Tolerance;

	Simplifier.SimplifyToTriangleCount( 1 );

	// compact result
	EditMesh.CompactInPlace();

	// recompute normals
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&EditMesh, EditMesh.Attributes()->PrimaryNormals(), RecomputeNormalsAngleThreshold);
	FMeshNormals::QuickRecomputeOverlayNormals(EditMesh);
}




static void ComputeBoxApproximation(
	const FDynamicMesh3& SourceMesh,
	FDynamicMesh3& OutputMesh )
{
	FMeshSimpleShapeApproximation ShapeApprox;
	ShapeApprox.InitializeSourceMeshes( {&SourceMesh} );
	ShapeApprox.bDetectBoxes = ShapeApprox.bDetectCapsules = ShapeApprox.bDetectConvexes = ShapeApprox.bDetectSpheres = false;

	FSimpleShapeSet3d ResultBoxes;
	ShapeApprox.Generate_OrientedBoxes(ResultBoxes);
	UE::Geometry::FOrientedBox3d OrientedBox = ResultBoxes.Boxes[0].Box;

	// oriented box fitting is under-determined, in cases where the AABB and the OBB have the nearly the
	// same volume, generally we prefer an AABB
	// (note: this rarely works due to tessellation of (eg) circles/spheres, and should be replaced w/ a better heuristic)
	FAxisAlignedBox3d AlignedBox = SourceMesh.GetBounds(false);
	if (AlignedBox.Volume() < 1.05*OrientedBox.Volume())
	{
		OrientedBox = UE::Geometry::FOrientedBox3d(AlignedBox);
	}

	FGridBoxMeshGenerator BoxGen;
	BoxGen.Box = OrientedBox;
	BoxGen.EdgeVertices = {0,0,0};
	OutputMesh.Copy(&BoxGen.Generate());
}



enum class EApproximatePartMethod : uint8
{
	OrientedBox = 0,
	MinVolumeSweptHull = 1,
	ConvexHull = 3,
	MinTriCountHull = 4,
	FlattendExtrusion = 5,

	AutoBestFit = 10,

	Original = 100

};



static void ComputeSimplePartApproximation(
	const FDynamicMesh3& SourcePartMesh, 
	FDynamicMesh3& DestMesh,
	EApproximatePartMethod ApproxMethod)
{

	if (ApproxMethod == EApproximatePartMethod::OrientedBox)
	{
		ComputeBoxApproximation(SourcePartMesh, DestMesh);
	}

	FMeshSimpleShapeApproximation ShapeApprox;
	ShapeApprox.InitializeSourceMeshes( {&SourcePartMesh} );
	ShapeApprox.bDetectBoxes = ShapeApprox.bDetectCapsules = ShapeApprox.bDetectConvexes = ShapeApprox.bDetectSpheres = false;

	FDynamicMesh3 ResultMesh;

	FDynamicMesh3 ConvexMesh;
	if ( ApproxMethod == EApproximatePartMethod::ConvexHull || ApproxMethod == EApproximatePartMethod::MinTriCountHull )
	{
		FSimpleShapeSet3d ResultConvex;
		ShapeApprox.Generate_ConvexHulls(ResultConvex);
		ConvexMesh = (ResultConvex.Convexes.Num() > 0) ? MoveTemp(ResultConvex.Convexes[0].Mesh) : FDynamicMesh3();
	}

	FDynamicMesh3 MinVolumeHull;
	if ( ApproxMethod != EApproximatePartMethod::ConvexHull )
	{
		FSimpleShapeSet3d ResultX, ResultY, ResultZ;
		ShapeApprox.Generate_ProjectedHulls(ResultX, FMeshSimpleShapeApproximation::EProjectedHullAxisMode::X);
		ShapeApprox.Generate_ProjectedHulls(ResultY, FMeshSimpleShapeApproximation::EProjectedHullAxisMode::Y);
		ShapeApprox.Generate_ProjectedHulls(ResultZ, FMeshSimpleShapeApproximation::EProjectedHullAxisMode::Z);
		FDynamicMesh3 SweptHullX = (ResultX.Convexes.Num() > 0) ? MoveTemp(ResultX.Convexes[0].Mesh) : FDynamicMesh3();
		double VolumeX = (SweptHullX.TriangleCount() > 0) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(SweptHullX)[0] : TNumericLimits<double>::Max();
		FDynamicMesh3 SweptHullY = (ResultY.Convexes.Num() > 0) ? MoveTemp(ResultY.Convexes[0].Mesh) : FDynamicMesh3();
		double VolumeY = (SweptHullY.TriangleCount() > 0) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(SweptHullY)[0] : TNumericLimits<double>::Max();
		FDynamicMesh3 SweptHullZ = (ResultZ.Convexes.Num() > 0) ? MoveTemp(ResultZ.Convexes[0].Mesh) : FDynamicMesh3();
		double VolumeZ = (SweptHullZ.TriangleCount() > 0) ? TMeshQueries<FDynamicMesh3>::GetVolumeArea(SweptHullZ)[0] : TNumericLimits<double>::Max();

		int Idx = MinElementIndex(FVector(VolumeX, VolumeY, VolumeZ));
		MinVolumeHull = (Idx == 0) ? SweptHullX : (Idx == 1) ? SweptHullY : SweptHullZ;
	}

	if (ApproxMethod == EApproximatePartMethod::ConvexHull)
	{
		ResultMesh = (ConvexMesh.TriangleCount() > 0) ?  MoveTemp(ConvexMesh) : SourcePartMesh;
	}
	else if (ApproxMethod == EApproximatePartMethod::MinVolumeSweptHull)
	{
		ResultMesh = (MinVolumeHull.TriangleCount() > 0) ?  MoveTemp(MinVolumeHull) : SourcePartMesh;
	}
	else if (ApproxMethod == EApproximatePartMethod::MinTriCountHull)
	{
		ResultMesh = (MinVolumeHull.TriangleCount() < ConvexMesh.TriangleCount()) ? 
			MoveTemp(MinVolumeHull) : MoveTemp(ConvexMesh);
	}

	DestMesh = (ResultMesh.TriangleCount() > 0) ? MoveTemp(ResultMesh) : SourcePartMesh;
}


static void SelectBestFittingMeshApproximation(
	const FDynamicMesh3& OriginalMesh, 
	const FDynamicMeshAABBTree3& OriginalMeshSpatial,
	FDynamicMesh3& ResultMesh,
	double AcceptableDeviationTol,
	double TriangleCost)
{
	FPartApproxSelector ApproxSelector;
	ApproxSelector.Initialize(&OriginalMesh, &OriginalMeshSpatial);
	ApproxSelector.TriangleCost = TriangleCost;

	ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
		ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::OrientedBox);
	}, (int32)EApproximatePartMethod::OrientedBox );

	ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
		ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::MinVolumeSweptHull);
	}, (int32)EApproximatePartMethod::MinVolumeSweptHull );

	ApproxSelector.AddGeneratedMesh( [&](FDynamicMesh3& PartMeshInOut) {
		ComputeSimplePartApproximation(PartMeshInOut, PartMeshInOut, EApproximatePartMethod::ConvexHull);
	}, (int32)EApproximatePartMethod::ConvexHull );

	ApproxSelector.SelectBestOption(ResultMesh);
}








void ComputeMeshApproximations(
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	FMeshInstanceAssembly& Assembly)
{
	using namespace UE::Geometry;
	const double AngleThresholdDeg = CombineOptions.HardNormalAngleDeg;

	int32 NumSets = Assembly.InstanceSets.Num();
	Assembly.OptimizedMeshGeometry.SetNum(NumSets);

	int32 NumSimplifiedLODs = CombineOptions.NumSimplifiedLODs;
	int32 NumApproxLODs = FMath::Max(1, 
		CombineOptions.NumLODs - CombineOptions.NumCopiedLODs - CombineOptions.NumSimplifiedLODs);

	ParallelFor(NumSets, [&](int32 Index)
	{
		TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[Index];
		const FSourceGeometry& SourceGeo = Assembly.SourceMeshGeometry[Index];
		const FDynamicMesh3& OptimizationSourceMesh = (CombineOptions.ApproximationSourceLOD < SourceGeo.SourceMeshLODs.Num()) ?
			SourceGeo.SourceMeshLODs[CombineOptions.ApproximationSourceLOD] : SourceGeo.SourceMeshLODs.Last();
		FOptimizedGeometry& ApproxGeo = Assembly.OptimizedMeshGeometry[Index];

		FDynamicMeshAABBTree3 OptimizationSourceMeshSpatial(&OptimizationSourceMesh, true);

		// compute simplified part LODs
		ApproxGeo.SimplifiedMeshLODs.SetNum(NumSimplifiedLODs);
		double InitialTolerance = CombineOptions.SimplifyBaseTolerance;
		for (int32 k = 0; k < NumSimplifiedLODs; ++k)
		{
			ApproxGeo.SimplifiedMeshLODs[k] = OptimizationSourceMesh;
			SimplifyPartMesh(ApproxGeo.SimplifiedMeshLODs[k], InitialTolerance, AngleThresholdDeg);
			InitialTolerance *= CombineOptions.SimplifyLODLevelToleranceScale;
		}

		// compute shape approximation LODs
		ApproxGeo.ApproximateMeshLODs.SetNum(NumApproxLODs);
		double InitialTriCost = CombineOptions.OptimizeBaseTriCost;
		for (int32 k = 0; k < NumApproxLODs; ++k)
		{
			SelectBestFittingMeshApproximation(OptimizationSourceMesh, OptimizationSourceMeshSpatial, 
				ApproxGeo.ApproximateMeshLODs[k], CombineOptions.SimplifyBaseTolerance, InitialTriCost);
			InitialTriCost *= CombineOptions.OptimizeLODLevelTriCostScale;

			// update enabled attribs (is this good?)
			ApproxGeo.ApproximateMeshLODs[k].EnableMatchingAttributes(OptimizationSourceMesh);

			// recompute normals
			FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&ApproxGeo.ApproximateMeshLODs[k], ApproxGeo.ApproximateMeshLODs[k].Attributes()->PrimaryNormals(), AngleThresholdDeg);
			FMeshNormals::QuickRecomputeOverlayNormals(ApproxGeo.ApproximateMeshLODs[k]);
		}

	});


	// try to filter out simplifications that did bad things
	// argh crashing!
	ReplaceBadSimplifiedLODs(Assembly);
}


// Remove hidden faces by (approximately) computing Ambient Occlusion, fully occluded faces are hidden
static void RemoveHiddenFaces_Occlusion(FDynamicMesh3& EditMesh, double MaxDistance = 200)
{
	TRemoveOccludedTriangles<FDynamicMesh3> Jacket(&EditMesh);

	Jacket.InsideMode = UE::Geometry::EOcclusionCalculationMode::SimpleOcclusionTest;
	Jacket.TriangleSamplingMethod = UE::Geometry::EOcclusionTriangleSampling::Centroids;
	Jacket.WindingIsoValue = 0.5;
	Jacket.NormalOffset = FMathd::ZeroTolerance;
	Jacket.AddRandomRays = 25;
	Jacket.AddTriangleSamples = 100;
	//if (MaxDistance > 0)
	//{
	//	Jacket.MaxDistance = MaxDistance;
	//}

	TArray<FTransformSRT3d> NoTransforms;
	NoTransforms.Add(FTransformSRT3d::Identity());

	//  set up AABBTree and FWNTree lists
	FDynamicMeshAABBTree3 Spatial(&EditMesh);
	TArray<FDynamicMeshAABBTree3*> OccluderTrees; 
	OccluderTrees.Add(&Spatial);
		
	TFastWindingTree<FDynamicMesh3> FastWinding(&Spatial, false);
	TArray<TFastWindingTree<FDynamicMesh3>*> OccluderWindings; 
	OccluderWindings.Add(&FastWinding);

	Jacket.Select(NoTransforms, OccluderTrees, OccluderWindings, NoTransforms);
	
	if (Jacket.RemovedT.Num() > 0)
	{
		Jacket.RemoveSelected();
	}

	EditMesh.CompactInPlace();
}




// Remove hidden faces by casting rays from exterior at sample points on triangles
// (This method works quite well and should eventually be extracted out to a general algorithm...)
static void RemoveHiddenFaces_ExteriorVisibility(FDynamicMesh3& TargetMesh, double SampleRadius)
{
	FDynamicMeshAABBTree3 Spatial(&TargetMesh, true);
	FAxisAlignedBox3d Bounds = Spatial.GetBoundingBox();
	double Radius = Bounds.DiagonalLength() * 0.5;


	auto FindHitTriangleTest = [&](FVector3d TargetPosition, FVector3d FarPosition) -> int
	{
		FVector3d RayDir(TargetPosition - FarPosition);
		double Distance = Normalize(RayDir);
		FRay3d Ray(FarPosition, RayDir, true);
		return Spatial.FindNearestHitTriangle(Ray, IMeshSpatial::FQueryOptions(Distance + 1.0));	// 1.0 is random fudge factor here...
	};


	// final triangle visibility, atomics can be updated on any thread
	TArray<std::atomic<bool>> ThreadSafeTriVisible;
	ThreadSafeTriVisible.SetNum(TargetMesh.MaxTriangleID());
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		ThreadSafeTriVisible[tid] = false;
	}

	// array of (+/-)X/Y/Z directions
	TArray<FVector3d> CardinalDirections;
	for (int32 k = 0; k < 3; ++k)
	{
		FVector3d Direction(0,0,0);
		Direction[k] = 1.0;
		CardinalDirections.Add(Direction);
		CardinalDirections.Add(-Direction);
	}

	//
	// First pass. For each triangle, cast a ray at it's centroid from 
	// outside the model, along the X/Y/Z directions and tri normal.
	// If tri is hit we mark it as having 'known' status, allowing it
	// to be skipped in the more expensive pass below
	//
	TArray<bool> TriStatusKnown;
	TriStatusKnown.Init(false, TargetMesh.MaxTriangleID());
	ParallelFor(TargetMesh.MaxTriangleID(), [&](int32 tid)
	{
		FVector3d Normal, Centroid; double Area;
		TargetMesh.GetTriInfo(tid, Normal, Area, Centroid);
		if (Normal.SquaredLength() < 0.1 || Area < FMathf::ZeroTolerance)
		{
			TriStatusKnown[tid] = true;
			return;
		}	

		for (FVector3d Direction : CardinalDirections)
		{
			if (FindHitTriangleTest(Centroid, Centroid + Radius*Direction) == tid)
			{
				ThreadSafeTriVisible[tid] = true;
				TriStatusKnown[tid] = true;
				return;
			}
		}
		if (FindHitTriangleTest(Centroid, Centroid + Radius*Normal) == tid)
		{
				ThreadSafeTriVisible[tid] = true;
				TriStatusKnown[tid] = true;
				return;
		}

		// triangle is not definitely visible or hidden
	});


	//
	// Construct set of exterior sample points, for each triangle sample point
	// below we will check if it is visible from any of these sample points.
	// Order is shuffled in hopes that for visible tris we don't waste a bunch
	// of time on the 'far' side
	//
	int32 NumExteriorSamplePoints = 128;
	TSphericalFibonacci<double> SphereSampler(NumExteriorSamplePoints);
	TArray<FVector3d> ExteriorSamplePoints;
	FModuloIteration ModuloIter(NumExteriorSamplePoints);
	uint32 SampleIndex = 0;
	while (ModuloIter.GetNextIndex(SampleIndex))
	{
		ExteriorSamplePoints.Add( Bounds.Center() + Radius * SphereSampler[SampleIndex] );
	}
	// add axis directions?


	//
	// For each triangle, generate a set of sample points on the triangle surface,
	// and then check if that point is visible from any of the exterior sample points.
	// This is the expensive part!
	// 
	// Does using a fixed set of exterior sample points make sense? Could also 
	// treat it as a set of sample directions. Seems more likely to hit tri
	// based on sample directions...
	//
	ParallelFor(TargetMesh.MaxTriangleID(), [&](int32 tid)
	{
		// if we already found out this triangle is visible or hidden, we can skip it
		if ( TriStatusKnown[tid] || ThreadSafeTriVisible[tid] ) return;

		FVector3d A,B,C;
		TargetMesh.GetTriVertices(tid, A,B,C);
		FVector3d Centroid = (A + B + C) / 3.0;
		double TriArea;
		FVector3d TriNormal = VectorUtil::NormalArea(A, B, C, TriArea);		// TriStatusKnown should skip degen tris, do not need to check here

		FFrame3d TriFrame(Centroid, TriNormal);
		FTriangle2d UVTriangle(TriFrame.ToPlaneUV(A), TriFrame.ToPlaneUV(B), TriFrame.ToPlaneUV(C));
		double DiscArea = (FMathd::Pi * SampleRadius * SampleRadius);
		int NumSamples = FMath::Max( (int)(TriArea / DiscArea), 2 );  // a bit arbitrary...
		FVector2d V1 = UVTriangle.V[1] - UVTriangle.V[0];
		FVector2d V2 = UVTriangle.V[2] - UVTriangle.V[0];

		TArray<int32> HitTris;		// re-use this array in inner loop to avoid hitting atomics so often

		int NumTested = 0;
		FRandomStream RandomStream(tid);
		while (NumTested < NumSamples)
		{
			double a1 = RandomStream.GetFraction();
			double a2 = RandomStream.GetFraction();
			FVector2d PointUV = UVTriangle.V[0] + a1 * V1 + a2 * V2;
			if (UVTriangle.IsInside(PointUV))
			{
				NumTested++;
				FVector3d Position = TriFrame.FromPlaneUV(PointUV, 2);

				// cast ray from all exterior sample locations for this triangle sample point
				HitTris.Reset();
				for (int32 k = 0; k < NumExteriorSamplePoints; ++k)
				{
					int32 HitTriID = FindHitTriangleTest(Position, ExteriorSamplePoints[k]);
					if ( HitTriID != IndexConstants::InvalidID && TriStatusKnown[HitTriID] == false )
					{
						HitTris.AddUnique(HitTriID);		// we hit some triangle, whether or not it is the one we are testing...
						if (HitTriID == tid)
						{
							break;
						}
					}
				}

				// mark any hit tris
				for ( int32 HitTriID : HitTris )
				{
					ThreadSafeTriVisible[HitTriID] = true;
				}

				// if our triangle has become visible (in this thread or another) we can terminate now
				if (ThreadSafeTriVisible[tid])
				{
					return;
				}
			}
		}

		// should we at any point lock and update TriStatusKnown?
	});

	// delete hidden tris
	TArray<int32> TrisToDelete;
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		if (ThreadSafeTriVisible[tid] == false)
		{
			TrisToDelete.Add(tid);
		}
	}
	FDynamicMeshEditor Editor(&TargetMesh);
	Editor.RemoveTriangles(TrisToDelete, true);

	TargetMesh.CompactInPlace();
}




static void PostProcessHiddenFaceRemovedMesh(FDynamicMesh3& TargetMesh, double Tolerance)
{
	// weld edges in case input was unwelded...
	FMergeCoincidentMeshEdges Welder(&TargetMesh);
	Welder.MergeVertexTolerance = Tolerance * 0.001;
	Welder.OnlyUniquePairs = false;
	Welder.Apply();

	// todo: try to simplify? need to be able to constrain by things like vertex color...

	TargetMesh.CompactInPlace();
}



namespace UE::Geometry
{




static void ComputeVoxWrapMesh(
	const FDynamicMesh3& CombinedMesh, 
	FDynamicMeshAABBTree3& CombinedMeshSpatial,
	FDynamicMesh3& ResultMesh,
	double ClosureDistance,
	double& TargetCellSizeInOut)
{
	TImplicitMorphology<FDynamicMesh3> Morphology;
	Morphology.Source = &CombinedMesh;
	Morphology.SourceSpatial = &CombinedMeshSpatial;
	Morphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close;
	Morphology.Distance = FMath::Max(ClosureDistance, 0.001);

	FAxisAlignedBox3d Bounds = CombinedMeshSpatial.GetBoundingBox();
	double UseCellSize = FMath::Max(0.001, TargetCellSizeInOut);
	int MaxGridDimEstimate = (int32)(Bounds.MaxDim() / UseCellSize);
	if (MaxGridDimEstimate > 256)
	{
		UseCellSize = (float)Bounds.MaxDim() / 256;
	}
	Morphology.GridCellSize = UseCellSize;
	Morphology.MeshCellSize = UseCellSize;
	TargetCellSizeInOut = UseCellSize;

	ResultMesh.Copy(&Morphology.Generate());
	ResultMesh.DiscardAttributes();
}

static void ComputeSimplifiedVoxWrapMesh(
	FDynamicMesh3& VoxWrapMesh,
	const FDynamicMesh3* CombinedMesh, 
	FDynamicMeshAABBTree3* CombinedMeshSpatial,
	double SimplifyTolerance,
	double MaxTriCount)
{
	FVolPresMeshSimplification Simplifier(&VoxWrapMesh);

	Simplifier.ProjectionMode = FVolPresMeshSimplification::ETargetProjectionMode::NoProjection;

	//FMeshProjectionTarget ProjectionTarget(&CombinedMesh, &CombinedMeshSpatial);
	//Simplifier.SetProjectionTarget(&ProjectionTarget);

	Simplifier.DEBUG_CHECK_LEVEL = 0;
	Simplifier.bRetainQuadricMemory = false; 

	//Simplifier.GeometricErrorConstraint = FVolPresMeshSimplification::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
	//Simplifier.GeometricErrorTolerance = SimplifyTolerance;

	//Simplifier.SimplifyToTriangleCount( 1 );

	if (VoxWrapMesh.TriangleCount() > MaxTriCount)
	{
		//Simplifier.SetProjectionTarget(nullptr);
		//Simplifier.GeometricErrorConstraint = FVolPresMeshSimplification::EGeometricErrorCriteria::None;
		Simplifier.SimplifyToTriangleCount( MaxTriCount );
	}

	VoxWrapMesh.CompactInPlace();
}



static void InitializeAttributes(
	FDynamicMesh3& TargetMesh,
	double NormalAngleThreshDeg,
	bool bProjectAttributes,
	const FDynamicMesh3* SourceMesh,
	FDynamicMeshAABBTree3* SourceMeshSpatial)
{
	TargetMesh.EnableTriangleGroups();
	TargetMesh.EnableAttributes();
	// recompute normals
	FMeshNormals::InitializeOverlayTopologyFromOpeningAngle(&TargetMesh, TargetMesh.Attributes()->PrimaryNormals(), NormalAngleThreshDeg);
	FMeshNormals::QuickRecomputeOverlayNormals(TargetMesh);

	if (bProjectAttributes == false || SourceMesh == nullptr || SourceMeshSpatial == nullptr)
	{
		return;
	}

	const FDynamicMeshColorOverlay* SourceColors = nullptr;
	FDynamicMeshColorOverlay* TargetColors = nullptr;
	if ( SourceMesh->HasAttributes() && SourceMesh->Attributes()->HasPrimaryColors() )
	{
		SourceColors = SourceMesh->Attributes()->PrimaryColors();
		TargetMesh.Attributes()->EnablePrimaryColors();
		TargetColors = TargetMesh.Attributes()->PrimaryColors();
	}

	const FDynamicMeshMaterialAttribute* SourceMaterialID = nullptr;
	FDynamicMeshMaterialAttribute* TargetMaterialID = nullptr;
	if ( SourceMesh->HasAttributes() && SourceMesh->Attributes()->HasMaterialID() )
	{
		SourceMaterialID = SourceMesh->Attributes()->GetMaterialID();
		TargetMesh.Attributes()->EnableMaterialID();
		TargetMaterialID = TargetMesh.Attributes()->GetMaterialID();
	}

	// compute projected group and MaterialID and vertex colors
	for (int32 tid : TargetMesh.TriangleIndicesItr())
	{
		FVector3d Centroid = TargetMesh.GetTriCentroid(tid);

		double NearDistSqr = 0;
		int32 NearestTID = SourceMeshSpatial->FindNearestTriangle(Centroid, NearDistSqr);

		if (SourceMaterialID != nullptr)
		{
			int32 MaterialID = SourceMaterialID->GetValue(NearestTID);
			TargetMaterialID->SetValue(tid, MaterialID);
		}

		if (SourceColors != nullptr)
		{
			FIndex3i SourceTriElems = SourceColors->GetTriangle(NearestTID);
			// TODO be smarter here...
			FVector4f Color = SourceColors->GetElement(SourceTriElems.A);
			int A = TargetColors->AppendElement(Color);
			int B = TargetColors->AppendElement(Color);
			int C = TargetColors->AppendElement(Color);
			TargetColors->SetTriangle(tid, FIndex3i(A,B,C));
		}
	}
}




struct FCombinedMeshLOD
{
	FDynamicMesh3 Mesh;
	FDynamicMeshEditor Editor;
	FDynamicMeshMaterialAttribute* MaterialIDs;

	FCombinedMeshLOD()
		: Editor(&Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnableMaterialID();
		
		// should we do this? maybe should be done via enable-matching?
		Mesh.Attributes()->EnablePrimaryColors();

		MaterialIDs = Mesh.Attributes()->GetMaterialID();
	}

};

}


enum class ECombinedLODType
{
	Copied = 0,
	Simplified = 1,
	Approximated = 2,
	VoxWrapped = 3
};



static void SortMesh(FDynamicMesh3& Mesh)
{
	if ( ! ensure(Mesh.HasAttributes() == false) ) return;

	TRACE_CPUPROFILER_EVENT_SCOPE(SortMesh);

	struct FVert
	{
		FVector3d Position;
		int32 VertexID;
		bool operator<(const FVert& V2) const
		{
			if ( Position.X != V2.Position.X ) { return Position.X < V2.Position.X; }
			if ( Position.Y != V2.Position.Y ) { return Position.Y < V2.Position.Y; }
			if ( Position.Z != V2.Position.Z ) { return Position.Z < V2.Position.Z; }
			return VertexID < V2.VertexID;
		}
	};
	struct FTri
	{
		FIndex3i Triangle;
		bool operator<(const FTri& Tri2) const
		{
			if ( Triangle.A != Tri2.Triangle.A ) { return Triangle.A < Tri2.Triangle.A; }
			if ( Triangle.B != Tri2.Triangle.B ) { return Triangle.B < Tri2.Triangle.B; }
			return Triangle.C < Tri2.Triangle.C; 
		}
	};

	TArray<FVert> Vertices;
	for ( int32 vid : Mesh.VertexIndicesItr() )
	{
		Vertices.Add( FVert{Mesh.GetVertex(vid), vid} );
	}
	Vertices.Sort();

	TArray<int32> VertMap;
	VertMap.SetNum(Mesh.MaxVertexID());
	for (int32 k = 0; k < Vertices.Num(); ++k)
	{
		const FVert& Vert = Vertices[k];
		VertMap[Vert.VertexID] = k;
	}

	TArray<FTri> Triangles;
	for ( int32 tid : Mesh.TriangleIndicesItr() )
	{
		FIndex3i Tri = Mesh.GetTriangle(tid);
		Tri.A = VertMap[Tri.A];
		Tri.B = VertMap[Tri.B];
		Tri.C = VertMap[Tri.C];
		Triangles.Add( FTri{Tri} );
	}
	Triangles.Sort();

	FDynamicMesh3 SortedMesh;
	for (const FVert& Vert : Vertices)
	{
		SortedMesh.AppendVertex(Mesh, Vert.VertexID);
	}
	for (const FTri& Tri : Triangles)
	{
		SortedMesh.AppendTriangle(Tri.Triangle.A, Tri.Triangle.B, Tri.Triangle.C);
	}

	Mesh = MoveTemp(SortedMesh);
}



void ComputeHiddenRemovalForLOD(
	FDynamicMesh3& MeshLOD,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RemoveHidden_LOD);
	bool bModified = false;
	switch (CombineOptions.RemoveHiddenFacesMethod)
	{
		case IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::OcclusionBased:
			RemoveHiddenFaces_Occlusion(MeshLOD, 200);		// 200 is arbitrary here! should improve once max-distance is actually available (currently ignored)
			bModified = true;
			break;
		case IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::ExteriorVisibility:
		case IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::Fastest:
			RemoveHiddenFaces_ExteriorVisibility(MeshLOD, CombineOptions.RemoveHiddenSamplingDensity);
			bModified = true;
			break;
	}

	if ( bModified )
	{
		PostProcessHiddenFaceRemovedMesh(MeshLOD, CombineOptions.SimplifyBaseTolerance);
	}
}


// change this to build a single LOD, and separate versions for (eg) source mesh vs approx mesh
// should we even bother w/ storing approx meshes? just generate them as needed?

void BuildCombinedMesh(
	const FMeshInstanceAssembly& Assembly,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	TArray<FDynamicMesh3>& CombinedMeshLODs)
{
	using namespace UE::Geometry;

	int32 NumLODs = CombineOptions.NumLODs;
	TArray<FCombinedMeshLOD> MeshLODs;
	MeshLODs.SetNum(NumLODs);

	int FirstVoxWrappedIndex = 9999;
	TArray<ECombinedLODType> LODTypes;
	LODTypes.Init(ECombinedLODType::Approximated, NumLODs);
	for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
	{
		if (LODLevel < CombineOptions.NumCopiedLODs)
		{
			LODTypes[LODLevel] = ECombinedLODType::Copied;
		}
		else if (LODLevel < CombineOptions.NumCopiedLODs + CombineOptions.NumSimplifiedLODs)
		{
			LODTypes[LODLevel] = ECombinedLODType::Simplified;
		}
		else if (LODLevel >= NumLODs - CombineOptions.NumVoxWrapLODs)
		{
			LODTypes[LODLevel] = ECombinedLODType::VoxWrapped;
			FirstVoxWrappedIndex = FMath::Min(LODLevel, FirstVoxWrappedIndex);
		}
	}

	//CombinedLOD0.Attributes()->SetNumPolygroupLayers(2);
	//FDynamicMeshPolygroupAttribute* PartIDAttrib = AccumMesh.Attributes()->GetPolygroupLayer(0);
	//FDynamicMeshPolygroupAttribute* PartInstanceMapAttrib = AccumMesh.Attributes()->GetPolygroupLayer(1);

	int32 NumSets = Assembly.InstanceSets.Num();

	//for ( int32 SetIndex = 0; SetIndex < NumSets; ++SetIndex )
	//{
	//	CombinedLOD0.EnableMatchingAttributes( Assembly.SourceMeshGeometry[Index].OriginalMesh, false, false );
	//}

	for ( int32 SetIndex = 0; SetIndex < NumSets; ++SetIndex )
	{
		const TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[SetIndex];
		const FSourceGeometry& SourceGeometry = Assembly.SourceMeshGeometry[SetIndex];
		const FOptimizedGeometry& OptimizedGeometry = Assembly.OptimizedMeshGeometry[SetIndex];
		UStaticMesh* StaticMesh = InstanceSet->SourceAsset;

		FMeshIndexMappings Mappings;

		for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
		{
			const FDynamicMesh3* SourceAppendMesh = nullptr;
			const FDynamicMesh3* ApproximateAppendMesh = nullptr;
			const FDynamicMesh3* UseAppendMesh = nullptr;

			// default approximate mesh to lowest-quality approximation (box), need to do this
			// so that we always have something to swap to for Decorative parts
			ApproximateAppendMesh = &OptimizedGeometry.ApproximateMeshLODs.Last();

			ECombinedLODType LevelLODType = LODTypes[LODLevel];
			if (LevelLODType == ECombinedLODType::Copied)
			{
				SourceAppendMesh = (LODLevel < SourceGeometry.SourceMeshLODs.Num()) ? 
					&SourceGeometry.SourceMeshLODs[LODLevel] : &SourceGeometry.SourceMeshLODs.Last();
				UseAppendMesh = SourceAppendMesh;
			}
			else if (LevelLODType == ECombinedLODType::Simplified)
			{
				int32 SimplifiedLODIndex = LODLevel - CombineOptions.NumCopiedLODs;
				SourceAppendMesh = &OptimizedGeometry.SimplifiedMeshLODs[SimplifiedLODIndex];
				UseAppendMesh = SourceAppendMesh;
			}
			else if (LevelLODType == ECombinedLODType::VoxWrapped)
			{
				SourceAppendMesh = &SourceGeometry.SourceMeshLODs.Last();
				UseAppendMesh = SourceAppendMesh;
			}
			else // ECombinedLODType::Approximated
			{
				int32 ApproxLODIndex = LODLevel - CombineOptions.NumCopiedLODs - CombineOptions.NumSimplifiedLODs;
				ApproximateAppendMesh = &OptimizedGeometry.ApproximateMeshLODs[ApproxLODIndex];
				UseAppendMesh = ApproximateAppendMesh;
			}

			FCombinedMeshLOD& CombinedMeshLODData = MeshLODs[LODLevel];

			for ( const FMeshInstance& Instance : InstanceSet->Instances )
			{
				bool bIsDecorativePart = (Instance.DetailLevel == EMeshDetailLevel::Decorative);

				if (bIsDecorativePart)
				{
					// filter out detail parts at higher LODs, or if we are doing VoxWrap LOD
					if ( LODLevel >= CombineOptions.FilterDecorativePartsLODLevel || LevelLODType == ECombinedLODType::VoxWrapped )
					{
						continue;
					}
					// at last detail part LOD, switch to approximate mesh
					if (LODLevel >= (CombineOptions.FilterDecorativePartsLODLevel - CombineOptions.ApproximateDecorativePartLODs) )
					{
						UseAppendMesh = ApproximateAppendMesh;
					}
				}

				// need to make a copy to run pre-process func
				FDynamicMesh3 TempAppendMesh(*UseAppendMesh);
				if (Assembly.PreProcessInstanceMeshFunc)
				{
					Assembly.PreProcessInstanceMeshFunc(TempAppendMesh, Instance);
				}

				Mappings.Reset();
				CombinedMeshLODData.Editor.AppendMesh(&TempAppendMesh, Mappings,
					[&](int, const FVector3d& Pos) { return Instance.WorldTransform.TransformPosition(Pos); },
					[&](int, const FVector3d& Normal) { return Instance.WorldTransform.TransformNormal(Normal); });

				// append part ID stuff here

				// could precompute these indexes for each instance?
				// also for source mesh we could transfer material IDs correctly...
				UMaterialInterface* UseMaterial = Instance.Materials[0];
				const int32* FoundMaterialIndex = Assembly.MaterialMap.Find(UseMaterial);
				int32 AssignMaterialIndex = (FoundMaterialIndex != nullptr) ? *FoundMaterialIndex : 0;

				for (int32 tid : TempAppendMesh.TriangleIndicesItr())
				{
					CombinedMeshLODData.MaterialIDs->SetValue( Mappings.GetNewTriangle(tid), AssignMaterialIndex );
				}
			}
		}
	}


	//
	// start hidden-removal passes on all meshes up to voxel LODs here, because we can compute voxel LOD at the same time
	//
	TArray<UE::Tasks::FTask> PendingRemoveHiddenTasks;
	bool bRemoveHiddenFaces = 
		(CombineOptions.RemoveHiddenFacesMethod != IGeometryProcessing_CombineMeshInstances::ERemoveHiddenFacesMode::None 
		&& CVarGeometryCombineMeshInstancesRemoveHidden.GetValueOnGameThread() > 0);
	if (bRemoveHiddenFaces)
	{
		for (int32 k = CombineOptions.RemoveHiddenStartLOD; k < MeshLODs.Num() && k < FirstVoxWrappedIndex; ++k)
		{
			UE::Tasks::FTask RemoveHiddenTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&MeshLODs, &CombineOptions, k]()
			{ 
				ComputeHiddenRemovalForLOD(MeshLODs[k].Mesh, CombineOptions);
			});
			PendingRemoveHiddenTasks.Add(RemoveHiddenTask);
		}
	}


	//
	// Process VoxWrapped LODs 
	//
	if ( FirstVoxWrappedIndex < 9999 )
	{
		FDynamicMesh3 SourceVoxWrapMesh = MoveTemp(MeshLODs[FirstVoxWrappedIndex].Mesh);
		FDynamicMeshAABBTree3 Spatial(&SourceVoxWrapMesh, true);

		FDynamicMesh3 TempBaseVoxWrapMesh;
		double VoxelDimension = 2.0;	// may be modified by ComputeVoxWrapMesh call
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ComputeVoxWrap);
			ComputeVoxWrapMesh(SourceVoxWrapMesh, Spatial, TempBaseVoxWrapMesh, 10.0, VoxelDimension);
			// currently need to re-sort output to remove non-determinism...
			SortMesh(TempBaseVoxWrapMesh);

			//UE_LOG(LogGeometry, Warning, TEXT("VoxWrapMesh has %d triangles %d vertices"), TempBaseVoxWrapMesh.TriangleCount(), TempBaseVoxWrapMesh.VertexCount());
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FastCollapsePrePass);
			TempBaseVoxWrapMesh.DiscardAttributes();
			FVolPresMeshSimplification Simplifier(&TempBaseVoxWrapMesh);
			Simplifier.bAllowSeamCollapse = false;
			Simplifier.FastCollapsePass(VoxelDimension*0.5, 10, false, 50000);
		}

		const FDynamicMesh3& LastApproxLOD = MeshLODs[FirstVoxWrappedIndex-1].Mesh;

		int32 MaxTriCount = CombineOptions.VoxWrapMaxTriCountBase;
		double SimplifyTolerance = CombineOptions.VoxWrapBaseTolerance;
		for (int32 LODIndex = FirstVoxWrappedIndex; LODIndex < NumLODs; ++LODIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SimplifyVoxWrap);

			// using previous-simplified mesh for next level may not be ideal...
			MeshLODs[LODIndex].Mesh = (LODIndex == FirstVoxWrappedIndex) ?
				MoveTemp(TempBaseVoxWrapMesh) : MeshLODs[LODIndex-1].Mesh;
			// need to do this because we projected attributes in previous loop
			MeshLODs[LODIndex].Mesh.DiscardAttributes();

			ComputeSimplifiedVoxWrapMesh(MeshLODs[LODIndex].Mesh, &SourceVoxWrapMesh, &Spatial, 
				SimplifyTolerance, MaxTriCount);

			InitializeAttributes(MeshLODs[LODIndex].Mesh, CombineOptions.HardNormalAngleDeg,
				/*bProjectAttributes*/true, &SourceVoxWrapMesh, &Spatial);

			SimplifyTolerance *= 1.5;
			MaxTriCount /= 2;
		}
	}


	// wait...
	UE::Tasks::Wait(PendingRemoveHiddenTasks);

	// remove hiddel faces on voxel LODs (todo: can do this via shape sorting, much faster)
	if (bRemoveHiddenFaces)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveHidden);
		ParallelFor(MeshLODs.Num(), [&](int32 k)
		{
			if ( k >= FirstVoxWrappedIndex )
			{ 
				ComputeHiddenRemovalForLOD(MeshLODs[k].Mesh, CombineOptions);
			}
		});
	}


	for (int32 LODLevel = 0; LODLevel < NumLODs; ++LODLevel)
	{
		FDynamicMesh3 LODMesh = MoveTemp(MeshLODs[LODLevel].Mesh);

		// If we ended up larger than the mesh in the previous LOD, we should use that instead!
		// This can happen particular with VoxWrap LODs
		if (LODLevel > 0)
		{
			if (LODMesh.TriangleCount() > CombinedMeshLODs.Last().TriangleCount())
			{
				LODMesh = CombinedMeshLODs.Last();
			}
		}
		CombinedMeshLODs.Add(MoveTemp(LODMesh));
	}

}






/**
 * Construct a new OrientedBox that contains both A and B. The main problem is to
 * determine the new Orientation, this is done by a 0.5 slerp of the orientations of A and B.
 * The new local Origin and Extents are then computed in this new orientation.
 */
FOrientedBox3d MergeBoxes(const FOrientedBox3d& A, const FOrientedBox3d& B)
{
	FOrientedBox3d NewBox;
	NewBox.Frame.Origin = (A.Center() + B.Center()) * 0.5;

	FQuaterniond RotationA(A.Frame.Rotation), RotationB(B.Frame.Rotation);
	if (RotationA.Dot(RotationB) < 0)
	{
		RotationB = -RotationB;
	}

	// this is just a slerp?
	FQuaterniond HalfRotation = RotationA + RotationB;
	HalfRotation.Normalize();
	NewBox.Frame.Rotation = HalfRotation;

	// likely faster to compute the frame X/Y/Z instead of calling ToFramePoint each time...
	FAxisAlignedBox3d LocalBounds(FVector3d::Zero(), FVector3d::Zero());
	A.EnumerateCorners([&](FVector3d P)
	{
		LocalBounds.Contain( NewBox.Frame.ToFramePoint(P) );
	});
	B.EnumerateCorners([&](FVector3d P)
	{
		LocalBounds.Contain( NewBox.Frame.ToFramePoint(P) );
	});

	// update origin and extents
	NewBox.Frame.Origin = NewBox.Frame.FromFramePoint( LocalBounds.Center() );
	NewBox.Extents = 0.5 * LocalBounds.Diagonal();

	return NewBox;
}



static void CombineCollisionShapes(
	FSimpleShapeSet3d& CollisionShapes,
	double AxisToleranceDelta = 0.01)
{
	// only going to merge boxes for now
	TArray<FOrientedBox3d> Boxes;
	for (FBoxShape3d Box : CollisionShapes.Boxes)
	{
		Boxes.Add(Box.Box);
	}

	// want to merge larger-volume boxes first
	Boxes.Sort([&](const FOrientedBox3d& A, const FOrientedBox3d& B)
	{
		return A.Volume() > B.Volume();
	});

	auto CalcOffsetVolume = [](FOrientedBox3d Box, double AxisDelta) -> double
	{
		Box.Extents.X = FMathd::Max(0, Box.Extents.X+AxisDelta);
		Box.Extents.Y = FMathd::Max(0, Box.Extents.Y+AxisDelta);
		Box.Extents.Z = FMathd::Max(0, Box.Extents.Z+AxisDelta);
		return Box.Volume();
	};

	double DotTol = 0.99;
	auto HasMatchingAxis = [DotTol](const FVector3d& Axis, const FOrientedBox3d& Box)
	{
		for (int32 k = 0; k < 3; ++k)
		{
			if (FMathd::Abs(Axis.Dot(Box.GetAxis(k))) > DotTol)
			{
				return true;
			}
		}
		return false;
	};

	bool bFoundMerge = true;
	while (bFoundMerge)
	{
		bFoundMerge = false;

		int32 N = Boxes.Num();
		for (int32 i = 0; i < N; ++i)
		{
			FOrientedBox3d Box1 = Boxes[i];

			for (int32 j = i + 1; j < N; ++j)
			{
				FOrientedBox3d Box2 = Boxes[j];

				// should we just be appending box2 to Box1? prevents getting skewed boxes...
				FOrientedBox3d NewBox = MergeBoxes(Box1, Box2);

				// check if newbox is still aligned w/ box2?
				bool bAllAxesAligned = true;
				for (int32 k = 0; k < 3; ++k)
				{
					bAllAxesAligned = bAllAxesAligned && HasMatchingAxis(Box1.GetAxis(k), NewBox) && HasMatchingAxis(Box2.GetAxis(k), NewBox);
				}
				if (!bAllAxesAligned)
				{
					continue;
				}

				double SumVolume = Box1.Volume() + Box2.Volume();
				if ( (CalcOffsetVolume(NewBox, AxisToleranceDelta) > SumVolume) &&
						(CalcOffsetVolume(NewBox, -AxisToleranceDelta) < SumVolume) )
				{
					bFoundMerge = true;
					Boxes[i] = NewBox;
					Boxes.RemoveAtSwap(j);
					j = N;
					N--;
				}
			}
		}
	}

	CollisionShapes.Boxes.Reset();
	for (FOrientedBox3d Box : Boxes)
	{
		CollisionShapes.Boxes.Add(FBoxShape3d(Box));
	}
}


void BuildCombinedCollisionShapes(
	const FMeshInstanceAssembly& Assembly,
	IGeometryProcessing_CombineMeshInstances::FOptions CombineOptions,
	FSimpleShapeSet3d& CombinedCollisionShapes)
{
	int32 NumSets = Assembly.InstanceSets.Num();

	for ( int32 SetIndex = 0; SetIndex < NumSets; ++SetIndex )
	{
		const TUniquePtr<FMeshInstanceSet>& InstanceSet = Assembly.InstanceSets[SetIndex];
		const FSourceGeometry& SourceGeometry = Assembly.SourceMeshGeometry[SetIndex];
		for ( const FMeshInstance& Instance : InstanceSet->Instances )
		{
			bool bIsDecorativePart = (Instance.DetailLevel == EMeshDetailLevel::Decorative);
			if ( ! bIsDecorativePart )
			{
				CombinedCollisionShapes.Append( SourceGeometry.CollisionShapes, Instance.WorldTransform );
			}
		}
	}

	// trivially merge any adjacent boxes that merge to a perfect combined-box
	CombineCollisionShapes(CombinedCollisionShapes, 0.01);
}








IGeometryProcessing_CombineMeshInstances::FOptions FCombineMeshInstancesImpl::ConstructDefaultOptions()
{
	//
	// Construct options for ApproximateActors operation
	//
	FOptions Options;

	Options.NumLODs = 5;

	Options.NumCopiedLODs = 1;

	Options.NumSimplifiedLODs = 3;
	Options.SimplifyBaseTolerance = 0.25;
	Options.SimplifyLODLevelToleranceScale = 2.0;

	Options.OptimizeBaseTriCost = 0.7;
	Options.OptimizeLODLevelTriCostScale = 2.5;

	//// LOD level to filter out detail parts
	Options.FilterDecorativePartsLODLevel = 2;


	Options.RemoveHiddenFacesMethod = ERemoveHiddenFacesMode::Fastest;

	return Options;
}



static void SetConstantVertexColor(FDynamicMesh3& Mesh, FLinearColor LinearColor)
{
	if (Mesh.HasAttributes() == false)
	{
		Mesh.EnableAttributes();
	}
	if (Mesh.Attributes()->HasPrimaryColors() == false)
	{
		Mesh.Attributes()->EnablePrimaryColors();
	}
	FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();
	TArray<int32> ElemIDs;
	ElemIDs.SetNum(Mesh.MaxVertexID());
	for (int32 VertexID : Mesh.VertexIndicesItr())
	{
		ElemIDs[VertexID] = Colors->AppendElement( (FVector4f)LinearColor );
	}
	for (int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		Colors->SetTriangle(TriangleID, FIndex3i(ElemIDs[Triangle.A], ElemIDs[Triangle.B], ElemIDs[Triangle.C]) );
	}
}



void FCombineMeshInstancesImpl::CombineMeshInstances(
	const FInstanceSet& MeshInstances, const FOptions& Options, FResults& ResultsOut)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInstances);

	FMeshInstanceAssembly InstanceAssembly;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_Setup);
		InitializeMeshInstanceAssembly(MeshInstances, InstanceAssembly);
		InitializeAssemblySourceMeshesFromLOD(InstanceAssembly, 0, Options.NumCopiedLODs);
		InitializeInstanceAssemblySpatials(InstanceAssembly);
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_PartApprox);
		ComputeMeshApproximations(Options, InstanceAssembly);
	}


	
	InstanceAssembly.PreProcessInstanceMeshFunc = [&InstanceAssembly, &MeshInstances](FDynamicMesh3& AppendMesh, const FMeshInstance& Instance)
	{
		int32 SourceInstance = Instance.ExternalInstanceIndex[0];
		int GroupDataIdx = MeshInstances.StaticMeshInstances[SourceInstance].GroupDataIndex;
		if (MeshInstances.InstanceGroupDatas[GroupDataIdx].bHasConstantOverrideVertexColor)
		{
			FColor VertexColorSRGB = MeshInstances.InstanceGroupDatas[GroupDataIdx].OverrideVertexColor;
			//FLinearColor VertexColorLinear(VertexColorSRGB);
			FLinearColor VertexColorLinear = VertexColorSRGB.ReinterpretAsLinear();
			SetConstantVertexColor(AppendMesh, VertexColorLinear);
		}
	};

	TArray<UE::Geometry::FDynamicMesh3> CombinedMeshLODs;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_BuildMeshes);
		BuildCombinedMesh(InstanceAssembly, Options, CombinedMeshLODs);
	}

	FSimpleShapeSet3d CombinedCollisionShapes;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CombineMeshInst_BuildCollision);
		BuildCombinedCollisionShapes(InstanceAssembly, Options, CombinedCollisionShapes);
	}
	FPhysicsDataCollection PhysicsData;
	PhysicsData.Geometry = CombinedCollisionShapes;
	PhysicsData.CopyGeometryToAggregate();		// need FPhysicsDataCollection to convert to agg geom, should fix this


	ResultsOut.CombinedMeshes.SetNum(1);
	IGeometryProcessing_CombineMeshInstances::FOutputMesh& OutputMesh = ResultsOut.CombinedMeshes[0];
	OutputMesh.MeshLODs = MoveTemp(CombinedMeshLODs);
	OutputMesh.MaterialSet = InstanceAssembly.UniqueMaterials;
	OutputMesh.SimpleCollisionShapes = PhysicsData.AggGeom;
}