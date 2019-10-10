// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshUtility.h"
#include "PxPhysicsAPI.h"
#include "StaticMeshAttributes.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "MeshFractureSettings.h"
#include "GeneratedFracturedChunk.h"
#if PLATFORM_WINDOWS
#include "NvBlast.h"
#include "NvBlastAssert.h"
#include "NvBlastGlobals.h"
#include "NvBlastExtAuthoring.h"
#include "NvBlastExtAuthoringMesh.h"
#include "NvBlastExtAuthoringFractureTool.h"
#endif

DEFINE_LOG_CATEGORY(LogBlastMeshUtility);

using namespace physx;

#if PLATFORM_WINDOWS
void FMeshUtility::EditableMeshToBlastMesh(const UEditableMesh* SourceMesh, Nv::Blast::Mesh*& OutBlastMesh)
{
	const FMeshDescription* MeshDescription = SourceMesh->GetMeshDescription();
	FStaticMeshConstAttributes Attributes(*MeshDescription);

	TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector2D> VertexUvs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesConstRef<FVector> VertexNormals = Attributes.GetVertexInstanceNormals();

	// Blast representation
	TArray<PxVec2> BlastUvs;
	TArray<PxVec3> BlastPositions;
	TArray<PxVec3> BlastNormals;
	TArray<uint32> BlastIndices;

	// push all positions
	for(const FVertexInstanceID VertexInstanceId : MeshDescription->VertexInstances().GetElementIDs())
	{
		FVector P = VertexPositions[MeshDescription->GetVertexInstanceVertex(VertexInstanceId)];
		BlastPositions.Push(PxVec3(P.X, P.Y, P.Z));

		FVector N = VertexNormals[VertexInstanceId];
		BlastNormals.Push(PxVec3(N.X, N.Y, N.Z));

		BlastUvs.Push(PxVec2(VertexUvs[VertexInstanceId].X, VertexUvs[VertexInstanceId].Y));
	}

	for(const FPolygonID PolygonID : MeshDescription->Polygons().GetElementIDs())
	{
		const TArray<FTriangleID>& TriangleIDs = MeshDescription->GetPolygonTriangleIDs(PolygonID);
		for(const FTriangleID TriangleID : TriangleIDs)
		{
			for(int32 TriVertIndex = 0; TriVertIndex < 3; ++TriVertIndex)
			{
				const FVertexInstanceID VertexInstanceId = MeshDescription->GetTriangleVertexInstance(TriangleID, TriVertIndex);
				BlastIndices.Push(VertexInstanceId.GetValue());
			}

		}

	}

	OutBlastMesh = NvBlastExtAuthoringCreateMesh(BlastPositions.GetData(), BlastNormals.GetData(), BlastUvs.GetData(), BlastPositions.Num(), BlastIndices.GetData(), BlastIndices.Num());

	int32 FacetCount = OutBlastMesh->getFacetCount();
	Nv::Blast::Facet* FacetBuffer = OutBlastMesh->getFacetsBufferWritable();

	// set the materialID coming from the MeshDescription on each blast mesh vertex
	TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = MeshDescription->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::MaterialAssetName);
	const FPolygonArray& Polys = MeshDescription->Polygons();
	for(int32 Facet = 0; Facet < FacetCount; Facet++)
	{
		FacetBuffer[Facet].materialId = MeshDescription->GetPolygonPolygonGroup(FPolygonID(Facet)).GetValue();
	}
}

void FMeshUtility::EditableMeshToBlastMesh(const UEditableMesh* SourceMesh, int32 PolygonGroup, Nv::Blast::Mesh*& OutBlastMesh)
{
	OutBlastMesh = nullptr;
	const FMeshDescription* MeshDescription = SourceMesh->GetMeshDescription();
	FStaticMeshConstAttributes Attributes(*MeshDescription);

	TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector2D> VertexUVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesConstRef<FVector> VertexNormals = Attributes.GetVertexInstanceNormals();
	UGeometryCollection* GeometryCollection = Cast<UGeometryCollection>(static_cast<UObject*>(SourceMesh->GetSubMeshAddress().MeshObjectPtr));

	//#define VALIDATE_INPUT
#ifdef VALIDATE_INPUT
	TSharedPtr<FGeometryCollection> GeometryCollectionPtr = GeometryCollection->GetGeometryCollection();
	FGeometryCollection* FGC = GeometryCollectionPtr.Get();
	TManagedArray<bool>& Visible = *FGC->Visible;
#endif // VALIDATE_INPUT

	// Blast representation
	TArray<PxVec2> BlastUVs;
	TArray<PxVec3> BlastPositions;
	TArray<PxVec3> BlastNormals;

	BlastPositions.Reserve(VertexPositions.GetNumElements());
	BlastNormals.Reserve(VertexPositions.GetNumElements());
	BlastUVs.Reserve(VertexPositions.GetNumElements());

	// push all positions
	for(const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
	{
		const FVector& P = VertexPositions[MeshDescription->GetVertexInstanceVertex(VertexInstanceID)];
		BlastPositions.Emplace( P.X, P.Y, P.Z );

		const FVector& N = VertexNormals[VertexInstanceID];
		BlastNormals.Emplace(N.X, N.Y, N.Z);

		const FVector2D& UV = VertexUVs[VertexInstanceID];
		BlastUVs.Emplace(UV.X, UV.Y);
	}

	const TArray<FPolygonID>& PolygonGroupIDs = MeshDescription->GetPolygonGroupPolygons(FPolygonGroupID(PolygonGroup));

	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionSPtr = GeometryCollection->GetGeometryCollection();
	FGeometryCollection* GeometryCollectionPtr = GeometryCollectionSPtr.Get();
	check(GeometryCollectionPtr);

	const TManagedArray<int32>& MaterialIDs = GeometryCollectionPtr->MaterialID;


	// We rebuild the arrays to remove un-indexed Vertices.
	// PhysX bounds generation doesn't seem to like unconnected verts.
	TMap<int32,int32> UsedVerticesMap;
	TArray<PxVec3> UsedBlastPositions;
	TArray<PxVec3> UsedBlastNormals;
	TArray<PxVec2> UsedBlastUVs;
	TArray<uint32> BlastIndices;
	TArray<int32> MaterialId;

	UsedBlastPositions.Reserve(BlastPositions.Num());
	UsedBlastNormals.Reserve(BlastPositions.Num());
	UsedBlastUVs.Reserve(BlastPositions.Num());
	BlastIndices.Reserve(BlastPositions.Num());
	MaterialId.Reserve(MaterialIDs.Num());

	for(const FPolygonID PolygonID : PolygonGroupIDs)
	{
		const TArray<FTriangleID>& TriangleIDs = MeshDescription->GetPolygonTriangleIDs(PolygonID);

		check(TriangleIDs.Num() == 1);

#ifdef VALIDATE_INPUT
		// does triangle match the geometry collection triangle
		const FVertexInstanceID VertexInstanceID0 = MeshTriangles[0].GetVertexInstanceID(0);
		const FVertexInstanceID VertexInstanceID1 = MeshTriangles[0].GetVertexInstanceID(1);
		const FVertexInstanceID VertexInstanceID2 = MeshTriangles[0].GetVertexInstanceID(2);

		TManagedArray<FIntVector>&  Indices = *FGC->Indices;
		FIntVector Ind = Indices[PolygonID.GetValue()];
		check(VertexInstanceID0.GetValue() == Ind[0])
			check(VertexInstanceID1.GetValue() == Ind[1])
			check(VertexInstanceID2.GetValue() == Ind[2])
			check(Visible[PolygonID.GetValue()]);
#endif // VALIDATE_INPUT

		for(const FTriangleID TriangleID : TriangleIDs)
		{
			for(int32 TriVertIndex = 0; TriVertIndex < 3; ++TriVertIndex)
			{
				const FVertexInstanceID VertexInstanceID = MeshDescription->GetTriangleVertexInstance(TriangleID, TriVertIndex);
				int32 VertexInstanceValue = VertexInstanceID.GetValue();
				if (UsedVerticesMap.Contains(VertexInstanceValue))
				{
					BlastIndices.Push(UsedVerticesMap[VertexInstanceValue]);
				}
				else
				{
					UsedVerticesMap.Add(VertexInstanceValue,UsedBlastPositions.Num());
					BlastIndices.Push(UsedVerticesMap[VertexInstanceValue]);
					UsedBlastPositions.Emplace(BlastPositions[VertexInstanceValue]);
					UsedBlastNormals.Emplace(BlastNormals[VertexInstanceValue]);
					UsedBlastUVs.Emplace(BlastUVs[VertexInstanceValue]);
				}
			}
			if(GeometryCollection)
			{
				// Material setup coming directly from GeometryCollection bypassing MeshDescription!
				int MaterialID = MaterialIDs[PolygonID.GetValue()];
				MaterialId.Push(MaterialID);
			}
		}
	}

	if(UsedBlastPositions.Num())
	{
		OutBlastMesh = NvBlastExtAuthoringCreateMesh(UsedBlastPositions.GetData(), UsedBlastNormals.GetData(), UsedBlastUVs.GetData(), UsedBlastPositions.Num(), BlastIndices.GetData(), BlastIndices.Num());

		int32 FacetCount = OutBlastMesh->getFacetCount();
		Nv::Blast::Facet* facetBuffer = OutBlastMesh->getFacetsBufferWritable();

		for(int32 Facet = 0; Facet < FacetCount; Facet++)
		{
			// retain the material ids coming from the geometry collection
			facetBuffer[Facet].materialId = MaterialId[Facet];
		}
	}
}

bool FMeshUtility::GenerateGeometryCollectionFromBlastChunk(Nv::Blast::FractureTool* BlastFractureTool, int32 ChunkIndex, UGeometryCollection* FracturedGeometryCollectionObject, bool bIsFirstEverChunk, FGeneratedFracturedChunk& ChunkOut, bool bReindexMaterials)
{
	// get shared vertices and index buffers for all chunks
	Nv::Blast::Vertex* VertexBuffer = nullptr;
	uint32* IndexBuffer = nullptr;
	uint32* IndexBufferOffsets = nullptr;
	uint32 NumVertices = BlastFractureTool->getBufferedBaseMeshes(VertexBuffer, IndexBuffer, IndexBufferOffsets);

	const Nv::Blast::ChunkInfo& ChunkInfo = BlastFractureTool->getChunkInfo(ChunkIndex);
	Nv::Blast::Mesh* ChunkMesh = ChunkInfo.meshData;

	physx::PxVec3 Origin(0, 0, 0);

	// make a geometry collection for each fractured chunk
	// this new collection will be appended to the OutGeometryCollection
	ChunkOut.GeometryCollectionObject = TSharedPtr<UGeometryCollection>(NewObject<UGeometryCollection>());
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> NewGeometryCollectionPtr = ChunkOut.GeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = NewGeometryCollectionPtr.Get();
	check(GeometryCollection);

	AddAdditionalAttributesIfRequired(*ChunkOut.GeometryCollectionObject);

	// Geometry Collection Accessors - Verts Group
	TManagedArray<FVector>& Vertices = GeometryCollection->Vertex;
	TManagedArray<FVector>& Normals = GeometryCollection->Normal;
	TManagedArray<FVector2D>& UVs = GeometryCollection->UV;

	// Geometry Collection Accessors - Geometry Group
	TManagedArray<FIntVector>&  Indices = GeometryCollection->Indices;
	TManagedArray<bool>&  Visible = GeometryCollection->Visible;
	TManagedArray<int32>&  MaterialID = GeometryCollection->MaterialID;
	TManagedArray<int32>&  MaterialIndex = GeometryCollection->MaterialIndex;

	// Geometry Collection Accessors - Transform Group
	TManagedArray<FTransform> & Transforms = GeometryCollection->Transform;

	// Geometry Collection Accessors - Material Group.g
	// TManagedArray<FGeometryCollectionSection> & Sections = GeometryCollection->Sections;

	uint32 BufIndex = IndexBufferOffsets[ChunkIndex];
	uint32 NumIndices = (IndexBufferOffsets[ChunkIndex + 1] - IndexBufferOffsets[ChunkIndex]);

	Nv::Blast::Triangle* Triangles = nullptr;
	uint32 NumTriangles = BlastFractureTool->getBaseMesh(ChunkIndex, Triangles);

	ensure(NumIndices > 3);
	ensure(NumTriangles > 3);
	check(NumIndices == NumTriangles * 3);

	if (NumIndices < 4 || NumTriangles < 4)
	{
		return false;
	}

	int NumMaterialsInChunk = 0;
	int LastMaterialIndex = -1;

	GeometryCollection->Reserve(NumTriangles, FGeometryCollection::FacesGroup);
	GeometryCollection->Reserve(NumIndices, FGeometryCollection::VerticesGroup);
	TMap<uint32, uint32> VertMapping;
	for(uint32 VertIndex = 0; VertIndex < NumIndices; VertIndex += 3)
	{
		int32 IndicesIndex = GeometryCollection->AddElements(1, FGeometryCollection::FacesGroup);
		const Nv::Blast::Triangle& Triangle = Triangles[VertIndex / 3];

		uint32 BaseIndex = BufIndex + VertIndex;

		uint32 RemappedIndex[3];

		for(int TriVertIndex = 0; TriVertIndex < 3; ++TriVertIndex)
		{
			uint32 NextIndex = BaseIndex + TriVertIndex;
			uint32 BlastVertIndex = IndexBuffer[NextIndex];
			check(BlastVertIndex < NumVertices);

			uint32 MappedIndex = VertMapping.FindRef(BlastVertIndex);
			if(MappedIndex > 0)
			{
				RemappedIndex[TriVertIndex] = MappedIndex - 1;
			}
			else
			{
				int GCVerticesIndex = GeometryCollection->AddElements(1, FGeometryCollection::VerticesGroup);
				RemappedIndex[TriVertIndex] = GCVerticesIndex;
				VertMapping.Add(BlastVertIndex, GCVerticesIndex + 1);

				const Nv::Blast::Vertex& BlastVertex = VertexBuffer[BlastVertIndex];

				Vertices[GCVerticesIndex] = FVector(BlastVertex.p.x, BlastVertex.p.y, BlastVertex.p.z);
				Normals[GCVerticesIndex] = FVector(BlastVertex.n.x, BlastVertex.n.y, BlastVertex.n.z);

				if(BlastVertex.n.magnitudeSquared() < 0.25f)
				{
					physx::PxVec3 Normal(Triangle.getNormal());
					Normals[GCVerticesIndex] = FVector(-Normal.x, -Normal.y, -Normal.z);
				}

				const PxVec2& BlastUVCoord = BlastVertex.uv[0];
				UVs[GCVerticesIndex] = FVector2D(BlastUVCoord.x, BlastUVCoord.y);
			}

		}

		Indices[IndicesIndex] = FIntVector(RemappedIndex[0], RemappedIndex[1], RemappedIndex[2]);
		Visible[IndicesIndex] = !bIsFirstEverChunk;

		MaterialID[IndicesIndex] = Triangle.materialId;

		MaterialIndex[IndicesIndex] = IndicesIndex;
	}

	// assign internal materials
	// find most common non interior material
	TMap<int32, int32> MaterialIDCount;
	int32 MaxCount = 0;
	int32 MostCommonMaterialID = -1;
	for (int i = 0; i < GeometryCollection->NumElements(FGeometryCollection::FacesGroup); ++i)
	{
		int32 CurrID = MaterialID[i];
		int32 &CurrCount = MaterialIDCount.FindOrAdd(CurrID);
		CurrCount++;

		if (CurrCount > MaxCount && CurrID != MATERIAL_INTERIOR)
		{
			MaxCount = CurrCount;
			MostCommonMaterialID = CurrID;
		}
	}


	// in this case, we likely have a piece that has all internal faces and no material id.
	// #todo(dmp): We should be assigning a default internal material here - We'll use the internal material for 0 for now
	if (MostCommonMaterialID == -1)
	{
		MostCommonMaterialID = 0;
	}

	// We know that the internal materials are the ones that come right after the surface materials
	// #todo(dmp): formalize the mapping between material and internal material, perhaps on the GC
	// if the most common material is an internal material, then just use this
	int32 InternalMaterialID = MostCommonMaterialID % 2 == 0 ? MostCommonMaterialID + 1 : MostCommonMaterialID;

	// Assign internal material to internal faces
	for (int i = 0; i < GeometryCollection->NumElements(FGeometryCollection::FacesGroup); ++i)
	{
		if (MaterialID[i] == MATERIAL_INTERIOR)
		{
			MaterialID[i] = InternalMaterialID;
		}
	}

	// Transfer color from original mesh to our new one.
	FName ColorName("Color");
	FGeometryCollection *BaseCollection = FracturedGeometryCollectionObject->GetGeometryCollection().Get();
	GeometryCollection::AttributeTransfer<FLinearColor>(BaseCollection, GeometryCollection, ColorName, ColorName);

	if (bReindexMaterials)
	{
		GeometryCollection->ReindexMaterials();
	}

	int ParticlesIndex = GeometryCollection->AddElements(1, FGeometryCollection::TransformGroup);
	FTransform RelativeTransform = FTransform::Identity;
	ChunkOut.ChunkLocation = CalcChunkDelta(ChunkMesh, Origin);
	Transforms[ParticlesIndex] = FTransform::Identity;
	if (!bIsFirstEverChunk)
	{
		GeometryCollectionAlgo::ReCenterGeometryAroundCentreOfMass(GeometryCollection, false);
	}
	GeometryCollectionAlgo::PrepareForSimulation(GeometryCollection, false);
	return true;
}

bool FMeshUtility::AddBlastMeshToGeometryCollection(Nv::Blast::FractureTool* BlastFractureTool, int32 FracturedChunkIndex, const FString& ParentName, const FTransform& ParentTransform, UGeometryCollection* FracturedGeometryCollectionObject, TArray<FGeneratedFracturedChunk>& GeneratedChunksOut, TArray<int32>& DeletedChunksOut)
{
	check(BlastFractureTool);
	AddAdditionalAttributesIfRequired(*FracturedGeometryCollectionObject);
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = FracturedGeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* FracturedGeometryCollection = GeometryCollectionPtr.Get();

	// keep note of chunks to delete - the ones we are now fracturing into smaller chunks
	if(FracturedChunkIndex < FracturedGeometryCollection->NumElements(FGeometryCollection::TransformGroup))
	{
		TManagedArray<int32>& BoneMap = FracturedGeometryCollection->BoneMap;
		TManagedArray<FIntVector>&  Indices = FracturedGeometryCollection->Indices;

		const int32 NumIndices = Indices.Num();
		for(int32 Index = 0; Index < NumIndices; Index++)
		{
			// only delete if fractured chunk got made into children
			if(BoneMap[Indices[Index][0]] == FracturedChunkIndex)
			{
				DeletedChunksOut.AddUnique(FracturedChunkIndex);
			}
		}

	}

	physx::PxVec3 Origin(0, 0, 0);
	uint32 NumChunks = BlastFractureTool->getChunkCount();

	int32 RootBone = 0;
	bool bIsFirstEverRoot = true;
	if(FracturedGeometryCollection->NumElements(FGeometryCollection::TransformGroup) > 0)
	{
		bIsFirstEverRoot = false;
		TArray<int32> RootBones;
		FGeometryCollectionClusteringUtility::GetRootBones(FracturedGeometryCollection, RootBones);
		check(RootBones.Num() == 1);
		RootBone = RootBones[0];
	}

	bool bAllChunksGood = true;

	for(uint32 ChunkIndex = 0; ChunkIndex < NumChunks; ChunkIndex++)
	{
		const bool bIsFirstEverChunk = (bIsFirstEverRoot && ChunkIndex == 0);
		const int32 ParentBone = bIsFirstEverChunk ? FGeometryCollection::Invalid: FracturedChunkIndex;

		// chunk 0 is the original model before fracture - when fracturing a fresh static mesh we keep level 0 geometry
		// otherwise we discard the first 'intact' mesh that comes back from blast
		if (ChunkIndex > 0 || bIsFirstEverRoot)
		{
			FGeneratedFracturedChunk ChunkOut;

			if (GenerateGeometryCollectionFromBlastChunk(BlastFractureTool, ChunkIndex, FracturedGeometryCollectionObject, bIsFirstEverChunk, ChunkOut))
			{
				ChunkOut.FracturedChunkIndex = FracturedChunkIndex;
				ChunkOut.FirstChunk = bIsFirstEverChunk;
				ChunkOut.ParentBone = ParentBone;
				GeneratedChunksOut.Push(ChunkOut);
			}
			else
			{
				bAllChunksGood = false;
			}
		}
	}
	return bAllChunksGood;
}
#endif

void FMeshUtility::AddAdditionalAttributesIfRequired(UGeometryCollection& OutGeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = OutGeometryCollectionObject.GetGeometryCollection();
	FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();
	check(OutGeometryCollection);

	if(!OutGeometryCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
	{
		OutGeometryCollection->AddAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
		OutGeometryCollection->AddAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
		check(OutGeometryCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup));
		check(OutGeometryCollection->HasAttribute("ExplodedTransform", FGeometryCollection::TransformGroup));
	}
}

void FMeshUtility::LogHierarchy(const UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();
	check(GeometryCollection);

	UE_LOG(LogBlastMeshUtility, Log, TEXT("Sizes: VerticesGroup %d, FacesGroup %d, GeometryGroup %d, TransformGroup %d"),
		GeometryCollection->NumElements(FGeometryCollection::VerticesGroup),
		GeometryCollection->NumElements(FGeometryCollection::FacesGroup),
		GeometryCollection->NumElements(FGeometryCollection::GeometryGroup),
		GeometryCollection->NumElements(FGeometryCollection::TransformGroup));

	const TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
	const TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
	const TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
	const TManagedArray<int32>& Levels = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

	int NumElements = GeometryCollection->NumElements(FGeometryCollection::TransformGroup);
	for(int BoneIndex = 0; BoneIndex < NumElements; BoneIndex++)
	{
		const FTransform& Transform = Transforms[BoneIndex];
		UE_LOG(LogBlastMeshUtility, Log, TEXT("Location %3.2f, %3.2f, %3.2f"), Transform.GetLocation().X, Transform.GetLocation().Y, Transform.GetLocation().Z);
		UE_LOG(LogBlastMeshUtility, Log, TEXT("Scaling %3.2f, %3.2f, %3.2f"), Transform.GetScale3D().X, Transform.GetScale3D().Y, Transform.GetScale3D().Z);

		UE_LOG(LogBlastMeshUtility, Log, TEXT("BoneID %d, Name %s, Level %d, IsGeometry %d, ParentBoneID %d, Vector (%3.2f, %3.2f, %3.2f)"),
			BoneIndex, BoneNames[BoneIndex].GetCharArray().GetData(), Levels[BoneIndex], GeometryCollection->IsGeometry(BoneIndex), Parents[BoneIndex], ExplodedVectors[BoneIndex].X, ExplodedVectors[BoneIndex].Y, ExplodedVectors[BoneIndex].Z);

		for(int32 Element : Children[BoneIndex])
		{
			UE_LOG(LogBlastMeshUtility, Log, TEXT("..ChildBoneID %d"), Element);
		}
	}
}

void FMeshUtility::ValidateGeometryCollectionState(const UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();
	check(GeometryCollection);

	const TManagedArray<int32>& Parents = GeometryCollection->Parent;
	const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;
	const TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;

	// there should only ever be one root node
	int NumRootNodes = 0;
	const int32 NumBones = Parents.Num();
	for(int HierarchyBoneIndex = 0; HierarchyBoneIndex < NumBones; HierarchyBoneIndex++)
	{
		if(Parents[HierarchyBoneIndex] == FGeometryCollection::Invalid)
		{
			NumRootNodes++;
		}
	}
	check(NumRootNodes == 1);

}

#if PLATFORM_WINDOWS
FVector FMeshUtility::CalcChunkDelta(Nv::Blast::Mesh* ChunkMesh, physx::PxVec3 Origin)
{
	const physx::PxBounds3& Bounds = ChunkMesh->getBoundingBox();
	const physx::PxVec3& Center = (Bounds.getCenter() - Origin) * 100.0f;
	FVector ChunkCenter = FVector(Center.x, Center.y, Center.z);
	FVector BoxExtents = FVector(Bounds.getExtents().x, Bounds.getExtents().y, Bounds.getExtents().z);
	float MaxExtent = FMath::Max(FMath::Max(BoxExtents.X, BoxExtents.Y), BoxExtents.Z);
	FVector Delta = ChunkCenter * MaxExtent * 20.0f;
	return Delta;
}

FVector FMeshUtility::GetChunkCenter(Nv::Blast::Mesh* ChunkMesh, physx::PxVec3 Origin)
{
	const physx::PxBounds3& Bounds = ChunkMesh->getBoundingBox();
	const physx::PxVec3& Center = (Bounds.getCenter() - Origin) * 100.0f;
	return FVector(Center.x, Center.y, Center.z);

}
#endif

int FMeshUtility::GetMaterialForIndex(const UGeometryCollection* GeometryCollectionObject, int TriangleIndex)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get();
	check(GeometryCollection);

	const TManagedArray<int32>& MaterialIDs = GeometryCollection->MaterialID;
	return MaterialIDs[TriangleIndex];
}
