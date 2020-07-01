// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "Algo/Copy.h"
#include "Misc/SecureHash.h"
#include "Serialization/BulkDataReader.h"
#include "Serialization/BulkDataWriter.h"
#include "UObject/EnterpriseObjectVersion.h"

void UDEPRECATED_MeshDescription::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_LOG(LogLoad, Error, TEXT( "UMeshDescription about to be deprecated - please resave %s"), *GetPathName());
	}

	// Discard the contents
	FMeshDescription MeshDescription;
	Ar << MeshDescription;
}



FMeshDescription::FMeshDescription()
{
	// Minimal requirement is that vertices have a Position attribute
	VertexAttributesSet.RegisterAttribute(MeshAttribute::Vertex::Position, 1, FVector::ZeroVector, EMeshAttributeFlags::Lerpable);
}


void FMeshDescription::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::MeshDescriptionNewSerialization)
	{
		UE_LOG(LogLoad, Warning, TEXT("Deprecated serialization format"));
	}

	Ar << VertexArray;
	Ar << VertexInstanceArray;
	Ar << EdgeArray;
	Ar << PolygonArray;
	Ar << PolygonGroupArray;

	Ar << VertexAttributesSet;
	Ar << VertexInstanceAttributesSet;
	Ar << EdgeAttributesSet;
	Ar << PolygonAttributesSet;
	Ar << PolygonGroupAttributesSet;

	// Serialize new triangle arrays since version MeshDescriptionTriangles
	if (!Ar.IsLoading() || Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::MeshDescriptionTriangles)
	{
		Ar << TriangleArray;
		Ar << TriangleAttributesSet;
	}

	if (Ar.IsLoading() && Ar.CustomVer(FReleaseObjectVersion::GUID) >= FReleaseObjectVersion::MeshDescriptionNewSerialization)
	{
		// Populate vertex instance IDs for vertices
		for (const FVertexInstanceID VertexInstanceID : VertexInstanceArray.GetElementIDs())
		{
			const FVertexID VertexID = GetVertexInstanceVertex(VertexInstanceID);
			VertexArray[VertexID].VertexInstanceIDs.Add(VertexInstanceID);
		}

		// Populate edge IDs for vertices
		for (const FEdgeID EdgeID : EdgeArray.GetElementIDs())
		{
			const FVertexID VertexID0 = GetEdgeVertex(EdgeID, 0);
			const FVertexID VertexID1 = GetEdgeVertex(EdgeID, 1);
			VertexArray[VertexID0].ConnectedEdgeIDs.Add(EdgeID);
			VertexArray[VertexID1].ConnectedEdgeIDs.Add(EdgeID);
		}

		if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::MeshDescriptionTriangles)
		{
			// Make reverse connection from polygons to triangles
			for (const FTriangleID TriangleID : TriangleArray.GetElementIDs())
			{
				const FPolygonID PolygonID = TriangleArray[TriangleID].PolygonID;
				PolygonArray[PolygonID].TriangleIDs.Add(TriangleID);
			}
		}

		// Populate polygon IDs for vertex instances, edges and polygon groups
		for (const FPolygonID PolygonID : PolygonArray.GetElementIDs())
		{
			if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::MeshDescriptionTriangles)
			{
				// If the polygon has no contour serialized, copy it over from the triangle
				if (PolygonArray[PolygonID].VertexInstanceIDs.Num() == 0)
				{
					check(PolygonArray[PolygonID].TriangleIDs.Num() == 1);
					const FTriangleID TriangleID = PolygonArray[PolygonID].TriangleIDs[0];
					for (int32 Index = 0; Index < 3; Index++)
					{
						PolygonArray[PolygonID].VertexInstanceIDs.Add(TriangleArray[TriangleID].GetVertexInstanceID(Index));
					}
				}
			}

			const FPolygonGroupID PolygonGroupID = PolygonArray[PolygonID].PolygonGroupID;
			PolygonGroupArray[PolygonGroupID].Polygons.Add(PolygonID);
		}
	}

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::MeshDescriptionTriangles)
		{
			TriangleArray.Reset();

			// If we didn't serialize triangles, generate them from the polygon contour
			for (const FPolygonID PolygonID : PolygonArray.GetElementIDs())
			{
				check(PolygonArray[PolygonID].TriangleIDs.Num() == 0);
				ComputePolygonTriangulation(PolygonID);
			}
		}
		else
		{
			// Otherwise connect existing triangles to vertex instances and edges
			for (const FTriangleID TriangleID : TriangleArray.GetElementIDs())
			{
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const FVertexInstanceID VertexInstanceID = GetTriangleVertexInstance(TriangleID, Index);
					const FVertexInstanceID NextVertexInstanceID = GetTriangleVertexInstance(TriangleID, (Index + 1 == 3) ? 0 : Index + 1);

					const FVertexID VertexID0 = GetVertexInstanceVertex(VertexInstanceID);
					const FVertexID VertexID1 = GetVertexInstanceVertex(NextVertexInstanceID);

					FEdgeID EdgeID = GetVertexPairEdge(VertexID0, VertexID1);
					check(EdgeID != FEdgeID::Invalid);

					VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Add(TriangleID);
					EdgeArray[EdgeID].ConnectedTriangles.Add(TriangleID);
				}
			}
		}
	}
}


void FMeshDescription::Empty()
{
	VertexArray.Reset();
	VertexInstanceArray.Reset();
	EdgeArray.Reset();
	TriangleArray.Reset();
	PolygonArray.Reset();
	PolygonGroupArray.Reset();

	// Empty all attributes
	VertexAttributesSet.Initialize(0);
	VertexInstanceAttributesSet.Initialize(0);
	EdgeAttributesSet.Initialize(0);
	TriangleAttributesSet.Initialize(0);
	PolygonAttributesSet.Initialize(0);
	PolygonGroupAttributesSet.Initialize(0);
}


bool FMeshDescription::IsEmpty() const
{
	return VertexArray.GetArraySize() == 0 &&
		   VertexInstanceArray.GetArraySize() == 0 &&
		   EdgeArray.GetArraySize() == 0 &&
		   TriangleArray.GetArraySize() == 0 &&
		   PolygonArray.GetArraySize() == 0 &&
		   PolygonGroupArray.GetArraySize() == 0;
}


void FMeshDescription::Compact(FElementIDRemappings& OutRemappings)
{
	VertexArray.Compact(OutRemappings.NewVertexIndexLookup);
	VertexInstanceArray.Compact(OutRemappings.NewVertexInstanceIndexLookup);
	EdgeArray.Compact(OutRemappings.NewEdgeIndexLookup);
	TriangleArray.Compact(OutRemappings.NewTriangleIndexLookup);
	PolygonArray.Compact(OutRemappings.NewPolygonIndexLookup);
	PolygonGroupArray.Compact(OutRemappings.NewPolygonGroupIndexLookup);

	RemapAttributes(OutRemappings);
	FixUpElementIDs(OutRemappings);
}


void FMeshDescription::Remap(const FElementIDRemappings& Remappings)
{
	VertexArray.Remap(Remappings.NewVertexIndexLookup);
	VertexInstanceArray.Remap(Remappings.NewVertexInstanceIndexLookup);
	EdgeArray.Remap(Remappings.NewEdgeIndexLookup);
	TriangleArray.Remap(Remappings.NewTriangleIndexLookup);
	PolygonArray.Remap(Remappings.NewPolygonIndexLookup);
	PolygonGroupArray.Remap(Remappings.NewPolygonGroupIndexLookup);

	RemapAttributes(Remappings);
	FixUpElementIDs(Remappings);
}


void FMeshDescription::RemapAttributes(const FElementIDRemappings& Remappings)
{
	VertexAttributesSet.Remap(Remappings.NewVertexIndexLookup);
	VertexInstanceAttributesSet.Remap(Remappings.NewVertexInstanceIndexLookup);
	EdgeAttributesSet.Remap(Remappings.NewEdgeIndexLookup);
	TriangleAttributesSet.Remap(Remappings.NewTriangleIndexLookup);
	PolygonAttributesSet.Remap(Remappings.NewPolygonIndexLookup);
	PolygonGroupAttributesSet.Remap(Remappings.NewPolygonGroupIndexLookup);
}


void FMeshDescription::FixUpElementIDs(const FElementIDRemappings& Remappings)
{
	for (const FVertexID VertexID : VertexArray.GetElementIDs())
	{
		FMeshVertex& Vertex = VertexArray[VertexID];

		// Fix up vertex instance index references in vertices array
		for (FVertexInstanceID& VertexInstanceID : Vertex.VertexInstanceIDs)
		{
			VertexInstanceID = Remappings.GetRemappedVertexInstanceID(VertexInstanceID);
		}

		// Fix up edge index references in the vertex array
		for (FEdgeID& EdgeID : Vertex.ConnectedEdgeIDs)
		{
			EdgeID = Remappings.GetRemappedEdgeID(EdgeID);
		}
	}

	// Fix up vertex index references in vertex instance array
	for (const FVertexInstanceID VertexInstanceID : VertexInstanceArray.GetElementIDs())
	{
		FMeshVertexInstance& VertexInstance = VertexInstanceArray[VertexInstanceID];

		VertexInstance.VertexID = Remappings.GetRemappedVertexID(VertexInstance.VertexID);

		for (FTriangleID& TriangleID : VertexInstance.ConnectedTriangles)
		{
			TriangleID = Remappings.GetRemappedTriangleID(TriangleID);
		}
	}

	for (const FEdgeID EdgeID : EdgeArray.GetElementIDs())
	{
		FMeshEdge& Edge = EdgeArray[EdgeID];

		// Fix up vertex index references in Edges array
		for (int32 Index = 0; Index < 2; Index++)
		{
			Edge.VertexIDs[Index] = Remappings.GetRemappedVertexID(Edge.VertexIDs[Index]);
		}

		for (FTriangleID& TriangleID : Edge.ConnectedTriangles)
		{
			TriangleID = Remappings.GetRemappedTriangleID(TriangleID);
		}
	}

	for (const FTriangleID TriangleID : TriangleArray.GetElementIDs())
	{
		FMeshTriangle& Triangle = TriangleArray[TriangleID];
		
		// Fix up vertex instance references in Triangle
		for (FVertexInstanceID& VertexInstanceID : Triangle.VertexInstanceIDs)
		{
			VertexInstanceID = Remappings.GetRemappedVertexInstanceID(VertexInstanceID);
		}

		Triangle.PolygonID = Remappings.GetRemappedPolygonID(Triangle.PolygonID);
	}

	for (const FPolygonID PolygonID : PolygonArray.GetElementIDs())
	{
		FMeshPolygon& Polygon = PolygonArray[PolygonID];

		// Fix up references to vertex indices in section polygons' contours
		for (FVertexInstanceID& VertexInstanceID : Polygon.VertexInstanceIDs)
		{
			VertexInstanceID = Remappings.GetRemappedVertexInstanceID(VertexInstanceID);
		}

		for (FTriangleID& TriangleID : Polygon.TriangleIDs)
		{
			TriangleID = Remappings.GetRemappedTriangleID(TriangleID);
		}

		Polygon.PolygonGroupID = Remappings.GetRemappedPolygonGroupID(Polygon.PolygonGroupID);
	}

	for (const FPolygonGroupID PolygonGroupID : PolygonGroupArray.GetElementIDs())
	{
		FMeshPolygonGroup& PolygonGroup = PolygonGroupArray[PolygonGroupID];

		for (FPolygonID& Polygon : PolygonGroup.Polygons)
		{
			Polygon = Remappings.GetRemappedPolygonID(Polygon);
		}
	}
}


void FMeshDescription::CreateVertexInstance_Internal(const FVertexInstanceID VertexInstanceID, const FVertexID VertexID)
{
	VertexInstanceArray[VertexInstanceID].VertexID = VertexID;
	check(!VertexArray[VertexID].VertexInstanceIDs.Contains(VertexInstanceID));
	VertexArray[VertexID].VertexInstanceIDs.Add(VertexInstanceID);
	VertexInstanceAttributesSet.Insert(VertexInstanceID);
}


template <template <typename...> class TContainer>
void FMeshDescription::DeleteVertexInstance_Internal(const FVertexInstanceID VertexInstanceID, TContainer<FVertexID>* InOutOrphanedVerticesPtr)
{
	check(VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Num() == 0);
	const FVertexID VertexID = VertexInstanceArray[VertexInstanceID].VertexID;
	verify(VertexArray[VertexID].VertexInstanceIDs.RemoveSingle(VertexInstanceID) == 1);
	if (InOutOrphanedVerticesPtr && VertexArray[VertexID].VertexInstanceIDs.Num() == 0 && VertexArray[VertexID].ConnectedEdgeIDs.Num() == 0)
	{
		AddUnique(*InOutOrphanedVerticesPtr, VertexID);
	}
	VertexInstanceArray.Remove(VertexInstanceID);
	VertexInstanceAttributesSet.Remove(VertexInstanceID);
}

void FMeshDescription::DeleteVertexInstance(const FVertexInstanceID VertexInstanceID, TArray<FVertexID>* InOutOrphanedVerticesPtr)
{
	DeleteVertexInstance_Internal<TArray>(VertexInstanceID, InOutOrphanedVerticesPtr);
}

void FMeshDescription::CreateEdge_Internal(const FEdgeID EdgeID, const FVertexID VertexID0, const FVertexID VertexID1)
{
	check(GetVertexPairEdge(VertexID0, VertexID1) == FEdgeID::Invalid);
	FMeshEdge& Edge = EdgeArray[EdgeID];
	Edge.VertexIDs[0] = VertexID0;
	Edge.VertexIDs[1] = VertexID1;
	VertexArray[VertexID0].ConnectedEdgeIDs.Add(EdgeID);
	VertexArray[VertexID1].ConnectedEdgeIDs.Add(EdgeID);
	EdgeAttributesSet.Insert(EdgeID);
}

template <template <typename...> class TContainer>
void FMeshDescription::DeleteEdge_Internal(const FEdgeID EdgeID, TContainer<FVertexID>* InOutOrphanedVerticesPtr)
{
	FMeshEdge& Edge = EdgeArray[EdgeID];
	for (const FVertexID& EdgeVertexID : Edge.VertexIDs)
	{
		FMeshVertex& Vertex = VertexArray[EdgeVertexID];
		verify(Vertex.ConnectedEdgeIDs.RemoveSingle(EdgeID) == 1);
		if (InOutOrphanedVerticesPtr && Vertex.ConnectedEdgeIDs.Num() == 0)
		{
			check(Vertex.VertexInstanceIDs.Num() == 0);  // We must already have deleted any vertex instances
			AddUnique(*InOutOrphanedVerticesPtr, EdgeVertexID);
		}
	}
	EdgeArray.Remove(EdgeID);
	EdgeAttributesSet.Remove(EdgeID);
}

void FMeshDescription::DeleteEdge(const FEdgeID EdgeID, TArray<FVertexID>* InOutOrphanedVerticesPtr)
{
	DeleteEdge_Internal<TArray>(EdgeID, InOutOrphanedVerticesPtr);
}

void FMeshDescription::CreateTriangle_Internal(const FTriangleID TriangleID, const FPolygonGroupID PolygonGroupID, TArrayView<const FVertexInstanceID> VertexInstanceIDs, TArray<FEdgeID>* OutEdgeIDs)
{
	if (OutEdgeIDs)
	{
		OutEdgeIDs->Empty();
	}

	// Fill out triangle vertex instances
	FMeshTriangle& Triangle = TriangleArray[TriangleID];
	check(VertexInstanceIDs.Num() == 3);
	Triangle.SetVertexInstanceID(0, VertexInstanceIDs[0]);
	Triangle.SetVertexInstanceID(1, VertexInstanceIDs[1]);
	Triangle.SetVertexInstanceID(2, VertexInstanceIDs[2]);

	// Make a polygon which will contain this triangle
	FPolygonID PolygonID = PolygonArray.Add();
	FMeshPolygon& Polygon = PolygonArray[PolygonID];
	PolygonAttributesSet.Insert(PolygonID);

	Polygon.VertexInstanceIDs.Reserve(3);
	Algo::Copy(VertexInstanceIDs, Polygon.VertexInstanceIDs);
	Polygon.PolygonGroupID = PolygonGroupID;
	PolygonGroupArray[PolygonGroupID].Polygons.Add(PolygonID);

	Triangle.PolygonID = PolygonID;
	check(!Polygon.TriangleIDs.Contains(TriangleID));
	Polygon.TriangleIDs.Add(TriangleID);

	TriangleAttributesSet.Insert(TriangleID);

	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(Index);
		const FVertexInstanceID NextVertexInstanceID = Triangle.GetVertexInstanceID((Index == 2) ? 0 : Index + 1);

		const FVertexID ThisVertexID = GetVertexInstanceVertex(VertexInstanceID);
		const FVertexID NextVertexID = GetVertexInstanceVertex(NextVertexInstanceID);

		FEdgeID EdgeID = GetVertexPairEdge(ThisVertexID, NextVertexID);
		if (EdgeID == FEdgeID::Invalid)
		{
			EdgeID = CreateEdge(ThisVertexID, NextVertexID);
			if (OutEdgeIDs)
			{
				OutEdgeIDs->Add(EdgeID);
			}
		}

		check(!VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Contains(TriangleID));
		VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Add(TriangleID);

		check(!EdgeArray[EdgeID].ConnectedTriangles.Contains(TriangleID));
		EdgeArray[EdgeID].ConnectedTriangles.Add(TriangleID);
	}
}

template <template <typename...> class TContainer>
void FMeshDescription::DeleteTriangle_Internal(const FTriangleID TriangleID, TContainer<FEdgeID>* InOutOrphanedEdgesPtr, TContainer<FVertexInstanceID>* InOutOrphanedVertexInstancesPtr, TContainer<FPolygonGroupID>* InOutOrphanedPolygonGroupsPtr)
{
	const FMeshTriangle& Triangle = TriangleArray[TriangleID];
	const FPolygonID PolygonID = Triangle.PolygonID;
	
	// Delete this triangle from the polygon
	verify(PolygonArray[PolygonID].TriangleIDs.RemoveSingle(TriangleID) == 1);

	if (PolygonArray[PolygonID].TriangleIDs.Num() == 0)
	{
		// If it was the only triangle in the polygon, delete the polygon too
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVertexInstanceID VertexInstanceID = Triangle.GetVertexInstanceID(Index);
			const FVertexInstanceID NextVertexInstanceID = Triangle.GetVertexInstanceID((Index == 2) ? 0 : Index + 1);

			const FVertexID VertexID0 = GetVertexInstanceVertex(VertexInstanceID);
			const FVertexID VertexID1 = GetVertexInstanceVertex(NextVertexInstanceID);

			FEdgeID EdgeID = GetVertexPairEdge(VertexID0, VertexID1);
			check(EdgeID != FEdgeID::Invalid);

			verify(VertexInstanceArray[VertexInstanceID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);
			verify(EdgeArray[EdgeID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);

			if (InOutOrphanedVertexInstancesPtr && VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Num() == 0)
			{
				AddUnique(*InOutOrphanedVertexInstancesPtr, VertexInstanceID);
			}

			if (InOutOrphanedEdgesPtr && EdgeArray[EdgeID].ConnectedTriangles.Num() == 0)
			{
				AddUnique(*InOutOrphanedEdgesPtr, EdgeID);
			}
		}

		// Remove the polygon
		const FPolygonGroupID PolygonGroupID = PolygonArray[PolygonID].PolygonGroupID;
		verify(PolygonGroupArray[PolygonGroupID].Polygons.RemoveSingle(PolygonID) == 1);

		if (InOutOrphanedPolygonGroupsPtr && PolygonGroupArray[PolygonGroupID].Polygons.Num() == 0)
		{
			AddUnique(*InOutOrphanedPolygonGroupsPtr, PolygonGroupID);
		}

		PolygonArray.Remove(PolygonID);
		PolygonAttributesSet.Remove(PolygonID);
	}
	else
	{
		// @todo: Handle this properly when deleting a triangle which forms part of an n-gon
		// Either it needs to shave off the triangle from the contour and update the contour vertex instances,
		// or it should just refuse to delete the triangle.
		check(false);
	}

	TriangleArray.Remove(TriangleID);
	TriangleAttributesSet.Remove(TriangleID);
}

void FMeshDescription::DeleteTriangle(const FTriangleID TriangleID, TArray<FEdgeID>* InOutOrphanedEdgesPtr, TArray<FVertexInstanceID>* InOutOrphanedVertexInstancesPtr, TArray<FPolygonGroupID>* InOutOrphanedPolygonGroupsPtr)
{
	DeleteTriangle_Internal<TArray>(TriangleID, InOutOrphanedEdgesPtr, InOutOrphanedVertexInstancesPtr, InOutOrphanedPolygonGroupsPtr);
}

void FMeshDescription::DeleteTriangles(const TArray<FTriangleID>& Triangles)
{
	TSet<FEdgeID> OrphanedEdges;
	TSet<FVertexInstanceID> OrphanedVertexInstances;
	TSet<FPolygonGroupID> OrphanedPolygonGroups;
	TSet<FVertexID> OrphanedVertices;

	for (FTriangleID TriangleID : Triangles)
	{
		DeleteTriangle_Internal<TSet>(TriangleID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
	}
	for (FPolygonGroupID PolygonGroupID : OrphanedPolygonGroups)
	{
		DeletePolygonGroup(PolygonGroupID);
	}
	for (FVertexInstanceID VertexInstanceID : OrphanedVertexInstances)
	{
		DeleteVertexInstance_Internal<TSet>(VertexInstanceID, &OrphanedVertices);
	}
	for (FEdgeID EdgeID : OrphanedEdges)
	{
		DeleteEdge_Internal<TSet>(EdgeID, &OrphanedVertices);
	}
	for (FVertexID VertexID : OrphanedVertices)
	{
		DeleteVertex(VertexID);
	}
}

void FMeshDescription::CreatePolygon_Internal(const FPolygonID PolygonID, const FPolygonGroupID PolygonGroupID, TArrayView<const FVertexInstanceID> VertexInstanceIDs, TArray<FEdgeID>* OutEdgeIDs)
{
	if (OutEdgeIDs)
	{
		OutEdgeIDs->Empty();
	}

	FMeshPolygon& Polygon = PolygonArray[PolygonID];
	const int32 NumVertices = VertexInstanceIDs.Num();
	Polygon.VertexInstanceIDs.SetNumUninitialized(NumVertices);

	for (int32 Index = 0; Index < NumVertices; ++Index)
	{
		const FVertexInstanceID ThisVertexInstanceID = VertexInstanceIDs[Index];
		const FVertexInstanceID NextVertexInstanceID = VertexInstanceIDs[(Index + 1 == NumVertices) ? 0 : Index + 1];
		const FVertexID ThisVertexID = GetVertexInstanceVertex(ThisVertexInstanceID);
		const FVertexID NextVertexID = GetVertexInstanceVertex(NextVertexInstanceID);

		Polygon.VertexInstanceIDs[Index] = ThisVertexInstanceID;

		FEdgeID EdgeID = GetVertexPairEdge(ThisVertexID, NextVertexID);
		if (EdgeID == FEdgeID::Invalid)
		{
			EdgeID = CreateEdge(ThisVertexID, NextVertexID);
			if (OutEdgeIDs)
			{
				OutEdgeIDs->Add(EdgeID);
			}
		}
	}

	check(PolygonGroupID != FPolygonGroupID::Invalid);
	Polygon.PolygonGroupID = PolygonGroupID;
	PolygonGroupArray[PolygonGroupID].Polygons.Add(PolygonID);

	check(Polygon.TriangleIDs.Num() == 0);
	ComputePolygonTriangulation(PolygonID);

	PolygonAttributesSet.Insert(PolygonID);
}

template <template <typename...> class TContainer>
void FMeshDescription::DeletePolygon_Internal(const FPolygonID PolygonID, TContainer<FEdgeID>* InOutOrphanedEdgesPtr, TContainer<FVertexInstanceID>* InOutOrphanedVertexInstancesPtr, TContainer<FPolygonGroupID>* InOutOrphanedPolygonGroupsPtr)
{
	FMeshPolygon& Polygon = PolygonArray[PolygonID];

	// Remove constituent triangles
	for (const FTriangleID& TriangleID : Polygon.TriangleIDs)
	{
		const FMeshTriangle& Triangle = TriangleArray[TriangleID];

		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVertexInstanceID ThisVertexInstanceID = Triangle.GetVertexInstanceID(Index);
			const FVertexInstanceID NextVertexInstanceID = Triangle.GetVertexInstanceID((Index == 2) ? 0 : Index + 1);
			const FVertexID ThisVertexID = GetVertexInstanceVertex(ThisVertexInstanceID);
			const FVertexID NextVertexID = GetVertexInstanceVertex(NextVertexInstanceID);
			const FEdgeID EdgeID = GetVertexPairEdge(ThisVertexID, NextVertexID);

			// If a valid edge isn't found, we deem this to be because it's an internal edge which was already removed
			// in a previous iteration through the triangle array.
			if (EdgeID != FEdgeID::Invalid)
			{
				if (IsEdgeInternal(EdgeID))
				{
					// Remove internal edges
					for (const FVertexID& EdgeVertexID : EdgeArray[EdgeID].VertexIDs)
					{
						verify(VertexArray[EdgeVertexID].ConnectedEdgeIDs.RemoveSingle(EdgeID) == 1);
					}
					EdgeArray.Remove(EdgeID);
					EdgeAttributesSet.Remove(EdgeID);
				}
				else
				{
					verify(EdgeArray[EdgeID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);

					if (InOutOrphanedEdgesPtr && EdgeArray[EdgeID].ConnectedTriangles.Num() == 0)
					{
						AddUnique(*InOutOrphanedEdgesPtr, EdgeID);
					}
				}
			}

			verify(VertexInstanceArray[ThisVertexInstanceID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);

			if (InOutOrphanedVertexInstancesPtr && VertexInstanceArray[ThisVertexInstanceID].ConnectedTriangles.Num() == 0)
			{
				AddUnique(*InOutOrphanedVertexInstancesPtr, ThisVertexInstanceID);
			}
		}

		TriangleArray.Remove(TriangleID);
		TriangleAttributesSet.Remove(TriangleID);
	}

	FMeshPolygonGroup& PolygonGroup = PolygonGroupArray[Polygon.PolygonGroupID];
	verify(PolygonGroup.Polygons.RemoveSingle(PolygonID) == 1);

	if (InOutOrphanedPolygonGroupsPtr && PolygonGroup.Polygons.Num() == 0)
	{
		AddUnique(*InOutOrphanedPolygonGroupsPtr, Polygon.PolygonGroupID);
	}

	PolygonArray.Remove(PolygonID);
	PolygonAttributesSet.Remove(PolygonID);
}

void FMeshDescription::DeletePolygon(const FPolygonID PolygonID, TArray<FEdgeID>* InOutOrphanedEdgesPtr, TArray<FVertexInstanceID>* InOutOrphanedVertexInstancesPtr, TArray<FPolygonGroupID>* InOutOrphanedPolygonGroupsPtr)
{
	DeletePolygon_Internal<TArray>(PolygonID, InOutOrphanedEdgesPtr, InOutOrphanedVertexInstancesPtr, InOutOrphanedPolygonGroupsPtr);
}

void FMeshDescription::DeletePolygons(const TArray<FPolygonID>& Polygons)
{
	TSet<FEdgeID> OrphanedEdges;
	TSet<FVertexInstanceID> OrphanedVertexInstances;
	TSet<FPolygonGroupID> OrphanedPolygonGroups;
	TSet<FVertexID> OrphanedVertices;

	for (FPolygonID PolygonID : Polygons)
	{
		DeletePolygon_Internal<TSet>(PolygonID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
	}
	for (FPolygonGroupID PolygonGroupID : OrphanedPolygonGroups)
	{
		DeletePolygonGroup(PolygonGroupID);
	}
	for (FVertexInstanceID VertexInstanceID : OrphanedVertexInstances)
	{
		DeleteVertexInstance_Internal<TSet>(VertexInstanceID, &OrphanedVertices);
	}
	for (FEdgeID EdgeID : OrphanedEdges)
	{
		DeleteEdge_Internal<TSet>(EdgeID, &OrphanedVertices);
	}
	for (FVertexID VertexID : OrphanedVertices)
	{
		DeleteVertex(VertexID);
	}
}

bool FMeshDescription::IsVertexOrphaned(const FVertexID VertexID) const
{
	for (const FVertexInstanceID& VertexInstanceID : VertexArray[VertexID].VertexInstanceIDs)
	{
		if (VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Num() > 0)
		{
			return false;
		}
	}

	return true;
}


FEdgeID FMeshDescription::GetVertexPairEdge(const FVertexID VertexID0, const FVertexID VertexID1) const
{
	for (const FEdgeID& VertexConnectedEdgeID : VertexArray[VertexID0].ConnectedEdgeIDs)
	{
		const FVertexID EdgeVertexID0 = EdgeArray[VertexConnectedEdgeID].VertexIDs[0];
		const FVertexID EdgeVertexID1 = EdgeArray[VertexConnectedEdgeID].VertexIDs[1];
		if ((EdgeVertexID0 == VertexID0 && EdgeVertexID1 == VertexID1) || (EdgeVertexID0 == VertexID1 && EdgeVertexID1 == VertexID0))
		{
			return VertexConnectedEdgeID;
		}
	}

	return FEdgeID::Invalid;
}


FEdgeID FMeshDescription::GetVertexInstancePairEdge(const FVertexInstanceID VertexInstanceID0, const FVertexInstanceID VertexInstanceID1) const
{
	const FVertexID VertexID0 = VertexInstanceArray[VertexInstanceID0].VertexID;
	const FVertexID VertexID1 = VertexInstanceArray[VertexInstanceID1].VertexID;
	for (const FEdgeID& VertexConnectedEdgeID : VertexArray[VertexID0].ConnectedEdgeIDs)
	{
		const FVertexID EdgeVertexID0 = EdgeArray[VertexConnectedEdgeID].VertexIDs[0];
		const FVertexID EdgeVertexID1 = EdgeArray[VertexConnectedEdgeID].VertexIDs[1];
		if ((EdgeVertexID0 == VertexID0 && EdgeVertexID1 == VertexID1) || (EdgeVertexID0 == VertexID1 && EdgeVertexID1 == VertexID0))
		{
			return VertexConnectedEdgeID;
		}
	}

	return FEdgeID::Invalid;
}


void FMeshDescription::SetPolygonVertexInstance(const FPolygonID PolygonID, const int32 PerimeterIndex, const FVertexInstanceID VertexInstanceID)
{
	FMeshPolygon& Polygon = PolygonArray[PolygonID];
	check(PerimeterIndex >= 0 && PerimeterIndex < Polygon.VertexInstanceIDs.Num());

	// Disconnect old vertex instance from polygon, and connect new one
	const FVertexInstanceID OldVertexInstanceID = Polygon.VertexInstanceIDs[PerimeterIndex];

	Polygon.VertexInstanceIDs[PerimeterIndex] = VertexInstanceID;

	// Fix up triangle list
	for (const FTriangleID& TriangleID : Polygon.TriangleIDs)
	{
		FMeshTriangle& Triangle = TriangleArray[TriangleID];
		for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
		{
			if (Triangle.GetVertexInstanceID(VertexIndex) == OldVertexInstanceID)
			{
				verify(VertexInstanceArray[OldVertexInstanceID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);
				check(!VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Contains(TriangleID));
				VertexInstanceArray[VertexInstanceID].ConnectedTriangles.Add(TriangleID);
				Triangle.SetVertexInstanceID(VertexIndex, VertexInstanceID);
			}
		}
	}
}


FPlane FMeshDescription::ComputePolygonPlane( const FPolygonID PolygonID ) const
{
	// NOTE: This polygon plane computation code is partially based on the implementation of "Newell's method" from Real-Time
	//       Collision Detection by Christer Ericson, published by Morgan Kaufmann Publishers, (c) 2005 Elsevier Inc

	// @todo mesheditor perf: For polygons that are just triangles, use a cross product to get the normal fast!
	// @todo mesheditor perf: We could skip computing the plane distance when we only need the normal
	// @todo mesheditor perf: We could cache these computed polygon normals; or just use the normal of the first three vertices' triangle if it is satisfactory in all cases
	// @todo mesheditor: For non-planar polygons, the result can vary. Ideally this should use the actual polygon triangulation as opposed to the arbitrary triangulation used here.

	FVector Centroid = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;

	TArray<FVertexID> PerimeterVertexIDs;
	GetPolygonVertices( PolygonID, /* Out */ PerimeterVertexIDs );

	// @todo Maybe this shouldn't be in FMeshDescription but in a utility class, as it references a specific attribute name
	TVertexAttributesConstRef<FVector> VertexPositions = VertexAttributes().GetAttributesRef<FVector>( MeshAttribute::Vertex::Position );

	// Use 'Newell's Method' to compute a robust 'best fit' plane from the vertices of this polygon
	for ( int32 VertexNumberI = PerimeterVertexIDs.Num() - 1, VertexNumberJ = 0; VertexNumberJ < PerimeterVertexIDs.Num(); VertexNumberI = VertexNumberJ, VertexNumberJ++ )
	{
		const FVertexID VertexIDI = PerimeterVertexIDs[ VertexNumberI ];
		const FVector PositionI = VertexPositions[ VertexIDI ];

		const FVertexID VertexIDJ = PerimeterVertexIDs[ VertexNumberJ ];
		const FVector PositionJ = VertexPositions[ VertexIDJ ];

		Centroid += PositionJ;

		Normal.X += ( PositionJ.Y - PositionI.Y ) * ( PositionI.Z + PositionJ.Z );
		Normal.Y += ( PositionJ.Z - PositionI.Z ) * ( PositionI.X + PositionJ.X );
		Normal.Z += ( PositionJ.X - PositionI.X ) * ( PositionI.Y + PositionJ.Y );
	}

	Normal.Normalize();

	// Construct a plane from the normal and centroid
	return FPlane( Normal, FVector::DotProduct( Centroid, Normal ) / ( float )PerimeterVertexIDs.Num() );
}


FVector FMeshDescription::ComputePolygonNormal( const FPolygonID PolygonID ) const
{
	// @todo mesheditor: Polygon normals are now computed and cached when changes are made to a polygon.
	// In theory, we can just return that cached value, but we need to check that there is nothing which relies on the value being correct before
	// the cache is updated at the end of a modification.
	const FPlane PolygonPlane = ComputePolygonPlane( PolygonID );
	const FVector PolygonNormal( PolygonPlane.X, PolygonPlane.Y, PolygonPlane.Z );
	return PolygonNormal;
}


/** Returns true if the triangle formed by the specified three positions has a normal that is facing the opposite direction of the reference normal */
static bool IsTriangleFlipped(const FVector ReferenceNormal, const FVector VertexPositionA, const FVector VertexPositionB, const FVector VertexPositionC)
{
	const FVector TriangleNormal = FVector::CrossProduct(
		VertexPositionC - VertexPositionA,
		VertexPositionB - VertexPositionA).GetSafeNormal();
	return (FVector::DotProduct(ReferenceNormal, TriangleNormal) <= 0.0f);
}


/** Given three direction vectors, indicates if A and B are on the same 'side' of Vec. */
static bool VectorsOnSameSide(const FVector& Vec, const FVector& A, const FVector& B, const float SameSideDotProductEpsilon)
{
	const FVector CrossA = FVector::CrossProduct(Vec, A);
	const FVector CrossB = FVector::CrossProduct(Vec, B);
	float DotWithEpsilon = SameSideDotProductEpsilon + FVector::DotProduct(CrossA, CrossB);
	return !FMath::IsNegativeFloat(DotWithEpsilon);
}


/** Util to see if P lies within triangle created by A, B and C. */
static bool PointInTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector& P, const float InsideTriangleDotProductEpsilon)
{
	// Cross product indicates which 'side' of the vector the point is on
	// If its on the same side as the remaining vert for all edges, then its inside.	
	return (VectorsOnSameSide(B - A, P - A, C - A, InsideTriangleDotProductEpsilon) &&
			VectorsOnSameSide(C - B, P - B, A - B, InsideTriangleDotProductEpsilon) &&
			VectorsOnSameSide(A - C, P - C, B - C, InsideTriangleDotProductEpsilon));
}


void FMeshDescription::ComputePolygonTriangulation(const FPolygonID PolygonID)
{
	// NOTE: This polygon triangulation code is partially based on the ear cutting algorithm described on
	//       page 497 of the book "Real-time Collision Detection", published in 2005.

	FMeshPolygon& Polygon = PolygonArray[PolygonID];
	const TArray<FVertexInstanceID>& PolygonVertexInstanceIDs = Polygon.VertexInstanceIDs;

	// Polygon must have at least three vertices/edges
	const int32 PolygonVertexCount = PolygonVertexInstanceIDs.Num();
	check(PolygonVertexCount >= 3);

	// If polygon was already triangulated, and only has three vertices, no need to do anything here
	if (Polygon.TriangleIDs.Num() == 1 && PolygonVertexCount == 3)
	{
		return;
	}

	// Remove currently configured triangles
	for (const FTriangleID& TriangleID : Polygon.TriangleIDs)
	{
		// Disconnect triangles from vertex instances
		for (const FVertexInstanceID& VertexInstanceID : GetTriangleVertexInstances(TriangleID))
		{
			verify(VertexInstanceArray[VertexInstanceID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);
		}

		// Disconnect triangles from perimeter edges, and delete internal edges
		for (const FEdgeID& EdgeID : GetTriangleEdges(TriangleID))
		{
			if (EdgeID != FEdgeID::Invalid)
			{
				// The edge may be invalid if it was an internal edge which was deleted in a previous iteration through the triangles.
				// So only do something with valid edges
				if (IsEdgeInternal(EdgeID))
				{
					// Remove internal edges completely (the first time they are seen)
					for (const FVertexID& VertexID : GetEdgeVertices(EdgeID))
					{
						// Disconnect edge from vertices
						verify(VertexArray[VertexID].ConnectedEdgeIDs.RemoveSingle(EdgeID) == 1);
					}

					EdgeArray.Remove(EdgeID);
					EdgeAttributesSet.Remove(EdgeID);
				}
				else
				{
					// Don't remove perimeter edge, but disconnect this triangle from it
					verify(EdgeArray[EdgeID].ConnectedTriangles.RemoveSingle(TriangleID) == 1);
				}
			}
		}

		TriangleArray.Remove(TriangleID);
		TriangleAttributesSet.Remove(TriangleID);
	}

	Polygon.TriangleIDs.Reset();

	// If perimeter only has 3 vertices, just add a single triangle and return
	if (PolygonVertexCount == 3)
	{
		const FTriangleID TriangleID = TriangleArray.Add();
		TriangleAttributesSet.Insert(TriangleID);
		
		FMeshTriangle& Triangle = TriangleArray[TriangleID];
		Triangle.PolygonID = PolygonID;
		Polygon.TriangleIDs.Add(TriangleID);

		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVertexInstanceID ThisVertexInstanceID = PolygonVertexInstanceIDs[Index];
			const FVertexInstanceID NextVertexInstanceID = PolygonVertexInstanceIDs[(Index == 2) ? 0 : Index + 1];
			const FVertexID ThisVertexID = GetVertexInstanceVertex(ThisVertexInstanceID);
			const FVertexID NextVertexID = GetVertexInstanceVertex(NextVertexInstanceID);
			const FEdgeID EdgeID = GetVertexPairEdge(ThisVertexID, NextVertexID);
			check(EdgeID != FEdgeID::Invalid);

			Triangle.SetVertexInstanceID(Index, ThisVertexInstanceID);

			check(!EdgeArray[EdgeID].ConnectedTriangles.Contains(TriangleID));
			EdgeArray[EdgeID].ConnectedTriangles.Add(TriangleID);

			check(!VertexInstanceArray[ThisVertexInstanceID].ConnectedTriangles.Contains(TriangleID));
			VertexInstanceArray[ThisVertexInstanceID].ConnectedTriangles.Add(TriangleID);
		}

		return;
	}

	// @todo mesheditor: Perhaps should always attempt to triangulate by splitting polygons along the shortest edge, for better determinism.

	// First figure out the polygon normal.  We need this to determine which triangles are convex, so that
	// we can figure out which ears to clip
	const FVector PolygonNormal = ComputePolygonNormal(PolygonID);

	// Make a simple linked list array of the previous and next vertex numbers, for each vertex number
	// in the polygon.  This will just save us having to iterate later on.
	TArray<int32> PrevVertexNumbers;
	TArray<int32> NextVertexNumbers;
	TArray<FVector> VertexPositions;

	{
		const TVertexAttributesRef<FVector> MeshVertexPositions = VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		PrevVertexNumbers.SetNumUninitialized(PolygonVertexCount, false);
		NextVertexNumbers.SetNumUninitialized(PolygonVertexCount, false);
		VertexPositions.SetNumUninitialized(PolygonVertexCount, false);

		for (int32 VertexNumber = 0; VertexNumber < PolygonVertexCount; ++VertexNumber)
		{
			PrevVertexNumbers[VertexNumber] = VertexNumber - 1;
			NextVertexNumbers[VertexNumber] = VertexNumber + 1;

			VertexPositions[VertexNumber] = MeshVertexPositions[GetVertexInstanceVertex(PolygonVertexInstanceIDs[VertexNumber])];
		}
		PrevVertexNumbers[0] = PolygonVertexCount - 1;
		NextVertexNumbers[PolygonVertexCount - 1] = 0;
	}

	int32 EarVertexNumber = 0;
	int32 EarTestCount = 0;
	for (int32 RemainingVertexCount = PolygonVertexCount; RemainingVertexCount >= 3; )
	{
		bool bIsEar = true;

		// If we're down to only a triangle, just treat it as an ear.  Also, if we've tried every possible candidate
		// vertex looking for an ear, go ahead and just treat the current vertex as an ear.  This can happen when 
		// vertices are colinear or other degenerate cases.
		if (RemainingVertexCount > 3 && EarTestCount < RemainingVertexCount)
		{
			const FVector PrevVertexPosition = VertexPositions[PrevVertexNumbers[EarVertexNumber]];
			const FVector EarVertexPosition = VertexPositions[EarVertexNumber];
			const FVector NextVertexPosition = VertexPositions[NextVertexNumbers[EarVertexNumber]];

			// Figure out whether the potential ear triangle is facing the same direction as the polygon
			// itself.  If it's facing the opposite direction, then we're dealing with a concave triangle
			// and we'll skip it for now.
			if (!IsTriangleFlipped(PolygonNormal, PrevVertexPosition, EarVertexPosition, NextVertexPosition))
			{
				int32 TestVertexNumber = NextVertexNumbers[NextVertexNumbers[EarVertexNumber]];

				do
				{
					// Test every other remaining vertex to make sure that it doesn't lie inside our potential ear
					// triangle.  If we find a vertex that's inside the triangle, then it cannot actually be an ear.
					const FVector TestVertexPosition = VertexPositions[TestVertexNumber];
					if (PointInTriangle(PrevVertexPosition, EarVertexPosition, NextVertexPosition, TestVertexPosition, SMALL_NUMBER))
					{
						bIsEar = false;
						break;
					}

					TestVertexNumber = NextVertexNumbers[TestVertexNumber];

				} while (TestVertexNumber != PrevVertexNumbers[EarVertexNumber]);
			}
			else
			{
				bIsEar = false;
			}
		}

		if (bIsEar)
		{
			// OK, we found an ear!  Let's save this triangle in our output buffer.
			// This will also create any missing internal edges.
			{
				// Add a new triangle
				const FTriangleID TriangleID = TriangleArray.Add();
				TriangleAttributesSet.Insert(TriangleID);

				// Set its vertex instances and connect it to its parent polygon
				FMeshTriangle& Triangle = TriangleArray[TriangleID];
				Triangle.SetVertexInstanceID(0, PolygonVertexInstanceIDs[PrevVertexNumbers[EarVertexNumber]]);
				Triangle.SetVertexInstanceID(1, PolygonVertexInstanceIDs[EarVertexNumber]);
				Triangle.SetVertexInstanceID(2, PolygonVertexInstanceIDs[NextVertexNumbers[EarVertexNumber]]);
				Triangle.PolygonID = PolygonID;
				check(!Polygon.TriangleIDs.Contains(TriangleID));
				Polygon.TriangleIDs.Add(TriangleID);

				// Now generate internal edges and connected vertex instances to the new triangle
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const FVertexInstanceID ThisVertexInstanceID = Triangle.GetVertexInstanceID(Index);
					const FVertexInstanceID NextVertexInstanceID = Triangle.GetVertexInstanceID((Index == 2) ? 0 : Index + 1);
					const FVertexID ThisVertexID = GetVertexInstanceVertex(ThisVertexInstanceID);
					const FVertexID NextVertexID = GetVertexInstanceVertex(NextVertexInstanceID);
					FEdgeID EdgeID = GetVertexPairEdge(ThisVertexID, NextVertexID);
					if (EdgeID == FEdgeID::Invalid)
					{
						// This must be an internal edge (as perimeter edges will already be defined)
						EdgeID = CreateEdge(ThisVertexID, NextVertexID);
					}

					check(!VertexInstanceArray[ThisVertexInstanceID].ConnectedTriangles.Contains(TriangleID));
					VertexInstanceArray[ThisVertexInstanceID].ConnectedTriangles.Add(TriangleID);

					check(!EdgeArray[EdgeID].ConnectedTriangles.Contains(TriangleID));
					EdgeArray[EdgeID].ConnectedTriangles.Add(TriangleID);
				}
			}

			// Update our linked list.  We're effectively cutting off the ear by pointing the ear vertex's neighbors to
			// point at their next sequential neighbor, and reducing the remaining vertex count by one.
			{
				NextVertexNumbers[PrevVertexNumbers[EarVertexNumber]] = NextVertexNumbers[EarVertexNumber];
				PrevVertexNumbers[NextVertexNumbers[EarVertexNumber]] = PrevVertexNumbers[EarVertexNumber];
				--RemainingVertexCount;
			}

			// Move on to the previous vertex in the list, now that this vertex was cut
			EarVertexNumber = PrevVertexNumbers[EarVertexNumber];

			EarTestCount = 0;
		}
		else
		{
			// The vertex is not the ear vertex, because it formed a triangle that either had a normal which pointed in the opposite direction
			// of the polygon, or at least one of the other polygon vertices was found to be inside the triangle.  Move on to the next vertex.
			EarVertexNumber = NextVertexNumbers[EarVertexNumber];

			// Keep track of how many ear vertices we've tested, so that if we exhaust all remaining vertices, we can
			// fall back to clipping the triangle and adding it to our mesh anyway.  This is important for degenerate cases.
			++EarTestCount;
		}
	}

	check(Polygon.TriangleIDs.Num() > 0);
}


FBoxSphereBounds FMeshDescription::GetBounds() const
{
	FBoxSphereBounds BoundingBoxAndSphere;

	FBox BoundingBox;
	BoundingBox.Init();

	TVertexAttributesConstRef<FVector> VertexPositions = VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	for (const FVertexID VertexID : Vertices().GetElementIDs())
	{
		if (!IsVertexOrphaned(VertexID))
		{
			BoundingBox += VertexPositions[VertexID];
		}
	}

	BoundingBox.GetCenterAndExtents(BoundingBoxAndSphere.Origin, BoundingBoxAndSphere.BoxExtent);

	// Calculate the bounding sphere, using the center of the bounding box as the origin.
	BoundingBoxAndSphere.SphereRadius = 0.0f;

	for (const FVertexID VertexID : Vertices().GetElementIDs())
	{
		if (!IsVertexOrphaned(VertexID))
		{
			BoundingBoxAndSphere.SphereRadius = FMath::Max((VertexPositions[VertexID] - BoundingBoxAndSphere.Origin).Size(), BoundingBoxAndSphere.SphereRadius);
		}
	}

	return BoundingBoxAndSphere;
}

void FMeshDescription::TriangulateMesh()
{
	// Perform triangulation directly into mesh polygons
	for( const FPolygonID PolygonID : Polygons().GetElementIDs() )
	{
		ComputePolygonTriangulation( PolygonID );
	}
}


namespace MeshAttribute_
{
	namespace Vertex
	{
		const FName CornerSharpness("CornerSharpness");
	}

	namespace VertexInstance
	{
		const FName TextureCoordinate("TextureCoordinate");
		const FName Normal("Normal");
		const FName Tangent("Tangent");
		const FName BinormalSign("BinormalSign");
		const FName Color("Color");
	}

	namespace Edge
	{
		const FName IsHard("IsHard");
		const FName IsUVSeam("IsUVSeam");
		const FName CreaseSharpness("CreaseSharpness");
	}

	namespace Polygon
	{
		const FName Normal("Normal");
		const FName Tangent("Tangent");
		const FName Binormal("Binormal");
		const FName Center("Center");
	}

	namespace PolygonGroup
	{
		const FName ImportedMaterialSlotName("ImportedMaterialSlotName");
		const FName EnableCollision("EnableCollision");
		const FName CastShadow("CastShadow");
	}
}


float FMeshDescription::GetPolygonCornerAngleForVertex(const FPolygonID PolygonID, const FVertexID VertexID) const
{
	const FMeshPolygon& Polygon = PolygonArray[PolygonID];

	// Lambda function which returns the inner angle at a given index on a polygon contour
	auto GetContourAngle = [this](const TArray<FVertexInstanceID>& VertexInstanceIDs, const int32 ContourIndex)
	{
		const int32 NumVertices = VertexInstanceIDs.Num();

		const int32 PrevIndex = (ContourIndex + NumVertices - 1) % NumVertices;
		const int32 NextIndex = (ContourIndex + 1) % NumVertices;

		const FVertexID PrevVertexID = GetVertexInstanceVertex(VertexInstanceIDs[PrevIndex]);
		const FVertexID ThisVertexID = GetVertexInstanceVertex(VertexInstanceIDs[ContourIndex]);
		const FVertexID NextVertexID = GetVertexInstanceVertex(VertexInstanceIDs[NextIndex]);

		TVertexAttributesConstRef<FVector> VertexPositions = VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

		const FVector PrevVertexPosition = VertexPositions[PrevVertexID];
		const FVector ThisVertexPosition = VertexPositions[ThisVertexID];
		const FVector NextVertexPosition = VertexPositions[NextVertexID];

		const FVector Direction1 = (PrevVertexPosition - ThisVertexPosition).GetSafeNormal();
		const FVector Direction2 = (NextVertexPosition - ThisVertexPosition).GetSafeNormal();

		return FMath::Acos(FVector::DotProduct(Direction1, Direction2));
	};

	const FVertexInstanceArray& VertexInstancesRef = VertexInstances();
	auto IsVertexInstancedFromThisVertex = [&VertexInstancesRef, VertexID](const FVertexInstanceID VertexInstanceID)
	{
		return VertexInstancesRef[VertexInstanceID].VertexID == VertexID;
	};

	// First look for the vertex instance in the perimeter
	int32 ContourIndex = Polygon.VertexInstanceIDs.IndexOfByPredicate(IsVertexInstancedFromThisVertex);
	if (ContourIndex != INDEX_NONE)
	{
		// Return the internal angle if found
		return GetContourAngle(Polygon.VertexInstanceIDs, ContourIndex);
	}

	// Found nothing; return 0
	return 0.0f;
}

FBox FMeshDescription::ComputeBoundingBox() const
{
	FBox BoundingBox(ForceInit);

	TVertexAttributesConstRef<FVector> VertexPositions = VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	for (const FVertexID VertexID : Vertices().GetElementIDs())
	{
		BoundingBox += VertexPositions[VertexID];
	}

	return BoundingBox;
}


void FMeshDescription::ReversePolygonFacing(const FPolygonID PolygonID)
{
	// Build a reverse perimeter
	FMeshPolygon& Polygon = PolygonArray[PolygonID];
	for (int32 i = 0; i < Polygon.VertexInstanceIDs.Num() / 2; ++i)
	{
		Polygon.VertexInstanceIDs.Swap(i, Polygon.VertexInstanceIDs.Num() - i - 1);
	}

	// Update the polygon's triangle vertex instance ids with the reversed ids
	for (FTriangleID TriangleID : GetPolygonTriangleIDs(PolygonID))
	{
		FMeshTriangle& Triangle = TriangleArray[TriangleID];
		Swap(Triangle.VertexInstanceIDs[0], Triangle.VertexInstanceIDs[2]);
	}
}


void FMeshDescription::ReverseAllPolygonFacing()
{
	// Perform triangulation directly into mesh polygons
	for (const FPolygonID PolygonID : Polygons().GetElementIDs())
	{
		ReversePolygonFacing(PolygonID);
	}
}


void FMeshDescription::RemapPolygonGroups(const TMap<FPolygonGroupID, FPolygonGroupID>& Remap)
{
	TPolygonGroupAttributesRef<FName> PolygonGroupNames = PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute_::PolygonGroup::ImportedMaterialSlotName);

	struct FOldPolygonGroupData
	{
		FName Name;
		TArray<FPolygonID> Polygons;
	};

	TMap<FPolygonGroupID, FOldPolygonGroupData> OldData;
	for (const FPolygonGroupID PolygonGroupID : PolygonGroups().GetElementIDs())
	{
		if (!Remap.Contains(PolygonGroupID) || PolygonGroupID == Remap[PolygonGroupID])
		{
			//No need to change this one
			continue;
		}
		FOldPolygonGroupData& PolygonGroupData = OldData.FindOrAdd(PolygonGroupID);
		PolygonGroupData.Name = PolygonGroupNames[PolygonGroupID];
		FMeshPolygonGroup& PolygonGroup = PolygonGroupArray[PolygonGroupID];
		PolygonGroupData.Polygons = PolygonGroup.Polygons;
		PolygonGroup.Polygons.Empty();
		DeletePolygonGroup(PolygonGroupID);
	}
	for (auto Kvp : OldData)
	{
		FPolygonGroupID GroupID = Kvp.Key;
		FPolygonGroupID ToGroupID = Remap[GroupID];
		if (!PolygonGroups().IsValid(ToGroupID))
		{
			CreatePolygonGroupWithID(ToGroupID);
		}
		TArray<FPolygonID>& Polygons = PolygonGroupArray[ToGroupID].Polygons;
		Polygons.Append(Kvp.Value.Polygons);
		PolygonGroupNames[ToGroupID] = Kvp.Value.Name;
		for (FPolygonID PolygonID : Polygons)
		{
			PolygonArray[PolygonID].PolygonGroupID = ToGroupID;
		}
	}
}



#if WITH_EDITORONLY_DATA

void FMeshDescriptionBulkData::Serialize( FArchive& Ar, UObject* Owner )
{
	Ar.UsingCustomVersion( FEditorObjectVersion::GUID );
	Ar.UsingCustomVersion( FEnterpriseObjectVersion::GUID );

	if( Ar.IsTransacting() )
	{
		// If transacting, keep these members alive the other side of an undo, otherwise their values will get lost
		CustomVersions.Serialize( Ar );
		Ar << bBulkDataUpdated;
	}
	else
	{
		if( Ar.IsLoading() )
		{
			// If loading, take a copy of the package custom version container, so it can be applied when unpacking
			// MeshDescription from the bulk data.
			CustomVersions = Ar.GetCustomVersions();
		}
		else if( Ar.IsSaving() )
		{
			// If the bulk data hasn't been updated since this was loaded, there's a possibility that it has old versioning.
			// Explicitly load and resave the FMeshDescription so that its version is in sync with the FMeshDescriptionBulkData.
			if( !bBulkDataUpdated )
			{
				FMeshDescription MeshDescription;
				LoadMeshDescription( MeshDescription );
				SaveMeshDescription( MeshDescription );
			}
		}
	}

	BulkData.Serialize( Ar, Owner );

	if( Ar.IsLoading() && Ar.CustomVer( FEditorObjectVersion::GUID ) < FEditorObjectVersion::MeshDescriptionBulkDataGuid )
	{
		FPlatformMisc::CreateGuid( Guid );
	}
	else
	{
		Ar << Guid;
	}
	
	// MeshDescriptionBulkData contains a bGuidIsHash so we can benefit from DDC caching.
	if( Ar.IsLoading() && Ar.CustomVer( FEnterpriseObjectVersion::GUID ) < FEnterpriseObjectVersion::MeshDescriptionBulkDataGuidIsHash )
	{
		bGuidIsHash = false;
	}
	else
	{
		Ar << bGuidIsHash;
	}
}


void FMeshDescriptionBulkData::SaveMeshDescription( FMeshDescription& MeshDescription )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshDescriptionBulkData::SaveMeshDescription);

	BulkData.RemoveBulkData();

	if( !MeshDescription.IsEmpty() )
	{
		const bool bIsPersistent = true;
		FBulkDataWriter Ar( BulkData, bIsPersistent );
		Ar << MeshDescription;

		// Preserve CustomVersions at save time so we can reuse the same ones when reloading direct from memory
		CustomVersions = Ar.GetCustomVersions();
	}

	if (bGuidIsHash)
	{
		UseHashAsGuid();
	}
	else
	{
		FPlatformMisc::CreateGuid( Guid );
	}

	// Mark the MeshDescriptionBulkData as having been updated.
	// This means we know that its version is up-to-date.
	bBulkDataUpdated = true;
}


void FMeshDescriptionBulkData::LoadMeshDescription( FMeshDescription& MeshDescription )
{
	MeshDescription.Empty();

	if( BulkData.GetElementCount() > 0 )
	{
		// Get a lock on the bulk data and read it into the mesh description
		{
			const bool bIsPersistent = true;
			FBulkDataReader Ar( BulkData, bIsPersistent );

			// Propagate the custom version information from the package to the bulk data, so that the MeshDescription
			// is serialized with the same versioning.
			Ar.SetCustomVersions( CustomVersions );
			Ar << MeshDescription;
		}
		// Unlock bulk data when we leave scope

		// Throw away the bulk data allocation as we don't need it now we have its contents as a FMeshDescription
		// @todo: revisit this
//		BulkData.UnloadBulkData();
	}
}


void FMeshDescriptionBulkData::Empty()
{
	BulkData.RemoveBulkData();
}


FString FMeshDescriptionBulkData::GetIdString() const
{
	FString GuidString = Guid.ToString();
	if (bGuidIsHash)
	{
		GuidString += TEXT("X");
	}
	return GuidString;
}


void FMeshDescriptionBulkData::UseHashAsGuid()
{
	uint32 Hash[5] = {};

	if (BulkData.GetBulkDataSize() > 0)
	{
		bGuidIsHash = true;
		void* Buffer = BulkData.Lock(LOCK_READ_ONLY);
		FSHA1::HashBuffer(Buffer, BulkData.GetBulkDataSize(), (uint8*)Hash);
		BulkData.Unlock();
	}

	Guid = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
}

#endif // #if WITH_EDITORONLY_DATA
