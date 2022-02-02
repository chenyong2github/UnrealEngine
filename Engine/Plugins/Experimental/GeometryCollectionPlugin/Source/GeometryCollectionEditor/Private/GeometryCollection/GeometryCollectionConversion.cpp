// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionConversion.h"

#include "AssetRegistryModule.h"
#include "AnimationRuntime.h"
#include "Async/ParallelFor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionFactory.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "Logging/LogMacros.h"
#include "Materials/Material.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"


DEFINE_LOG_CATEGORY_STATIC(UGeometryCollectionConversionLogging, Log, All);

struct FUniqueVertex
{
	FVector3f Normal;
	FVector3f Tangent;
	TArray<FVector2f> UVs;

	bool operator==(const FUniqueVertex& Other) const
	{
		if (this->UVs.Num() != Other.UVs.Num())
		{
			return false;
		}
		
		bool bEquality = true;
		bEquality &= (this->Normal == Other.Normal);
		bEquality &= (this->Tangent == Other.Tangent);
		for (int32 UVLayerIdx = 0; UVLayerIdx < UVs.Num(); ++UVLayerIdx)
		{
			bEquality &= (this->UVs[UVLayerIdx] == Other.UVs[UVLayerIdx]);
		}
		
		return bEquality;
	}
};

FORCEINLINE uint32 GetTypeHash(const FUniqueVertex& UniqueVertex)
{
	uint32 VertexHash = GetTypeHash(UniqueVertex.Normal);
	VertexHash = HashCombine(VertexHash, GetTypeHash(UniqueVertex.Tangent));
	for (int32 UVLayerIdx = 0; UVLayerIdx < UniqueVertex.UVs.Num(); ++UVLayerIdx)
	{
		VertexHash = HashCombine(VertexHash, GetTypeHash(UniqueVertex.UVs[UVLayerIdx]));
	}
	
	return VertexHash;
}

void FGeometryCollectionConversion::AppendStaticMesh(const UStaticMesh* StaticMesh, const TArray<UMaterialInterface*>& Materials, const FTransform& StaticMeshTransform, UGeometryCollection* GeometryCollectionObject, bool ReindexMaterials)
{
	if (StaticMesh == nullptr)
	{
		return;
	}

	// Prefer the HiRes description, although this isn't always available.
	FMeshDescription* MeshDescription = nullptr;
	if (StaticMesh->IsHiResMeshDescriptionValid())
	{
		MeshDescription = StaticMesh->GetHiResMeshDescription();
	}
	else
	{
		MeshDescription = StaticMesh->GetMeshDescription(0);
	}

	check(GeometryCollectionObject);
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();
	check(GeometryCollection);

	if (MeshDescription)
	{
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(*MeshDescription);
		FStaticMeshOperations::RecomputeNormalsAndTangentsIfNeeded(*MeshDescription, EComputeNTBsFlags::UseMikkTSpace);

		// source vertex information
		FStaticMeshAttributes Attributes(*MeshDescription);
		TArrayView<const FVector3f> SourcePosition = Attributes.GetVertexPositions().GetRawArray();
		TArrayView<const FVector3f> SourceTangent = Attributes.GetVertexInstanceTangents().GetRawArray();
		TArrayView<const float> SourceBinormalSign = Attributes.GetVertexInstanceBinormalSigns().GetRawArray();
		TArrayView<const FVector3f> SourceNormal = Attributes.GetVertexInstanceNormals().GetRawArray();
		TArrayView<const FVector4f> SourceColor = Attributes.GetVertexInstanceColors().GetRawArray();

		TVertexInstanceAttributesConstRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();
		const int32 NumUVLayers = InstanceUVs.GetNumChannels();
		TArray<TArrayView<const FVector2f>> SourceUVArrays;
		SourceUVArrays.SetNum(NumUVLayers);
		for (int32 UVLayerIdx = 0; UVLayerIdx < NumUVLayers; ++UVLayerIdx)
		{
			SourceUVArrays[UVLayerIdx] = InstanceUVs.GetRawArray(UVLayerIdx);
		}
		
		// target vertex information
		TManagedArray<FVector3f>& TargetVertex = GeometryCollection->Vertex;
		TManagedArray<FVector3f>& TargetTangentU = GeometryCollection->TangentU;
		TManagedArray<FVector3f>& TargetTangentV = GeometryCollection->TangentV;
		TManagedArray<FVector3f>& TargetNormal = GeometryCollection->Normal;
		TManagedArray<TArray<FVector2f>>& TargetUVs = GeometryCollection->UVs;
		TManagedArray<FLinearColor>& TargetColor = GeometryCollection->Color;
		TManagedArray<int32>& TargetBoneMap = GeometryCollection->BoneMap;
		TManagedArray<FLinearColor>& TargetBoneColor = GeometryCollection->BoneColor;
		TManagedArray<FString>& TargetBoneName = GeometryCollection->BoneName;

		const int32 VertexStart = GeometryCollection->NumElements(FGeometryCollection::VerticesGroup);
		int32 VertexCount = 0;
		
		FVector Scale = StaticMeshTransform.GetScale3D();
		
		// We'll need to re-introduce UV seams, etc. by splitting vertices.
		// A new mapping of MeshDescription vertex instances to the split vertices is maintained.
		TMap<FVertexInstanceID, int32> VertexInstanceToGeometryCollectionVertex;
		VertexInstanceToGeometryCollectionVertex.Reserve(Attributes.GetVertexInstanceNormals().GetNumElements());
		
		for (const FVertexID VertexIndex : MeshDescription->Vertices().GetElementIDs())
		{		
			TArrayView<const FVertexInstanceID> ReferencingVertexInstances = MeshDescription->GetVertexVertexInstanceIDs(VertexIndex);

			// Generate per instance hash of splittable attributes.
			TMap<FUniqueVertex, TArray<FVertexInstanceID>> SplitVertices;
			for (const FVertexInstanceID& InstanceID : ReferencingVertexInstances)
			{
				TArray<FVector2f> SourceUVs;
				SourceUVs.SetNum(NumUVLayers);
				for (int32 UVLayerIdx = 0; UVLayerIdx < NumUVLayers; ++UVLayerIdx)
				{
					SourceUVs[UVLayerIdx] = SourceUVArrays[UVLayerIdx][InstanceID];
				}
				
				FUniqueVertex UniqueVertex{ SourceNormal[InstanceID], SourceTangent[InstanceID], SourceUVs };
				TArray<FVertexInstanceID>& SplitVertex = SplitVertices.FindOrAdd(UniqueVertex);
				SplitVertex.Add(InstanceID);
			}

			int32 CurrentVertex = GeometryCollection->AddElements(SplitVertices.Num(), FGeometryCollection::VerticesGroup);
			
			// Create a new vertex for each split vertex and map the mesh description instance to it.
			for (const TTuple<FUniqueVertex,TArray<FVertexInstanceID>>& SplitVertex : SplitVertices)
			{
				const TArray<FVertexInstanceID>& InstanceIDs = SplitVertex.Value;
				const FVertexInstanceID& ExemplarInstanceID = InstanceIDs[0];

				TargetVertex[CurrentVertex] = SourcePosition[VertexIndex] * (FVector3f)Scale;
				TargetBoneMap[CurrentVertex] = GeometryCollection->NumElements(FGeometryCollection::TransformGroup);

				TargetNormal[CurrentVertex] = SourceNormal[ExemplarInstanceID];
				TargetTangentU[CurrentVertex] = SourceTangent[ExemplarInstanceID];
				TargetTangentV[CurrentVertex] = (FVector3f)SourceBinormalSign[ExemplarInstanceID] * FVector3f::CrossProduct(TargetNormal[CurrentVertex], TargetTangentU[CurrentVertex]);

				TargetUVs[CurrentVertex] = SplitVertex.Key.UVs;

				if (SourceColor.Num() > 0)
				{
					TargetColor[CurrentVertex] = FLinearColor(SourceColor[ExemplarInstanceID]);
				}
				else
				{
					TargetColor[CurrentVertex] = FLinearColor::White;
				}

				for (const FVertexInstanceID& InstanceID : InstanceIDs)
				{
					VertexInstanceToGeometryCollectionVertex.Add(InstanceID, CurrentVertex);
				}

				++CurrentVertex;
				++VertexCount;
			}
		}


		// for each material, add a reference in our GeometryCollectionObject
		const int32 MaterialStart = GeometryCollectionObject->Materials.Num();
		const int32 NumMeshMaterials = Materials.Num();
		GeometryCollectionObject->Materials.Reserve(MaterialStart + NumMeshMaterials);

		for (int32 Index = 0; Index < NumMeshMaterials; ++Index)
		{
			UMaterialInterface* CurrMaterial = Materials[Index];

			// Possible we have a null entry - replace with default
			if (CurrMaterial == nullptr)
			{
				CurrMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
			}

			// We add the material twice, once for interior and again for exterior.
			GeometryCollectionObject->Materials.Add(CurrMaterial);
			GeometryCollectionObject->Materials.Add(CurrMaterial);
		}

		// target triangle indices
		TManagedArray<FIntVector>& TargetIndices = GeometryCollection->Indices;
		TManagedArray<bool>& TargetVisible = GeometryCollection->Visible;
		TManagedArray<int32>& TargetMaterialID = GeometryCollection->MaterialID;
		TManagedArray<int32>& TargetMaterialIndex = GeometryCollection->MaterialIndex;

		const int32 IndicesCount = MeshDescription->Triangles().Num();
		const int32 InitialNumIndices = GeometryCollection->NumElements(FGeometryCollection::FacesGroup);
		const int32 IndicesStart = GeometryCollection->AddElements(IndicesCount, FGeometryCollection::FacesGroup);
		int32 TargetIndex = IndicesStart;
		for (const int32 TriangleIndex : MeshDescription->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> TriangleVertices = MeshDescription->GetTriangleVertexInstances(TriangleIndex);

			TargetIndices[TargetIndex] = FIntVector(
				VertexInstanceToGeometryCollectionVertex[TriangleVertices[0]],
				VertexInstanceToGeometryCollectionVertex[TriangleVertices[1]],
				VertexInstanceToGeometryCollectionVertex[TriangleVertices[2]]
			);

			TargetVisible[TargetIndex] = true;

			// Materials are ganged in pairs and we want the id to associate with the first of each pair.
			TargetMaterialID[TargetIndex] = MaterialStart + (MeshDescription->GetTrianglePolygonGroup(TriangleIndex) * 2);

			// Is this right?
			TargetMaterialIndex[TargetIndex] = TargetIndex;

			++TargetIndex;
		}

		// Geometry transform
		TManagedArray<FTransform>& Transform = GeometryCollection->Transform;

		int32 TransformIndex1 = GeometryCollection->AddElements(1, FGeometryCollection::TransformGroup);
		Transform[TransformIndex1] = StaticMeshTransform;
		Transform[TransformIndex1].SetScale3D(FVector(1.f, 1.f, 1.f));

		// Bone Hierarchy - Added at root with no common parent
		TManagedArray<int32>& Parent = GeometryCollection->Parent;
		TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;
		Parent[TransformIndex1] = FGeometryCollection::Invalid;
		SimulationType[TransformIndex1] = FGeometryCollection::ESimulationTypes::FST_Rigid;

		const FColor RandBoneColor(FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, 255);
		TargetBoneColor[TransformIndex1] = FLinearColor(RandBoneColor);
		TargetBoneName[TransformIndex1] = StaticMesh->GetName();

		// GeometryGroup
		int GeometryIndex = GeometryCollection->AddElements(1, FGeometryCollection::GeometryGroup);

		TManagedArray<int32>& TransformIndex = GeometryCollection->TransformIndex;
		TManagedArray<FBox>& BoundingBox = GeometryCollection->BoundingBox;
		TManagedArray<float>& InnerRadius = GeometryCollection->InnerRadius;
		TManagedArray<float>& OuterRadius = GeometryCollection->OuterRadius;
		TManagedArray<int32>& VertexStartArray = GeometryCollection->VertexStart;
		TManagedArray<int32>& VertexCountArray = GeometryCollection->VertexCount;
		TManagedArray<int32>& FaceStartArray = GeometryCollection->FaceStart;
		TManagedArray<int32>& FaceCountArray = GeometryCollection->FaceCount;

		TransformIndex[GeometryIndex] = TargetBoneMap[VertexStart];
		VertexStartArray[GeometryIndex] = VertexStart;
		VertexCountArray[GeometryIndex] = VertexCount;
		FaceStartArray[GeometryIndex] = InitialNumIndices;
		FaceCountArray[GeometryIndex] = IndicesCount;

		// TransformGroup
		TManagedArray<int32>& TransformToGeometryIndexArray = GeometryCollection->TransformToGeometryIndex;
		TransformToGeometryIndexArray[TransformIndex1] = GeometryIndex;

		FVector Center(0);
		for (int32 VertexIndex = VertexStart; VertexIndex < VertexStart + VertexCount; VertexIndex++)
		{
			Center += (FVector)TargetVertex[VertexIndex];
		}
		if (VertexCount) Center /= VertexCount;

		// Inner/Outer edges, bounding box
		BoundingBox[GeometryIndex] = FBox(ForceInitToZero);
		InnerRadius[GeometryIndex] = FLT_MAX;
		OuterRadius[GeometryIndex] = -FLT_MAX;
		for (int32 VertexIndex = VertexStart; VertexIndex < VertexStart + VertexCount; VertexIndex++)
		{
			BoundingBox[GeometryIndex] += (FVector)TargetVertex[VertexIndex];

			float Delta = (Center - (FVector)TargetVertex[VertexIndex]).Size();
			InnerRadius[GeometryIndex] = FMath::Min(InnerRadius[GeometryIndex], Delta);
			OuterRadius[GeometryIndex] = FMath::Max(OuterRadius[GeometryIndex], Delta);
		}

		// Inner/Outer centroid
		for (int fdx = IndicesStart; fdx < IndicesStart + IndicesCount; fdx++)
		{
			FVector Centroid(0);
			for (int e = 0; e < 3; e++)
			{
				Centroid += (FVector)TargetVertex[TargetIndices[fdx][e]];
			}
			Centroid /= 3;

			float Delta = (Center - Centroid).Size();
			InnerRadius[GeometryIndex] = FMath::Min(InnerRadius[GeometryIndex], Delta);
			OuterRadius[GeometryIndex] = FMath::Max(OuterRadius[GeometryIndex], Delta);
		}

		// Inner/Outer edges
		for (int fdx = IndicesStart; fdx < IndicesStart + IndicesCount; fdx++)
		{
			for (int e = 0; e < 3; e++)
			{
				int i = e, j = (e + 1) % 3;
				FVector Edge = (FVector)TargetVertex[TargetIndices[fdx][i]] + 0.5 * FVector(TargetVertex[TargetIndices[fdx][j]] - TargetVertex[TargetIndices[fdx][i]]);
				float Delta = (Center - Edge).Size();
				InnerRadius[GeometryIndex] = FMath::Min(InnerRadius[GeometryIndex], Delta);
				OuterRadius[GeometryIndex] = FMath::Max(OuterRadius[GeometryIndex], Delta);
			}
		}

		if (ReindexMaterials) {
			GeometryCollection->ReindexMaterials();
		}
	}
}

void FGeometryCollectionConversion::AppendGeometryCollection(const UGeometryCollection* SourceGeometryCollection, const TArray<UMaterialInterface*>& Materials, const FTransform& GeometryCollectionTransform, UGeometryCollection* TargetGeometryCollectionObject, bool ReindexMaterials)
{
	if (SourceGeometryCollection == nullptr)
	{
		return;
	}
	const TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> SourceGeometryCollectionPtr = SourceGeometryCollection->GetGeometryCollection();

	check(TargetGeometryCollectionObject);
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = TargetGeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();
	check(GeometryCollection);

	// Assemble offsets and add elements
	const int32 VertexCount = SourceGeometryCollectionPtr->Vertex.Num();
	const int32 FaceCount = SourceGeometryCollectionPtr->Indices.Num();
	const int32 TransformCount = SourceGeometryCollectionPtr->Transform.Num();
	const int32 GeometryCount = SourceGeometryCollectionPtr->TransformIndex.Num();
	const int32 SectionCount = SourceGeometryCollectionPtr->Sections.Num();

	const int32 AssetMaterialStart = TargetGeometryCollectionObject->Materials.Num();

	FVector Scale = GeometryCollectionTransform.GetScale3D();
	FTransform AppliedTransform = GeometryCollectionTransform;
	AppliedTransform.RemoveScaling();

	const int32 VertexStart = GeometryCollection->AddElements(VertexCount, FGeometryCollection::VerticesGroup);
	const int32 FaceStart = GeometryCollection->AddElements(FaceCount, FGeometryCollection::FacesGroup);
	const int32 TransformStart = GeometryCollection->AddElements(TransformCount, FGeometryCollection::TransformGroup);
	const int32 GeometryStart = GeometryCollection->AddElements(GeometryCount, FGeometryCollection::GeometryGroup);
	const int32 SectionStart = GeometryCollection->AddElements(SectionCount, FGeometryCollection::MaterialGroup);

	// source vertex information
	const TManagedArray<FVector3f>& SourceVertex = SourceGeometryCollectionPtr->Vertex;
	const TManagedArray<FVector3f>& SourceTangentU = SourceGeometryCollectionPtr->TangentU;
	const TManagedArray<FVector3f>& SourceTangentV = SourceGeometryCollectionPtr->TangentV;
	const TManagedArray<FVector3f>& SourceNormal = SourceGeometryCollectionPtr->Normal;
	const TManagedArray<TArray<FVector2f>>& SourceUVs = SourceGeometryCollectionPtr->UVs;
	const TManagedArray<FLinearColor>& SourceColor = SourceGeometryCollectionPtr->Color;
	const TManagedArray<int32>& SourceBoneMap = SourceGeometryCollectionPtr->BoneMap;

	// target vertex information
	TManagedArray<FVector3f>& TargetVertex = GeometryCollection->Vertex;
	TManagedArray<FVector3f>& TargetTangentU = GeometryCollection->TangentU;
	TManagedArray<FVector3f>& TargetTangentV = GeometryCollection->TangentV;
	TManagedArray<FVector3f>& TargetNormal = GeometryCollection->Normal;
	TManagedArray<TArray<FVector2f>>& TargetUVs = GeometryCollection->UVs;
	TManagedArray<FLinearColor>& TargetColor = GeometryCollection->Color;
	TManagedArray<int32>& TargetBoneMap = GeometryCollection->BoneMap;

	// append vertices
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
	{
		const int32 VertexOffset = VertexStart + VertexIndex;
		TargetVertex[VertexOffset] = SourceVertex[VertexIndex] * (FVector3f)Scale;
		
		TargetTangentU[VertexOffset] = SourceTangentU[VertexIndex];
		TargetTangentV[VertexOffset] = SourceTangentV[VertexIndex];
		TargetNormal[VertexOffset] = SourceNormal[VertexIndex];
		TargetUVs[VertexOffset] = SourceUVs[VertexIndex];
		TargetColor[VertexOffset] = SourceColor[VertexIndex];

		TargetBoneMap[VertexOffset] = SourceBoneMap[VertexIndex] + TransformStart;
	}

	// source face information
	const TManagedArray<FIntVector>& SourceIndices = SourceGeometryCollectionPtr->Indices;
	const TManagedArray<bool>& SourceVisible = SourceGeometryCollectionPtr->Visible;
	const TManagedArray<int32>& SourceMaterialID = SourceGeometryCollectionPtr->MaterialID;
	const TManagedArray<int32>& SourceMaterialIndex = SourceGeometryCollectionPtr->MaterialIndex;
	
	// target face information
	TManagedArray<FIntVector>& TargetIndices = GeometryCollection->Indices;
	TManagedArray<bool>& TargetVisible = GeometryCollection->Visible;
	TManagedArray<int32>& TargetMaterialID = GeometryCollection->MaterialID;
	TManagedArray<int32>& TargetMaterialIndex = GeometryCollection->MaterialIndex;

	// append faces
	for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
	{
		const FIntVector& SourceFace = SourceIndices[FaceIndex];
		const int32 FaceOffset = FaceStart + FaceIndex;
		TargetIndices[FaceOffset] = FIntVector(
			SourceFace[0] + VertexStart,
			SourceFace[1] + VertexStart,
			SourceFace[2] + VertexStart);
		TargetVisible[FaceOffset] = SourceVisible[FaceIndex];
		
		TargetMaterialID[FaceOffset] = AssetMaterialStart + SourceMaterialID[FaceIndex];
		TargetMaterialIndex[FaceOffset] = FaceOffset;
	}

	// source transform information
	const TManagedArray<FTransform>& SourceTransform = SourceGeometryCollectionPtr->Transform;
	const TManagedArray<FString>& SourceBoneName = SourceGeometryCollectionPtr->BoneName;
	const TManagedArray<FLinearColor>& SourceBoneColor = SourceGeometryCollectionPtr->BoneColor;
	const TManagedArray<int32>& SourceParent = SourceGeometryCollectionPtr->Parent;
	const TManagedArray<TSet<int32>>& SourceChildren = SourceGeometryCollectionPtr->Children;
	const TManagedArray<int32>& SourceTransformToGeometryIndex = SourceGeometryCollectionPtr->TransformToGeometryIndex;
	const TManagedArray<int32>& SourceSimulationType = SourceGeometryCollectionPtr->SimulationType;
	const TManagedArray<int32>& SourceStatusFlags = SourceGeometryCollectionPtr->StatusFlags;
	const TManagedArray<int32>& SourceInitialDynamicState = SourceGeometryCollectionPtr->InitialDynamicState;
	
	// target transform information
	TManagedArray<FTransform>& TargetTransform = GeometryCollection->Transform;
	TManagedArray<FString>& TargetBoneName = GeometryCollection->BoneName;
	TManagedArray<FLinearColor>& TargetBoneColor = GeometryCollection->BoneColor;
	TManagedArray<int32>& TargetParent = GeometryCollection->Parent;
	TManagedArray<TSet<int32>>& TargetChildren = GeometryCollection->Children;
	TManagedArray<int32>& TargetTransformToGeometryIndex = GeometryCollection->TransformToGeometryIndex;
	TManagedArray<int32>& TargetSimulationType = GeometryCollection->SimulationType;
	TManagedArray<int32>& TargetStatusFlags = GeometryCollection->StatusFlags;
	TManagedArray<int32>& TargetInitialDynamicState = GeometryCollection->InitialDynamicState;

	// append transform hierarchy
	for (int32 TransformIndex = 0; TransformIndex < TransformCount; ++TransformIndex)
	{
		const int32 TransformOffset = TransformStart + TransformIndex;

		// Only apply the transform to the parent node. Child nodes only need scaling applied to translation offsets.
		if (SourceParent[TransformIndex] == INDEX_NONE)
		{
			TargetTransform[TransformOffset] = SourceTransform[TransformIndex] * AppliedTransform;
		}
		else
		{
			FTransform ScaledTranslation = SourceTransform[TransformIndex];
			ScaledTranslation.ScaleTranslation(Scale);
			TargetTransform[TransformOffset] = ScaledTranslation;
		}
	
		// #todo Get this Bone name to be unique
		TargetBoneName[TransformOffset] = SourceBoneName[TransformIndex];

		const FColor RandBoneColor(FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, 255);
		TargetBoneColor[TransformOffset] = FLinearColor(RandBoneColor);

		TargetParent[TransformOffset] = (SourceParent[TransformIndex]==INDEX_NONE) ? INDEX_NONE: SourceParent[TransformIndex] + TransformStart;

		const TSet<int32>& SourceChildrenSet = SourceChildren[TransformIndex];
		for (int32 ChildIndex : SourceChildrenSet)
		{
			TargetChildren[TransformOffset].Add(ChildIndex + TransformStart);
		}

		TargetTransformToGeometryIndex[TransformOffset] = SourceTransformToGeometryIndex[TransformIndex] + GeometryStart;
		TargetSimulationType[TransformOffset] = SourceSimulationType[TransformIndex];
		TargetStatusFlags[TransformOffset] = SourceStatusFlags[TransformIndex];
		TargetInitialDynamicState[TransformOffset] = SourceInitialDynamicState[TransformIndex];
	}

	// source geometry information
	const TManagedArray<int32>& SourceTransformIndex = SourceGeometryCollectionPtr->TransformIndex;
	const TManagedArray<int32>& SourceVertexStart = SourceGeometryCollectionPtr->VertexStart;
	const TManagedArray<int32>& SourceVertexCount = SourceGeometryCollectionPtr->VertexCount;
	const TManagedArray<int32>& SourceFaceStart = SourceGeometryCollectionPtr->FaceStart;
	const TManagedArray<int32>& SourceFaceCount = SourceGeometryCollectionPtr->FaceCount;
	
	// target geometry information
	TManagedArray<int32>& TargetTransformIndex = GeometryCollection->TransformIndex;
	TManagedArray<FBox>& TargetBoundingBox = GeometryCollection->BoundingBox;
	TManagedArray<float>& TargetInnerRadius = GeometryCollection->InnerRadius;
	TManagedArray<float>& TargetOuterRadius = GeometryCollection->OuterRadius;
	TManagedArray<int32>& TargetVertexStart = GeometryCollection->VertexStart;
	TManagedArray<int32>& TargetVertexCount = GeometryCollection->VertexCount;
	TManagedArray<int32>& TargetFaceStart = GeometryCollection->FaceStart;
	TManagedArray<int32>& TargetFaceCount = GeometryCollection->FaceCount;

	// append geometry
	for (int32 GeometryIndex = 0; GeometryIndex < GeometryCount; ++GeometryIndex)
	{
		const int32 GeometryOffset = GeometryStart + GeometryIndex;

		TargetTransformIndex[GeometryOffset] = SourceTransformIndex[GeometryIndex] + TransformStart;
		
		TargetVertexStart[GeometryOffset] = SourceVertexStart[GeometryIndex] + VertexStart;
		TargetVertexCount[GeometryOffset] = SourceVertexCount[GeometryIndex];
		TargetFaceStart[GeometryOffset] = SourceFaceStart[GeometryIndex] + FaceStart;
		TargetFaceCount[GeometryOffset] = SourceFaceCount[GeometryIndex];

		// Find centroid of geometry for inner/outer radius calculations
		FVector Center(0);
		for (int32 VertexIndex = TargetVertexStart[GeometryOffset]; VertexIndex < TargetVertexStart[GeometryOffset] + TargetVertexCount[GeometryOffset]; ++VertexIndex)
		{
			Center += (FVector)TargetVertex[VertexIndex];
		}
		if (TargetVertexCount[GeometryOffset]) Center /= TargetVertexCount[GeometryOffset];
		
		TargetBoundingBox[GeometryOffset] = FBox(ForceInitToZero);
		TargetInnerRadius[GeometryOffset] = FLT_MAX;
		TargetOuterRadius[GeometryOffset] = -FLT_MAX;
		for (int32 VertexIndex = TargetVertexStart[GeometryOffset]; VertexIndex < TargetVertexStart[GeometryOffset] + TargetVertexCount[GeometryOffset]; ++VertexIndex)
		{
			TargetBoundingBox[GeometryOffset] += (FVector)TargetVertex[VertexIndex];

			float Delta = (Center - (FVector)TargetVertex[VertexIndex]).Size();
			TargetInnerRadius[GeometryOffset] = FMath::Min(TargetInnerRadius[GeometryOffset], Delta);
			TargetOuterRadius[GeometryOffset] = FMath::Max(TargetOuterRadius[GeometryOffset], Delta);
		}
	}

	// for each material, add a reference in our GeometryCollectionObject
	const int32 NumMeshMaterials = Materials.Num();
	TargetGeometryCollectionObject->Materials.Reserve(AssetMaterialStart + NumMeshMaterials);

	for (int32 Index = 0; Index < NumMeshMaterials; ++Index)
	{
		UMaterialInterface* CurrMaterial = Materials[Index];

		// Possible we have a null entry - replace with default
		if (CurrMaterial == nullptr)
		{
			CurrMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		TargetGeometryCollectionObject->Materials.Add(CurrMaterial);
	}

	// source material information
	const TManagedArray<FGeometryCollectionSection>& SourceSections = SourceGeometryCollectionPtr->Sections;

	// target material information
	TManagedArray<FGeometryCollectionSection>& TargetSections = GeometryCollection->Sections;

	// append sections
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		int32 SectionOffset = SectionStart + SectionIndex;

		TargetSections[SectionOffset].MaterialID = AssetMaterialStart + SourceSections[SectionIndex].MaterialID;

		TargetSections[SectionOffset].FirstIndex = SourceSections[SectionIndex].FirstIndex + FaceStart * 3;
		TargetSections[SectionOffset].MinVertexIndex = VertexStart + SourceSections[SectionIndex].MinVertexIndex;

		TargetSections[SectionOffset].NumTriangles = SourceSections[SectionIndex].NumTriangles;
		TargetSections[SectionOffset].MaxVertexIndex = VertexStart + SourceSections[SectionIndex].MaxVertexIndex;
	}

	if (ReindexMaterials) {
		GeometryCollection->ReindexMaterials();
	}

}


void FGeometryCollectionConversion::AppendStaticMesh(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, const FTransform& StaticMeshTransform, UGeometryCollection* GeometryCollectionObject, bool ReindexMaterials)
{
	if (StaticMesh == nullptr)
	{
		return;
	}

	TArray<UMaterialInterface*> Materials;
	Materials.Reserve(StaticMesh->GetStaticMaterials().Num());

	for (int32 Index = 0; Index < StaticMesh->GetStaticMaterials().Num(); ++Index)
	{
		UMaterialInterface* CurrMaterial = StaticMeshComponent ? StaticMeshComponent->GetMaterial(Index) : StaticMesh->GetMaterial(Index);
		Materials.Add(CurrMaterial);
	}

	// Geometry collections usually carry the selection material, which we'll delete before appending
	UMaterialInterface* BoneSelectedMaterial = LoadObject<UMaterialInterface>(nullptr, UGeometryCollection::GetSelectedMaterialPath(), nullptr, LOAD_None, nullptr);
	GeometryCollectionObject->Materials.Remove(BoneSelectedMaterial);
	Materials.Remove(BoneSelectedMaterial);

	AppendStaticMesh(StaticMesh, Materials, StaticMeshTransform, GeometryCollectionObject, ReindexMaterials);
}


void FGeometryCollectionConversion::AppendGeometryCollection(const UGeometryCollection* SourceGeometryCollection, const UGeometryCollectionComponent* GeometryCollectionComponent, const FTransform& GeometryCollectionTransform, UGeometryCollection* TargetGeometryCollectionObject, bool ReindexMaterials)
{
	if (SourceGeometryCollection == nullptr)
	{
		return;
	}

	TArray<UMaterialInterface*> Materials;
	Materials.Reserve(SourceGeometryCollection->Materials.Num());

	for (int32 Index = 0; Index < SourceGeometryCollection->Materials.Num(); ++Index)
	{
		UMaterialInterface* CurrMaterial = GeometryCollectionComponent ? GeometryCollectionComponent->GetMaterial(Index) : SourceGeometryCollection->Materials[Index].Get();
		Materials.Add(CurrMaterial);
	}

	// Geometry collections usually carry the selection material, which we'll delete before appending
	UMaterialInterface* BoneSelectedMaterial = LoadObject<UMaterialInterface>(nullptr, UGeometryCollection::GetSelectedMaterialPath(), nullptr, LOAD_None, nullptr);
	TargetGeometryCollectionObject->Materials.Remove(BoneSelectedMaterial);
	Materials.Remove(BoneSelectedMaterial);

	AppendGeometryCollection(SourceGeometryCollection, Materials, GeometryCollectionTransform, TargetGeometryCollectionObject, ReindexMaterials);
}


void FGeometryCollectionConversion::AppendSkeletalMesh(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent *SkeletalMeshComponent, const FTransform & SkeletalMeshTransform, UGeometryCollection * GeometryCollectionObject, bool ReindexMaterials)
{
	//UE_LOG(UGeometryCollectionConversionLogging, Log, TEXT("FGeometryCollectionConversion::AppendSkeletalMesh()"));
	check(SkeletalMesh);
	if (GeometryCollectionObject)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{

			if (const USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
			{
				if (const FSkeletalMeshRenderData * SkelMeshRenderData = SkeletalMesh->GetResourceForRendering())
				{
					if (SkelMeshRenderData->LODRenderData.Num())
					{
						const FSkeletalMeshLODRenderData& SkeletalMeshLODRenderData = SkelMeshRenderData->LODRenderData[0];
						const FSkinWeightVertexBuffer& SkinWeightVertexBuffer = *SkeletalMeshLODRenderData.GetSkinWeightVertexBuffer();
						const FStaticMeshVertexBuffers& VertexBuffers = SkeletalMeshLODRenderData.StaticVertexBuffers;
						const FPositionVertexBuffer& PositionVertexBuffer = VertexBuffers.PositionVertexBuffer;
						const int32 VertexCount = PositionVertexBuffer.GetNumVertices();
						// Check that all vertex weightings are rigid. 
						for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
						{
							int32 SkeletalBoneIndex = -1;
							if (!SkinWeightVertexBuffer.GetRigidWeightBone(VertexIndex, SkeletalBoneIndex))
							{
								UE_LOG(UGeometryCollectionConversionLogging, Error, TEXT("Non-rigid weighting found on vertex %d: Cannot convert to GeometryCollection."), VertexIndex);
								return;
							}
						}
						const FSkelMeshRenderSection& RenderSection = SkeletalMeshLODRenderData.RenderSections[0];
						const TArray<FBoneIndexType>& SkeletalBoneMap = RenderSection.BoneMap;

						//
						// The Component transform for each Mesh will become the FTransform that drives
						// its associated VerticesGroup. The Skeleton will contain a nested transform hierarchy
						// that is evaluated using the GetComponentSpaceTransformRefPose. The resulting
						// Transforms array stored in the GeometryCollection will be the same size as
						// the SkeletalBoneMap. Note the @todo: the SkeletalBoneMap is pulled from only
						// the first render section, this will need to be expanded to include all render
						// sections.
						//
						TManagedArray<FTransform>& Transform = GeometryCollection->Transform;
						int32 TransformBaseIndex = GeometryCollection->AddElements(SkeletalBoneMap.Num(), FGeometryCollection::TransformGroup);
						const FReferenceSkeleton & ReferenceSkeletion = Skeleton->GetReferenceSkeleton();
						const TArray<FTransform> & RestArray = Skeleton->GetRefLocalPoses();
						for (int32 BoneIndex = 0; BoneIndex < SkeletalBoneMap.Num(); BoneIndex++)
						{
							FTransform BoneTransform = FAnimationRuntime::GetComponentSpaceTransformRefPose(ReferenceSkeletion, SkeletalBoneMap[BoneIndex]);
							Transform[TransformBaseIndex + BoneIndex] = BoneTransform;
						}


						//
						// The Triangle Indices
						//
						TManagedArray<FIntVector>& Indices = GeometryCollection->Indices;
						TManagedArray<bool>& Visible = GeometryCollection->Visible;
						TManagedArray<int32>& MaterialID = GeometryCollection->MaterialID;
						TManagedArray<int32>& MaterialIndex = GeometryCollection->MaterialIndex;

						TArray<uint32> IndexBuffer;
						SkeletalMeshLODRenderData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

						const int32 IndicesCount = IndexBuffer.Num() / 3;
						int NumVertices = GeometryCollection->NumElements(FGeometryCollection::VerticesGroup);
						int InitialNumIndices = GeometryCollection->NumElements(FGeometryCollection::FacesGroup);
						int IndicesBaseIndex = GeometryCollection->AddElements(IndicesCount, FGeometryCollection::FacesGroup);
						for (int32 IndicesIndex = 0, StaticIndex = 0; IndicesIndex < IndicesCount; IndicesIndex++, StaticIndex += 3)
						{
							int32 IndicesOffset = IndicesBaseIndex + IndicesIndex;
							Indices[IndicesOffset] = FIntVector(
								IndexBuffer[StaticIndex] + NumVertices,
								IndexBuffer[StaticIndex + 1] + NumVertices,
								IndexBuffer[StaticIndex + 2] + NumVertices);
							Visible[IndicesOffset] = true;
							MaterialID[IndicesOffset] = 0;
							MaterialIndex[IndicesOffset] = IndicesOffset;
						}

						//
						// Vertex Attributes
						//
						TManagedArray<FVector3f>& Vertex = GeometryCollection->Vertex;
						TManagedArray<FVector3f>& TangentU = GeometryCollection->TangentU;
						TManagedArray<FVector3f>& TangentV = GeometryCollection->TangentV;
						TManagedArray<FVector3f>& Normal = GeometryCollection->Normal;
						TManagedArray<TArray<FVector2f>>& UVs = GeometryCollection->UVs;
						TManagedArray<FLinearColor>& Color = GeometryCollection->Color;
						TManagedArray<int32>& BoneMap = GeometryCollection->BoneMap;
						TManagedArray<FLinearColor>& BoneColor = GeometryCollection->BoneColor;
						TManagedArray<FString>& BoneName = GeometryCollection->BoneName;

						// 
						// Transform Attributes 
						// 
						TManagedArray<int32>& Parent = GeometryCollection->Parent;
						TManagedArray<int32>& SimulationType = GeometryCollection->SimulationType;
						int InitialNumVertices = GeometryCollection->NumElements(FGeometryCollection::VerticesGroup);
						int VertexBaseIndex = GeometryCollection->AddElements(VertexCount, FGeometryCollection::VerticesGroup);
						const int32 NumUVLayers = VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
						for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
						{
							int VertexOffset = VertexBaseIndex + VertexIndex;
							BoneMap[VertexOffset] = -1;
							int32 SkeletalBoneIndex = -1;
							SkinWeightVertexBuffer.GetRigidWeightBone(VertexIndex, SkeletalBoneIndex);
							if (SkeletalBoneIndex > -1)
							{
								BoneMap[VertexOffset] = SkeletalBoneIndex + TransformBaseIndex;
								Vertex[VertexOffset] = (FVector4f)Transform[BoneMap[VertexOffset]].ToInverseMatrixWithScale().TransformPosition((FVector)PositionVertexBuffer.VertexPosition(VertexIndex));
							}
							check(BoneMap[VertexOffset] != -1);
							TangentU[VertexOffset] = VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
							TangentV[VertexOffset] = VertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex);
							Normal[VertexOffset] = VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex);
							
							UVs[VertexOffset].SetNum(NumUVLayers);
							for (int32 UVLayerIdx = 0; UVLayerIdx < NumUVLayers; ++UVLayerIdx)
							{
								UVs[VertexOffset][UVLayerIdx] = VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, UVLayerIdx);
							}
							
							if (VertexBuffers.ColorVertexBuffer.GetNumVertices() == VertexCount)
								Color[VertexOffset] = VertexBuffers.ColorVertexBuffer.VertexColor(VertexIndex);
							else
								Color[VertexOffset] = FLinearColor::White;
						}

						int32 InitialIndex = -1;
						int32 LastParentIndex = -1;
						int32 CurrentLevel = 0;
						for (int32 BoneIndex = 0; BoneIndex < SkeletalBoneMap.Num(); BoneIndex++)
						{
							// transform based on position of the actor. 
							Transform[TransformBaseIndex + BoneIndex] = SkeletalMeshTransform * Transform[TransformBaseIndex + BoneIndex];

							// bone attributes
							BoneName[TransformBaseIndex + BoneIndex] = ReferenceSkeletion.GetBoneName(SkeletalBoneMap[BoneIndex]).ToString();
							const FColor RandBoneColor(FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, FMath::Rand() % 100 + 5, 255);
							BoneColor[TransformBaseIndex + BoneIndex] = FLinearColor(RandBoneColor);

							// Bone Hierarchy - Added at root with no common parent
							int32 ParentIndex = ReferenceSkeletion.GetParentIndex(SkeletalBoneMap[BoneIndex]);
							int32 UseParentIndex = ParentIndex + InitialIndex;
							if (LastParentIndex != UseParentIndex)
							{
								LastParentIndex = UseParentIndex;
							}
							Parent[TransformBaseIndex + BoneIndex] = UseParentIndex;
							SimulationType[TransformBaseIndex + BoneIndex] = FGeometryCollection::ESimulationTypes::FST_Rigid;
						}

						// Geometry Group
						TArray<int32> GeometryIndices;
						GeometryCollectionAlgo::ContiguousArray(GeometryIndices, GeometryCollection->NumElements(FGeometryCollection::GeometryGroup));
						GeometryCollection->RemoveDependencyFor(FGeometryCollection::GeometryGroup);
						GeometryCollection->RemoveElements(FGeometryCollection::GeometryGroup, GeometryIndices);
						::GeometryCollection::AddGeometryProperties(GeometryCollection);

						// for each material, add a reference in our GeometryCollectionObject
						int CurrIdx = 0;
						
						const TArray<FSkeletalMaterial>& SkeletalMeshMaterials = SkeletalMesh->GetMaterials();

						UMaterialInterface* CurrMaterial = SkeletalMeshComponent ? SkeletalMeshComponent->GetMaterial(CurrIdx) : ToRawPtr(SkeletalMeshMaterials[CurrIdx].MaterialInterface);


						int MaterialStart = GeometryCollectionObject->Materials.Num();
						while (CurrMaterial)
						{
							GeometryCollectionObject->Materials.Add(CurrMaterial);
							CurrMaterial = SkeletalMeshComponent ? SkeletalMeshComponent->GetMaterial(++CurrIdx) : ToRawPtr(SkeletalMeshMaterials[++CurrIdx].MaterialInterface);

						}

						const TArray<FSkelMeshRenderSection> &StaticMeshSections = SkeletalMesh->GetResourceForRendering()->LODRenderData[0].RenderSections;

						TManagedArray<FGeometryCollectionSection> & Sections = GeometryCollection->Sections;

						for (const FSkelMeshRenderSection &CurrSection : StaticMeshSections)
						{
							// create new section
							int32 SectionIndex = GeometryCollection->AddElements(1, FGeometryCollection::MaterialGroup);
						
							Sections[SectionIndex].MaterialID = MaterialStart + CurrSection.MaterialIndex;

							Sections[SectionIndex].FirstIndex = IndicesBaseIndex * 3 + CurrSection.BaseIndex;
							Sections[SectionIndex].MinVertexIndex = VertexBaseIndex + CurrSection.BaseVertexIndex;

							Sections[SectionIndex].NumTriangles = CurrSection.NumTriangles;

							// #todo(dmp): what should we set this to?  SkeletalMesh sections are different
							// but we are resetting this when the re indexing happens
							Sections[SectionIndex].MaxVertexIndex = VertexBaseIndex + CurrSection.NumVertices;

							// set the materialid for all of the faces
							for (int32 i = Sections[SectionIndex].FirstIndex / 3; i < Sections[SectionIndex].FirstIndex / 3 + Sections[SectionIndex].NumTriangles; ++i)
							{
								MaterialID[i] = SectionIndex;
							}
						}

					}
				}
			}

			if (ReindexMaterials) {
				GeometryCollection->ReindexMaterials();
			}
		}
	}
}

void FGeometryCollectionConversion::CreateGeometryCollectionCommand(UWorld * World)
{
	UPackage* Package = CreatePackage(TEXT("/Game/GeometryCollectionAsset"));
	auto GeometryCollectionFactory = NewObject<UGeometryCollectionFactory>();
	UGeometryCollection* GeometryCollection = static_cast<UGeometryCollection*>(
		GeometryCollectionFactory->FactoryCreateNew(UGeometryCollection::StaticClass(), Package,
			FName("GeometryCollectionAsset"), RF_Standalone | RF_Public, NULL, GWarn));
	FAssetRegistryModule::AssetCreated(GeometryCollection);
	Package->SetDirtyFlag(true);
}

void FGeometryCollectionConversion::CreateFromSelectedActorsCommand(UWorld * World)
{
	//UE_LOG(UGeometryCollectionConversionLogging, Log, TEXT("FGeometryCollectionConversion::CreateCommand()"));
	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors)
	{
		return;
	}

	UPackage* Package = nullptr;
	UGeometryCollection* GeometryCollection = nullptr;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		if (AActor* Actor = Cast<AActor>(*Iter))
		{
			TArray<UStaticMeshComponent *> StaticMeshComponents;
			Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
			if (StaticMeshComponents.Num() > 0)
			{
				for (int Index = 0; Index < StaticMeshComponents.Num(); Index++)
				{
					if (StaticMeshComponents[Index]->GetStaticMesh())
					{
						if (!Package || !GeometryCollection)
						{
							Package = CreatePackage(TEXT("/Game/GeometryCollectionAsset"));
							auto GeometryCollectionFactory = NewObject<UGeometryCollectionFactory>();
							GeometryCollection = static_cast<UGeometryCollection*>(
								GeometryCollectionFactory->FactoryCreateNew(
									UGeometryCollection::StaticClass(),
									Package,
									FName("GeometryCollectionAsset"),
									RF_Standalone | RF_Public,
									NULL,
									GWarn));
						}
						FGeometryCollectionConversion::AppendStaticMesh(
							StaticMeshComponents[Index]->GetStaticMesh(),
							StaticMeshComponents[Index],
							Actor->GetTransform(),
							GeometryCollection);
					}
				}
			}

			TArray < USkeletalMeshComponent * > SkeletalMeshComponents;
			Actor->GetComponents<USkeletalMeshComponent>(SkeletalMeshComponents);
			if (SkeletalMeshComponents.Num() > 0)
			{
				for (int Index = 0; Index < SkeletalMeshComponents.Num(); Index++)
				{
					if (SkeletalMeshComponents[Index]->SkeletalMesh)
					{
						if (!Package || !GeometryCollection)
						{
							Package = CreatePackage(TEXT("/Game/GeometryCollectionAsset"));
							auto GeometryCollectionFactory = NewObject<UGeometryCollectionFactory>();
							GeometryCollection = static_cast<UGeometryCollection*>(
								GeometryCollectionFactory->FactoryCreateNew(
									UGeometryCollection::StaticClass(),
									Package,
									FName("GeometryCollectionAsset"),
									RF_Standalone | RF_Public,
									NULL,
									GWarn));
						}
						FGeometryCollectionConversion::AppendSkeletalMesh(
							SkeletalMeshComponents[Index]->SkeletalMesh,
							SkeletalMeshComponents[Index],
							Actor->GetTransform(),
							GeometryCollection);
					}
				}
			}
		}
	}

	if(GeometryCollection != nullptr)
	{
		GeometryCollectionAlgo::PrepareForSimulation(GeometryCollection->GetGeometryCollection().Get());

		FAssetRegistryModule::AssetCreated(GeometryCollection);
		GeometryCollection->MarkPackageDirty();
		Package->SetDirtyFlag(true);
	}
}

void FGeometryCollectionConversion::CreateFromSelectedAssetsCommand(UWorld * World)
{
	//UE_LOG(UGeometryCollectionConversionLogging, Log, TEXT("FGeometryCollectionConversion::CreateCommand()"));
	UPackage* Package = nullptr;
	UGeometryCollection* GeometryCollection = nullptr;

	TArray<FAssetData> SelectedAssets;
	GEditor->GetContentBrowserSelections(SelectedAssets);
	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (AssetData.GetAsset()->IsA<UStaticMesh>())
		{
			UE_LOG(UGeometryCollectionConversionLogging, Log, TEXT("Static Mesh Content Browser : %s"), *AssetData.GetClass()->GetName());
			if (!Package || !GeometryCollection)
			{
				Package = CreatePackage(TEXT("/Game/GeometryCollectionAsset"));
				auto GeometryCollectionFactory = NewObject<UGeometryCollectionFactory>();
				GeometryCollection = static_cast<UGeometryCollection*>(
					GeometryCollectionFactory->FactoryCreateNew(
						UGeometryCollection::StaticClass(), 
						Package,
						FName("GeometryCollectionAsset"), 
						RF_Standalone | RF_Public, 
						NULL, 
						GWarn));
			}
			FGeometryCollectionConversion::AppendStaticMesh(
				static_cast<const UStaticMesh*>(AssetData.GetAsset()),
				nullptr,
				FTransform(), 
				GeometryCollection);
		}
		else if (AssetData.GetAsset()->IsA<USkeletalMesh>())
		{
			UE_LOG(UGeometryCollectionConversionLogging, Log, TEXT("Skeletal Mesh Content Browser : %s"), *AssetData.GetClass()->GetName());
			if (!Package || !GeometryCollection)
			{
				Package = CreatePackage(TEXT("/Game/GeometryCollectionAsset"));
				auto GeometryCollectionFactory = NewObject<UGeometryCollectionFactory>();
				GeometryCollection = static_cast<UGeometryCollection*>(
					GeometryCollectionFactory->FactoryCreateNew(
						UGeometryCollection::StaticClass(), 
						Package,
						FName("GeometryCollectionAsset"), 
						RF_Standalone | RF_Public, 
						NULL, 
						GWarn));
			}
			FGeometryCollectionConversion::AppendSkeletalMesh(
				static_cast<const USkeletalMesh *>(AssetData.GetAsset()), 
				nullptr,
				FTransform(),
				GeometryCollection);
		}
	}

	if(GeometryCollection != nullptr)
	{
		GeometryCollectionAlgo::PrepareForSimulation(GeometryCollection->GetGeometryCollection().Get());

		FAssetRegistryModule::AssetCreated(GeometryCollection);
		GeometryCollection->MarkPackageDirty();
		Package->SetDirtyFlag(true);
	}
}
