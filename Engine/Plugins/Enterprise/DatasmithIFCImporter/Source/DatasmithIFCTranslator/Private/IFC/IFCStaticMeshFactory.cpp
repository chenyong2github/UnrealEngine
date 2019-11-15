// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IFCStaticMeshFactory.h"

#include "AssetRegistryModule.h"
#include "DatasmithMeshHelper.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "MeshDescription.h"
#include "PackageTools.h"

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
		const int32 NumUVs = 1;

		TVertexAttributesRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

		TEdgeAttributesRef<bool>  EdgeHardnesses = MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
		TEdgeAttributesRef<float> EdgeCreaseSharpnesses = MeshDescription->EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness);
		TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
		TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
		TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
		TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = MeshDescription->VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
		TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
		VertexInstanceUVs.SetNumIndices(NumUVs);

		FIndexVertexIdMap PositionIndexToVertexId;
		PositionIndexToVertexId.Empty(InObject->facesVerticesCount);

		for (int64 I = 0; I < InObject->facesVerticesCount; I++)
		{
			const FVertexID& VertexID = MeshDescription->CreateVertex();
			VertexPositions[VertexID].X = InObject->facesVertices[(I * (InObject->vertexElementSize / sizeof(float))) + 0];
			// Flip Y to keep mesh looking the same as the coordinate system changes from RH -> LH
			VertexPositions[VertexID].Y = - InObject->facesVertices[(I * (InObject->vertexElementSize / sizeof(float))) + 1];
			VertexPositions[VertexID].Z = InObject->facesVertices[(I * (InObject->vertexElementSize / sizeof(float))) + 2];
			VertexPositions[VertexID] *= ImportUniformScale;
			PositionIndexToVertexId.Add(I, VertexID);
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

		bool bMeshUsesEmptyMaterial = false;
		bool bDidGenerateTexCoords = false;

		FTransform WorldToObject = InObject->Transform.Inverse();

		for (int32 Index = 0; Index < InObject->TrianglesArray.Num(); ++Index)
		{
			const IFC::FPolygon* IFCPolygon = &InObject->TrianglesArray[Index];

			TArray<FVertexInstanceID> VertexInstanceIDs;
			TArray<FVertexID> VertexIDs;

			// flip polygon to fix its orientation
			for (int32 P = IFCPolygon->Points.Num() - 1; P > -1; --P)
			{
				const int32 VertexIndex = IFCPolygon->Points[P];
				const FVertexID VertexID = PositionIndexToVertexId[VertexIndex];
				const FVertexInstanceID& VertexInstanceID = MeshDescription->CreateVertexInstance(VertexID);

				for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
				{
					VertexInstanceUVs.Set(VertexInstanceID, UVIndex, FVector2D::ZeroVector);
				}

				VertexInstanceIDs.Add(VertexInstanceID);
				VertexIDs.Add(VertexID);

				const float* Vertex = &(InObject->facesVertices[(VertexIndex * (InObject->vertexElementSize / sizeof(float)))]);

				// Flip Y to go from RH -> LH
				FVector Normal = WorldToObject.TransformVector(FVector(Vertex[3], -Vertex[4], Vertex[5]));
				VertexInstanceNormals.Set(VertexInstanceID, Normal.GetSafeNormal());
			}

			bool bIsWrong = false;
			for (int32 T = 1; T < VertexIDs.Num(); T++)
			{
				if (VertexIDs[T - 1] == VertexIDs[T])
				{
					bIsWrong = true;
				}
			}
			if (bIsWrong)
			{
				continue;	// Skip this data.
			}

			MeshDescription->CreatePolygon(MaterialIndexToPolygonGroupID[IFCPolygon->MaterialIndex], VertexInstanceIDs);
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
