// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StaticMeshOperations.h"
#include "StaticMeshAttributes.h"


static bool GetPolygonTangentsAndNormals(FMeshDescription& MeshDescription,
										 FPolygonID PolygonID,
										 float ComparisonThreshold, 
										 TVertexAttributesRef<const FVector> VertexPositions,
										 TVertexInstanceAttributesRef<const FVector2D> VertexUVs,
										 TPolygonAttributesRef<FVector> PolygonNormals,
										 TPolygonAttributesRef<FVector> PolygonTangents,
										 TPolygonAttributesRef<FVector> PolygonBinormals,
										 TPolygonAttributesRef<FVector> PolygonCenters)
{
	bool bValidNTBs = true;

	// Calculate the center of this polygon
	FVector Center = FVector::ZeroVector;
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(PolygonID);
	for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
	{
		Center += VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceID)];
	}
	Center /= float(VertexInstanceIDs.Num());

	// Calculate the tangent basis for the polygon, based on the average of all constituent triangles
	FVector Normal = FVector::ZeroVector;
	FVector Tangent = FVector::ZeroVector;
	FVector Binormal = FVector::ZeroVector;

	for (const FTriangleID TriangleID : MeshDescription.GetPolygonTriangleIDs(PolygonID))
	{
		TArrayView<const FVertexInstanceID> TriangleVertexInstances = MeshDescription.GetTriangleVertexInstances(TriangleID);
		const FVertexID VertexID0 = MeshDescription.GetVertexInstanceVertex(TriangleVertexInstances[0]);
		const FVertexID VertexID1 = MeshDescription.GetVertexInstanceVertex(TriangleVertexInstances[1]);
		const FVertexID VertexID2 = MeshDescription.GetVertexInstanceVertex(TriangleVertexInstances[2]);

		const FVector DPosition1 = VertexPositions[VertexID1] - VertexPositions[VertexID0];
		const FVector DPosition2 = VertexPositions[VertexID2] - VertexPositions[VertexID0];

		const FVector2D DUV1 = VertexUVs[TriangleVertexInstances[1]] - VertexUVs[TriangleVertexInstances[0]];
		const FVector2D DUV2 = VertexUVs[TriangleVertexInstances[2]] - VertexUVs[TriangleVertexInstances[0]];

		// We have a left-handed coordinate system, but a counter-clockwise winding order
		// Hence normal calculation has to take the triangle vectors cross product in reverse.
		FVector TmpNormal = FVector::CrossProduct(DPosition2, DPosition1);
		if (!TmpNormal.IsNearlyZero(ComparisonThreshold) && !TmpNormal.ContainsNaN())
		{
			Normal += TmpNormal;
			// ...and tangent space seems to be right-handed.
			const float DetUV = FVector2D::CrossProduct(DUV1, DUV2);
			const float InvDetUV = (DetUV == 0.0f) ? 0.0f : 1.0f / DetUV;

			Tangent += (DPosition1 * DUV2.Y - DPosition2 * DUV1.Y) * InvDetUV;
			Binormal += (DPosition2 * DUV1.X - DPosition1 * DUV2.X) * InvDetUV;
		}
		else
		{
			// The polygon is degenerated
			bValidNTBs = false;
		}
	}

	PolygonNormals[PolygonID] = Normal.GetSafeNormal();
	PolygonTangents[PolygonID] = Tangent.GetSafeNormal();
	PolygonBinormals[PolygonID] = Binormal.GetSafeNormal();
	PolygonCenters[PolygonID] = Center;

	return bValidNTBs;
}


static TArray<FPolygonID> GetPolygonsInSameSoftEdgedGroupAsPolygon(const FMeshDescription& MeshDescription, FPolygonID PolygonID, const TArray<FPolygonID>& CandidatePolygonIDs, const TArray<FEdgeID>& SoftEdgeIDs)
{
	// The aim of this method is:
	// - given a polygon ID,
	// - given a set of candidate polygons connected to the same vertex (which should include the polygon ID),
	// - given a set of soft edges connected to the same vertex,
	// return the polygon IDs which form an adjacent run without crossing a hard edge.

	TArray<FPolygonID> OutPolygonIDs;

	// Maintain a list of polygon IDs to be examined. Adjacents are added to the list if suitable.
	// Add the start poly here.
	TArray<FPolygonID> PolygonsToCheck;
	PolygonsToCheck.Reset(CandidatePolygonIDs.Num());
	PolygonsToCheck.Add(PolygonID);

	int32 Index = 0;
	while (Index < PolygonsToCheck.Num())
	{
		const FPolygonID PolygonToCheck = PolygonsToCheck[Index];
		Index++;

		if (CandidatePolygonIDs.Contains(PolygonToCheck))
		{
			OutPolygonIDs.Add(PolygonToCheck);

			// Now look at its adjacent polygons. If they are joined by a soft edge which includes the vertex we're interested in, we want to consider them.
			// We take a shortcut by doing this process in reverse: we already know all the soft edges we are interested in, so check if any of them
			// have the current polygon as an adjacent.
			for (const FEdgeID SoftEdgeID : SoftEdgeIDs)
			{
				const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(SoftEdgeID);
				if (EdgeConnectedPolygons.Contains(PolygonToCheck))
				{
					for (const FPolygonID AdjacentPolygon : EdgeConnectedPolygons)
					{
						// Only add new polygons which haven't yet been added to the list. This prevents circular runs of polygons triggering infinite loops.
						PolygonsToCheck.AddUnique(AdjacentPolygon);
					}
				}
			}
		}
	}

	return OutPolygonIDs;
}


static TArray<FEdgeID> GetConnectedSoftEdges(const FMeshDescription& MeshDescription, FVertexID VertexID)
{
	TArray<FEdgeID> OutConnectedSoftEdges;
	OutConnectedSoftEdges.Reserve(MeshDescription.GetNumVertexConnectedEdges(VertexID));

	FStaticMeshConstAttributes Attributes(MeshDescription);
	TEdgeAttributesConstRef<bool> EdgeHardnesses = Attributes.GetEdgeHardnesses();
	for (const FEdgeID ConnectedEdgeID : MeshDescription.GetVertexConnectedEdges(VertexID))
	{
		if (!EdgeHardnesses[ConnectedEdgeID])
		{
			OutConnectedSoftEdges.Add(ConnectedEdgeID);
		}
	}

	return OutConnectedSoftEdges;
}


static TArray<FPolygonID> GetVertexConnectedPolygonsInSameSoftEdgedGroup(const FMeshDescription& MeshDescription, FVertexID VertexID, FPolygonID PolygonID)
{
	// The aim here is to determine which polygons form part of the same soft edged group as the polygons attached to this vertex.
	// They should all contribute to the final vertex instance normal.

	// Get all polygons connected to this vertex.
	TArray<FPolygonID> ConnectedPolygons = MeshDescription.GetVertexConnectedPolygons(VertexID);

	// Cache a list of all soft edges which share this vertex.
	// We're only interested in finding adjacent polygons which are not the other side of a hard edge.
	TArray<FEdgeID> ConnectedSoftEdges = GetConnectedSoftEdges(MeshDescription, VertexID);

	return GetPolygonsInSameSoftEdgedGroupAsPolygon(MeshDescription, PolygonID, ConnectedPolygons, ConnectedSoftEdges);
}


static void GetTangentsAndNormals(FMeshDescription& MeshDescription,
								  FVertexInstanceID VertexInstanceID,
								  EComputeNTBsFlags ComputeNTBsOptions,
								  TPolygonAttributesRef<const FVector> PolygonNormals,
								  TPolygonAttributesRef<const FVector> PolygonTangents,
								  TPolygonAttributesRef<const FVector> PolygonBinormals,
								  TVertexInstanceAttributesRef<FVector> VertexNormals,
								  TVertexInstanceAttributesRef<FVector> VertexTangents,
								  TVertexInstanceAttributesRef<float> VertexBinormalSigns)
{
	bool bComputeNormals = !!(ComputeNTBsOptions & EComputeNTBsFlags::Normals);
	bool bComputeTangents = !!(ComputeNTBsOptions & EComputeNTBsFlags::Tangents);
	bool bUseWeightedNormals = !!(ComputeNTBsOptions & EComputeNTBsFlags::WeightedNTBs);

	FVector Normal = FVector::ZeroVector;
	FVector Tangent = FVector::ZeroVector;
	FVector Binormal = FVector::ZeroVector;

	FVector& NormalRef = VertexNormals[VertexInstanceID];
	FVector& TangentRef = VertexTangents[VertexInstanceID];
	float& BinormalRef = VertexBinormalSigns[VertexInstanceID];

	if (!bComputeNormals && !bComputeTangents)
	{
		// Nothing to compute
		return;
	}

	const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);

	if (bComputeNormals || NormalRef.IsNearlyZero())
	{
		// Get all polygons connected to this vertex instance
		const TArray<FPolygonID>& VertexInstanceConnectedPolygons = MeshDescription.GetVertexInstanceConnectedPolygons(VertexInstanceID);
		check(VertexInstanceConnectedPolygons.Num() > 0);

		// Add also any in the same smoothing group connected to a different vertex instance
		// (as they still have influence over the normal).
		TArray<FPolygonID> AllConnectedPolygons = GetVertexConnectedPolygonsInSameSoftEdgedGroup(MeshDescription, VertexID, VertexInstanceConnectedPolygons[0]);

		// The vertex instance normal is computed as a sum of all connected polygons' normals, weighted by the angle they make with the vertex
		for (const FPolygonID ConnectedPolygonID : AllConnectedPolygons)
		{
			const float Angle = bUseWeightedNormals ? MeshDescription.GetPolygonCornerAngleForVertex(ConnectedPolygonID, VertexID) : 1.0f;

			Normal += PolygonNormals[ConnectedPolygonID] * Angle;

			// If this polygon is actually connected to the vertex instance we're processing, also include its contributions towards the tangent
			if (VertexInstanceConnectedPolygons.Contains(ConnectedPolygonID))
			{
				Tangent += PolygonTangents[ConnectedPolygonID] * Angle;
				Binormal += PolygonBinormals[ConnectedPolygonID] * Angle;
			}
		}

		// Normalize Normal
		Normal = Normal.GetSafeNormal();
	}
	else
	{
		// We use existing normals so just use all polygons having a vertex instance at the same location sharing the same normals
		Normal = NormalRef;
		TArray<FVertexInstanceID> VertexInstanceIDs = MeshDescription.GetVertexVertexInstances(VertexID);
		for (const FVertexInstanceID& ConnectedVertexInstanceID : VertexInstanceIDs)
		{
			if (ConnectedVertexInstanceID != VertexInstanceID && !VertexNormals[ConnectedVertexInstanceID].Equals(Normal))
			{
				continue;
			}

			const TArray<FPolygonID>& ConnectedPolygons = MeshDescription.GetVertexInstanceConnectedPolygons(ConnectedVertexInstanceID);
			for (const FPolygonID ConnectedPolygonID : ConnectedPolygons)
			{
				const float Angle = bUseWeightedNormals ? MeshDescription.GetPolygonCornerAngleForVertex(ConnectedPolygonID, VertexID) : 1.0f;

				// If this polygon is actually connected to the vertex instance we're processing, also include its contributions towards the tangent
				Tangent += PolygonTangents[ConnectedPolygonID] * Angle;
				Binormal += PolygonBinormals[ConnectedPolygonID] * Angle;
			}
		}
	}


	float BinormalSign = 1.0f;
	if (bComputeTangents)
	{
		// Make Tangent orthonormal to Normal.
		// This is a quicker method than normalizing Tangent, taking the cross product Normal X Tangent, and then a further cross product with that result
		Tangent = (Tangent - Normal * FVector::DotProduct(Normal, Tangent)).GetSafeNormal();

		// Calculate binormal sign
		BinormalSign = (FVector::DotProduct(FVector::CrossProduct(Normal, Tangent), Binormal) < 0.0f) ? -1.0f : 1.0f;
	}

	// Set the value that need to be set
	if (NormalRef.IsNearlyZero())
	{
		NormalRef = Normal;
	}

	if (bComputeTangents)
	{
		if (TangentRef.IsNearlyZero())
		{
			TangentRef = Tangent;
		}

		if (FMath::IsNearlyZero(BinormalRef))
		{
			BinormalRef = BinormalSign;
		}
	}
}



void FStaticMeshOperations::ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, TArrayView<const FPolygonID> PolygonIDs, float ComparisonThreshold)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	TVertexAttributesRef<const FVector> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<const FVector2D> VertexUVs = Attributes.GetVertexInstanceUVs();
	TPolygonAttributesRef<FVector> PolygonNormals = Attributes.GetPolygonNormals();
	TPolygonAttributesRef<FVector> PolygonTangents = Attributes.GetPolygonTangents();
	TPolygonAttributesRef<FVector> PolygonBinormals = Attributes.GetPolygonBinormals();
	TPolygonAttributesRef<FVector> PolygonCenters = Attributes.GetPolygonCenters();

	TArray<FPolygonID> DegeneratePolygonIDs;
	for (const FPolygonID PolygonID : PolygonIDs)
	{
		if (!GetPolygonTangentsAndNormals(MeshDescription, PolygonID, ComparisonThreshold, VertexPositions, VertexUVs, PolygonNormals, PolygonTangents, PolygonBinormals, PolygonCenters))
		{
			DegeneratePolygonIDs.Add(PolygonID);
		}
	}

	// Remove degenerated polygons
	// Delete the degenerated polygons. The array is filled only if the remove degenerated option is turned on.
	if (DegeneratePolygonIDs.Num() > 0)
	{
		TArray<FEdgeID> OrphanedEdges;
		TArray<FVertexInstanceID> OrphanedVertexInstances;
		TArray<FPolygonGroupID> OrphanedPolygonGroups;
		TArray<FVertexID> OrphanedVertices;

		for (FPolygonID PolygonID : DegeneratePolygonIDs)
		{
			MeshDescription.DeletePolygon(PolygonID, &OrphanedEdges, &OrphanedVertexInstances, &OrphanedPolygonGroups);
		}
		for (FPolygonGroupID PolygonGroupID : OrphanedPolygonGroups)
		{
			MeshDescription.DeletePolygonGroup(PolygonGroupID);
		}
		for (FVertexInstanceID VertexInstanceID : OrphanedVertexInstances)
		{
			MeshDescription.DeleteVertexInstance(VertexInstanceID, &OrphanedVertices);
		}
		for (FEdgeID EdgeID : OrphanedEdges)
		{
			MeshDescription.DeleteEdge(EdgeID, &OrphanedVertices);
		}
		for (FVertexID VertexID : OrphanedVertices)
		{
			MeshDescription.DeleteVertex(VertexID);
		}
		// Compact and Remap IDs so we have clean ID from 0 to n since we just erase some polygons
		FElementIDRemappings RemappingInfos;
		MeshDescription.Compact(RemappingInfos);
	}
}

void FStaticMeshOperations::ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, float ComparisonThreshold)
{
	TArray<FPolygonID> PolygonsToComputeNTBs;
	PolygonsToComputeNTBs.Reserve(MeshDescription.Polygons().Num());

	for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		PolygonsToComputeNTBs.Add(PolygonID);
	}

	ComputePolygonTangentsAndNormals(MeshDescription, PolygonsToComputeNTBs, ComparisonThreshold);
}

void FStaticMeshOperations::ComputeTangentsAndNormals(FMeshDescription& MeshDescription, TArrayView<const FVertexInstanceID> VertexInstanceIDs, EComputeNTBsFlags ComputeNTBsOptions)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	TPolygonAttributesRef<const FVector> PolygonNormals = Attributes.GetPolygonNormals();
	TPolygonAttributesRef<const FVector> PolygonTangents = Attributes.GetPolygonTangents();
	TPolygonAttributesRef<const FVector> PolygonBinormals = Attributes.GetPolygonBinormals();
	TVertexInstanceAttributesRef<FVector> VertexNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector> VertexTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> VertexBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();

	for (const FVertexInstanceID VertexInstanceID : VertexInstanceIDs)
	{
		GetTangentsAndNormals(MeshDescription, VertexInstanceID, ComputeNTBsOptions, PolygonNormals, PolygonTangents, PolygonBinormals, VertexNormals, VertexTangents, VertexBinormalSigns);
	}
}

void FStaticMeshOperations::ComputeTangentsAndNormals(FMeshDescription& MeshDescription, EComputeNTBsFlags ComputeNTBsOptions)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	TPolygonAttributesRef<const FVector> PolygonNormals = Attributes.GetPolygonNormals();
	TPolygonAttributesRef<const FVector> PolygonTangents = Attributes.GetPolygonTangents();
	TPolygonAttributesRef<const FVector> PolygonBinormals = Attributes.GetPolygonBinormals();
	TVertexInstanceAttributesRef<FVector> VertexNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector> VertexTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> VertexBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();

	for (const FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		GetTangentsAndNormals(MeshDescription, VertexInstanceID, ComputeNTBsOptions, PolygonNormals, PolygonTangents, PolygonBinormals, VertexNormals, VertexTangents, VertexBinormalSigns);
	}
}

void FStaticMeshOperations::DetermineEdgeHardnessesFromVertexInstanceNormals(FMeshDescription& MeshDescription, float Tolerance)
{
	FStaticMeshAttributes Attributes(MeshDescription);

	TVertexInstanceAttributesRef<const FVector> VertexNormals = Attributes.GetVertexInstanceNormals();
	TEdgeAttributesRef<bool> EdgeHardnesses = Attributes.GetEdgeHardnesses();

	// Holds unique vertex instance IDs for a given edge vertex
	TArray<FVertexInstanceID> UniqueVertexInstanceIDs;

	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		// Get list of polygons connected to this edge
		TArray<FPolygonID, TInlineAllocator<2>> ConnectedPolygonIDs = MeshDescription.GetEdgeConnectedPolygons<TInlineAllocator<2>>(EdgeID);
		if (ConnectedPolygonIDs.Num() == 0)
		{
			// What does it mean if an edge has no connected polygons? For now we just skip it
			continue;
		}

		// Assume by default that the edge is soft - but as soon as any vertex instance belonging to a connected polygon
		// has a distinct normal from the others (within the given tolerance), we mark it as hard.
		// The exception is if an edge has exactly one connected polygon: in this case we automatically deem it a hard edge.
		bool bEdgeIsHard = (ConnectedPolygonIDs.Num() == 1);

		// Examine vertices on each end of the edge, if we haven't yet identified it as 'hard'
		for (int32 VertexIndex = 0; !bEdgeIsHard && VertexIndex < 2; ++VertexIndex)
		{
			const FVertexID VertexID = MeshDescription.GetEdgeVertex(EdgeID, VertexIndex);

			const int32 ReservedElements = 4;
			UniqueVertexInstanceIDs.Reset(ReservedElements);

			// Get a list of all vertex instances for this vertex which form part of any polygon connected to the edge
			for (const FVertexInstanceID VertexInstanceID : MeshDescription.GetVertexVertexInstances(VertexID))
			{
				for (const FPolygonID PolygonID : MeshDescription.GetVertexInstanceConnectedPolygons<TInlineAllocator<8>>(VertexInstanceID))
				{
					if (ConnectedPolygonIDs.Contains(PolygonID))
					{
						UniqueVertexInstanceIDs.AddUnique(VertexInstanceID);
						break;
					}
				}
			}
			check(UniqueVertexInstanceIDs.Num() > 0);

			// First unique vertex instance is used as a reference against which the others are compared.
			// (not a perfect approach: really the 'median' should be used as a reference)
			const FVector ReferenceNormal = VertexNormals[UniqueVertexInstanceIDs[0]];
			for (int32 Index = 1; Index < UniqueVertexInstanceIDs.Num(); ++Index)
			{
				if (!VertexNormals[UniqueVertexInstanceIDs[Index]].Equals(ReferenceNormal, Tolerance))
				{
					bEdgeIsHard = true;
					break;
				}
			}
		}

		EdgeHardnesses[EdgeID] = bEdgeIsHard;
	}
}


void FStaticMeshOperations::ExtrudePolygons(FMeshDescription& MeshDescription, const TArray<FPolygonID>& PolygonIDs, float ExtrudeDistance, bool bKeepNeighborsTogether, TArray<FPolygonID>& OutNewExtrudedFrontPolygons)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	TVertexAttributesRef<FVector> VertexPositions = Attributes.GetVertexPositions();

	OutNewExtrudedFrontPolygons.Reset();

	// Convert our incoming polygon array to a TSet so we can lookup quickly to see which polygons in the mesh sare members of the set
	TSet<FPolygonID> PolygonsSet;
	PolygonsSet.Append(PolygonIDs);

	TArray<FPolygonID> AllNewPolygons;

//	static TArray<FAttributesForEdge> AttributesForEdges;
//	AttributesForEdges.Reset();

//	static TArray<FAttributesForVertex> AttributesForVertices;
//	AttributesForVertices.Reset();

	// First, let's figure out which of the polygons we were asked to extrude share edges or vertices.  We'll keep those edges intact!
	TMap<FEdgeID, uint32> EdgeUsageCounts;	// Maps an edge ID to the number of times it is referenced by the incoming polygons
	TSet<FVertexID> UniqueVertexIDs;

	for (const FPolygonID PolygonID : PolygonIDs)
	{
		for (const FEdgeID EdgeID : MeshDescription.GetPolygonPerimeterEdges(PolygonID))
		{
			if (uint32* EdgeUsageCountPtr = EdgeUsageCounts.Find(EdgeID))
			{
				++(*EdgeUsageCountPtr);
			}
			else
			{
				EdgeUsageCounts.Add(EdgeID, 1);
			}
		}

		UniqueVertexIDs.Append(MeshDescription.GetPolygonVertices(PolygonID));
	}

	int32 NumVerticesToCreate = UniqueVertexIDs.Num();

	// Create new vertices for all of the extruded polygons
	TArray<FVertexID> ExtrudedVertexIDs;
	ExtrudedVertexIDs.Reserve(NumVerticesToCreate);
	ExtrudedVertexIDs.Add(MeshDescription.CreateVertex());

	int32 NextAvailableExtrudedVertexIDNumber = 0;

	TMap<FVertexID, FVertexID> VertexIDToExtrudedCopy;
	#if 0
	for (int32 PassIndex = 0; PassIndex < 2; ++PassIndex)
	{
		// Extrude all of the shared edges first, then do the non-shared edges.  This is to make sure that a vertex doesn't get offset
		// without taking into account all of the connected polygons in our set
		const bool bIsExtrudingSharedEdges = (PassIndex == 0);

		for (const FPolygonID PolygonID : PolygonIDs)
		{
			const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);

			if (!bKeepNeighborsTogether)
			{
				VertexIDToExtrudedCopy.Reset();
			}

			// Map all of the edge vertices to their new extruded counterpart
			TArray<FVertexID> PolygonVertices = MeshDescription.GetPolygonVertices(PolygonID);
			TArray<FEdgeID> PolygonPerimeterEdges = MeshDescription.GetPolygonPerimeterEdges(PolygonID);
			for (const FEdgeID EdgeID : PolygonPerimeterEdges)
			{
				// @todo mesheditor perf: We can change GetPolygonPerimeterEdges() to have a version that returns whether winding is reversed or not, and avoid this call entirely.
				// @todo mesheditor perf: O(log N^2) iteration here. For every edge, for every edge up to this index.  Need to clean this up. 
				//		--> Also, there are quite a few places where we are stepping through edges in perimeter-order.  We need to have a nice way to walk that.
				bool bEdgeWindingIsReversedForPolygon;
				const FEdgeID EdgeID = this->GetPolygonPerimeterEdge( PolygonID, PerimeterEdgeNumber, /* Out */ bEdgeWindingIsReversedForPolygon );

				const bool bIsSharedEdge = bKeepNeighborsTogether && EdgeUsageCounts[ EdgeID ] > 1;
				if( bIsSharedEdge == bIsExtrudingSharedEdges )
				{
					FVertexID EdgeVertexIDs[ 2 ];
					GetEdgeVertices( EdgeID, /* Out */ EdgeVertexIDs[ 0 ], /* Out */ EdgeVertexIDs[ 1 ] );
					if( bEdgeWindingIsReversedForPolygon )
					{
						::Swap( EdgeVertexIDs[ 0 ], EdgeVertexIDs[ 1 ] );
					}

					if( !bIsSharedEdge )
					{
						// After extruding, all of the edges of the original polygon become hard edges
						AttributesForEdges.Emplace();
						FAttributesForEdge& AttributesForEdge = AttributesForEdges.Last();
						AttributesForEdge.EdgeID = EdgeID;
						AttributesForEdge.EdgeAttributes.Attributes.Emplace( MeshAttribute::Edge::IsHard, 0, FMeshElementAttributeValue( true ) );
					}

					FVertexID ExtrudedEdgeVertexIDs[ 2 ];
					for( int32 EdgeVertexNumber = 0; EdgeVertexNumber < 2; ++EdgeVertexNumber )
					{
						const FVertexID EdgeVertexID = EdgeVertexIDs[ EdgeVertexNumber ];
						FVertexID* ExtrudedEdgeVertexIDPtr = VertexIDToExtrudedCopy.Find( EdgeVertexID );

						// @todo mesheditor extrude: Ideally we would detect whether the vertex that was already extruded came from a edge
						// from a polygon that does not actually share an edge with any polygons this polygon shares an edge with.  This
						// would avoid the problem where extruding two polygons that are connected only by a vertex are not extruded
						// separately.
						const bool bVertexIsSharedByAnEdgeOfAnotherSelectedPolygon = false;
						if( ExtrudedEdgeVertexIDPtr != nullptr)// && !bVertexIsSharedByAnEdgeOfAnotherSelectedPolygon )
						{
							ExtrudedEdgeVertexIDs[ EdgeVertexNumber ] = *ExtrudedEdgeVertexIDPtr;
						}
						else
						{
							// Create a copy of this vertex for the extruded face
							const FVertexID ExtrudedVertexID = ExtrudedVertexIDs[ NextAvailableExtrudedVertexIDNumber++ ];

							ExtrudedEdgeVertexIDPtr = &VertexIDToExtrudedCopy.Add( EdgeVertexID, ExtrudedVertexID );
							// Push the vertex out along the polygon's normal
							const FVector OriginalVertexPosition = VertexPositions[ EdgeVertexID ];

							FVector ExtrudedVertexPosition;
							if( bIsSharedEdge )
							{
								// Get all of the polygons that share this edge that were part of the set of polygons passed in.  We'll
								// generate an extrude direction that's the average of those polygon normals.
								FVector ExtrudeDirection = FVector::ZeroVector;

								static TArray<FPolygonID> ConnectedPolygonIDs;
								GetVertexConnectedPolygons( EdgeVertexID, /* Out */ ConnectedPolygonIDs );

								static TArray<FPolygonID> NeighborPolygonIDs;
								NeighborPolygonIDs.Reset();
								for( const FPolygonID ConnectedPolygonID : ConnectedPolygonIDs )
								{
									// We only care about polygons that are members of the set of polygons we were asked to extrude
									if( PolygonsSet.Contains( ConnectedPolygonID ) )
									{
										NeighborPolygonIDs.Add( ConnectedPolygonID );

										// We'll need this polygon's normal to figure out where to put the extruded copy of the polygon
										const FVector NeighborPolygonNormal = ComputePolygonNormal( ConnectedPolygonID );
										ExtrudeDirection += NeighborPolygonNormal;
									}
								}
								ExtrudeDirection.Normalize();


								// OK, we have the direction to extrude for this vertex.  Now we need to know how far to extrude.  We'll
								// loop over all of the neighbor polygons to this vertex, and choose the closest intersection point with our
								// vertex's extrude direction and the neighbor polygon's extruded plane
								FVector ClosestIntersectionPointWithExtrudedPlanes;
								float ClosestIntersectionDistanceSquared = TNumericLimits<float>::Max();

								for( const FPolygonID NeighborPolygonID : NeighborPolygonIDs )
								{
									const FPlane NeighborPolygonPlane = ComputePolygonPlane( NeighborPolygonID );

									// Push the plane out
									const FPlane ExtrudedPlane = [NeighborPolygonPlane, ExtrudeDistance]
									{ 
										FPlane NewPlane = NeighborPolygonPlane;
										NewPlane.W += ExtrudeDistance; 
										return NewPlane; 
									}();

									// Is this the closest intersection point so far?
									const FVector IntersectionPointWithExtrudedPlane = FMath::RayPlaneIntersection( OriginalVertexPosition, ExtrudeDirection, ExtrudedPlane );
									const float IntersectionDistanceSquared = FVector::DistSquared( OriginalVertexPosition, IntersectionPointWithExtrudedPlane );
									if( IntersectionDistanceSquared < ClosestIntersectionDistanceSquared )
									{
										ClosestIntersectionPointWithExtrudedPlanes = IntersectionPointWithExtrudedPlane;
										ClosestIntersectionDistanceSquared = IntersectionDistanceSquared;
									}
								}

								ExtrudedVertexPosition = ClosestIntersectionPointWithExtrudedPlanes;
							}
							else
							{
								// We'll need this polygon's normal to figure out where to put the extruded copy of the polygon
								const FVector PolygonNormal = ComputePolygonNormal( PolygonID );
								ExtrudedVertexPosition = OriginalVertexPosition + ExtrudeDistance * PolygonNormal;
							}

							// Fill in the vertex
							AttributesForVertices.Emplace();
							FAttributesForVertex& AttributesForVertex = AttributesForVertices.Last();
							AttributesForVertex.VertexID = ExtrudedVertexID;
							AttributesForVertex.VertexAttributes.Attributes.Emplace( MeshAttribute::Vertex::Position, 0, FMeshElementAttributeValue( ExtrudedVertexPosition ) );
						}
						ExtrudedEdgeVertexIDs[ EdgeVertexNumber ] = *ExtrudedEdgeVertexIDPtr;
					}

					if( !bIsSharedEdge )
					{
						static TArray<FVertexAndAttributes> NewSidePolygonVertices;
						NewSidePolygonVertices.Reset( 4 );
						NewSidePolygonVertices.SetNum( 4, false );	// Always four edges in an extruded face

						NewSidePolygonVertices[ 0 ].VertexID = EdgeVertexIDs[ 1 ];
						NewSidePolygonVertices[ 1 ].VertexID = EdgeVertexIDs[ 0 ];
						NewSidePolygonVertices[ 2 ].VertexID = ExtrudedEdgeVertexIDs[ 0 ];
						NewSidePolygonVertices[ 3 ].VertexID = ExtrudedEdgeVertexIDs[ 1 ];

						// Get vertex instance IDs on this polygon corresponding to the edge start/end vertices
						const FVertexInstanceID EdgeVertexInstanceID0 = GetMeshDescription()->GetVertexInstanceForPolygonVertex( PolygonID, EdgeVertexIDs[ 0 ] );
						const FVertexInstanceID EdgeVertexInstanceID1 = GetMeshDescription()->GetVertexInstanceForPolygonVertex( PolygonID, EdgeVertexIDs[ 1 ] );

						BackupAllAttributes( NewSidePolygonVertices[ 0 ].PolygonVertexAttributes, GetMeshDescription()->VertexInstanceAttributes(), EdgeVertexInstanceID1 );
						BackupAllAttributes( NewSidePolygonVertices[ 1 ].PolygonVertexAttributes, GetMeshDescription()->VertexInstanceAttributes(), EdgeVertexInstanceID0 );
						BackupAllAttributes( NewSidePolygonVertices[ 2 ].PolygonVertexAttributes, GetMeshDescription()->VertexInstanceAttributes(), EdgeVertexInstanceID0 );
						BackupAllAttributes( NewSidePolygonVertices[ 3 ].PolygonVertexAttributes, GetMeshDescription()->VertexInstanceAttributes(), EdgeVertexInstanceID1 );

						FPolygonID NewSidePolygonID;	// Filled in below
						{
							static TArray<FPolygonToCreate> PolygonsToCreate;
							PolygonsToCreate.Reset();

							// Create the polygon
							// @todo mesheditor perf: Ideally we support creating multiple polygons at once and batching up the work
							FPolygonToCreate& PolygonToCreate = *new( PolygonsToCreate ) FPolygonToCreate();
							PolygonToCreate.PolygonGroupID = PolygonGroupID;
							PolygonToCreate.PerimeterVertices = NewSidePolygonVertices;	// @todo mesheditor perf: Copying static array here, ideally allocations could be avoided
							PolygonToCreate.PolygonEdgeHardness = EPolygonEdgeHardness::AllEdgesHard;

							static TArray<FPolygonID> NewPolygonIDs;
							NewPolygonIDs.Reset();
							static TArray<FEdgeID> NewEdgeIDs;
							NewEdgeIDs.Reset();
							CreatePolygons( PolygonsToCreate, /* Out */ NewPolygonIDs, /* Out */ NewEdgeIDs );	// @todo mesheditor perf: Extra allocatons/copies: Ideally MoveTemp() here but we can't move a STATIC local!

							NewSidePolygonID = NewPolygonIDs[ 0 ];
						}
						AllNewPolygons.Add( NewSidePolygonID );
					}
				}
			}
		}
	}

	for( int32 PolygonIter = 0; PolygonIter < PolygonIDs.Num(); ++PolygonIter )
	{
		const FPolygonID PolygonID = PolygonIDs[ PolygonIter ];
		const FPolygonGroupID PolygonGroupID = GetGroupForPolygon( PolygonID );

		static TArray<FVertexID> PolygonVertexIDs;
		this->GetPolygonPerimeterVertices( PolygonID, /* Out */ PolygonVertexIDs );

		// Create a new extruded polygon for the face
		FPolygonID ExtrudedFrontPolygonID;	// Filled in below
		{
			static TArray<FVertexAndAttributes> NewFrontPolygonVertices;
			NewFrontPolygonVertices.Reset( PolygonVertexIDs.Num() );
			NewFrontPolygonVertices.SetNum( PolygonVertexIDs.Num(), false );

			const TArray<FVertexInstanceID>& VertexInstanceIDs = GetMeshDescription()->GetPolygonVertexInstances( PolygonID );

			// Map all of the polygon's vertex IDs to their extruded counterparts to create the new polygon perimeter
			for( int32 PolygonVertexNumber = 0; PolygonVertexNumber < PolygonVertexIDs.Num(); ++PolygonVertexNumber )
			{
				const FVertexID VertexID = PolygonVertexIDs[ PolygonVertexNumber ];
				const FVertexID* ExtrudedCopyVertexIDPtr = VertexIDToExtrudedCopy.Find( VertexID );
				if( ExtrudedCopyVertexIDPtr != nullptr )
				{
					NewFrontPolygonVertices[ PolygonVertexNumber ].VertexID = VertexIDToExtrudedCopy[ VertexID ];
				}
				else
				{
					// We didn't need to extrude a new copy of this vertex (because it was part of a shared edge), so just connect the polygon to the original vertex
					NewFrontPolygonVertices[ PolygonVertexNumber ].VertexID = VertexID;
				}

				// Copy vertex instance attributes from original polygon vertex to extruded polygon vertex
				BackupAllAttributes( NewFrontPolygonVertices[ PolygonVertexNumber ].PolygonVertexAttributes, GetMeshDescription()->VertexInstanceAttributes(), VertexInstanceIDs[ PolygonVertexNumber ] );
			}

			{
				static TArray<FPolygonToCreate> PolygonsToCreate;
				PolygonsToCreate.Reset();

				// Create the polygon
				// @todo mesheditor perf: Ideally we support creating multiple polygons at once and batching up the work
				FPolygonToCreate& PolygonToCreate = *new( PolygonsToCreate ) FPolygonToCreate();
				PolygonToCreate.PolygonGroupID = PolygonGroupID;
				PolygonToCreate.PolygonEdgeHardness = EPolygonEdgeHardness::AllEdgesHard;
				PolygonToCreate.PerimeterVertices = NewFrontPolygonVertices;	// @todo mesheditor perf: Copying static array here, ideally allocations could be avoided
				static TArray<FPolygonID> NewPolygonIDs;
				NewPolygonIDs.Reset();
				static TArray<FEdgeID> NewEdgeIDs;
				NewEdgeIDs.Reset();
				CreatePolygons( PolygonsToCreate, /* Out */ NewPolygonIDs, /* Out */ NewEdgeIDs );	// @todo mesheditor perf: Extra allocatons/copies: Ideally MoveTemp() here but we can't move a STATIC local!

				ExtrudedFrontPolygonID = NewPolygonIDs[ 0 ];
			}
			AllNewPolygons.Add( ExtrudedFrontPolygonID );

			// All of the border edges of the new polygon will be hard.  If it was a shared edge, then we'll just preserve whatever was
			// originally going on with the internal edge.
			{
				const TEdgeAttributesRef<bool> EdgeHardnesses = GetMeshDescription()->EdgeAttributes().GetAttributesRef<bool>( MeshAttribute::Edge::IsHard );

				const int32 NewPerimeterEdgeCount = this->GetPolygonPerimeterEdgeCount( ExtrudedFrontPolygonID );
				check( NewPerimeterEdgeCount == GetPolygonPerimeterEdgeCount( PolygonID ) );	// New polygon should always have the same number of edges (in the same order) as the original!
				for( int32 PerimeterEdgeNumber = 0; PerimeterEdgeNumber < NewPerimeterEdgeCount; ++PerimeterEdgeNumber )
				{
					bool bOriginalEdgeWindingIsReversedForPolygon;
					const FEdgeID OriginalEdgeID = this->GetPolygonPerimeterEdge( PolygonID, PerimeterEdgeNumber, /* Out */ bOriginalEdgeWindingIsReversedForPolygon );
					const bool bIsSharedEdge = bKeepNeighborsTogether && EdgeUsageCounts[ OriginalEdgeID ] > 1;

					bool bEdgeWindingIsReversedForPolygon;
					const FEdgeID EdgeID = this->GetPolygonPerimeterEdge( ExtrudedFrontPolygonID, PerimeterEdgeNumber, /* Out */ bEdgeWindingIsReversedForPolygon );

					const bool bNewEdgeHardnessAttribute = bIsSharedEdge ? EdgeHardnesses[ OriginalEdgeID ] : true;

					FAttributesForEdge& AttributesForEdge = *new( AttributesForEdges ) FAttributesForEdge();
					AttributesForEdge.EdgeID = EdgeID;
					AttributesForEdge.EdgeAttributes.Attributes.Emplace( MeshAttribute::Edge::IsHard, 0, FMeshElementAttributeValue( bNewEdgeHardnessAttribute ) );
				}
			}
		}

		OutNewExtrudedFrontPolygons.Add( ExtrudedFrontPolygonID );
	}
	check( NextAvailableExtrudedVertexIDNumber == ExtrudedVertexIDs.Num() );	// Make sure all of the vertices we created were actually used by new polygons

																				// Update edge attributes in bulk
	SetEdgesAttributes( AttributesForEdges );

	// Update vertex attributes in bulk
	SetVerticesAttributes( AttributesForVertices );

	// Delete the original polygons
	{
		const bool bDeleteOrphanedEdges = true;
		const bool bDeleteOrphanedVertices = true;
		const bool bDeleteOrphanedVertexInstances = true;
		const bool bDeleteEmptyPolygonGroups = false;
		DeletePolygons( PolygonIDs, bDeleteOrphanedEdges, bDeleteOrphanedVertices, bDeleteOrphanedVertexInstances, bDeleteEmptyPolygonGroups );
	}
	#endif
}
