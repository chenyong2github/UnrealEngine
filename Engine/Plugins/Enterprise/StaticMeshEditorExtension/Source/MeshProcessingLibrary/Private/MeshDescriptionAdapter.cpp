// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if WITH_MESH_SIMPLIFIER

#include "MeshDescriptionAdapter.h"

#include "Math/Plane.h"
#include "StaticMeshAttributes.h"

namespace MeshDescriptionAdapterUtils
{
	// NOTE: This is the same as FMeshDescription::GetPolygonEdges(), although the method should be renamed to GetPolygonPerimeterEdges()
	void GetPolygonPerimeterEdges(const FMeshDescription& MeshDescription, const FPolygonID& PolygonID, TArray<FEdgeID>& OutPolygonPerimeterEdgeIDs)
	{
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(PolygonID);
		int32 EdgeCount = VertexInstanceIDs.Num();

		OutPolygonPerimeterEdgeIDs.SetNumUninitialized(EdgeCount, false);

		for (int32 Index = 0; Index < EdgeCount; ++Index)
		{
			int32 NextIndex = (Index + 1) % EdgeCount;

			const FVertexID& VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Index]);
			const FVertexID& NextVertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[NextIndex]);

			OutPolygonPerimeterEdgeIDs[Index] = MeshDescription.GetVertexPairEdge(VertexID, NextVertexID);
		}
	}
}

FMeshDescriptionAdapter::FMeshDescriptionAdapter(FMeshDescription& InMeshDescription)
	: MeshDescription(InMeshDescription)
{
	// Polygon normals are a transient attribute, so register the attribute here if it is not already registered.
	MeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Normal, 1, FVector::ZeroVector, EMeshAttributeFlags::Transient);

	TPolygonAttributesRef<FVector> PolygonNormals = MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal);
	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

		// @todo: RichTW: This code is assuming polygons with 3 vertices, when a valid mesh description may have polygons with any number of vertices.
		// Are triangles the expected input here? We should check(VertexInstanceIDs.Num() == 3), or make this code more flexible.
		FVector TrianglePositions[3];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);
			TrianglePositions[Corner] = VertexPositions[VertexID];
		}

		FVector TriangleEdges[3];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			TriangleEdges[Corner] = TrianglePositions[(Corner+1)%3] - TrianglePositions[Corner];
		}

		FVector PolygonNormal(ForceInitToZero);
		PolygonNormal -= TriangleEdges[0] ^ TriangleEdges[1];
		PolygonNormal -= TriangleEdges[1] ^ TriangleEdges[2];
		PolygonNormal -= TriangleEdges[2] ^ TriangleEdges[0];

		PolygonNormal[0] *= -1.0f;  // Third party library is right handed
		PolygonNormals[TriangleID] = PolygonNormal * 0.33333333f;
	}

	// If there are more than one polygon groups, edges between groups are considered feature lines
	if (!MeshDescription.EdgeAttributes().HasAttribute( MeshDescriptionAdapterUtils::FeatureLine ))
	{
		MeshDescription.EdgeAttributes().RegisterAttribute<bool>( MeshDescriptionAdapterUtils::FeatureLine, 1, false, EMeshAttributeFlags::Transient);
	}

	if (!MeshDescription.EdgeAttributes().HasAttribute( MeshDescriptionAdapterUtils::Debug ))
	{
		MeshDescription.EdgeAttributes().RegisterAttribute<bool>( MeshDescriptionAdapterUtils::Debug, 1, false, EMeshAttributeFlags::Transient);
	}

	// If there are more than one polygon groups, edges between groups are considered feature lines
	if (!MeshDescription.EdgeAttributes().HasAttribute( MeshDescriptionAdapterUtils::EdgeLength ))
	{
		MeshDescription.EdgeAttributes().RegisterAttribute<float>( MeshDescriptionAdapterUtils::EdgeLength, 1, false, EMeshAttributeFlags::Transient);
	}

	ValidateMesh();
}

FMeshDescriptionAdapter::~FMeshDescriptionAdapter()
{
	if (MeshDescription.EdgeAttributes().HasAttribute( MeshDescriptionAdapterUtils::FeatureLine ))
	{
		MeshDescription.EdgeAttributes().UnregisterAttribute( MeshDescriptionAdapterUtils::FeatureLine );
	}

	// If there are more than one polygon groups, edges between groups are considered feature lines
	if (MeshDescription.EdgeAttributes().HasAttribute( MeshDescriptionAdapterUtils::EdgeLength ))
	{
		MeshDescription.EdgeAttributes().UnregisterAttribute( MeshDescriptionAdapterUtils::EdgeLength );
	}

	TEdgeAttributesRef<bool> DebugAttr = MeshDescription.EdgeAttributes().GetAttributesRef<bool>( MeshDescriptionAdapterUtils::Debug );
	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		int32 Index = EdgeID.GetValue();
		DebugAttr[EdgeID] = MeshDescriptionAdapterUtils::IsElementExtraSet(EdgeMetaData[Index], ELEMENT_CRITICAL_ZONE_MASK);
	}
}

int FMeshDescriptionAdapter::GetElementsCount(int& VertexCount, int& EdgeCount, int& TriangleCount)
{
	VertexCount = MeshDescription.Vertices().Num();
	EdgeCount = MeshDescription.Edges().Num();
	TriangleCount = MeshDescription.Polygons().Num();
	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetElementsCount(uint8_t CategoryMask, int& VertexCount, int& EdgeCount, int& TriangleCount)
{
	VertexCount = EdgeCount = TriangleCount = 0;

	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		const int32 Index = VertexID.GetValue();

		switch (VertexMetaData[Index].Category)
		{
		case MeshSimplifier::EElementCategory::ElementCategoryFree:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
			{
				VertexCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryLine:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
			{
				VertexCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryBorder:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
			{
				VertexCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategorySurface:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
			{
				VertexCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
			{
				VertexCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryUnused:
		default:
			break;
		}
	}

	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		const int32 Index = EdgeID.GetValue();

		switch (EdgeMetaData[Index].Category)
		{
		case MeshSimplifier::EElementCategory::ElementCategoryFree:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
			{
				EdgeCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryLine:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
			{
				EdgeCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryBorder:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
			{
				EdgeCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategorySurface:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
			{
				EdgeCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
			{
				EdgeCount++;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryUnused:
		default:
			break;
		}
	}

	TriangleCount = MeshDescription.Polygons().Num();

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetTriangleCount()
{
	return MeshDescription.Polygons().Num();
}

bool FMeshDescriptionAdapter::HasFeatureLines()
{
	TEdgeAttributesConstRef<bool> FeatureLineAttr = MeshDescription.EdgeAttributes().GetAttributesRef<bool>( MeshDescriptionAdapterUtils::FeatureLine );
	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		if (FeatureLineAttr[EdgeID] == true)
		{
			return true;
		}
	}

	return false;
}

bool FMeshDescriptionAdapter::HasNormals()
{
	// @todo: RichTW: This code is almost certainly not doing what's intended.
	// If there are gaps in the VertexInstance array, the below will be false, otherwise it will always be true.
	// Attributes' arrays are always the same size as the elements' GetArraySize() (but not necessarily the Num())
	// Maybe need to check for whether the attribute exists instead?
//	return MeshDescription.VertexInstanceAttributes().GetAttributes<FVector>(MeshAttribute::VertexInstance::Normal).Num() == MeshDescription.VertexInstances().Num();
	return true;
}

void FMeshDescriptionAdapter::SetTrianglesStatus()
{
	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Markers |= ELEMENT_STATUS_MASK;
	}
}

void FMeshDescriptionAdapter::SetEdgesStatus(uint8_t CategoryMask)
{
	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		const int32 Index = EdgeID.GetValue();

		switch (EdgeMetaData[Index].Category)
		{
		case MeshSimplifier::EElementCategory::ElementCategoryFree:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_STATUS_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryLine:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_STATUS_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryBorder:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_STATUS_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategorySurface:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_STATUS_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_STATUS_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryUnused:
		default:
			break;
		}
	}
}

void FMeshDescriptionAdapter::SetEdgesMarkers(uint8_t CategoryMask)
{
	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		const int32 Index = EdgeID.GetValue();

		switch (EdgeMetaData[Index].Category)
		{
		case MeshSimplifier::EElementCategory::ElementCategoryFree:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_FIRST_MARKER_MASK | ELEMENT_SECOND_MARKER_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryLine:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_FIRST_MARKER_MASK | ELEMENT_SECOND_MARKER_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryBorder:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_FIRST_MARKER_MASK | ELEMENT_SECOND_MARKER_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategorySurface:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_FIRST_MARKER_MASK | ELEMENT_SECOND_MARKER_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
			{
				EdgeMetaData[Index].Markers |= ELEMENT_FIRST_MARKER_MASK | ELEMENT_SECOND_MARKER_MASK;
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryUnused:
		default:
			break;
		}
	}
}

FMSTriangleID FMeshDescriptionAdapter::GetMeshFirstFace()
{
	return MeshDescription.Polygons().GetFirstValidID().GetValue();
}

void FMeshDescriptionAdapter::ResetTrianglesStatus()
{
	const uint16 MaskValue = ~ELEMENT_STATUS_MASK;

	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Markers &= MaskValue;
	}
}

void FMeshDescriptionAdapter::ResetEdgesStatus()
{
	const uint16 MaskValue = ~ELEMENT_STATUS_MASK;

	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		EdgeMetaData[EdgeID.GetValue()].Markers &= MaskValue;
	}
}

void FMeshDescriptionAdapter::ResetVerticesStatus()
{
	const uint16 MaskValue = ~ELEMENT_STATUS_MASK;

	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		VertexMetaData[VertexID.GetValue()].Markers &= MaskValue;
	}
}

int FMeshDescriptionAdapter::GetTriangles(std::vector<FMSTriangleID>& Triangles)
{
	Triangles.clear();

	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		Triangles.push_back(TriangleID.GetValue());
	}

	return (int)Triangles.size();
}

int FMeshDescriptionAdapter::GetEdges(uint8_t CategoryMask, std::vector<FMSEdgeID>& edgeSet)
{
	edgeSet.clear();

	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		const int32 Index = EdgeID.GetValue();

		switch (EdgeMetaData[Index].Category)
		{
		case MeshSimplifier::EElementCategory::ElementCategoryFree:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
			{
				edgeSet.push_back(Index);
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryLine:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
			{
				edgeSet.push_back(Index);
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryBorder:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
			{
				edgeSet.push_back(Index);
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategorySurface:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
			{
				edgeSet.push_back(Index);
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
			if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
			{
				edgeSet.push_back(Index);
			}
			break;
		case MeshSimplifier::EElementCategory::ElementCategoryUnused:
		default:
			break;
		}
	}

	return (int)edgeSet.size();
}

void FMeshDescriptionAdapter::SetStatusRecursively()
{
	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Markers |= ELEMENT_STATUS_MASK;

		TArray<FEdgeID> PolygonEdges;
		MeshDescription.GetPolygonPerimeterEdges(TriangleID, PolygonEdges);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (PolygonEdges[Corner] != FEdgeID::Invalid)
			{
				EdgeMetaData[PolygonEdges[Corner].GetValue()].Markers |= ELEMENT_STATUS_MASK;

				const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 0);
				const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 1);

				if (VertexID0 != FVertexID::Invalid)
				{
					VertexMetaData[VertexID0.GetValue()].Markers |= ELEMENT_STATUS_MASK;
				}

				if (VertexID1 != FVertexID::Invalid)
				{
					VertexMetaData[VertexID1.GetValue()].Markers |= ELEMENT_STATUS_MASK;
				}
			}
		}
	}
}

void FMeshDescriptionAdapter::SetStatusRecursively(uint8_t CategoryMask)
{
	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Markers |= ELEMENT_STATUS_MASK;

		TArray<FEdgeID> PolygonEdges;
		MeshDescription.GetPolygonPerimeterEdges(TriangleID, PolygonEdges);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (PolygonEdges[Corner] != FEdgeID::Invalid && IsEdgeOfCategory(PolygonEdges[Corner].GetValue(), CategoryMask))
			{
				EdgeMetaData[PolygonEdges[Corner].GetValue()].Markers |= ELEMENT_STATUS_MASK;

				const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 0);
				const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 1);

				if (VertexID0 != FVertexID::Invalid && IsVertexOfCategory(VertexID0.GetValue(), CategoryMask))
				{
					VertexMetaData[VertexID0.GetValue()].Markers |= ELEMENT_STATUS_MASK;
				}

				if (VertexID1 != FVertexID::Invalid && IsVertexOfCategory(VertexID1.GetValue(), CategoryMask))
				{
					VertexMetaData[VertexID1.GetValue()].Markers |= ELEMENT_STATUS_MASK;
				}
			}
		}
	}
}

void FMeshDescriptionAdapter::ResetStatusRecurcively()
{
	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Markers &= ~ELEMENT_STATUS_MASK;

		TArray<FEdgeID> PolygonEdges;
		MeshDescription.GetPolygonPerimeterEdges(TriangleID, PolygonEdges);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (PolygonEdges[Corner] != FEdgeID::Invalid)
			{
				EdgeMetaData[PolygonEdges[Corner].GetValue()].Markers &= ~ELEMENT_STATUS_MASK;

				const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 0);
				const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 1);

				if (VertexID0 != FVertexID::Invalid)
				{
					VertexMetaData[VertexID0.GetValue()].Markers &= ~ELEMENT_STATUS_MASK;
				}

				if (VertexID1 != FVertexID::Invalid)
				{
					VertexMetaData[VertexID1.GetValue()].Markers &= ~ELEMENT_STATUS_MASK;
				}
			}
		}
	}
}

void FMeshDescriptionAdapter::ResetMarkersRecursively()
{
	const uint16 Mask = ~(ELEMENT_FIRST_MARKER_MASK | ELEMENT_SECOND_MARKER_MASK);

	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Markers &= Mask;

		TArray<FEdgeID> PolygonEdges;
		MeshDescription.GetPolygonPerimeterEdges(TriangleID, PolygonEdges);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (PolygonEdges[Corner] != FEdgeID::Invalid)
			{
				EdgeMetaData[PolygonEdges[Corner].GetValue()].Markers &= Mask;

				const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 0);
				const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(PolygonEdges[Corner], 1);

				if (VertexID0 != FVertexID::Invalid)
				{
					VertexMetaData[VertexID0.GetValue()].Markers &= Mask;
				}

				if (VertexID1 != FVertexID::Invalid)
				{
					VertexMetaData[VertexID1.GetValue()].Markers &= Mask;
				}
			}
		}
	}
}

void FMeshDescriptionAdapter::SetVerticesStatus(uint8_t CategoryMask)
{
	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		VertexMetaData[VertexID.GetValue()].Markers |= ELEMENT_STATUS_MASK;
	}
}

int FMeshDescriptionAdapter::GetVertices(uint8_t CategoryMask, std::vector<FMSVertexID>& Vertices)
{
	Vertices.reserve(MeshDescription.Vertices().Num());

	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		if (IsVertexOfCategory(VertexID.GetValue(), CategoryMask))
		{
			Vertices.push_back(VertexID.GetValue());
		}
	}

	return MS_SUCCESS;
}

// Triangle Section

MeshSimplifier::EElementCategory FMeshDescriptionAdapter::GetTriangleCategory(FMSTriangleID TriangleIndex)
{
	return (MeshSimplifier::EElementCategory)TriangleMetaData[TriangleIndex].Category;
}

bool FMeshDescriptionAdapter::GetTriangleStatus(FMSTriangleID TriangleIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(TriangleMetaData[TriangleIndex], ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::SetTriangleStatus(FMSTriangleID TriangleIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(TriangleMetaData[TriangleIndex], Value, ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::ResetTriangleStatus(FMSTriangleID TriangleIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(TriangleMetaData[TriangleIndex], ELEMENT_STATUS_MASK);
}

bool FMeshDescriptionAdapter::GetTriangleFirstMarker(FMSTriangleID TriangleIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(TriangleMetaData[TriangleIndex], ELEMENT_FIRST_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetTriangleFirstMarker(FMSTriangleID TriangleIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(TriangleMetaData[TriangleIndex], Value, ELEMENT_FIRST_MARKER_MASK);
}

void FMeshDescriptionAdapter::ResetTriangleFirstMarker(FMSTriangleID TriangleIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(TriangleMetaData[TriangleIndex], ELEMENT_FIRST_MARKER_MASK);
}

bool FMeshDescriptionAdapter::GetTriangleSecondMarker(FMSTriangleID TriangleIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(TriangleMetaData[TriangleIndex], ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetTriangleSecondMarker(FMSTriangleID TriangleIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(TriangleMetaData[TriangleIndex], Value, ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::ResetTriangleSecondMarker(FMSTriangleID TriangleIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(TriangleMetaData[TriangleIndex], ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetTriangleStatusRecursively(FMSTriangleID TriangleIndex)
{
	MeshDescriptionAdapterUtils::SetElementMarker(TriangleMetaData[TriangleIndex], true, ELEMENT_STATUS_MASK);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(FPolygonID(TriangleIndex), TriangleEdges);

	for (const FEdgeID EdgeID : TriangleEdges)
	{
		SetEdgeStatusRecursively(EdgeID.GetValue());
	}
}

void FMeshDescriptionAdapter::SetTriangleStatusRecursively(FMSTriangleID TriangleIndex, uint8_t CategoryMask)
{
	MeshDescriptionAdapterUtils::SetElementMarker(TriangleMetaData[TriangleIndex], true, ELEMENT_STATUS_MASK);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(FPolygonID(TriangleIndex), TriangleEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		const FEdgeID EdgeID = TriangleEdges[Corner];

		if (IsEdgeOfCategory(EdgeID.GetValue(), CategoryMask))
		{
			SetEdgeStatusRecursively(EdgeID.GetValue());
		}

		const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
		const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

		if (IsVertexOfCategory(VertexID0.GetValue(), CategoryMask))
		{
			SetVertexStatus(VertexID0.GetValue());
		}

		if (IsVertexOfCategory(VertexID1.GetValue(), CategoryMask))
		{
			SetVertexStatus(VertexID1.GetValue());
		}
	}
}

void FMeshDescriptionAdapter::ResetTriangleStatusRecursively(FMSTriangleID TriangleIndex)
{
	MeshDescriptionAdapterUtils::SetElementMarker(TriangleMetaData[TriangleIndex], true, ELEMENT_STATUS_MASK);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(FPolygonID(TriangleIndex), TriangleEdges);

	for (const FEdgeID EdgeID : TriangleEdges)
	{
		SetEdgeStatusRecursively(EdgeID.GetValue());
	}
}

int FMeshDescriptionAdapter::GetTriangleEdges(FMSTriangleID TriangleIndex, FMSEdgeID* EdgeArray)
{
	const FPolygonID TriangleID(TriangleIndex);

	TArray<FEdgeID> TrianglesEdges;
	MeshDescription.GetPolygonPerimeterEdges(TriangleID, TrianglesEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		EdgeArray[Corner] = TrianglesEdges[Corner].GetValue();
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetEdgesAndVerticesForTriangle(FMSTriangleID TriangleIndex, FMSEdgeID* EdgeSet, FMSVertexID* NodeSet)
{
	const FPolygonID TriangleID(TriangleIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		NodeSet[Corner] = MeshDescription.GetVertexInstanceVertex( VertexInstanceIDs[Corner] ).GetValue();

		EdgeSet[Corner] = TriangleEdges[Corner].GetValue();
	}

	return MS_SUCCESS;
}

bool FMeshDescriptionAdapter::GetEdgeDirectionForTriangle(FMSTriangleID TriangleIndex, FMSEdgeID EdgeIndex)
{
	const FPolygonID TriangleID(TriangleIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		if (TriangleEdges[Corner].GetValue() == EdgeIndex)
		{
			FVertexID VertexID[2];

			VertexID[0] = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);
			VertexID[1] = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[(Corner + 1) % 3]);

			const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 0);
			const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 1);

			return (EdgeVertexID0 == VertexID[0] && EdgeVertexID1 == VertexID[1]) ? true : false;
		}
	}

	return false;
}

int FMeshDescriptionAdapter::GetEdgeDirectionsForTriangle(FMSTriangleID TriangleIndex, bool *EdgeDirections)
{
	const FPolygonID TriangleID(TriangleIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		FVertexID VertexID[2];

		VertexID[0] = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);
		VertexID[1] = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[(Corner + 1) % 3]);

		const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 0);
		const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 1);

		EdgeDirections[Corner] = (EdgeVertexID0 == VertexID[0] && EdgeVertexID1 == VertexID[1]) ? true : false;
	}

	return MS_SUCCESS;
}

FMSVertexID FMeshDescriptionAdapter::GetOppositeVertexOnEdgeForTriangle(FMSTriangleID TriangleIndex, FMSEdgeID EdgeIndex)
{
	const FPolygonID TriangleID(TriangleIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		if (TriangleEdges[Corner].GetValue() == EdgeIndex)
		{
			const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);
			const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 0);
			const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 1);

			return EdgeVertexID0 == VertexID ? EdgeVertexID1.GetValue() : EdgeVertexID0.GetValue();
		}
	}

	return BAD_INDEX;
}

FMSEdgeID FMeshDescriptionAdapter::GetOppositeEdgeAtVertexForTriangle(FMSTriangleID TriangleIndex, FMSVertexID VertexIndex)
{
	const FPolygonID TriangleID(TriangleIndex);
	const FVertexID VertexID(VertexIndex);

	TArray<FEdgeID> TriangleEdges;
	MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 0);
		const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 1);

		if (EdgeVertexID0 != VertexID && EdgeVertexID1 != VertexID)
		{
			return TriangleEdges[Corner].GetValue();
		}
	}

	return BAD_INDEX;
}

int FMeshDescriptionAdapter::GetTriangleNormal(FMSTriangleID TriangleIndex, MeshSimplifier::Vector3D& Normal)
{
	const FPolygonID TriangleID(TriangleIndex);
	const FVector Vector = MeshDescription.PolygonAttributes().GetAttribute<FVector>(TriangleID, MeshAttribute::Polygon::Normal);

	Normal.set(-Vector[0], -Vector[1], -Vector[2]);

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetTriangleInvertedNormal(FMSTriangleID TriangleIndex, MeshSimplifier::Vector3D& Normal)
{
	const FPolygonID TriangleID(TriangleIndex);
	const FVector Vector = MeshDescription.PolygonAttributes().GetAttribute<FVector>(TriangleID, MeshAttribute::Polygon::Normal);

	Normal.set(Vector[0], Vector[1], Vector[2]);

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetTriangleNormalizedNormal(FMSTriangleID TriangleIndex, MeshSimplifier::Vector3D& Normal)
{
	const FPolygonID TriangleID(TriangleIndex);
	const FVector Vector = MeshDescription.PolygonAttributes().GetAttribute<FVector>(TriangleID, MeshAttribute::Polygon::Normal).GetSafeNormal();

	Normal.set(Vector[0], Vector[1], Vector[2]);

	return MS_SUCCESS;
}

double FMeshDescriptionAdapter::GetTriangleArea(FMSTriangleID TriangleIndex)
{
	const FPolygonID TriangleID(TriangleIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>( MeshAttribute::Vertex::Position );
	FVector TrianglePositions[3];

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);

		TrianglePositions[Corner] = VertexPositions[VertexID];
	}

	FVector Normal = ( TrianglePositions[ 1 ] - TrianglePositions[ 2 ] ) ^ ( TrianglePositions[ 0 ] - TrianglePositions[ 2 ] );

	return Normal.Size() * 0.5f;
}

double FMeshDescriptionAdapter::GetTriangleFastArea(FMSTriangleID TriangleIndex)
{
	return GetTriangleArea(TriangleIndex);
}

FMSPartitionID FMeshDescriptionAdapter::GetTrianglePartition(FMSTriangleID TriangleIndex)
{
	if (IsValidTriangle(TriangleIndex))
	{
		return MeshDescription.GetPolygonPolygonGroup(FPolygonID(TriangleIndex)).GetValue();
	}

	return BAD_INDEX;
}

int FMeshDescriptionAdapter::GetTriangleVertices(FMSTriangleID TriangleIndex, FMSVertexID* VertexArray)
{
	if (!IsValidTriangle(TriangleIndex))
	{
		return MS_ERROR;
	}

	const FPolygonID TriangleID(TriangleIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		VertexArray[Corner] = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]).GetValue();
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::SetOrientationFromTriangle(FMSTriangleID TriangleIndex)
{
	SetStatusRecursively();

	TArray<FPolygonID> TriangleToProcess;
	TriangleToProcess.Add(FPolygonID(TriangleIndex));

	while (TriangleToProcess.Num() > 0)
	{
		FPolygonID TriangleID = TriangleToProcess.Pop();

		if (GetTriangleStatus(TriangleID.GetValue()))
		{
			ResetTriangleStatus(TriangleID.GetValue());

			// Swap Orientation

			TArray<FEdgeID> TriangleEdges;
			MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

			for (FEdgeID EdgeID : TriangleEdges)
			{
				if (IsEdgeOfCategory(EdgeID.GetValue(), MeshSimplifier::EElementCategory::ElementCategorySurface))
				{
					const TArray<FPolygonID>& EdgeTriangles = MeshDescription.GetEdgeConnectedPolygons(EdgeID);
					if (EdgeTriangles.Num() > 0 && EdgeTriangles.Num() < 3)
					{
						const FPolygonID OtherTriangle = TriangleID == EdgeTriangles[0] ? EdgeTriangles[1] : EdgeTriangles[0];
						if (GetEdgeDirectionAtFirstTriangle(EdgeID.GetValue()) == GetEdgeDirectionAtSecondTriangle(EdgeID.GetValue()))
						{
							TriangleToProcess.Add(OtherTriangle);
						}
					}
				}
			}
		}
	}

	return MS_SUCCESS;
}

// Edge Section

MeshSimplifier::EElementCategory FMeshDescriptionAdapter::GetEdgeCategory(FMSEdgeID EdgeIndex)
{
	return (MeshSimplifier::EElementCategory)EdgeMetaData[EdgeIndex].Category;
}

int FMeshDescriptionAdapter::SetEdgeCategory(FMSEdgeID EdgeIndex, MeshSimplifier::EElementCategory Category)
{
	EdgeMetaData[EdgeIndex].Category = (uint16)Category;
	return MS_SUCCESS;
}

bool FMeshDescriptionAdapter::IsEdgeOfCategory(FMSEdgeID EdgeIndex, uint8_t CategoryMask)
{
	switch (EdgeMetaData[EdgeIndex].Category)
	{
	case MeshSimplifier::EElementCategory::ElementCategoryFree:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryLine:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryBorder:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategorySurface:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryUnused:
	default:
		break;
	}

	return false;
}

bool FMeshDescriptionAdapter::IsEdgeOfCategory(FMSEdgeID EdgeIndex, MeshSimplifier::EElementCategory Category)
{
	return EdgeMetaData[EdgeIndex].Category == (uint16)Category;
}

bool FMeshDescriptionAdapter::GetEdgeStatus(FMSEdgeID EdgeIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(EdgeMetaData[EdgeIndex], ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::SetEdgeStatus(FMSEdgeID EdgeIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(EdgeMetaData[EdgeIndex], Value, ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::ResetEdgeStatus(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(EdgeMetaData[EdgeIndex], ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::SetEdgeStatusRecursively(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::SetElementMarker(EdgeMetaData[EdgeIndex], true, ELEMENT_STATUS_MASK);

	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[EdgeVertexID0.GetValue()], true, ELEMENT_STATUS_MASK);

	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[EdgeVertexID1.GetValue()], true, ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::ResetEdgeStatusRecursively(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(EdgeMetaData[EdgeIndex], ELEMENT_STATUS_MASK);

	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

	MeshDescriptionAdapterUtils::ResetElementMarker(VertexMetaData[EdgeVertexID0.GetValue()], ELEMENT_STATUS_MASK);

	MeshDescriptionAdapterUtils::ResetElementMarker(VertexMetaData[EdgeVertexID1.GetValue()], ELEMENT_STATUS_MASK);
}

bool FMeshDescriptionAdapter::GetEdgeFirstMarker(FMSEdgeID EdgeIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(EdgeMetaData[EdgeIndex], ELEMENT_FIRST_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetEdgeFirstMarker(FMSEdgeID EdgeIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(EdgeMetaData[EdgeIndex], Value, ELEMENT_FIRST_MARKER_MASK);
}

void FMeshDescriptionAdapter::ResetEdgeFirstMarker(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(EdgeMetaData[EdgeIndex], ELEMENT_FIRST_MARKER_MASK);
}

bool FMeshDescriptionAdapter::GetEdgeSecondMarker(FMSEdgeID EdgeIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(EdgeMetaData[EdgeIndex], ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetEdgeSecondMarker(FMSEdgeID EdgeIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(EdgeMetaData[EdgeIndex], Value, ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::ResetEdgeSecondMarker(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(EdgeMetaData[EdgeIndex], ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::ResetEdgeMarkers(FMSEdgeID EdgeIndex)
{
	EdgeMetaData[EdgeIndex].Markers = 0;
}

void FMeshDescriptionAdapter::SetEdgeMarkersRecursively(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::SetElementMarker(EdgeMetaData[EdgeIndex], true, ELEMENT_FIRST_MARKER_MASK);
	MeshDescriptionAdapterUtils::SetElementMarker(EdgeMetaData[EdgeIndex], true, ELEMENT_SECOND_MARKER_MASK);

	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[EdgeVertexID0.GetValue()], true, ELEMENT_FIRST_MARKER_MASK);
	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[EdgeVertexID0.GetValue()], true, ELEMENT_SECOND_MARKER_MASK);

	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[EdgeVertexID1.GetValue()], true, ELEMENT_FIRST_MARKER_MASK);
	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[EdgeVertexID1.GetValue()], true, ELEMENT_SECOND_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetEdgeAsCriticalZone(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::SetElementExtra(EdgeMetaData[EdgeIndex], true, ELEMENT_CRITICAL_ZONE_MASK);
}

bool FMeshDescriptionAdapter::IsEdgeInCriticalZone(FMSEdgeID EdgeIndex)
{
	return MeshDescriptionAdapterUtils::IsElementExtraSet(EdgeMetaData[EdgeIndex], ELEMENT_CRITICAL_ZONE_MASK);
}

int FMeshDescriptionAdapter::ResetEdgePartitionBorder(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::ResetElementExtra(EdgeMetaData[EdgeIndex], ELEMENT_PARTITION_BORDER_MASK);
	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::SetEdgePartitionBorder(FMSEdgeID EdgeIndex)
{
	MeshDescriptionAdapterUtils::SetElementExtra(EdgeMetaData[EdgeIndex], true, ELEMENT_PARTITION_BORDER_MASK);
	return MS_SUCCESS;
}

bool FMeshDescriptionAdapter::IsEdgeAtUVDiscontinuity(FMSEdgeID EdgeIndex)
{
	TVertexInstanceAttributesConstRef<FVector2D> MeshTextureCoordinates = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	if (MeshTextureCoordinates.GetNumIndices() == 0)
	{
		return false;
	}

	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);
	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(EdgeID);

	// Non-discontinuity for non-manifold, border or free edge
	if (EdgeConnectedPolygons.Num() != 2)
	{
		return false;
	}

	FVertexInstanceID VertexInstances[2][2];

	VertexInstances[0][0] = MeshDescription.GetVertexInstanceForPolygonVertex(EdgeConnectedPolygons[0], EdgeVertexID0);
	VertexInstances[0][1] = MeshDescription.GetVertexInstanceForPolygonVertex(EdgeConnectedPolygons[0], EdgeVertexID1);

	VertexInstances[1][0] = MeshDescription.GetVertexInstanceForPolygonVertex(EdgeConnectedPolygons[1], EdgeVertexID0);
	VertexInstances[1][1] = MeshDescription.GetVertexInstanceForPolygonVertex(EdgeConnectedPolygons[1], EdgeVertexID1);

	if (VertexInstances[0][0] != FVertexInstanceID::Invalid && VertexInstances[0][1] != FVertexInstanceID::Invalid &&
		VertexInstances[1][0] != FVertexInstanceID::Invalid && VertexInstances[1][1] != FVertexInstanceID::Invalid)
	{
		FVector2D VertexInstanceTexCoords[2][2];

		const int32 UVIndex = 0;	// Use UV0
		VertexInstanceTexCoords[0][0] = MeshTextureCoordinates.Get(VertexInstances[0][0], UVIndex);
		VertexInstanceTexCoords[0][1] = MeshTextureCoordinates.Get(VertexInstances[0][1], UVIndex);
		VertexInstanceTexCoords[1][0] = MeshTextureCoordinates.Get(VertexInstances[1][0], UVIndex);
		VertexInstanceTexCoords[1][1] = MeshTextureCoordinates.Get(VertexInstances[1][1], UVIndex);

		return VertexInstanceTexCoords[0][0] != VertexInstanceTexCoords[1][0] || VertexInstanceTexCoords[0][1] != VertexInstanceTexCoords[1][1];
	}

	return false;
}

int FMeshDescriptionAdapter::GetConnectingTrianglesAtEdge(FMSEdgeID EdgeIndex, FMSTriangleID* TriangleIndices)
{
	TriangleIndices[0] = TriangleIndices[1] = BAD_INDEX;
	if (!IsValidEdge(EdgeIndex))
	{
		return MS_ERROR;
	}

	const TArray<FPolygonID>& EdgePolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	TriangleIndices[0] = EdgePolygons.Num() == 0 ? BAD_INDEX : EdgePolygons[0].GetValue();
	TriangleIndices[1] = EdgePolygons.Num() < 2 ? BAD_INDEX : EdgePolygons[1].GetValue();

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetConnectingTrianglesAtEdge(FMSEdgeID EdgeIndex, std::vector<FMSTriangleID> &TriangleIndices)
{
	TriangleIndices.clear();

	const TArray<FPolygonID>& EdgePolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	if (EdgePolygons.Num() > 0)
	{
		TriangleIndices.reserve(2);

		TriangleIndices.push_back(EdgePolygons[0].GetValue());

		if (EdgePolygons.Num() > 1)
		{
			TriangleIndices.push_back(EdgePolygons[1].GetValue());
		}
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetEdgeTriangles(FMSEdgeID EdgeIndex, std::vector<FMSTriangleID>& Triangles)
{
	const TArray<FPolygonID>& EdgePolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	Triangles.clear();

	if (EdgePolygons.Num() > 0)
	{
		Triangles.resize(EdgePolygons.Num());

		int32 Index = 0;
		for (const FPolygonID TriangleID : EdgePolygons)
		{
			Triangles[Index++] = TriangleID.GetValue();
		}
	}

	return (int)Triangles.size();
}

FMSVertexID FMeshDescriptionAdapter::GetEdgeOtherVertex(FMSEdgeID EdgeIndex, const FMSVertexID VertexIndex)
{
	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

	return EdgeVertexID0 == FVertexID(VertexIndex) ? EdgeVertexID1.GetValue() : EdgeVertexID0.GetValue();
}

FMSVertexID FMeshDescriptionAdapter::GetEdgeStartingNode(FMSEdgeID EdgeIndex)
{
	return MeshDescription.GetEdgeVertex(FEdgeID(EdgeIndex), 0).GetValue();
}

FMSVertexID FMeshDescriptionAdapter::GetEdgeEndingNode(FMSEdgeID EdgeIndex)
{
	return MeshDescription.GetEdgeVertex(FEdgeID(EdgeIndex), 1).GetValue();
}

FMSTriangleID FMeshDescriptionAdapter::GetEdgeFirstTriangle(FMSEdgeID EdgeIndex)
{
	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	return EdgeConnectedPolygons.Num() == 0 ? BAD_INDEX : EdgeConnectedPolygons[0].GetValue();
}

FMSTriangleID FMeshDescriptionAdapter::GetEdgeSecondTriangle(FMSEdgeID EdgeIndex)
{
	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	return EdgeConnectedPolygons.Num() < 2 ? BAD_INDEX : EdgeConnectedPolygons[1].GetValue();
}

FMSTriangleID FMeshDescriptionAdapter::GetOtherTriangleAtEdge(FMSEdgeID EdgeIndex, FMSTriangleID TriangleIndex)
{
	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	return EdgeConnectedPolygons.Num() < 2 ? BAD_INDEX : (EdgeConnectedPolygons[0].GetValue() == TriangleIndex ? EdgeConnectedPolygons[1].GetValue() : EdgeConnectedPolygons[0].GetValue());
}

bool FMeshDescriptionAdapter::IsOnFeatureLine(FMSEdgeID EdgeIndex)
{
	TEdgeAttributesConstRef<bool> FeatureLineAttr = MeshDescription.EdgeAttributes().GetAttributesRef<bool>( MeshDescriptionAdapterUtils::FeatureLine );
	return FeatureLineAttr[FEdgeID(EdgeIndex)];
}

int FMeshDescriptionAdapter::RemoveAsFeatureLine(FMSEdgeID EdgeIndex)
{
	TEdgeAttributesRef<bool> FeatureLineAttr = MeshDescription.EdgeAttributes().GetAttributesRef<bool>( MeshDescriptionAdapterUtils::FeatureLine );
	FeatureLineAttr[FEdgeID(EdgeIndex)] = false;
	return MS_SUCCESS;
}

FMSEdgeID FMeshDescriptionAdapter::FindEdgeFromVertices(FMSVertexID StartVertexIndex, FMSVertexID EndVertexIndex)
{
	FEdgeID EdgeID = MeshDescription.GetVertexPairEdge(FVertexID(StartVertexIndex), FVertexID(EndVertexIndex));
	return EdgeID == FEdgeID::Invalid ? BAD_INDEX : EdgeID.GetValue();
}

bool FMeshDescriptionAdapter::GetEdgeDirectionAtFirstTriangle(FMSEdgeID EdgeIndex)
{
	FEdgeID EdgeID(EdgeIndex);

	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(EdgeID);

	if (EdgeConnectedPolygons.Num() > 0)
	{
		const FPolygonID TriangleID = EdgeConnectedPolygons[0];
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

		TArray<FEdgeID> TriangleEdges;
		MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (TriangleEdges[Corner] == EdgeID)
			{
				const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);

				return MeshDescription.GetEdgeVertex(EdgeID, 0) == VertexID ? true : false;
			}
		}
	}

	return false;
}

bool FMeshDescriptionAdapter::GetEdgeDirectionAtSecondTriangle(FMSEdgeID EdgeIndex)
{
	FEdgeID EdgeID(EdgeIndex);

	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(EdgeID);

	if (EdgeConnectedPolygons.Num() > 1)
	{
		const FPolygonID TriangleID = EdgeConnectedPolygons[1];
		const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(TriangleID);

		TArray<FEdgeID> TriangleEdges;
		MeshDescription.GetPolygonPerimeterEdges(TriangleID, TriangleEdges);

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (TriangleEdges[Corner] == EdgeID)
			{
				const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]);

				return MeshDescription.GetEdgeVertex(EdgeID, 0) == VertexID ? true : false;
			}
		}
	}

	return false;
}

FMSVertexID FMeshDescriptionAdapter::GetSharedVerticesBetweenEdges(FMSEdgeID EdgeIndex, FMSEdgeID OtherEdgeIndex)
{
	if (EdgeIndex == BAD_INDEX || OtherEdgeIndex == BAD_INDEX)
	{
		return BAD_INDEX;
	}

	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

	const FEdgeID OtherEdgeID(OtherEdgeIndex);
	const FVertexID OtherEdgeVertexID0 = MeshDescription.GetEdgeVertex(OtherEdgeID, 0);
	const FVertexID OtherEdgeVertexID1 = MeshDescription.GetEdgeVertex(OtherEdgeID, 1);

	FVertexID VertexID = EdgeVertexID0 == OtherEdgeVertexID0 || EdgeVertexID0 == OtherEdgeVertexID1 ? EdgeVertexID0 : ( EdgeVertexID1 == OtherEdgeVertexID0 || EdgeVertexID1 == OtherEdgeVertexID1 ? EdgeVertexID1 : FVertexID::Invalid);

	return VertexID == FVertexID::Invalid ? BAD_INDEX : VertexID.GetValue();
}

bool FMeshDescriptionAdapter::IsEdgeLimitedByVertex(FMSEdgeID EdgeIndex, FMSVertexID VertexIndex)
{
	const FEdgeID EdgeID(EdgeIndex);
	const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);
	const FVertexID VertexID(VertexIndex);

	return EdgeVertexID0 == VertexID ? true : (EdgeVertexID1 == VertexID ? true : false);
}

double FMeshDescriptionAdapter::GetEdgeLength(FMSEdgeID EdgeIndex)
{
	if (EdgeIndex == BAD_INDEX)
	{
		return 0.;
	}

	return MeshDescription.EdgeAttributes().GetAttribute<float>( FEdgeID(EdgeIndex), MeshDescriptionAdapterUtils::EdgeLength );
}

// Vertex Section

bool FMeshDescriptionAdapter::IsVertexOfCategory(FMSVertexID VertexIndex, uint8_t CategoryMask)
{
	switch (VertexMetaData[VertexIndex].Category)
	{
	case MeshSimplifier::EElementCategory::ElementCategoryFree:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskFree) == MeshSimplifier::ECategoryMask::CategoryMaskFree)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryLine:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskLine) == MeshSimplifier::ECategoryMask::CategoryMaskLine)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryBorder:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskBorder) == MeshSimplifier::ECategoryMask::CategoryMaskBorder)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategorySurface:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskSurface) == MeshSimplifier::ECategoryMask::CategoryMaskSurface)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryNonManifold:
		if ((CategoryMask & MeshSimplifier::ECategoryMask::CategoryMaskNonManifold) == MeshSimplifier::ECategoryMask::CategoryMaskNonManifold)
		{
			return true;
		}
		break;
	case MeshSimplifier::EElementCategory::ElementCategoryUnused:
	default:
		break;
	}

	return false;
}

bool FMeshDescriptionAdapter::IsVertexOfCategory(FMSVertexID VertexIndex, MeshSimplifier::EElementCategory Category)
{
	return VertexMetaData[VertexIndex].Category == (uint16)Category;
}

bool FMeshDescriptionAdapter::GetVertexStatus(FMSVertexID VertexIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(VertexMetaData[VertexIndex], ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::SetVertexStatus(FMSVertexID VertexIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[VertexIndex], Value, ELEMENT_STATUS_MASK);
}

void FMeshDescriptionAdapter::ResetVertexStatus(FMSVertexID VertexIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(VertexMetaData[VertexIndex], ELEMENT_STATUS_MASK);
}

bool FMeshDescriptionAdapter::GetVertexFirstMarker(FMSVertexID VertexIndex)
{
	return MeshDescriptionAdapterUtils::IsElementMarkerSet(VertexMetaData[VertexIndex], ELEMENT_FIRST_MARKER_MASK);
}

void FMeshDescriptionAdapter::SetVertexFirstMarker(FMSVertexID VertexIndex, bool Value /*= true*/)
{
	MeshDescriptionAdapterUtils::SetElementMarker(VertexMetaData[VertexIndex], Value, ELEMENT_FIRST_MARKER_MASK);
}

void FMeshDescriptionAdapter::ResetVertexFirstMarker(FMSVertexID VertexIndex)
{
	MeshDescriptionAdapterUtils::ResetElementMarker(VertexMetaData[VertexIndex], ELEMENT_FIRST_MARKER_MASK);
}

MeshSimplifier::EElementCategory FMeshDescriptionAdapter::GetVertexCategory(FMSVertexID VertexIndex)
{
	return (MeshSimplifier::EElementCategory)VertexMetaData[VertexIndex].Category;
}

void FMeshDescriptionAdapter::SetVertexAsCriticalZone(FMSVertexID VertexIndex)
{
	MeshDescriptionAdapterUtils::SetElementExtra(VertexMetaData[VertexIndex], true, ELEMENT_CRITICAL_ZONE_MASK);
}

bool FMeshDescriptionAdapter::IsVertexInCriticalZone(FMSVertexID VertexIndex)
{
	return MeshDescriptionAdapterUtils::IsElementExtraSet(VertexMetaData[VertexIndex], ELEMENT_CRITICAL_ZONE_MASK);
}

int FMeshDescriptionAdapter::GetVertexCoordinates(FMSVertexID VertexIndex, MeshSimplifier::Point3D& Coordinates) const
{
	const FVector VertexPosition = MeshDescription.VertexAttributes().GetAttribute<FVector>(FVertexID(VertexIndex), MeshAttribute::Vertex::Position);

	Coordinates[0] = -VertexPosition[0]; // Third party library is right-handed
	Coordinates[1] = VertexPosition[1];
	Coordinates[2] = VertexPosition[2];

	return MS_SUCCESS;
}

MeshSimplifier::Point3D FMeshDescriptionAdapter::GetVertexCoordinates(FMSVertexID VertexIndex)
{
	const FVector VertexPosition = MeshDescription.VertexAttributes().GetAttribute<FVector>(FVertexID(VertexIndex), MeshAttribute::Vertex::Position);
	return MeshSimplifier::Point3D( /*Third party library is right-handed*/-VertexPosition[0], VertexPosition[1], VertexPosition[2]);
}

int FMeshDescriptionAdapter::GetConnectingEdgesAtVertex(FMSVertexID VertexIndex, std::vector<FMSEdgeID>& OutEdges)
{
	const TArray<FEdgeID>& VertexEdges = MeshDescription.GetVertexConnectedEdges(FVertexID(VertexIndex));

	OutEdges.resize(VertexEdges.Num());
	for (int32 Index = 0; Index < VertexEdges.Num(); ++Index)
	{
		OutEdges[Index] = VertexEdges[Index].GetValue();
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetEdgesAtVertex(FMSVertexID VertexIndex, std::vector<FMSEdgeID>& Edges)
{
	if (!IsValidVertex(VertexIndex))
	{
		return 0;
	}

	const TArray<FEdgeID>& VertexEdges = MeshDescription.GetVertexConnectedEdges(FVertexID(VertexIndex));

	Edges.clear();

	if (VertexEdges.Num() > 0)
	{
		Edges.resize(VertexEdges.Num());

		for (int32 Index = 0; Index < VertexEdges.Num(); ++Index)
		{
			Edges[Index] = VertexEdges[Index].GetValue();
		}
	}

	return Edges.size();
}

int FMeshDescriptionAdapter::GetVertexConnectingTriangles(FMSVertexID VertexIndex, std::unordered_set<FMSTriangleID> &OutTriangles)
{
	const TArray<FEdgeID>& EdgeIDs = MeshDescription.GetVertexConnectedEdges(FVertexID(VertexIndex));

	OutTriangles.clear();

	for (const FEdgeID EdgeID : EdgeIDs)
	{
		const TArray<FPolygonID>& EdgeTriangles = MeshDescription.GetEdgeConnectedPolygons(EdgeID);

		for (const FPolygonID TriangleID : EdgeTriangles)
		{
			OutTriangles.insert(TriangleID.GetValue());
		}
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::ValidateVertexTopology(FMSVertexID VertexIndex)
{
	if (!IsValidVertex(VertexIndex))
	{
		if (VertexMetaData.IsValidIndex(VertexIndex))
		{
			MeshDescriptionAdapterUtils::ResetElementData(VertexMetaData[VertexIndex]);
		}
		return MS_ERROR;
	}

	// Logic took from GPNode::evaluateConnectivity
	const FVertexID VertexID(VertexIndex);
	const TArray<FEdgeID>& VertexConnectedEdgeIDs = MeshDescription.GetVertexConnectedEdges(VertexID);

	switch (VertexConnectedEdgeIDs.Num())
	{
	case 0:
		VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryFree;
		break;

	case 1:
		VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder;
		break;
	default:
		{
			int CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryMax] = { 0 };
			for (const FEdgeID EdgeID : VertexConnectedEdgeIDs)
			{
				CountPerCategory[EdgeMetaData[EdgeID.GetValue()].Category]++;
			}

			if (CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold] > 0)
			{
				VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
			}
			else if (CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder] > 0)
			{
				// is the neighborhood of this node be homeomorphic to an half disc ?
				if (CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder] == 2 && CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryLine] == 0)
				{
					// find the first border edge
					const int32 EdgeCount = VertexConnectedEdgeIDs.Num();
					int32 EdgeIndex = 0;
					while (EdgeIndex < EdgeCount && EdgeMetaData[VertexConnectedEdgeIDs[EdgeIndex].GetValue()].Category != (uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder) ++EdgeIndex;

					if (EdgeIndex < EdgeCount)
					{
						// count the number of face between the both border edge
						FEdgeID EdgeID(VertexConnectedEdgeIDs[EdgeIndex]);
						FPolygonID TriangleID(FPolygonID::Invalid);
						int32 TriangleCount = 0;

						do
						{
							const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(EdgeID);
							// Border edge no more triangles to process, exit from the loop
							if (EdgeConnectedPolygons.Num() < 2)
							{
								break;
							}

							TriangleID = TriangleID == EdgeConnectedPolygons[0] ? EdgeConnectedPolygons[1] : EdgeConnectedPolygons[0];
							++TriangleCount;

							TArray<FEdgeID> TriangleEdges;
							MeshDescriptionAdapterUtils::GetPolygonPerimeterEdges(MeshDescription, TriangleID, TriangleEdges);

							for (int32 Corner = 0; Corner < 3; ++Corner)
							{
								if (EdgeID != TriangleEdges[Corner])
								{
									const FEdgeID NextEdgeID = TriangleEdges[Corner];
									const FVertexID NextEdgeVertexID0 = MeshDescription.GetEdgeVertex(NextEdgeID, 0);
									const FVertexID NextEdgeVertexID1 = MeshDescription.GetEdgeVertex(NextEdgeID, 1);
									if (NextEdgeVertexID0 == VertexID || NextEdgeVertexID1 == VertexID)
									{
										EdgeID = TriangleEdges[Corner];
										break;
									}
								}
							}
						} while (TriangleCount < EdgeCount && EdgeMetaData[EdgeID.GetValue()].Category != (uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder);

						++TriangleCount;

						VertexMetaData[VertexIndex].Category = TriangleCount == EdgeCount ? (uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder : (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
					}
					else
					{
						VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
					}

				}
				else
				{
					VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
				}
			}
			else if (CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryLine] > 0)
			{
				if(CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategoryLine] == 2 && VertexConnectedEdgeIDs.Num() == 2)
				{
					VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryLine;
				}
				else
				{
					VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
				}
			}
			else if (CountPerCategory[(uint16)MeshSimplifier::EElementCategory::ElementCategorySurface] > 0)
			{
				const int32 EdgeCount = VertexConnectedEdgeIDs.Num();
				const FEdgeID FirstEdgeID(VertexConnectedEdgeIDs[0]);
				FEdgeID EdgeID(FirstEdgeID);

				FPolygonID TriangleID(FPolygonID::Invalid);
				int32 TriangleCount = 0;

				do
				{
					const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(EdgeID);
					// Border edge no more triangles to process, exit from the loop
					if (EdgeConnectedPolygons.Num() < 2)
					{
						break;
					}

					TriangleID = TriangleID == EdgeConnectedPolygons[0] ? EdgeConnectedPolygons[1] : EdgeConnectedPolygons[0];
					++TriangleCount;

					TArray<FEdgeID> TriangleEdges;
					MeshDescriptionAdapterUtils::GetPolygonPerimeterEdges(MeshDescription, TriangleID, TriangleEdges);

					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						if (EdgeID != TriangleEdges[Corner])
						{
							const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 0);
							const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(TriangleEdges[Corner], 1);
							if (EdgeVertexID0 == VertexID || EdgeVertexID1 == VertexID)
							{
								EdgeID = TriangleEdges[Corner];
								break;
							}
						}
					}
				} while (EdgeID != FirstEdgeID);

				VertexMetaData[VertexIndex].Category = TriangleCount == EdgeCount ? (uint16)MeshSimplifier::EElementCategory::ElementCategorySurface : (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
			}
			else
			{
				VertexMetaData[VertexIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryUndefined;
			}
		}
		break;
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::ValidateEdgeTopology(FMSEdgeID EdgeIndex)
{
	if (!IsValidEdge(EdgeIndex))
	{
		if (EdgeMetaData.IsValidIndex(EdgeIndex))
		{
			MeshDescriptionAdapterUtils::ResetElementData(EdgeMetaData[EdgeIndex]);
		}
		return MS_ERROR;
	}

	const TArray<FPolygonID>& EdgeConnectedPolygons = MeshDescription.GetEdgeConnectedPolygons(FEdgeID(EdgeIndex));

	switch (EdgeConnectedPolygons.Num())
	{
	case 0:
		EdgeMetaData[EdgeIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryLine;
		break;
	case 1:
		EdgeMetaData[EdgeIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryBorder;
		break;
	case 2:
		EdgeMetaData[EdgeIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategorySurface;
		break;
	default:
		EdgeMetaData[EdgeIndex].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryNonManifold;
		break;
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::ValidateMesh()
{
	TriangleMetaData.SetNumZeroed(MeshDescription.Polygons().GetArraySize());
	EdgeMetaData.SetNumZeroed(MeshDescription.Edges().GetArraySize());
	VertexMetaData.SetNumZeroed(MeshDescription.Vertices().GetArraySize());

	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TPolygonAttributesConstRef<FVector> PolygonNormals = MeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal);
	for (const FPolygonID TriangleID : MeshDescription.Polygons().GetElementIDs())
	{
		TriangleMetaData[TriangleID.GetValue()].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategorySurface;
	}

	// If there are more than one polygon groups, edges between groups are considered feature lines
	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		ValidateEdgeTopology(EdgeID.GetValue());
	}

	for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
	{
		ValidateVertexTopology(VertexID.GetValue());
	}

	TEdgeAttributesRef<bool> FeatureLineAttr = MeshDescription.EdgeAttributes().GetAttributesRef<bool>( MeshDescriptionAdapterUtils::FeatureLine );
	TEdgeAttributesRef<float> EdgeLengthAttr = MeshDescription.EdgeAttributes().GetAttributesRef<float>( MeshDescriptionAdapterUtils::EdgeLength );
	TEdgeAttributesConstRef<bool> EdgeHardnesses = MeshDescription.EdgeAttributes().GetAttributesRef<bool>( MeshAttribute::Edge::IsHard );

	for (const FEdgeID EdgeID : MeshDescription.Edges().GetElementIDs())
	{
		bool Value = EdgeHardnesses[EdgeID];

		const TArray<FPolygonID>& EdgeTriangles = MeshDescription.GetEdgeConnectedPolygons(EdgeID);
		if (EdgeTriangles.Num() == 2 && MeshDescription.GetPolygonPolygonGroup(EdgeTriangles[0]) != MeshDescription.GetPolygonPolygonGroup(EdgeTriangles[1]))
		{
			Value = true;
		}

		FeatureLineAttr[EdgeID] = Value;

		const FVertexID EdgeVertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
		const FVertexID EdgeVertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

		EdgeLengthAttr[EdgeID] = (VertexPositions[EdgeVertexID1] - VertexPositions[EdgeVertexID0]).Size();
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::GetTriangleNormalAtVertex(FMSVertexID VertexIndex, FMSTriangleID TriangleIndex, MeshSimplifier::Point3D& Normal)
{
	const FVertexID VertexID(VertexIndex);
	const TArray<FVertexInstanceID>& VertexInstanceIDs = MeshDescription.GetPolygonVertexInstances(FPolygonID(TriangleIndex));
	TVertexInstanceAttributesConstRef<FVector> VertexNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);

	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		if (VertexID == MeshDescription.GetVertexInstanceVertex(VertexInstanceIDs[Corner]))
		{
			const FVector VertexInstanceNormal = VertexNormals[VertexInstanceIDs[Corner]];

			Normal[0] = -VertexInstanceNormal[0];
			Normal[1] = VertexInstanceNormal[1];
			Normal[2] = VertexInstanceNormal[2];

			return MS_SUCCESS;
		}
	}

	return MS_ERROR;
}

int FMeshDescriptionAdapter::GetTriangleStarAtVertex(FMSVertexID VertexIndex, std::vector<FMSTriangleID>& OutTriangles)
{
	TArray<FPolygonID> VertexTriangles;
	MeshDescription.GetVertexConnectedPolygons(FVertexID(VertexIndex), VertexTriangles);

	OutTriangles.resize(VertexTriangles.Num());

	for (int32 Index = 0; Index < VertexTriangles.Num(); ++Index)
	{
		OutTriangles[Index] = VertexTriangles[Index].GetValue();
	}

	return MS_SUCCESS;
}

void FMeshDescriptionAdapter::ValidateTriangles(std::vector<FMSTriangleID>& Triangles)
{
	std::set<FMSTriangleID> OtherTriangles;

	for (FMSTriangleID Triangle : Triangles)
	{
		FMSVertexID VertexSet[3];

		GetTriangleVertices(Triangle, VertexSet);

		for (int Corner = 0; Corner < 3; ++Corner)
		{
			std::vector<FMSTriangleID> NodeTriangles;
			GetTriangleStarAtVertex(VertexSet[Corner], NodeTriangles);
			OtherTriangles.insert(NodeTriangles.begin(), NodeTriangles.end());
		}
	}

	FPolygonGroupID PolygonGroupID = FPolygonGroupID::Invalid;
	for (FMSTriangleID TriangleIndex : OtherTriangles)
	{
		// TODO : Find the right one
		const FPolygonGroupID TrianglePolygonGroupID = MeshDescription.GetPolygonPolygonGroup(FPolygonID(TriangleIndex));
		if (TrianglePolygonGroupID != FPolygonGroupID::Invalid)
		{
			PolygonGroupID = TrianglePolygonGroupID;
			break;
		}
	}

	if (PolygonGroupID != FPolygonGroupID::Invalid)
	{
		for (FMSTriangleID TriangleIndex : Triangles)
		{
			// RichTW: I think this fixes a bug. The polygon group IDs previously did not have their reverse pointers back to member PolygonIDs patched up correctly.
			MeshDescription.SetPolygonPolygonGroup(FPolygonID(TriangleIndex), PolygonGroupID);
		}
	}
}

bool FMeshDescriptionAdapter::ValidateMeshNodes(int VertexCount, FMSVertexID* Vertices, std::vector<FMSTriangleID>& Triangles)
{
	// No action. Kept for testing with GPure which was doing some updates on UVs
	return true;
}

int FMeshDescriptionAdapter::DeleteIsolatedElements()
{
	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::CreateTriangle(FMSVertexID VertexIndexA, FMSVertexID VertexIndexB, FMSVertexID VertexIndexC, std::vector<FMSTriangleID>& Triangles, FMSPartitionID Partition)
{
	if (!IsValidVertex(VertexIndexA) || !IsValidVertex(VertexIndexB) || !IsValidVertex(VertexIndexC))
	{
		return MS_ERROR;
	}

	FVertexID	VertexIDs[3] = { FVertexID(VertexIndexA), FVertexID(VertexIndexB), FVertexID(VertexIndexC) };

	FVector A = MeshDescriptionAdapterUtils::ConvertFromPoint3D(GetVertexCoordinates(VertexIndexA));
	FVector B = MeshDescriptionAdapterUtils::ConvertFromPoint3D(GetVertexCoordinates(VertexIndexB));
	FVector C = MeshDescriptionAdapterUtils::ConvertFromPoint3D(GetVertexCoordinates(VertexIndexC));

	FVector TriangleNormal = ((B - A) ^ (C - A)).GetSafeNormal();

	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = MeshDescription.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	int32 NumTexCoords = VertexInstanceUVs.GetNumIndices();

	// Try to reuse existing Vertex instances to create the new polygon
	TArray<FVertexInstanceID> TriangleVertexInstanceIDs;
	TriangleVertexInstanceIDs.SetNum(3);
	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		// vertex instance's color, uvs are copied from first instance of vertex if such instance exists
		const TArray<FVertexInstanceID>& VertexInstances = MeshDescription.GetVertexVertexInstances(VertexIDs[Corner]);

		FVertexInstanceID VertexInstanceID = FVertexInstanceID::Invalid;
		for (const FVertexInstanceID CandidateVertexInstance : VertexInstances)
		{
			if ((VertexInstanceNormals[CandidateVertexInstance] ^ TriangleNormal).SizeSquared() < 0.00001f)
			{
				VertexInstanceID = CandidateVertexInstance;
				break;
			}
		}

		// No match, let's create a new one
		if (VertexInstanceID == FVertexInstanceID::Invalid)
		{
			VertexInstanceID = MeshDescription.CreateVertexInstance(VertexIDs[Corner]);

			VertexInstanceColors[VertexInstanceID] = VertexInstances.Num() == 0 ? FLinearColor::White : VertexInstanceColors[VertexInstances[0]];
			VertexInstanceNormals[VertexInstanceID] = TriangleNormal;
			VertexInstanceTangents[VertexInstanceID] = VertexInstances.Num() == 0 ? FVector(ForceInitToZero) : VertexInstanceTangents[VertexInstances[0]];
			VertexInstanceBinormalSigns[VertexInstanceID] = VertexInstances.Num() == 0 ? 0.0f : VertexInstanceBinormalSigns[VertexInstances[0]];

			for (int32 TextureCoordinnateIndex = 0; TextureCoordinnateIndex < NumTexCoords; ++TextureCoordinnateIndex)
			{
				VertexInstanceUVs.Set(VertexInstanceID, TextureCoordinnateIndex, VertexInstances.Num() == 0 ? FVector2D(ForceInitToZero) : VertexInstanceUVs.Get(VertexInstances[0], TextureCoordinnateIndex));
			}
		}

		TriangleVertexInstanceIDs[Corner] = VertexInstanceID;
	}

	for (uint32 Corner = 0; Corner < 3; ++Corner)
	{
		//Find the matching edge ID
		int32 CornerIndices[2];
		CornerIndices[0] = (Corner + 0) % 3;
		CornerIndices[1] = (Corner + 1) % 3;

		FVertexID EdgeVertexIDs[2];
		EdgeVertexIDs[0] = VertexIDs[CornerIndices[0]];
		EdgeVertexIDs[1] = VertexIDs[CornerIndices[1]];

		FEdgeID MatchEdgeId = MeshDescription.GetVertexPairEdge(EdgeVertexIDs[0], EdgeVertexIDs[1]);
		if (MatchEdgeId == FEdgeID::Invalid)
		{
			MatchEdgeId = MeshDescription.CreateEdge(EdgeVertexIDs[0], EdgeVertexIDs[1]);
			int32 Increment = MeshDescription.Edges().GetArraySize() - EdgeMetaData.Num();
			if (Increment > 0)
			{
				EdgeMetaData.AddZeroed(Increment);
			}
			EdgeMetaData[MatchEdgeId.GetValue()].Category = (uint16_t)MeshSimplifier::EElementCategory::ElementCategoryBorder;
		}
	}

	FPolygonGroupID PolygonGroupID(Partition);
	if (!MeshDescription.IsPolygonGroupValid(PolygonGroupID))
	{
		PolygonGroupID = *MeshDescription.PolygonGroups().GetElementIDs().CreateConstIterator();
	}
	const FPolygonID NewPolygonID = MeshDescription.CreatePolygon(PolygonGroupID, TriangleVertexInstanceIDs);

	MeshDescription.PolygonAttributes().SetAttribute(NewPolygonID, MeshAttribute::Polygon::Normal, 0, TriangleNormal);

	{
		int32 Increment = MeshDescription.Polygons().GetArraySize() - TriangleMetaData.Num();
		if (Increment > 0)
		{
			TriangleMetaData.AddZeroed(Increment);
		}
		TriangleMetaData[NewPolygonID.GetValue()].Category = (uint16)MeshSimplifier::EElementCategory::ElementCategorySurface;
	}

	Triangles.push_back(NewPolygonID.GetValue());

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::DeleteVertex(FMSVertexID VertexIndex)
{
	if (!IsValidVertex(VertexIndex))
	{
		return MS_ERROR;
	}

	const FVertexID VertexID(VertexIndex);

	TArray<FEdgeID> EdgeIDsToDelete(MeshDescription.GetVertexConnectedEdges(VertexID));

	for (const FEdgeID EdgeIDToDelete : EdgeIDsToDelete)
	{
		// Make sure the edge still exists.  It may have been deleted as a polygon's edges were deleted during
		// a previous iteration through this loop.
		if (MeshDescription.IsEdgeValid(EdgeIDToDelete))
		{
			TArray<FPolygonID> PolygonIDsToDelete(MeshDescription.GetEdgeConnectedPolygons(EdgeIDToDelete));

			for (FPolygonID PolygonIDToDelete : PolygonIDsToDelete)
			{
				DeleteTriangle(PolygonIDToDelete.GetValue());
			}
		}
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::DeleteTriangle(FMSTriangleID TriangleIndex)
{
	if (!IsValidTriangle(TriangleIndex))
	{
		return MS_ERROR;
	}

	// FROM : UEditableMesh::DeletePolygons
	TArray<FEdgeID> OrphanedEdgeIDs;
	TArray<FVertexInstanceID> OrphanedVertexInstanceIDs;
	TArray<FPolygonGroupID> EmptyPolygonGroupIDs;

	FMSEdgeID TriangleEdges[3];
	GetTriangleEdges(TriangleIndex, TriangleEdges);

	MeshDescription.DeletePolygon(FPolygonID(TriangleIndex), &OrphanedEdgeIDs, &OrphanedVertexInstanceIDs, &EmptyPolygonGroupIDs);

	MeshDescriptionAdapterUtils::ResetElementData(TriangleMetaData[TriangleIndex]);

	// Remove vertex instances which are exclusively used by this polygon.
	// We do not want this to remove orphaned vertices; this will optionally happen below when removing edges.
	if (OrphanedVertexInstanceIDs.Num() > 0)
	{
		for (FVertexInstanceID VertexInstanceID : OrphanedVertexInstanceIDs)
		{
			MeshDescription.DeleteVertexInstance(VertexInstanceID, nullptr /* Do not delete isolated vertex yet. */);
		}
	}

	// Remove any edges which may have been orphaned. This may also optionally remove any orphaned vertices.
	// We can do this here because we know any edges which were orphaned will have had only a single vertex instance at each vertex.
	// Therefore the vertex will now have no instances further to deleting them above.
	// Note: there is never a situation where there could be orphaned vertices but not orphaned edges.
	if (OrphanedEdgeIDs.Num() > 0)
	{
		for (const FEdgeID EdgeID : OrphanedEdgeIDs)
		{
			DeleteEdge(EdgeID.GetValue());
		}
	}

	// Remove any empty polygon groups which may have resulted
	if (EmptyPolygonGroupIDs.Num() > 0)
	{
		for (const FPolygonGroupID PolygonGroupID : EmptyPolygonGroupIDs)
		{
			MeshDescription.DeletePolygonGroup(PolygonGroupID);
		}
	}

	for (int Corner = 0; Corner < 3; ++Corner)
	{
		if (MeshDescription.IsEdgeValid(FEdgeID(TriangleEdges[Corner])))
		{
			ValidateEdgeTopology(TriangleEdges[Corner]);
		}
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::SetVertexCoordinates(FMSVertexID VertexIndex, MeshSimplifier::Point3D Point)
{
	if (!IsValidVertex(VertexIndex))
	{
		return MS_ERROR;
	}

	MeshDescription.VertexAttributes().SetAttribute(FVertexID(VertexIndex), MeshAttribute::Vertex::Position, 0, MeshDescriptionAdapterUtils::ConvertFromPoint3D(Point));

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::MergeVertices(FMSVertexID VertexIndex, FMSVertexID OtherVertexIndex, std::list<FMSEdgeID>* /*= nullptr*/, std::list<FMSTriangleID>* /*= nullptr*/, std::list<std::pair<FMSEdgeID, FMSEdgeID> >* /*= nullptr*/, uint8_t /*= GP_ALLALL*/, bool /*= true*/, bool /*= true*/)
{
	if (!IsValidVertex(VertexIndex) || !IsValidVertex(OtherVertexIndex))
	{
		return MS_ERROR;
	}

	FVertexID VertexID(VertexIndex);

	TArray<FEdgeID> OtherVertexEdges(MeshDescription.GetVertexConnectedEdges(FVertexID(OtherVertexIndex)));

	TArray<FEdgeID> EdgeIDsToDelete;
	for (FEdgeID EdgeID : OtherVertexEdges)
	{
		if (MeshDescription.IsEdgeValid(EdgeID))
		{
			const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
			const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);
			if (VertexID == VertexID0 || VertexID == VertexID1)
			{
				EdgeIDsToDelete.Add(EdgeID);
			}
		}
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::MergeVertices(FMSVertexID VertexIndex, FMSVertexID OtherVertex, int& nbMergedEdges, uint8_t CategoryMask /*= GP_ALLALL*/, bool ValidateVertexTopology/*=true*/)
{
	nbMergedEdges = 0;
	return MergeVertices(VertexIndex, OtherVertex, nullptr, nullptr, nullptr, CategoryMask, ValidateVertexTopology);
}

int FMeshDescriptionAdapter::ExplodeConnectionAtVertex(FMSVertexID VertexIndex, std::set<FMSVertexID>* newBorderNodeSet /*= nullptr*/)
{
	if (!IsValidVertex(VertexIndex))
	{
		return MS_ERROR;
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::DeleteEdge(FMSEdgeID EdgeIndex)
{
	if (!IsValidEdge(EdgeIndex))
	{
		return MS_ERROR;
	}

	const FEdgeID EdgeID(EdgeIndex);

	const FVertexID VertexID0 = MeshDescription.GetEdgeVertex(EdgeID, 0);
	const FVertexID VertexID1 = MeshDescription.GetEdgeVertex(EdgeID, 1);

	MeshDescription.DeleteEdge(EdgeID);

	MeshDescriptionAdapterUtils::ResetElementData(EdgeMetaData[EdgeIndex]);

	ValidateVertexTopology(VertexID0.GetValue());
	ValidateVertexTopology(VertexID1.GetValue());

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::DisconnectTrianglesAtEdge(FMSEdgeID EdgeIndex)
{
	if (!IsValidEdge(EdgeIndex))
	{
		return MS_ERROR;
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::MergeEdges(FMSEdgeID EdgeIndex, FMSEdgeID OtherEdgeIndex, bool updateCategory /*= true*/)
{
	if (!IsValidEdge(EdgeIndex) || !IsValidEdge(OtherEdgeIndex))
	{
		return MS_ERROR;
	}

	return MS_SUCCESS;
}

int FMeshDescriptionAdapter::MergeEdgeAtVertex(FMSVertexID VertexIndex, FMSEdgeID EdgeIndex, double tol, uint8_t CategoryMask, FMSEdgeID* newEdge1 /*= nullptr*/, FMSEdgeID* newEdge2 /*= nullptr*/)
{
	if (!IsValidVertex(VertexIndex) || !IsValidEdge(EdgeIndex))
	{
		return MS_ERROR;
	}

	return MS_SUCCESS;
}

FMSEdgeID FMeshDescriptionAdapter::CreateEdgeFromVertices(FMSVertexID StartIndex, FMSVertexID EndIndex)
{
	if (!IsValidVertex(StartIndex) || !IsValidVertex(EndIndex))
	{
		return BAD_INDEX;
	}

	return MS_SUCCESS;
}

#endif // WITH_MESH_SIMPLIFIER
