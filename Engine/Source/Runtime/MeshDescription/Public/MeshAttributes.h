// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

namespace MeshAttribute
{
	namespace Vertex
	{
		extern MESHDESCRIPTION_API const FName Position;
	}
}


class MESHDESCRIPTION_API FMeshAttributes
{
public:
	explicit FMeshAttributes(FMeshDescription& InMeshDescription)
		: MeshDescription(InMeshDescription)
	{}

	virtual ~FMeshAttributes() = default;

	virtual void Register();

	/** Accessors for cached vertex position array */
	TVertexAttributesRef<FVector> GetVertexPositions() { return MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position); }
	TVertexAttributesConstRef<FVector> GetVertexPositions() const { return MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position); }

protected:

	FMeshDescription& MeshDescription;
};


class FMeshConstAttributes
{
public:
	explicit FMeshConstAttributes(const FMeshDescription& InMeshDescription)
		: MeshDescription(InMeshDescription)
	{}

	/** Accessors for cached vertex position array */
	TVertexAttributesConstRef<FVector> GetVertexPositions() const { return MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position); }

protected:

	const FMeshDescription& MeshDescription;
};
