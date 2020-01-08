// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithMeshExporter.h"

#include "DatasmithMesh.h"
#include "DatasmithMeshUObject.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "MeshDescriptionOperations.h"
#include "UVMapSettings.h"
#include "StaticMeshAttributes.h"


namespace
{
	UDatasmithMesh* FDatasmithMeshToUDatasmithMesh( const FDatasmithMesh& Mesh, bool bValidateRawMesh )
	{
		UDatasmithMesh* UMesh = NewObject< UDatasmithMesh >();
		UMesh->AddToRoot();

		UMesh->MeshName = Mesh.GetName();

		FRawMesh RawMesh;
		FDatasmithMeshUtils::ToRawMesh(Mesh, RawMesh, bValidateRawMesh);

		FDatasmithMeshSourceModel BaseModel;
		BaseModel.RawMeshBulkData.SaveRawMesh( RawMesh );

		UMesh->SourceModels.Add( BaseModel );

		for ( int32 LODIndex = 0; LODIndex < Mesh.GetLODsCount(); ++LODIndex )
		{
			FDatasmithMeshUtils::ToRawMesh(Mesh.GetLOD( LODIndex ), RawMesh, bValidateRawMesh);

			FRawMeshBulkData LODRawMeshBulkData;
			LODRawMeshBulkData.SaveRawMesh( RawMesh );

			FDatasmithMeshSourceModel LODModel;
			LODModel.RawMeshBulkData = LODRawMeshBulkData;

			UMesh->SourceModels.Add( LODModel );
		}

		return UMesh;
	}
}

TSharedPtr< IDatasmithMeshElement > FDatasmithMeshExporter::ExportToUObject( const TCHAR* Filepath, const TCHAR* Filename, FDatasmithMesh& Mesh, FDatasmithMesh* CollisionMesh, EDSExportLightmapUV LightmapUV )
{
	FString NormalizedFilepath = Filepath;
	FPaths::NormalizeDirectoryName( NormalizedFilepath );

	FString NormalizedFilename = Filename;
	FPaths::NormalizeFilename( NormalizedFilename );

	PreExport( Mesh, *NormalizedFilepath, *NormalizedFilename, LightmapUV );

	TArray< UDatasmithMesh* > MeshesToExport;

	// Static mesh
	MeshesToExport.Add( FDatasmithMeshToUDatasmithMesh( Mesh, true ) );

	// Collision mesh
	if ( CollisionMesh )
	{
		UDatasmithMesh* DSColMesh = FDatasmithMeshToUDatasmithMesh( *CollisionMesh, false );
		DSColMesh->bIsCollisionMesh = true;

		MeshesToExport.Add( DSColMesh );
	}

	FString FullPath = FPaths::Combine( *NormalizedFilepath, FPaths::SetExtension( *NormalizedFilename, UDatasmithMesh::GetFileExtension() ) );

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileWriter( *FullPath ) );

	if ( !Archive.IsValid() )
	{
		LastError = FString::Printf( TEXT("Failed writing to file %s"), *FullPath );

		return TSharedPtr< IDatasmithMeshElement >();
	}

	int32 NumMeshes = MeshesToExport.Num();

	*Archive << NumMeshes;

	FMD5 MD5;
	for ( UDatasmithMesh* MeshToExport : MeshesToExport )
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
		MeshToExport->RemoveFromRoot();
	}
	FMD5Hash Hash;
	Hash.Set(MD5);

	Archive->Close();

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh( *FPaths::GetBaseFilename( NormalizedFilename ) );
	MeshElement->SetFile( *FullPath );
	MeshElement->SetFileHash(Hash);

	PostExport( Mesh, MeshElement.ToSharedRef() );

	return MeshElement;
}

void FDatasmithMeshExporter::PreExport( FDatasmithMesh& Mesh, const TCHAR* Filepath, const TCHAR* Filename, EDSExportLightmapUV LightmapUV )
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

void FDatasmithMeshExporter::PostExport( const FDatasmithMesh& DatasmithMesh, TSharedRef< IDatasmithMeshElement > MeshElement )
{
	FBox Extents = DatasmithMesh.GetExtents();
	float Width = Extents.Max[0] - Extents.Min[0];
	float Height = Extents.Max[2] - Extents.Min[2];
	float Depth = Extents.Max[1] - Extents.Min[1];

	MeshElement->SetDimensions( DatasmithMesh.ComputeArea(), Width, Height, Depth );

	MeshElement->SetLightmapSourceUV( DatasmithMesh.GetLightmapSourceUVChannel() );
}

void FDatasmithMeshExporter::CreateDefaultUVs( FDatasmithMesh& Mesh )
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
	FMeshDescriptionOperations::GenerateBoxUV(MeshDescription, UVParameters, TexCoords);
	
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

void FDatasmithMeshExporter::RegisterStaticMeshAttributes(FMeshDescription& MeshDescription)
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
