// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFDegenerateUVSectionsChecker.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"

void FGLTFDegenerateUVSectionsChecker::Sanitize(const FMeshDescription*& Description, int32& TexCoord)
{
	if (Description != nullptr)
	{
		const TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs =
			Description->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		const int32 TexCoordCount = VertexInstanceUVs.GetNumIndices();

		if (TexCoord < 0 || TexCoord >= TexCoordCount)
		{
			Description = nullptr;
		}
	}
}

const TArray<int32>* FGLTFDegenerateUVSectionsChecker::Convert(const FMeshDescription* Description, int32 TexCoord)
{
	if (Description == nullptr)
	{
		// TODO: report warning?

		return nullptr;
	}

	TArray<uint32> DegenerateSections;

	const TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs =
		Description->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	for (const FPolygonGroupID PolygonGroupID : Description->PolygonGroups().GetElementIDs())
	{
		bool bGroupIsValid = false;
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
					bGroupIsValid = true;
					break;
				}

				++Index;
			}

			if (bGroupIsValid)
			{
				break;
			}
		}

		if (!bGroupIsValid)
		{
			DegenerateSections.Add(PolygonGroupID.GetValue());
		}
	}

	TUniquePtr<TArray<int32>> Output = MakeUnique<TArray<int32>>(DegenerateSections);
	return Outputs.Add_GetRef(MoveTemp(Output)).Get();
}
