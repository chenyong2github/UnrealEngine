// Copyright Epic Games, Inc. All Rights Reserved.

#include "IFCStaticMeshFactory.h"

#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "Utility/DatasmithMeshHelper.h"

DEFINE_LOG_CATEGORY(LogDatasmithIFCMeshFactory);

namespace IFC
{
	FStaticMeshFactory::FStaticMeshFactory() {}

	FStaticMeshFactory::~FStaticMeshFactory() {}


	FMD5Hash FStaticMeshFactory::ComputeHash(const IFC::FObject& InObject)
	{
		FMD5 MD5;
		MD5.Update(reinterpret_cast<const uint8*>(&InObject.facesVerticesCount), sizeof(InObject.facesVerticesCount));

		MD5.Update(reinterpret_cast<const uint8*>(InObject.Materials.GetData()), InObject.Materials.GetTypeSize() * InObject.Materials.Num());

		MD5.Update(reinterpret_cast<const uint8*>(InObject.facesVertices.GetData()), InObject.facesVertices.GetTypeSize() * InObject.facesVertices.Num());

		for (int32 TriangleIndex = 0; TriangleIndex < InObject.TrianglesArray.Num(); ++TriangleIndex)
		{
			const IFC::FPolygon& IFCPolygon = InObject.TrianglesArray[TriangleIndex];

			MD5.Update(reinterpret_cast<const uint8*>(&IFCPolygon.MaterialIndex), sizeof(IFCPolygon.MaterialIndex));
			MD5.Update(reinterpret_cast<const uint8*>(IFCPolygon.Points.GetData()), IFCPolygon.Points.GetTypeSize() * IFCPolygon.Points.Num());
		}

		FMD5Hash Hash;
		Hash.Set(MD5);
		return Hash;
	}

	void FStaticMeshFactory::FillMeshDescription(const IFC::FObject* InObject, FMeshDescription* MeshDescription) const
	{
		TVertexAttributesRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
		TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
		TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

		const int32 NumUVs = 1;
		VertexInstanceUVs.SetNumIndices(NumUVs);

		FIndexVertexIdMap PositionIndexToVertexId;
		PositionIndexToVertexId.Empty(InObject->facesVerticesCount);

		for (int64 IFCVertexIndex = 0; IFCVertexIndex < InObject->facesVerticesCount; IFCVertexIndex++)
		{
			const FVertexID& VertexID = MeshDescription->CreateVertex();
			VertexPositions[VertexID].X = InObject->facesVertices[(IFCVertexIndex * (InObject->vertexElementSize / sizeof(float))) + 0];
			// Flip Y to keep mesh looking the same as the coordinate system changes from RH -> LH
			VertexPositions[VertexID].Y = - InObject->facesVertices[(IFCVertexIndex * (InObject->vertexElementSize / sizeof(float))) + 1];
			VertexPositions[VertexID].Z = InObject->facesVertices[(IFCVertexIndex * (InObject->vertexElementSize / sizeof(float))) + 2];
			VertexPositions[VertexID] *= ImportUniformScale;
			PositionIndexToVertexId.Add(IFCVertexIndex, VertexID);
		}

		// Add the PolygonGroups.
		TMap<int32, FPolygonGroupID> MaterialIndexToPolygonGroupID;
		MaterialIndexToPolygonGroupID.Reserve(10);
		for (int32 MaterialIndex = 0; MaterialIndex < InObject->Materials.Num() || MaterialIndex < 1; MaterialIndex++)
		{
			const FPolygonGroupID& PolygonGroupID = MeshDescription->CreatePolygonGroup();
			MaterialIndexToPolygonGroupID.Add(MaterialIndex, PolygonGroupID);
			const FName ImportedSlotName(*FString::FromInt(MaterialIndex));
			PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = ImportedSlotName;
		}

		FTransform WorldToObject = InObject->Transform.Inverse();

		for (const IFC::FPolygon& IFCPolygon : InObject->TrianglesArray)
		{
			bool bInvalidTriangle = false;

			TArray<FVertexID> VertexIDs;
			TArray<FVertexInstanceID> VertexInstanceIDs;

			// Flip polygon to fix its orientation
			for (int32 IndexInPolygon = IFCPolygon.Points.Num() - 1; IndexInPolygon > -1; --IndexInPolygon)
			{
				const int32 IFCVertexIndex = IFCPolygon.Points[IndexInPolygon];
				const FVertexID& VertexID = PositionIndexToVertexId[IFCVertexIndex];

				if (VertexIDs.Num() > 0 && VertexID == VertexIDs.Last())
				{
					bInvalidTriangle = true;
					break;
				}

				VertexIDs.Add(VertexID);
			}

			if (bInvalidTriangle)
			{
				continue;
			}

			for(int32 IndexInPolygon = 0; IndexInPolygon < IFCPolygon.Points.Num(); ++IndexInPolygon)
			{
				const int32 IFCVertexIndex = IFCPolygon.Points[IndexInPolygon];
				const FVertexID& VertexID = VertexIDs[IndexInPolygon];

				const FVertexInstanceID& VertexInstanceID = MeshDescription->CreateVertexInstance(VertexID);
				VertexInstanceIDs.Add(VertexInstanceID);

				for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
				{
					VertexInstanceUVs.Set(VertexInstanceID, UVIndex, FVector2D::ZeroVector);
				}

				const float* Vertex = &(InObject->facesVertices[(IFCVertexIndex * (InObject->vertexElementSize / sizeof(float)))]);

				// Flip Y to go from RH -> LH
				FVector Normal = WorldToObject.TransformVector(FVector(Vertex[3], -Vertex[4], Vertex[5]));
				VertexInstanceNormals.Set(VertexInstanceID, Normal.GetSafeNormal());
			}

			MeshDescription->CreatePolygon(MaterialIndexToPolygonGroupID[IFCPolygon.MaterialIndex], VertexInstanceIDs);
		}

		DatasmithMeshHelper::RemoveEmptyPolygonGroups(*MeshDescription);
	}

	float FStaticMeshFactory::GetUniformScale() const
	{
		return ImportUniformScale;
	}

	void FStaticMeshFactory::SetUniformScale(const float Scale)
	{
		ImportUniformScale = Scale;
	}

}  //  namespace IFC
