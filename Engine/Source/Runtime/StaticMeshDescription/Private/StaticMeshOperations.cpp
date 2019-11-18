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


static void RemovePolygonsAndCompact(FMeshDescription& MeshDescription, const TArray<FPolygonID>& PolygonsToRemove)
{
	if (PolygonsToRemove.Num() > 0)
	{
		TArray<FEdgeID> OrphanedEdges;
		TArray<FVertexInstanceID> OrphanedVertexInstances;
		TArray<FPolygonGroupID> OrphanedPolygonGroups;
		TArray<FVertexID> OrphanedVertices;

		for (FPolygonID PolygonID : PolygonsToRemove)
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


void FStaticMeshOperations::ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, TArrayView<const FPolygonID> PolygonIDs, float ComparisonThreshold)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.RegisterPolygonNormalAndTangentAttributes();
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
	RemovePolygonsAndCompact(MeshDescription, DegeneratePolygonIDs);
}

void FStaticMeshOperations::ComputePolygonTangentsAndNormals(FMeshDescription& MeshDescription, float ComparisonThreshold)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.RegisterPolygonNormalAndTangentAttributes();
	TVertexAttributesRef<const FVector> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<const FVector2D> VertexUVs = Attributes.GetVertexInstanceUVs();
	TPolygonAttributesRef<FVector> PolygonNormals = Attributes.GetPolygonNormals();
	TPolygonAttributesRef<FVector> PolygonTangents = Attributes.GetPolygonTangents();
	TPolygonAttributesRef<FVector> PolygonBinormals = Attributes.GetPolygonBinormals();
	TPolygonAttributesRef<FVector> PolygonCenters = Attributes.GetPolygonCenters();

	TArray<FPolygonID> DegeneratePolygonIDs;
	for (const FPolygonID PolygonID : MeshDescription.Polygons().GetElementIDs())
	{
		if (!GetPolygonTangentsAndNormals(MeshDescription, PolygonID, ComparisonThreshold, VertexPositions, VertexUVs, PolygonNormals, PolygonTangents, PolygonBinormals, PolygonCenters))
		{
			DegeneratePolygonIDs.Add(PolygonID);
		}
	}

	// Remove degenerated polygons
	RemovePolygonsAndCompact(MeshDescription, DegeneratePolygonIDs);
}


void FStaticMeshOperations::ComputeTangentsAndNormals(FMeshDescription& MeshDescription, TArrayView<const FVertexInstanceID> VertexInstanceIDs, EComputeNTBsFlags ComputeNTBsOptions)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.RegisterPolygonNormalAndTangentAttributes();
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
	Attributes.RegisterPolygonNormalAndTangentAttributes();
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
