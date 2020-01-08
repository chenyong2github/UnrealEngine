// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMeshFactory.h"

#include "GLTFLogger.h"
#include "GLTFAsset.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "RenderUtils.h"

namespace GLTF
{
	class FMeshFactoryImpl : public GLTF::FBaseLogger
	{
	public:
		FMeshFactoryImpl();

		void FillMeshDescription(const GLTF::FMesh &Mesh, FMeshDescription* MeshDescription);

		void CleanUp();

	private:

		bool ImportPrimitive(const GLTF::FPrimitive&                        Primitive,  //
			int32                                          PrimitiveIndex,
			int32                                          NumUVs,
			bool                                           bMeshHasTagents,
			bool                                           bMeshHasColors,
			const TVertexInstanceAttributesRef<FVector>&   VertexInstanceNormals,
			const TVertexInstanceAttributesRef<FVector>&   VertexInstanceTangents,
			const TVertexInstanceAttributesRef<float>&     VertexInstanceBinormalSigns,
			const TVertexInstanceAttributesRef<FVector2D>& VertexInstanceUVs,
			const TVertexInstanceAttributesRef<FVector4>&  VertexInstanceColors,
			const TEdgeAttributesRef<bool>&                EdgeHardnesses,
			const TEdgeAttributesRef<float>&               EdgeCreaseSharpnesses,
			FMeshDescription* MeshDescription);

		inline TArray<FVector4>& GetVector4dBuffer(int32 Index)
		{
			check(Index < (sizeof(Vector4dBuffers) / sizeof(Vector4dBuffers[0])));
			uint32 ReserveSize = Vector4dBuffers[Index].Num() + Vector4dBuffers[Index].GetSlack();
			Vector4dBuffers[Index].Empty(ReserveSize);
			return Vector4dBuffers[Index];
		}

		inline TArray<FVector>& GetVectorBuffer(int32 Index)
		{
			check(Index < (sizeof(VectorBuffers) / sizeof(VectorBuffers[0])));
			uint32 ReserveSize = VectorBuffers[Index].Num() + VectorBuffers[Index].GetSlack();
			VectorBuffers[Index].Empty(ReserveSize);
			return VectorBuffers[Index];
		}

		inline TArray<FVector2D>& GetVector2dBuffer(int32 Index)
		{
			check(Index < (sizeof(Vector2dBuffers) / sizeof(Vector2dBuffers[0])));
			uint32 ReserveSize = Vector2dBuffers[Index].Num() + Vector2dBuffers[Index].GetSlack();
			Vector2dBuffers[Index].Empty(ReserveSize);
			return Vector2dBuffers[Index];
		}

		inline TArray<uint32>& GetIntBuffer()
		{
			uint32 ReserveSize = IntBuffer.Num() + IntBuffer.GetSlack();
			IntBuffer.Empty(ReserveSize);
			return IntBuffer;
		}

	private:
		enum
		{
			NormalBufferIndex = 0,
			TangentBufferIndex = 1,
			PositionBufferIndex = 2,
			ReindexBufferIndex = 3,
			VectorBufferCount = 4,
			UvReindexBufferIndex = MAX_MESH_TEXTURE_COORDS_MD,
			ColorBufferIndex = 0,
			Reindex4dBufferIndex = 1,
			Vector4dBufferCount = 2,
		};

		using FIndexVertexIdMap = TMap<int32, FVertexID>;

		float                ImportUniformScale;

		TSet<int32>                  MaterialIndicesUsed;
		TMap<int32, FPolygonGroupID> MaterialIndexToPolygonGroupID;
		TArray<FIndexVertexIdMap>    PositionIndexToVertexIdPerPrim;

		TArray<FVector2D>                       Vector2dBuffers[MAX_MESH_TEXTURE_COORDS_MD + 1];
		TArray<FVector>                         VectorBuffers[VectorBufferCount];
		TArray<FVector4>                        Vector4dBuffers[Vector4dBufferCount];
		TArray<uint32>                          IntBuffer;
		TArray<FVertexInstanceID>				CornerVertexInstanceIDs;
		uint32                                  MaxReserveSize;

		friend class FMeshFactory;
	};

	namespace
	{
		template <typename T>
		void ReIndex(const TArray<T>& Source, const TArray<uint32>& Indices, TArray<T>& Dst)
		{
			check(&Source != &Dst);

			Dst.Reserve(Indices.Num());
			for (uint32 Index : Indices)
			{
				Dst.Add(Source[Index]);
			}
		}

		void GenerateFlatNormals(const TArray<FVector>& Positions, const TArray<uint32>& Indices, TArray<FVector>& Normals)
		{
			Normals.Empty();

			const uint32 N = Indices.Num();
			check(N % 3 == 0);
			Normals.AddUninitialized(N);

			for (uint32 i = 0; i < N; i += 3)
			{
				const FVector& A = Positions[Indices[i]];
				const FVector& B = Positions[Indices[i + 1]];
				const FVector& C = Positions[Indices[i + 2]];

				const FVector Normal = FVector::CrossProduct(A - B, A - C).GetSafeNormal();
				// Same for each corner of the triangle.
				Normals[i] = Normal;
				Normals[i + 1] = Normal;
				Normals[i + 2] = Normal;
			}
		}

		int32 GetNumUVs(const GLTF::FMesh& Mesh)
		{
			int32 NumUVs = 0;
			for (int32 UVIndex = 0; UVIndex < MAX_MESH_TEXTURE_COORDS_MD; ++UVIndex)
			{
				if (Mesh.HasTexCoords(UVIndex))
				{
					NumUVs++;
				}
				else
				{
					break;
				}
			}
			return NumUVs;
		}
	}

	FMeshFactoryImpl::FMeshFactoryImpl()
		: ImportUniformScale(1.f)
	{
		CornerVertexInstanceIDs.SetNum(3);
	}

	void FMeshFactoryImpl::FillMeshDescription(const FMesh &Mesh, FMeshDescription* MeshDescription)
	{
		const int32 NumUVs = FMath::Max(1, GetNumUVs(Mesh));

		FStaticMeshAttributes StaticMeshAttributes(*MeshDescription);
		TVertexAttributesRef<FVector> VertexPositions = StaticMeshAttributes.GetVertexPositions();
		TEdgeAttributesRef<bool>  EdgeHardnesses = StaticMeshAttributes.GetEdgeHardnesses();
		TEdgeAttributesRef<float> EdgeCreaseSharpnesses = StaticMeshAttributes.GetEdgeCreaseSharpnesses();
		TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames();
		TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = StaticMeshAttributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = StaticMeshAttributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = StaticMeshAttributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = StaticMeshAttributes.GetVertexInstanceUVs();
		TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = StaticMeshAttributes.GetVertexInstanceColors();
		VertexInstanceUVs.SetNumIndices(NumUVs);

		MaterialIndicesUsed.Empty(10);
		// Add the vertex position
		PositionIndexToVertexIdPerPrim.SetNum(FMath::Max(Mesh.Primitives.Num(), PositionIndexToVertexIdPerPrim.Num()));
		for (int32 Index = 0; Index < Mesh.Primitives.Num(); ++Index)
		{
			const FPrimitive& Primitive = Mesh.Primitives[Index];
			// Remember which primitives use which materials.
			MaterialIndicesUsed.Add(Primitive.MaterialIndex);

			TArray<FVector>& Positions = GetVectorBuffer(PositionBufferIndex);
			Primitive.GetPositions(Positions);

			FIndexVertexIdMap& PositionIndexToVertexId = PositionIndexToVertexIdPerPrim[Index];
			PositionIndexToVertexId.Empty(Positions.Num());
			for (int32 PositionIndex = 0; PositionIndex < Positions.Num(); ++PositionIndex)
			{
				const FVertexID& VertexID = MeshDescription->CreateVertex();
				VertexPositions[VertexID] = Positions[PositionIndex] * ImportUniformScale;
				PositionIndexToVertexId.Add(PositionIndex, VertexID);
			}
		}

		// Add the PolygonGroup
		MaterialIndexToPolygonGroupID.Empty(10);
		for (int32 MaterialIndex : MaterialIndicesUsed)
		{
			const FPolygonGroupID& PolygonGroupID = MeshDescription->CreatePolygonGroup();
			MaterialIndexToPolygonGroupID.Add(MaterialIndex, PolygonGroupID);

			const FName ImportedSlotName(*FString::FromInt(MaterialIndex));
			PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = ImportedSlotName;
		}

		// Add the VertexInstance
		bool bMeshUsesEmptyMaterial = false;
		bool bDidGenerateTexCoords = false;
		for (int32 Index = 0; Index < Mesh.Primitives.Num(); ++Index)
		{
			const FPrimitive& Primitive = Mesh.Primitives[Index];
			const bool        bHasDegenerateTriangles =
				ImportPrimitive(Primitive, Index, NumUVs, Mesh.HasTangents(), Mesh.HasColors(),  //
					VertexInstanceNormals, VertexInstanceTangents, VertexInstanceBinormalSigns, VertexInstanceUVs,
					VertexInstanceColors,  //
					EdgeHardnesses, EdgeCreaseSharpnesses, MeshDescription);

			bMeshUsesEmptyMaterial |= Primitive.MaterialIndex == INDEX_NONE;
			for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
			{
				if (!Primitive.HasTexCoords(UVIndex))
				{
					bDidGenerateTexCoords = true;
					break;
				}
			}

			if (bHasDegenerateTriangles)
			{
				Messages.Emplace(EMessageSeverity::Warning,
					FString::Printf(TEXT("Mesh %s has primitive with degenerate triangles: %d"), *Mesh.Name, Index));
			}
		}
		if (bMeshUsesEmptyMaterial)
		{
			Messages.Emplace(EMessageSeverity::Warning, TEXT("Mesh has primitives with no materials assigned: ") + Mesh.Name);
		}
	}

	bool FMeshFactoryImpl::ImportPrimitive(const GLTF::FPrimitive&                        Primitive,  //
		int32                                          PrimitiveIndex,
		int32                                          NumUVs,
		bool                                           bMeshHasTagents,
		bool                                           bMeshHasColors,
		const TVertexInstanceAttributesRef<FVector>&   VertexInstanceNormals,
		const TVertexInstanceAttributesRef<FVector>&   VertexInstanceTangents,
		const TVertexInstanceAttributesRef<float>&     VertexInstanceBinormalSigns,
		const TVertexInstanceAttributesRef<FVector2D>& VertexInstanceUVs,
		const TVertexInstanceAttributesRef<FVector4>&  VertexInstanceColors,
		const TEdgeAttributesRef<bool>&                EdgeHardnesses,
		const TEdgeAttributesRef<float>&               EdgeCreaseSharpnesses,
		FMeshDescription* MeshDescription)
	{

		const FPolygonGroupID CurrentPolygonGroupID = MaterialIndexToPolygonGroupID[Primitive.MaterialIndex];
		const uint32          TriCount = Primitive.TriangleCount();

		TArray<uint32>& Indices = GetIntBuffer();
		Primitive.GetTriangleIndices(Indices);

		TArray<FVector>& Normals = GetVectorBuffer(NormalBufferIndex);
		// glTF does not guarantee each primitive within a mesh has the same attributes.
		// Fill in gaps as needed:
		// - missing normals will be flat, based on triangle orientation
		// - missing UVs will be (0,0)
		// - missing tangents will be (0,0,1)
		if (Primitive.HasNormals())
		{
			TArray<FVector>& ReindexBuffer = GetVectorBuffer(ReindexBufferIndex);
			Primitive.GetNormals(Normals);
			ReIndex(Normals, Indices, ReindexBuffer);
			Swap(Normals, ReindexBuffer);
		}
		else
		{
			TArray<FVector>& Positions = GetVectorBuffer(PositionBufferIndex);
			Primitive.GetPositions(Positions);
			GenerateFlatNormals(Positions, Indices, Normals);
		}

		TArray<FVector>& Tangents = GetVectorBuffer(TangentBufferIndex);
		if (Primitive.HasTangents())
		{
			TArray<FVector>& ReindexBuffer = GetVectorBuffer(ReindexBufferIndex);
			Primitive.GetTangents(Tangents);
			ReIndex(Tangents, Indices, ReindexBuffer);
			Swap(Tangents, ReindexBuffer);
		}
		else if (bMeshHasTagents)
		{
			// If other primitives in this mesh have tangents, generate filler ones for this primitive, to avoid gaps.
			Tangents.Init(FVector(0.0f, 0.0f, 1.0f), Primitive.VertexCount());
		}

		TArray<FVector4>& Colors = GetVector4dBuffer(ColorBufferIndex);
		if (Primitive.HasColors())
		{
			TArray<FVector4>& ReindexBuffer = GetVector4dBuffer(Reindex4dBufferIndex);
			Primitive.GetColors(Colors);
			ReIndex(Colors, Indices, ReindexBuffer);
			Swap(Colors, ReindexBuffer);
		}
		else if (bMeshHasColors)
		{
			// If other primitives in this mesh have colors, generate filler ones for this primitive, to avoid gaps.
			Colors.Init(FVector4(1.0f), Primitive.VertexCount());
		}

		int32_t            AvailableBufferIndex = 0;
		TArray<FVector2D>* UVs[MAX_MESH_TEXTURE_COORDS_MD];
		for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
		{
			UVs[UVIndex] = &GetVector2dBuffer(AvailableBufferIndex++);
			if (Primitive.HasTexCoords(UVIndex))
			{
				TArray<FVector2D>& ReindexBuffer = GetVector2dBuffer(UvReindexBufferIndex);
				Primitive.GetTexCoords(UVIndex, *UVs[UVIndex]);
				ReIndex(*UVs[UVIndex], Indices, ReindexBuffer);
				Swap(*UVs[UVIndex], ReindexBuffer);
			}
			else
			{
				// Unreal StaticMesh must have UV channel 0.
				// glTF doesn't require this since not all materials need texture coordinates.
				// We also fill UV channel > 1 for this primitive if other primitives have it, to avoid gaps.
				(*UVs[UVIndex]).AddZeroed(Primitive.VertexCount());
			}
		}

		bool bHasDegenerateTriangles = false;
		// Now add all vertexInstances
		FVertexID         CornerVertexIDs[3];
		for (uint32 TriangleIndex = 0; TriangleIndex < TriCount; ++TriangleIndex)
		{
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const uint32 IndiceIndex = TriangleIndex * 3 + Corner;
				const int32  VertexIndex = Indices[IndiceIndex];

				const FVertexID          VertexID = PositionIndexToVertexIdPerPrim[PrimitiveIndex][VertexIndex];
				const FVertexInstanceID& VertexInstanceID = MeshDescription->CreateVertexInstance(VertexID);

				CornerVertexInstanceIDs[Corner] = VertexInstanceID;
				CornerVertexIDs[Corner] = VertexID;
			}

			// Check for degenerate triangles
			const FVertexID& Vertex1 = CornerVertexIDs[0];
			const FVertexID& Vertex2 = CornerVertexIDs[1];
			const FVertexID& Vertex3 = CornerVertexIDs[2];

			if (Vertex1 == Vertex2 || Vertex2 == Vertex3 || Vertex1 == Vertex3)
			{
				bHasDegenerateTriangles = true;
				continue; // Triangle is degenerate, skip it
			}

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const uint32 IndiceIndex = TriangleIndex * 3 + Corner;

				const FVertexInstanceID& VertexInstanceID = CornerVertexInstanceIDs[Corner];

				if (Tangents.Num() > 0)
				{
					VertexInstanceTangents[VertexInstanceID] = Tangents[IndiceIndex];
				}

				VertexInstanceNormals[VertexInstanceID] = Normals[IndiceIndex];
				VertexInstanceBinormalSigns[VertexInstanceID] =
					GetBasisDeterminantSign(VertexInstanceTangents[VertexInstanceID].GetSafeNormal(),
					(VertexInstanceNormals[VertexInstanceID] ^ VertexInstanceTangents[VertexInstanceID]).GetSafeNormal(),
						VertexInstanceNormals[VertexInstanceID].GetSafeNormal());

				for (int32 UVIndex = 0; UVIndex < NumUVs; ++UVIndex)
				{
					VertexInstanceUVs.Set(VertexInstanceID, UVIndex, (*UVs[UVIndex])[IndiceIndex]);
				}

				if (Colors.Num() > 0)
				{
					VertexInstanceColors[VertexInstanceID] = Colors[IndiceIndex];
				}
			}

			// Insert a polygon into the mesh
			TArray<FEdgeID> NewEdgeIDs;
			const FPolygonID NewPolygonID = MeshDescription->CreatePolygon(CurrentPolygonGroupID, CornerVertexInstanceIDs, &NewEdgeIDs);

			for (const FEdgeID NewEdgeID : NewEdgeIDs)
			{
				// Make all faces part of the same smoothing group, so Unreal will combine identical adjacent verts.
				// (Is there a way to set auto-gen smoothing threshold? glTF spec says to generate flat normals if they're not specified.
				//   We want to combine identical verts whether they're smooth neighbors or triangles belonging to the same flat polygon.)
				EdgeHardnesses[NewEdgeID] = false;
				EdgeCreaseSharpnesses[NewEdgeID] = 0.0f;
			}
		}
		return bHasDegenerateTriangles;
	}

	inline void FMeshFactoryImpl::CleanUp()
	{
		const uint32 ReserveSize = FMath::Min(uint32(IntBuffer.Num() + IntBuffer.GetSlack()), MaxReserveSize);  // cap reserved size
		IntBuffer.Empty(ReserveSize);
		Vector2dBuffers[0].Empty(ReserveSize);
		for (TArray<FVector>& Array : VectorBuffers)
		{
			Array.Empty(ReserveSize);
		}
		for (int32 Index = 1; Index < MAX_MESH_TEXTURE_COORDS_MD + 1; ++Index)
		{
			TArray<FVector2D>& Array = Vector2dBuffers[Index];
			Array.Empty();
		}
		for (TArray<FVector4>& Array : Vector4dBuffers)
		{
			Array.Empty();
		}

		Messages.Empty();
	}

	//

	FMeshFactory::FMeshFactory()
		: Impl(new FMeshFactoryImpl())
	{
	}

	FMeshFactory::~FMeshFactory() {}

	void FMeshFactory::FillMeshDescription(const GLTF::FMesh &Mesh, FMeshDescription* MeshDescription)
	{
		Impl->FillMeshDescription(Mesh, MeshDescription);
	}

	const TArray<FLogMessage>& FMeshFactory::GetLogMessages() const
	{
		return Impl->GetLogMessages();
	}

	float FMeshFactory::GetUniformScale() const
	{
		return Impl->ImportUniformScale;
	}

	void FMeshFactory::SetUniformScale(float Scale)
	{
		Impl->ImportUniformScale = Scale;
	}

	void FMeshFactory::SetReserveSize(uint32 Size)
	{
		Impl->MaxReserveSize = Size;
	}

	void FMeshFactory::CleanUp()
	{
		Impl->CleanUp();
	}
} //namespace GLTF