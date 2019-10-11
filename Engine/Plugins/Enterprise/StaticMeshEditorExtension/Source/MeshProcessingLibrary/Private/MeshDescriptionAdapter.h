// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_MESH_SIMPLIFIER

#include "CoreMinimal.h"

#include "MeshTypes.h"
#include "MeshDescription.h"

#include "IMeshAdapter.h"
#include "MeshAdapterUtil.h"

#define EDGE_IS_HARD      0x01
#define EDGE_IS_UV_SEAM   0x02

#define ELEMENT_STATUS_MASK			0x01
#define ELEMENT_FIRST_MARKER_MASK	0x02
#define ELEMENT_SECOND_MARKER_MASK	0x04

#define ELEMENT_CRITICAL_ZONE_MASK		0x01
#define ELEMENT_PARTITION_BORDER_MASK	0x02

struct FElementMetaData
{
	uint16 Category:4;
	uint16 Markers:4;
	uint16 Extras:4;
};

namespace MeshDescriptionAdapterUtils
{
	const FName Debug( "Debug" );
	const FName FeatureLine( "FeatureLine" );
	const FName EdgeLength( "EdgeLength" );

	FORCEINLINE bool IsElementMarkerSet(const FElementMetaData& ElementMetaData, const uint16 Mask)
	{
		return (ElementMetaData.Markers & Mask) == Mask;
	}

	FORCEINLINE void SetElementMarker(FElementMetaData& ElementMetaData, const bool Value, const uint16 Mask)
	{
		if (Value)
		{
			ElementMetaData.Markers |= Mask;
		}
		else
		{
			ElementMetaData.Markers &= ~Mask;
		}
	}

	FORCEINLINE void ResetElementMarker(FElementMetaData& ElementMetaData, const uint16 Mask)
	{
		ElementMetaData.Markers &= ~Mask;
	}

	FORCEINLINE bool IsElementExtraSet(const FElementMetaData& ElementMetaData, const uint16 Mask)
	{
		return (ElementMetaData.Extras & Mask) == Mask;
	}

	FORCEINLINE void SetElementExtra(FElementMetaData& ElementMetaData, const bool Value, const uint16 Mask)
	{
		if (Value)
		{
			ElementMetaData.Extras |= Mask;
		}
		else
		{
			ElementMetaData.Extras &= ~Mask;
		}
	}

	FORCEINLINE void ResetElementExtra(FElementMetaData& ElementMetaData, const uint16 Mask)
	{
		ElementMetaData.Extras &= ~Mask;
	}

	FORCEINLINE void ResetElementData(FElementMetaData& ElementMetaData)
	{
		ElementMetaData.Category = (uint16)MeshSimplifier::EElementCategory::ElementCategoryUnused;
		ElementMetaData.Markers = 0;
		ElementMetaData.Extras = 0;
	}

	FORCEINLINE FVector ConvertFromPoint3D(const MeshSimplifier::Point3D& Point)
	{
		return FVector(Point[0], Point[1], Point[2]);
	}

	void GetPolygonPerimeterEdges(const FMeshDescription& MeshDescription, const FPolygonID& PolygonID, TArray<FEdgeID>& OutPolygonPerimeterEdgeIDs);
}

class FMeshDescriptionAdapter : public MeshSimplifier::IMeshAdapter
{
private:
	FMeshDescription& MeshDescription;

	TArray<FElementMetaData> VertexMetaData;
	TArray<FElementMetaData> EdgeMetaData;
	TArray<FElementMetaData> TriangleMetaData;

private:
	bool IsValidVertex(FMSVertexID VertexIndex)
	{
		return VertexIndex != BAD_INDEX && MeshDescription.IsVertexValid(FVertexID(VertexIndex));
	}

	bool IsValidEdge(FMSEdgeID EdgeIndex)
	{
		return EdgeIndex != BAD_INDEX && MeshDescription.IsEdgeValid(FEdgeID(EdgeIndex));
	}

	bool IsValidTriangle(FMSTriangleID TriangleIndex)
	{
		return TriangleIndex != BAD_INDEX && MeshDescription.IsPolygonValid(FPolygonID(TriangleIndex));
	}

public:
	FMeshDescriptionAdapter(FMeshDescription& InMeshDescription);
	virtual ~FMeshDescriptionAdapter();

	virtual int GetTriangles(std::vector<FMSTriangleID>& Triangles) override;

	virtual void SetTrianglesStatus() override;
	virtual void SetEdgesStatus(uint8_t CategoryMask) override;
	virtual void SetEdgesMarkers(uint8_t CategoryMask) override;
	virtual void ResetTrianglesStatus() override;
	virtual void ResetEdgesStatus() override;
	virtual void ResetVerticesStatus() override;
	virtual int GetVertices(uint8_t CategoryMask, std::vector<FMSVertexID>& nodeSet) override;
	virtual int GetEdges(uint8_t CategoryMask, std::vector<FMSEdgeID>& edgeSet) override;
	virtual int GetElementsCount(int& nbNodes, int& nbEdges, int& nbFaces) override;
	virtual void SetStatusRecursively() override;
	virtual void SetStatusRecursively(uint8_t CategoryMask) override;
	virtual void ResetStatusRecurcively() override;
	virtual void ResetMarkersRecursively() override;
	virtual void SetVerticesStatus(uint8_t CategoryMask) override;
	virtual int GetElementsCount(uint8_t CategoryMask, int& nbNodes, int& nbEdges, int& nbFaces) override;
	virtual int GetTriangleCount() override;

	// GPData
	virtual bool HasFeatureLines() override;
	virtual bool HasNormals() override;
	virtual int RemoveAsFeatureLine(FMSEdgeID Edge) override;
	virtual bool IsOnFeatureLine(FMSEdgeID Edge) override;
	virtual void ValidateTriangles(std::vector<FMSTriangleID>& Triangles) override;
	virtual bool ValidateMeshNodes(int nbNodes, FMSVertexID* holeNodeSet, std::vector<FMSTriangleID>& newFaceSet) override;

	virtual FMSTriangleID GetMeshFirstFace() override;
	virtual int DeleteIsolatedElements() override;

	virtual int ValidateMesh() override;
	virtual int ValidateVertexTopology(FMSVertexID Vertex) override;
	virtual int ValidateEdgeTopology(FMSEdgeID EdgeIndex);

	virtual int CreateTriangle(FMSVertexID node0, FMSVertexID node1, FMSVertexID node2, std::vector<FMSTriangleID>& faceSet, FMSPartitionID Partition = 0) override;

	virtual bool IsVertexOfCategory(FMSVertexID Vertex, uint8_t CategoryMask) override;
	virtual bool IsVertexOfCategory(FMSVertexID Vertex, MeshSimplifier::EElementCategory Category) override;
	virtual bool GetVertexStatus(FMSVertexID Vertex) override;
	virtual bool GetVertexFirstMarker(FMSVertexID Vertex) override;
	virtual void SetVertexFirstMarker(FMSVertexID Vertex, bool Value = true) override;
	virtual void SetVertexStatus(FMSVertexID Vertex, bool Value = true) override;
	virtual MeshSimplifier::EElementCategory GetVertexCategory(FMSVertexID Vertex) override;
	virtual void SetVertexAsCriticalZone(FMSVertexID Vertex) override;
	virtual bool IsVertexInCriticalZone(FMSVertexID Vertex) override;
	virtual int DeleteVertex(FMSVertexID Vertex) override;
	virtual void ResetVertexStatus(FMSVertexID Vertex) override;
	virtual void ResetVertexFirstMarker(FMSVertexID Vertex) override;

	virtual int GetVertexCoordinates(FMSVertexID Vertex, MeshSimplifier::Point3D& coordinates) const override;
	virtual MeshSimplifier::Point3D GetVertexCoordinates(FMSVertexID Vertex) override;
	virtual int GetConnectingEdgesAtVertex(FMSVertexID Vertex, std::vector<FMSEdgeID>& edgeSet) override;
	virtual int GetEdgesAtVertex(FMSVertexID Vertex, std::vector<FMSEdgeID>& Edges) override;
	virtual int GetVertexConnectingTriangles(FMSVertexID Vertex, std::unordered_set<FMSTriangleID> &faceSet) override;
	virtual int GetTriangleNormalAtVertex(FMSVertexID Vertex, FMSTriangleID face, MeshSimplifier::Point3D& normal) override;
	virtual int GetTriangleStarAtVertex(FMSVertexID Vertex, std::vector<FMSTriangleID>& faces) override;

	virtual int SetVertexCoordinates(FMSVertexID Vertex, MeshSimplifier::Point3D point) override;
	virtual int MergeVertices(FMSVertexID Vertex, FMSVertexID OtherVertex, std::list<FMSEdgeID>* = nullptr, std::list<FMSTriangleID>* = nullptr, std::list<std::pair<FMSEdgeID, FMSEdgeID> >* = nullptr, uint8_t = (uint8_t)MeshSimplifier::ECategoryMask::CategoryMaskAny, bool = true, bool = true) override;
	virtual int MergeEdgeAtVertex(FMSVertexID Vertex, FMSEdgeID edge, double tol, uint8_t CategoryMask, FMSEdgeID* newEdge1 = nullptr, FMSEdgeID* newEdge2 = nullptr) override;
	virtual int MergeVertices(FMSVertexID Vertex, FMSVertexID OtherVertex, int& nbMergedEdges, uint8_t CategoryMask = MeshSimplifier::ECategoryMask::CategoryMaskAny, bool ValidateVertexTopology = true) override;
	virtual int ExplodeConnectionAtVertex(FMSVertexID Vertex, std::set<FMSVertexID>* newBorderNodeSet = nullptr) override;

	virtual bool IsEdgeOfCategory(FMSEdgeID Edge, uint8_t CategoryMask) override;
	virtual bool IsEdgeOfCategory(FMSEdgeID Edge, MeshSimplifier::EElementCategory Category) override;
	virtual bool GetEdgeStatus(FMSEdgeID Edge) override;
	virtual bool GetEdgeFirstMarker(FMSEdgeID Edge) override;
	virtual void SetEdgeFirstMarker(FMSEdgeID Edge, bool Value = true) override;
	virtual void ResetEdgeFirstMarker(FMSEdgeID Edge) override;
	virtual bool GetEdgeSecondMarker(FMSEdgeID Edge) override;
	virtual void SetEdgeSecondMarker(FMSEdgeID Edge, bool Value = true) override;
	virtual void ResetEdgeSecondMarker(FMSEdgeID Edge) override;
	virtual void SetEdgeStatus(FMSEdgeID Edge, bool Value = true) override;
	virtual void SetEdgeStatusRecursively(FMSEdgeID Edge) override;
	virtual MeshSimplifier::EElementCategory GetEdgeCategory(FMSEdgeID Edge) override;
	virtual int SetEdgeCategory(FMSEdgeID Edge, MeshSimplifier::EElementCategory Categoryegory) override;
	virtual void SetEdgeAsCriticalZone(FMSEdgeID Edge) override;
	virtual bool IsEdgeInCriticalZone(FMSEdgeID Edge) override;
	virtual void ResetEdgeStatus(FMSEdgeID Edge) override;
	virtual void ResetEdgeStatusRecursively(FMSEdgeID Edge) override;
	virtual void ResetEdgeMarkers(FMSEdgeID Edge) override;
	virtual void SetEdgeMarkersRecursively(FMSEdgeID Edge) override;

	virtual int ResetEdgePartitionBorder(FMSEdgeID Edge) override;
	virtual int SetEdgePartitionBorder(FMSEdgeID Edge) override;
	virtual bool IsEdgeAtUVDiscontinuity(FMSEdgeID Edge) override;

	virtual int GetEdgeTriangles(FMSEdgeID Edge, std::vector<FMSTriangleID>& Triangles) override;
	virtual FMSVertexID GetEdgeOtherVertex(FMSEdgeID Edge, const FMSVertexID node) override;
	virtual FMSVertexID GetEdgeStartingNode(FMSEdgeID Edge) override;
	virtual FMSVertexID GetEdgeEndingNode(FMSEdgeID Edge) override;
	virtual FMSTriangleID GetEdgeFirstTriangle(FMSEdgeID Edge) override;
	virtual FMSTriangleID GetEdgeSecondTriangle(FMSEdgeID Edge) override;
	virtual FMSTriangleID GetOtherTriangleAtEdge(FMSEdgeID Edge, FMSTriangleID face) override;
	virtual int GetConnectingTrianglesAtEdge(FMSEdgeID Edge, FMSTriangleID* set) override;
	virtual FMSEdgeID CreateEdgeFromVertices(FMSVertexID sNode, FMSVertexID eNode) override;
	virtual FMSEdgeID FindEdgeFromVertices(FMSVertexID sNode, FMSVertexID eNode) override;
	virtual bool GetEdgeDirectionAtFirstTriangle(FMSEdgeID Edge) override;
	virtual bool GetEdgeDirectionAtSecondTriangle(FMSEdgeID Edge) override;
	virtual FMSVertexID GetSharedVerticesBetweenEdges(FMSEdgeID Edge, FMSEdgeID OtherEdge) override;
	virtual bool IsEdgeLimitedByVertex(FMSEdgeID Edge, FMSVertexID Vertex) override;
	virtual int GetConnectingTrianglesAtEdge(FMSEdgeID Edge, std::vector<FMSTriangleID> &faceSet) override;

	virtual int DeleteEdge(FMSEdgeID Edge) override;
	virtual int DisconnectTrianglesAtEdge(FMSEdgeID Edge) override;
	virtual int MergeEdges(FMSEdgeID Edge, FMSEdgeID OtherEdge, bool updateCategory = true) override;

	virtual double GetEdgeLength(FMSEdgeID Edge) override;

	virtual bool GetTriangleStatus(FMSTriangleID Triangle) override;
	virtual void SetTriangleStatus(FMSTriangleID Triangle, bool Value = true) override;
	virtual bool GetTriangleFirstMarker(FMSTriangleID Triangle) override;
	virtual void SetTriangleFirstMarker(FMSTriangleID Triangle, bool Value = true) override;
	virtual bool GetTriangleSecondMarker(FMSTriangleID Triangle) override;
	virtual void SetTriangleSecondMarker(FMSTriangleID Triangle, bool Value = true) override;
	virtual void ResetTriangleSecondMarker(FMSTriangleID Triangle) override;
	virtual void SetTriangleStatusRecursively(FMSTriangleID Triangle) override;
	virtual void SetTriangleStatusRecursively(FMSTriangleID Triangle, uint8_t CategoryMask) override;
	virtual void ResetTriangleStatusRecursively(FMSTriangleID Triangle) override;
	virtual MeshSimplifier::EElementCategory GetTriangleCategory(FMSTriangleID Triangle) override;
	virtual int DeleteTriangle(FMSTriangleID Triangle) override;
	virtual void ResetTriangleStatus(FMSTriangleID Triangle) override;
	virtual void ResetTriangleFirstMarker(FMSTriangleID Triangle) override;


	virtual int GetTriangleVertices(FMSTriangleID Triangle, FMSVertexID* NodeSet) override;
	virtual int GetTriangleEdges(FMSTriangleID Triangle, FMSEdgeID* EdgeSet) override;
	virtual int GetEdgesAndVerticesForTriangle(FMSTriangleID Triangle, FMSEdgeID* EdgeSet, FMSVertexID* NodeSet) override;
	virtual bool GetEdgeDirectionForTriangle(FMSTriangleID Triangle, FMSEdgeID edge) override;
	virtual int GetEdgeDirectionsForTriangle(FMSTriangleID Triangle, bool *direction) override;
	virtual FMSVertexID GetOppositeVertexOnEdgeForTriangle(FMSTriangleID Triangle, FMSEdgeID edge) override;
	virtual FMSEdgeID GetOppositeEdgeAtVertexForTriangle(FMSTriangleID Triangle, FMSVertexID Vertex) override;
	virtual FMSPartitionID GetTrianglePartition(FMSTriangleID Triangle) override;

	virtual int GetTriangleNormal(FMSTriangleID Triangle, MeshSimplifier::Vector3D& normal) override;
	virtual int GetTriangleInvertedNormal(FMSTriangleID Triangle, MeshSimplifier::Vector3D& normal) override;
	virtual int GetTriangleNormalizedNormal(FMSTriangleID Triangle, MeshSimplifier::Vector3D& normal) override;
	virtual double GetTriangleArea(FMSTriangleID Triangle) override;
	virtual double GetTriangleFastArea(FMSTriangleID Triangle) override;

	virtual int SetOrientationFromTriangle(FMSTriangleID Triangle) override;
};

#endif // WITH_MESH_SIMPLIFIER
