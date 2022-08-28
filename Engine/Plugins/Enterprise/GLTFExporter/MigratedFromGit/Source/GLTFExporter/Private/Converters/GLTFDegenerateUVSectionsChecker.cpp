// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFDegenerateUVSectionsChecker.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"

void FGLTFDegenerateUVSectionsChecker::Sanitize(const FMeshDescription*& Description, int32& SectionIndex, int32& TexCoord)
{
	if (Description != nullptr)
	{
		const int32 SectionCount = Description->PolygonGroups().Num();
		if (SectionIndex < 0 || SectionIndex >= SectionCount)
		{
			Description = nullptr;
		}
	}

	if (Description != nullptr)
	{
		const int32 TexCoordCount = Description->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate).GetNumIndices();
		if (TexCoord < 0 || TexCoord >= TexCoordCount)
		{
			Description = nullptr;
		}
	}
}

bool FGLTFDegenerateUVSectionsChecker::Convert(const FMeshDescription* Description, int32 SectionIndex, int32 TexCoord)
{
	if (Description == nullptr)
	{
		// TODO: report warning?

		return false;
	}

	const FPolygonGroupID PolygonGroupID = FPolygonGroupID(SectionIndex);
	const TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs =
		Description->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	uint32 Index = 0;
	FVector2D ReferenceUV;

	for (const FPolygonID PolygonID : Description->GetPolygonGroupPolygons(PolygonGroupID))
	{
		for (const FVertexInstanceID VertexInstanceID : Description->GetPolygonVertexInstances(PolygonID))
		{
			const FVector2D UV = VertexInstanceUVs.Get(VertexInstanceID, TexCoord);

			if (Index == 0)
			{
				ReferenceUV = UV;
			}
			else if (!UV.Equals(ReferenceUV))
			{
				return false;
			}

			++Index;
		}
	}

	return true;
}
