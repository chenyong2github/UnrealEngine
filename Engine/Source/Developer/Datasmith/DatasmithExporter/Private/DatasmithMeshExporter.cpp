// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithMeshExporter.h"

#include "DatasmithMesh.h"
#include "DatasmithMeshUObject.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

#include "Containers/LockFreeList.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UVMapSettings.h"

/**
 *	Implementation class of the DatasmithMeshExporter
 *	We use a lockfree UDatasmithMesh pool to avoid creating new UObject when exporting and reduces our memory footprint.
 */
class FDatasmithMeshExporterImpl
{
public:
	FDatasmithMeshExporterImpl() :
		UniqueID(FGuid::NewGuid())
	{}

	~FDatasmithMeshExporterImpl()
	{
		ClearUDatasmithMeshPool();
	}

	/**
	 * Generate and fill a UDatasmithMesh from a FDatasmithMesh.
	 * Note: The returned TSharedPtr uses a special destructor to return the UDatasmithMesh to a pool instead of destroying it (which wouldn't work anyways).				
	 */
	TSharedPtr<UDatasmithMesh> GeneratePooledUDatasmithMeshFromFDatasmithMesh( const FDatasmithMesh& Mesh, bool bIsCollisionMesh );

	void PreExport(FDatasmithMesh& DatasmithMesh, const TCHAR* Filepath, const TCHAR* Filename, EDSExportLightmapUV LightmapUV);
	void PostExport(const FDatasmithMesh& DatasmithMesh, TSharedRef< IDatasmithMeshElement > MeshElement);

	void CreateDefaultUVs(FDatasmithMesh& DatasmithMesh);
	void RegisterStaticMeshAttributes(FMeshDescription& MeshDescription);

	FString LastError;

private:
	/**
	 * This function allows reusing an instanced UDatasmithMesh. Reusing the same object will avoid creating new garbage in memory.
	 */
	void FillUDatasmithMeshFromFDatasmithMesh( TSharedPtr<UDatasmithMesh>& UMesh, const FDatasmithMesh& Mesh, bool bValidateRawMesh );

	TSharedPtr<UDatasmithMesh> GetPooledUDatasmithMesh(bool bIsCollisionMesh);

	void ReturnUDatasmithMeshToPool(UDatasmithMesh*& UMesh);

	void ClearUDatasmithMeshPool();

	/**
	 * A pool of UDatasmithMesh that we use to avoid creating new UObject, this greatly reduce the garbage created when one instance of FDatasmithMeshExporter is used to export multiple Meshes.
	 */
	TLockFreePointerListLIFO< UDatasmithMesh > DatasmithMeshUObjectPool;
	static TAtomic<int32> NumberOfUMeshPendingGC;

	/**
	 * This exporter's UniqueID. Used to make sure pooled meshes are not using the same names across different threads.
	 */
	FGuid UniqueID;

	/**
	 * Custom deleter used by the TSharedPtr of a pooled UDatasmithMesh to return it to the pool instead of deleting them.
	 */
	struct FPooledUDatasmithMeshDeleter
	{
		FPooledUDatasmithMeshDeleter(FDatasmithMeshExporterImpl* InExporterPtr)
			: ExporterPtr(InExporterPtr)
		{}

		FORCEINLINE void operator()(UDatasmithMesh* Object) const
		{
			check(Object != nullptr && ExporterPtr != nullptr);
			ExporterPtr->ReturnUDatasmithMeshToPool(Object);
		}

		FDatasmithMeshExporterImpl* ExporterPtr;
	};
};

TAtomic<int32> FDatasmithMeshExporterImpl::NumberOfUMeshPendingGC(0);

FDatasmithMeshExporter::FDatasmithMeshExporter()
{
	Impl = new FDatasmithMeshExporterImpl();
}

FDatasmithMeshExporter::~FDatasmithMeshExporter()
{
	delete Impl;
	Impl = nullptr;
}

TSharedPtr< IDatasmithMeshElement > FDatasmithMeshExporter::ExportToUObject( const TCHAR* Filepath, const TCHAR* Filename, FDatasmithMesh& Mesh, FDatasmithMesh* CollisionMesh, EDSExportLightmapUV LightmapUV )
{
	FString NormalizedFilepath = Filepath;
	FPaths::NormalizeDirectoryName( NormalizedFilepath );

	FString NormalizedFilename = Filename;
	FPaths::NormalizeFilename( NormalizedFilename );

	Impl->PreExport( Mesh, *NormalizedFilepath, *NormalizedFilename, LightmapUV );

	TArray<TSharedPtr<UDatasmithMesh>, TInlineAllocator<2>> MeshesToExport;

	// Static mesh, we keep a static UDatasmithMesh alive as a utility object and re-use it for every export instead of creating a new one every time. This avoid creating garbage in memory.
	bool bIsCollisionMesh = false;
	MeshesToExport.Add( Impl->GeneratePooledUDatasmithMeshFromFDatasmithMesh( Mesh, bIsCollisionMesh ) );

	// Collision mesh
	if ( CollisionMesh )
	{
		bIsCollisionMesh = true;
		MeshesToExport.Add( Impl->GeneratePooledUDatasmithMeshFromFDatasmithMesh( *CollisionMesh, bIsCollisionMesh ) );
	}

	FString FullPath = FPaths::Combine( *NormalizedFilepath, FPaths::SetExtension( *NormalizedFilename, UDatasmithMesh::GetFileExtension() ) );

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileWriter( *FullPath ) );

	if ( !Archive.IsValid() )
	{
		Impl->LastError = FString::Printf( TEXT("Failed writing to file %s"), *FullPath );

		return TSharedPtr< IDatasmithMeshElement >();
	}

	int32 NumMeshes = MeshesToExport.Num();

	*Archive << NumMeshes;

	FMD5 MD5;
	for ( TSharedPtr<UDatasmithMesh>& MeshToExport : MeshesToExport )
	{
		TArray< uint8 > Bytes;
		FMemoryWriter MemoryWriter( Bytes, true );
		MemoryWriter.ArIgnoreClassRef = false;
		MemoryWriter.ArIgnoreArchetypeRef = false;
		MemoryWriter.ArNoDelta = false;
		MemoryWriter.SetWantBinaryPropertySerialization(true);

		MeshToExport->Serialize( MemoryWriter );

		// Calculate the Hash of all the mesh to export
		for (FDatasmithMeshSourceModel& Model : MeshToExport->SourceModels)
		{
			uint8* Buffer = (uint8*)Model.RawMeshBulkData.GetBulkData().LockReadOnly();
			MD5.Update(Buffer, Model.RawMeshBulkData.GetBulkData().GetBulkDataSize());
			Model.RawMeshBulkData.GetBulkData().Unlock();
		}

		*Archive << Bytes;
	}
	FMD5Hash Hash;
	Hash.Set(MD5);

	Archive->Close();

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh( *FPaths::GetBaseFilename( NormalizedFilename ) );
	MeshElement->SetFile( *FullPath );
	MeshElement->SetFileHash(Hash);

	Impl->PostExport( Mesh, MeshElement.ToSharedRef() );

	return MeshElement;
}

FString FDatasmithMeshExporter::GetLastError() const
{
	return Impl->LastError;
}

void FDatasmithMeshExporterImpl::PreExport( FDatasmithMesh& Mesh, const TCHAR* Filepath, const TCHAR* Filename, EDSExportLightmapUV LightmapUV )
{
	// If the mesh doesn't have a name, use the filename as its name
	if ( FCString::Strlen( Mesh.GetName() ) == 0 )
	{
		Mesh.SetName( *FPaths::GetBaseFilename( Filename ) );
	}

	bool bHasUVs = Mesh.GetUVChannelsCount() > 0;
	if ( !bHasUVs )
	{
		CreateDefaultUVs( Mesh );
	}

	for ( int32 LODIndex = 0; LODIndex < Mesh.GetLODsCount(); ++LODIndex )
	{
		CreateDefaultUVs( Mesh.GetLOD( LODIndex ) );
	}
}

void FDatasmithMeshExporterImpl::PostExport( const FDatasmithMesh& DatasmithMesh, TSharedRef< IDatasmithMeshElement > MeshElement )
{
	FBox Extents = DatasmithMesh.GetExtents();
	float Width = Extents.Max[0] - Extents.Min[0];
	float Height = Extents.Max[2] - Extents.Min[2];
	float Depth = Extents.Max[1] - Extents.Min[1];

	MeshElement->SetDimensions( DatasmithMesh.ComputeArea(), Width, Height, Depth );

	MeshElement->SetLightmapSourceUV( DatasmithMesh.GetLightmapSourceUVChannel() );
}

void FDatasmithMeshExporterImpl::CreateDefaultUVs( FDatasmithMesh& Mesh )
{
	if ( Mesh.GetUVChannelsCount() > 0 )
	{
		return;
	}

	// Get the mesh description to generate BoxUV.
	FMeshDescription MeshDescription;
	RegisterStaticMeshAttributes(MeshDescription);
	FDatasmithMeshUtils::ToMeshDescription(Mesh, MeshDescription);
	FUVMapParameters UVParameters(Mesh.GetExtents().GetCenter(), FQuat::Identity, Mesh.GetExtents().GetSize(), FVector::OneVector, FVector2D::UnitVector);
	TMap<FVertexInstanceID, FVector2D> TexCoords;
	FStaticMeshOperations::GenerateBoxUV(MeshDescription, UVParameters, TexCoords);
	
	// Put the results in a map to determine the number of unique values.
	TMap<FVector2D, TArray<int32>> UniqueTexCoordMap;
	for (const TPair<FVertexInstanceID, FVector2D>& Pair : TexCoords)
	{
		TArray<int32>& MappedIndices = UniqueTexCoordMap.FindOrAdd(Pair.Value);
		MappedIndices.Add(Pair.Key.GetValue());
	}

	//Set the UV values
	Mesh.AddUVChannel();
	Mesh.SetUVCount(0, UniqueTexCoordMap.Num());
	int32 UVIndex = 0;
	TArray<int32> IndicesMapping;
	IndicesMapping.AddZeroed(TexCoords.Num());
	for (const TPair<FVector2D, TArray<int32>>& UniqueCoordPair : UniqueTexCoordMap)
	{
		Mesh.SetUV(0, UVIndex, UniqueCoordPair.Key.X, UniqueCoordPair.Key.Y);
		for (int32 IndicesIndex : UniqueCoordPair.Value)
		{
			IndicesMapping[IndicesIndex] = UVIndex;
		}
		UVIndex++;
	}

	//Map the UV indices.
	for (int32 FaceIndex = 0; FaceIndex < Mesh.GetFacesCount(); ++FaceIndex)
	{
		const int32 IndicesOffset = FaceIndex * 3;
		check(IndicesOffset + 2 < IndicesMapping.Num());
		Mesh.SetFaceUV(FaceIndex, 0, IndicesMapping[IndicesOffset + 0], IndicesMapping[IndicesOffset + 1], IndicesMapping[IndicesOffset + 2]);
	}
}

void FDatasmithMeshExporterImpl::RegisterStaticMeshAttributes(FMeshDescription& MeshDescription)
{
	// This is a local inlining of the UStaticMesh::RegisterMeshAttributes() function, to avoid having to include "Engine" in our dependencies..
	///////
	// Add basic vertex attributes
	MeshDescription.VertexAttributes().RegisterAttribute<FVector>(MeshAttribute::Vertex::Position, 1, FVector::ZeroVector, EMeshAttributeFlags::Lerpable);
	MeshDescription.VertexAttributes().RegisterAttribute<float>(MeshAttribute::Vertex::CornerSharpness, 1, 0.0f, EMeshAttributeFlags::Lerpable);

	// Add basic vertex instance attributes
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate, 1, FVector2D::ZeroVector, EMeshAttributeFlags::Lerpable);
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector>(MeshAttribute::VertexInstance::Normal, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated);
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector>(MeshAttribute::VertexInstance::Tangent, 1, FVector::ZeroVector, EMeshAttributeFlags::AutoGenerated);
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<float>(MeshAttribute::VertexInstance::BinormalSign, 1, 0.0f, EMeshAttributeFlags::AutoGenerated);
	MeshDescription.VertexInstanceAttributes().RegisterAttribute<FVector4>(MeshAttribute::VertexInstance::Color, 1, FVector4(1.0f, 1.0f, 1.0f, 1.0f), EMeshAttributeFlags::Lerpable);

	// Add basic edge attributes
	MeshDescription.EdgeAttributes().RegisterAttribute<bool>(MeshAttribute::Edge::IsHard, 1, false);
	MeshDescription.EdgeAttributes().RegisterAttribute<float>(MeshAttribute::Edge::CreaseSharpness, 1, 0.0f, EMeshAttributeFlags::Lerpable);

	// Add basic polygon group attributes
	MeshDescription.PolygonGroupAttributes().RegisterAttribute<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName); //The unique key to match the mesh material slot
}

TSharedPtr<UDatasmithMesh> FDatasmithMeshExporterImpl::GeneratePooledUDatasmithMeshFromFDatasmithMesh(const FDatasmithMesh& Mesh, bool bIsCollisionMesh)
{
	TSharedPtr<UDatasmithMesh> PooledMesh = GetPooledUDatasmithMesh(bIsCollisionMesh);
	FillUDatasmithMeshFromFDatasmithMesh(PooledMesh, Mesh, !bIsCollisionMesh);
	PooledMesh->bIsCollisionMesh = bIsCollisionMesh;
	return PooledMesh;
}

/**
 * This function allows reusing an instanced UDatasmithMesh. Reusing the same object will avoid creating new garbage in memory.
 */
void FDatasmithMeshExporterImpl::FillUDatasmithMeshFromFDatasmithMesh(TSharedPtr<UDatasmithMesh>& UMesh, const FDatasmithMesh& Mesh, bool bValidateRawMesh)
{
	UMesh->MeshName = Mesh.GetName();

	FRawMesh RawMesh;
	FDatasmithMeshUtils::ToRawMesh(Mesh, RawMesh, bValidateRawMesh);

	FDatasmithMeshSourceModel BaseModel;
	BaseModel.RawMeshBulkData.SaveRawMesh(RawMesh);

	UMesh->SourceModels.Add(BaseModel);

	for (int32 LODIndex = 0; LODIndex < Mesh.GetLODsCount(); ++LODIndex)
	{
		FDatasmithMeshUtils::ToRawMesh(Mesh.GetLOD(LODIndex), RawMesh, bValidateRawMesh);

		FRawMeshBulkData LODRawMeshBulkData;
		LODRawMeshBulkData.SaveRawMesh(RawMesh);

		FDatasmithMeshSourceModel LODModel;
		LODModel.RawMeshBulkData = LODRawMeshBulkData;

		UMesh->SourceModels.Add(LODModel);
	}
}

TSharedPtr<UDatasmithMesh> FDatasmithMeshExporterImpl::GetPooledUDatasmithMesh(bool bIsCollisionMesh)
{
	const FString GuidString = UniqueID.ToString();
	const FString CollisionString = bIsCollisionMesh ? TEXT("_Collision") : TEXT("");
	const FString PooledMeshName = FString::Printf(TEXT("DatasmithExporter_%s_TransientPooledUDatasmithMesh%s"), *GuidString, *CollisionString);

	if (UDatasmithMesh* PooledMesh = DatasmithMeshUObjectPool.Pop())
	{
		PooledMesh->Rename(*PooledMeshName);
		return TSharedPtr<UDatasmithMesh>(PooledMesh, FPooledUDatasmithMeshDeleter(this));
	}

	return TSharedPtr<UDatasmithMesh>(NewObject< UDatasmithMesh >((UObject*)GetTransientPackage(), *PooledMeshName, RF_Transient | RF_MarkAsRootSet), FPooledUDatasmithMeshDeleter(this));
}

void FDatasmithMeshExporterImpl::ReturnUDatasmithMeshToPool(UDatasmithMesh*& UMesh)
{
	//Clear the UDatasmithMesh.
	UMesh->SourceModels.Empty();

	//Put it back into the pool
	DatasmithMeshUObjectPool.Push(UMesh);
	//Null the reference to make sure we don't use the object returned to the pool.
	UMesh = nullptr;
}

void FDatasmithMeshExporterImpl::ClearUDatasmithMeshPool()
{
	TArray<UDatasmithMesh*> PooledMeshes;
	DatasmithMeshUObjectPool.PopAll(PooledMeshes);

	for (UDatasmithMesh* UMesh : PooledMeshes)
	{
		UMesh->RemoveFromRoot();
		UMesh->MarkPendingKill();
	}

	//Keep track of the number of garbage UObject generated by clearing the cache so that we can trigger the GC after a while.
	//Even if our UObjects are basically empty at this point and don't have a big memory footprint, the engine will assert when reaching around 65k UObjects.
	//So we need to call the GC before that point.
	NumberOfUMeshPendingGC += PooledMeshes.Num();
	if (NumberOfUMeshPendingGC % 2000 == 0)
	{
		CollectGarbage(RF_NoFlags);
		NumberOfUMeshPendingGC = 0;
	}
}
