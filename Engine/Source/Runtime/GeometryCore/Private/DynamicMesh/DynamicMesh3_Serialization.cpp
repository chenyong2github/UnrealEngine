// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

enum class EDynamicMeshSerializationVersion
{
	InitialVersion = 1
};


void FDynamicMesh3::Serialize(FArchive& Ar)
{
	checkSlow(CheckValidity(FValidityOptions(), EValidityCheckFailMode::Ensure));

	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	int32 SerializationVersion = (int32)EDynamicMeshSerializationVersion::InitialVersion;
	Ar << SerializationVersion;

	Ar << Vertices;
	Ar << VertexRefCounts;
	Ar << VertexNormals;
	Ar << VertexColors;
	Ar << VertexUVs;
	Ar << VertexEdgeLists;
	Ar << Triangles;
	Ar << TriangleRefCounts;
	Ar << TriangleEdges;
	Ar << TriangleGroups;
	Ar << GroupIDCounter;
	Ar << Edges;
	Ar << EdgeRefCounts;

	bool bHasAttributes = HasAttributes();
	Ar << bHasAttributes;
	if (bHasAttributes)
	{
		if (Ar.IsLoading())
		{
			EnableAttributes();
		}
		Ar << *AttributeSet;
	}
}