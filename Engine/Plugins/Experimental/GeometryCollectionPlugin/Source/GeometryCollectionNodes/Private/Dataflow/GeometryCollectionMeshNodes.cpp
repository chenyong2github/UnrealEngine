// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionMeshNodes.h"
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
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionMeshNodes)

namespace Dataflow
{

	void GeometryCollectionMeshNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPointsToMeshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBoxToMeshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMeshInfoDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMeshToCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FStaticMeshToMeshDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMeshAppendDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMeshBooleanDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMeshCopyToPointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetMeshDataDataflowNode);

		// Mesh
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("Mesh", FLinearColor(1.f, 0.16f, 0.05f), CDefaultNodeBodyTintColor);
	}
}


void FPointsToMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh) || Out->IsA<int32>(&TriangleCount))
	{
		const TArray<FVector>& PointsArr = GetValue<TArray<FVector>>(Context, &Points);

		if (PointsArr.Num() > 0)
		{
			TObjectPtr<UDynamicMesh> DynamicMesh = NewObject<UDynamicMesh>();
			DynamicMesh->Reset();

			UE::Geometry::FDynamicMesh3& DynMesh = DynamicMesh->GetMeshRef();

			for (auto& Point : PointsArr)
			{
				DynMesh.AppendVertex(Point);
			}

			SetValue<TObjectPtr<UDynamicMesh>>(Context, DynamicMesh, &Mesh);
			SetValue<int32>(Context, DynamicMesh->GetTriangleCount(), &TriangleCount);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
			SetValue<int32>(Context, 0, &TriangleCount);
		}
	}
}

void FBoxToMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh) || Out->IsA<int32>(&TriangleCount))
	{
		TObjectPtr<UDynamicMesh> DynamicMesh = NewObject<UDynamicMesh>();
		DynamicMesh->Reset();

		UE::Geometry::FDynamicMesh3& DynMesh = DynamicMesh->GetMeshRef();

		FBox InBox = GetValue<FBox>(Context, &Box);
		FVector Min = InBox.Min;
		FVector Max = InBox.Max;

		// Add vertices
		int Vertex0 = DynMesh.AppendVertex(Min);
		int Vertex1 = DynMesh.AppendVertex(FVector(Min.X, Max.Y, Min.Z));
		int Vertex2 = DynMesh.AppendVertex(FVector(Max.X, Max.Y, Min.Z));
		int Vertex3 = DynMesh.AppendVertex(FVector(Max.X, Min.Y, Min.Z));
		int Vertex4 = DynMesh.AppendVertex(FVector(Min.X, Min.Y, Max.Z));
		int Vertex5 = DynMesh.AppendVertex(FVector(Min.X, Max.Y, Max.Z));
		int Vertex6 = DynMesh.AppendVertex(Max);
		int Vertex7 = DynMesh.AppendVertex(FVector(Max.X, Min.Y, Max.Z));

		// Add triangles
		int GroupID = 0;
		DynMesh.AppendTriangle(Vertex0, Vertex1, Vertex3, GroupID);
		DynMesh.AppendTriangle(Vertex1, Vertex2, Vertex3, GroupID);
		DynMesh.AppendTriangle(Vertex3, Vertex6, Vertex7, GroupID);
		DynMesh.AppendTriangle(Vertex3, Vertex2, Vertex6, GroupID);
		DynMesh.AppendTriangle(Vertex7, Vertex4, Vertex0, GroupID);
		DynMesh.AppendTriangle(Vertex0, Vertex3, Vertex7, GroupID);
		DynMesh.AppendTriangle(Vertex0, Vertex4, Vertex5, GroupID);
		DynMesh.AppendTriangle(Vertex0, Vertex5, Vertex1, GroupID);
		DynMesh.AppendTriangle(Vertex1, Vertex5, Vertex6, GroupID);
		DynMesh.AppendTriangle(Vertex6, Vertex2, Vertex1, GroupID);
		DynMesh.AppendTriangle(Vertex4, Vertex6, Vertex5, GroupID);
		DynMesh.AppendTriangle(Vertex4, Vertex7, Vertex6, GroupID);

		SetValue<TObjectPtr<UDynamicMesh>>(Context, DynamicMesh, &Mesh);
		SetValue<int32>(Context, DynamicMesh->GetTriangleCount(), &TriangleCount);
	}
}

void FMeshInfoDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FString>(&InfoString))
	{
		UE::Geometry::FDynamicMesh3& DynMesh = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh))->GetMeshRef();

		SetValue<FString>(Context, DynMesh.MeshInfoString(), &InfoString);
	}
}

void FMeshToCollectionDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const UE::Geometry::FDynamicMesh3& DynMesh = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh))->GetMeshRef();
		if (DynMesh.VertexCount() > 0)
		{
			FMeshDescription MeshDescription;
			FStaticMeshAttributes Attributes(MeshDescription);
			Attributes.Register();

			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(&DynMesh, MeshDescription, true);

			FGeometryCollection NewGeometryCollection = FGeometryCollection();
			FGeometryCollectionEngineConversion::AppendMeshDescription(&MeshDescription, FString(TEXT("TEST")), 0, FTransform().Identity, &NewGeometryCollection);

			FManagedArrayCollection Col = FManagedArrayCollection();
			NewGeometryCollection.CopyTo(&Col);

			SetValue<FManagedArrayCollection>(Context, Col, &Collection);
		}
		else
		{
			SetValue<FManagedArrayCollection>(Context, FManagedArrayCollection(), &Collection);
		}
	}
}

void FStaticMeshToMeshDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
#if WITH_EDITORONLY_DATA
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		if (FMeshDescription* MeshDescription = UseHiRes ? StaticMesh->GetHiResMeshDescription() : StaticMesh->GetMeshDescription(LODLevel))
		{
			TObjectPtr<UDynamicMesh> DynamicMesh = NewObject<UDynamicMesh>();
			DynamicMesh->Reset();

			UE::Geometry::FDynamicMesh3& DynMesh = DynamicMesh->GetMeshRef();
			{
				FMeshDescriptionToDynamicMesh ConverterToDynamicMesh;
				ConverterToDynamicMesh.Convert(MeshDescription, DynMesh);
			}

			SetValue<TObjectPtr<UDynamicMesh>>(Context, DynamicMesh, &Mesh);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
		}
	}
#endif
}


void FMeshAppendDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		const UE::Geometry::FDynamicMesh3& DynMesh1 = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh1))->GetMeshRef();
		const UE::Geometry::FDynamicMesh3& DynMesh2 = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh2))->GetMeshRef();

		if (DynMesh1.VertexCount() > 0 || DynMesh2.VertexCount() > 0)
		{
			TObjectPtr<UDynamicMesh> DynamicMesh = NewObject<UDynamicMesh>();
			DynamicMesh->Reset();

			UE::Geometry::FDynamicMesh3& ResultDynMesh = DynamicMesh->GetMeshRef();

			UE::Geometry::FDynamicMeshEditor MeshEditor(&ResultDynMesh);

			UE::Geometry::FMeshIndexMappings IndexMaps1;
			MeshEditor.AppendMesh(&DynMesh1, IndexMaps1);

			UE::Geometry::FMeshIndexMappings IndexMaps2;
			MeshEditor.AppendMesh(&DynMesh2, IndexMaps2);

			SetValue<TObjectPtr<UDynamicMesh>>(Context, DynamicMesh, &Mesh);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
		}
	}
}

void FMeshBooleanDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		const UE::Geometry::FDynamicMesh3& DynMesh1 = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh1))->GetMeshRef();
		const UE::Geometry::FDynamicMesh3& DynMesh2 = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh2))->GetMeshRef();

		if (DynMesh1.VertexCount() > 0 && DynMesh2.VertexCount() > 0)
		{
			// Get output
			TObjectPtr<UDynamicMesh> DynamicMesh = NewObject<UDynamicMesh>();
			DynamicMesh->Reset();
			UE::Geometry::FDynamicMesh3& ResultDynMesh = DynamicMesh->GetMeshRef();

			UE::Geometry::FMeshBoolean::EBooleanOp BoolOp = UE::Geometry::FMeshBoolean::EBooleanOp::Intersect;
			if (Operation == EMeshBooleanOperationEnum::Dataflow_MeshBoolean_Intersect)
			{
				BoolOp = UE::Geometry::FMeshBoolean::EBooleanOp::Intersect;
			}
			else if (Operation == EMeshBooleanOperationEnum::Dataflow_MeshBoolean_Union)
			{
				BoolOp = UE::Geometry::FMeshBoolean::EBooleanOp::Union;
			}
			else if (Operation == EMeshBooleanOperationEnum::Dataflow_MeshBoolean_Difference)
			{
				BoolOp = UE::Geometry::FMeshBoolean::EBooleanOp::Difference;
			}

			UE::Geometry::FMeshBoolean Boolean(&DynMesh1, &DynMesh2, &ResultDynMesh, BoolOp);
			Boolean.bSimplifyAlongNewEdges = true;
			Boolean.PreserveUVsOnlyForMesh = 0; // slight warping of the autogenerated cell UVs generally doesn't matter
			Boolean.bWeldSharedEdges = false;
			Boolean.bTrackAllNewEdges = true;
			if (!Boolean.Compute())
			{
				SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
				return;
			}

			SetValue<TObjectPtr<UDynamicMesh>>(Context, DynamicMesh, &Mesh);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
		}
	}
}

void FMeshCopyToPointsDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TObjectPtr<UDynamicMesh>>(&Mesh))
	{
		const TArray<FVector>& InPoints = GetValue<TArray<FVector>>(Context, &Points);
		const UE::Geometry::FDynamicMesh3& InDynMeshToCopy = (GetValue<TObjectPtr<UDynamicMesh>>(Context, &MeshToCopy))->GetMeshRef();

		if (InPoints.Num() > 0 && InDynMeshToCopy.VertexCount() > 0)
		{
			TObjectPtr<UDynamicMesh> DynamicMesh = NewObject<UDynamicMesh>();
			DynamicMesh->Reset();

			UE::Geometry::FDynamicMesh3& ResultDynMesh = DynamicMesh->GetMeshRef();
			UE::Geometry::FDynamicMeshEditor MeshEditor(&ResultDynMesh);

			for (auto& Point : InPoints)
			{
				UE::Geometry::FDynamicMesh3 DynMeshTemp(InDynMeshToCopy);
				UE::Geometry::FRefCountVector VertexRefCounts = DynMeshTemp.GetVerticesRefCounts();

				UE::Geometry::FRefCountVector::IndexIterator ItVertexID = VertexRefCounts.BeginIndices();
				const UE::Geometry::FRefCountVector::IndexIterator ItEndVertexID = VertexRefCounts.EndIndices();

				while (ItVertexID != ItEndVertexID)
				{
					DynMeshTemp.SetVertex(*ItVertexID, Scale * DynMeshTemp.GetVertex(*ItVertexID) + Point);
					++ItVertexID;
				}

				UE::Geometry::FMeshIndexMappings IndexMaps;
				MeshEditor.AppendMesh(&DynMeshTemp, IndexMaps);
			}

			SetValue<TObjectPtr<UDynamicMesh>>(Context, DynamicMesh, &Mesh);
		}
		else
		{
			SetValue<TObjectPtr<UDynamicMesh>>(Context, NewObject<UDynamicMesh>(), &Mesh);
		}
	}
}


void FGetMeshDataDataflowNode::Evaluate(Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<int32>(&VertexCount))
	{
		TObjectPtr<UDynamicMesh> DynamicMesh = GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh);
		const UE::Geometry::FDynamicMesh3& DynMesh = DynamicMesh->GetMeshRef();

		SetValue<int32>(Context, DynMesh.VertexCount(), &VertexCount);
	}
	else if (Out->IsA<int32>(&EdgeCount))
	{
		TObjectPtr<UDynamicMesh> DynamicMesh = GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh);
		const UE::Geometry::FDynamicMesh3& DynMesh = DynamicMesh->GetMeshRef();

		SetValue<int32>(Context, DynMesh.EdgeCount(), &EdgeCount);
	}
	else if (Out->IsA<int32>(&TriangleCount))
	{
		TObjectPtr<UDynamicMesh> DynamicMesh = GetValue<TObjectPtr<UDynamicMesh>>(Context, &Mesh);
		const UE::Geometry::FDynamicMesh3& DynMesh = DynamicMesh->GetMeshRef();

		SetValue<int32>(Context, DynMesh.TriangleCount(), &TriangleCount);
	}
}

