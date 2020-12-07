// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshAttributes.h"
#include "StaticMeshAttributes.h"

//Add any skeletalmesh specific attributes here

namespace MeshAttribute
{
 	namespace Vertex
 	{
		extern SKELETALMESHDESCRIPTION_API const FName InfluenceCount;
 		extern SKELETALMESHDESCRIPTION_API const FName InfluenceBone;
		extern SKELETALMESHDESCRIPTION_API const FName InfluenceWeight;
 	}
}


class SKELETALMESHDESCRIPTION_API FSkeletalMeshAttributes : public FStaticMeshAttributes
{
public:

	explicit FSkeletalMeshAttributes(FMeshDescription& InMeshDescription)
		: FStaticMeshAttributes(InMeshDescription)
	{}

	//TODO support uint16 in mesh description
	TVertexAttributesRef<TArrayAttribute<int32>> GetVertexInfluenceBones() { return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<int32>>(MeshAttribute::Vertex::InfluenceBone); }
	TVertexAttributesConstRef<TArrayAttribute<int32>> GetVertexInfluenceBones() const { return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<int32>>(MeshAttribute::Vertex::InfluenceBone); }

	TVertexAttributesRef<TArrayAttribute<float>> GetVertexInfluenceWeights() { return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<float>>(MeshAttribute::Vertex::InfluenceWeight); }
	TVertexAttributesConstRef<TArrayAttribute<float>> GetVertexInfluenceWeights() const { return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<float>>(MeshAttribute::Vertex::InfluenceWeight); }

	virtual void Register() override;


};


class FSkeletalMeshConstAttributes : public FStaticMeshConstAttributes
{
public:

	explicit FSkeletalMeshConstAttributes(const FMeshDescription& InMeshDescription)
		: FStaticMeshConstAttributes(InMeshDescription)
	{}

	TVertexAttributesConstRef<TArrayAttribute<int32>> GetVertexInfluenceBones() const { return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<int32>>(MeshAttribute::Vertex::InfluenceBone); }
	TVertexAttributesConstRef<TArrayAttribute<float>> GetVertexInfluenceWeights() const { return MeshDescription.VertexAttributes().GetAttributesRef<TArrayAttribute<float>>(MeshAttribute::Vertex::InfluenceWeight); }
};
