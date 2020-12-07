// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshAttributes.h"

namespace MeshAttribute
{
	const FName Vertex::InfluenceCount("InfluenceCount");
	const FName Vertex::InfluenceBone("InfluenceBone");
	const FName Vertex::InfluenceWeight("InfluenceWeight");
}



void FSkeletalMeshAttributes::Register()
{
	// Add skeletalmesh attribute
	MeshDescription.VertexAttributes().RegisterAttribute<int32[]>(MeshAttribute::Vertex::InfluenceBone, 1, 0, EMeshAttributeFlags::IndexReference | EMeshAttributeFlags::Mandatory);
	MeshDescription.VertexAttributes().RegisterAttribute<float[]>(MeshAttribute::Vertex::InfluenceWeight, 1, 0.0f, EMeshAttributeFlags::Mandatory);

	// Call super class
	FStaticMeshAttributes::Register();
}
