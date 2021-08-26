// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

enum class EDynamicMeshSerializationVersion
{
	InitialVersion = 1,

	// ----- new versions to be added above this line -------------------------------------------------
	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

int FDynamicMesh3::SerializeInternal_LatestVersion()
{
	return static_cast<int>(EDynamicMeshSerializationVersion::LatestVersion);
}

void FDynamicMesh3::Serialize(FArchive& Ar)
{
	// Check validity before saving data.
	checkSlow(Ar.IsLoading() || CheckValidity(FValidityOptions(), EValidityCheckFailMode::Ensure));

	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	int32 SerializationVersion = static_cast<int>(EDynamicMeshSerializationVersion::LatestVersion);
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

	// Check validity after loading data.
	checkSlow(!Ar.IsLoading() || CheckValidity(FValidityOptions(), EValidityCheckFailMode::Ensure));
}