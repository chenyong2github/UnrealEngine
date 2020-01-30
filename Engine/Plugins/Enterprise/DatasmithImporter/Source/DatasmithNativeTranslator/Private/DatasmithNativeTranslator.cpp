// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithNativeTranslator.h"

#include "DatasmithAnimationElements.h"
#include "DatasmithAnimationSerializer.h"
#include "DatasmithMeshUObject.h"
#include "DatasmithSceneSource.h"
#include "DatasmithSceneXmlReader.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMeshHelper.h"

#include "HAL/FileManager.h"
#include "MeshDescriptionOperations.h"
#include "Serialization/MemoryReader.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UObject/StrongObjectPtr.h"


void FDatasmithNativeTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
	OutCapabilities.SupportedFileFormats.Emplace(TEXT("udatasmith"), TEXT("Datasmith files"));
	OutCapabilities.bParallelLoadStaticMeshSupported = true;
}

bool FDatasmithNativeTranslator::LoadScene(TSharedRef<IDatasmithScene> OutScene)
{
	FDatasmithSceneXmlReader XmlParser;
	return XmlParser.ParseFile(GetSource().GetSourceFile(), OutScene);
}


namespace DatasmithNativeTranslatorImpl
{

	TArray< UDatasmithMesh* > GetDatasmithMeshFromMeshElement( const TSharedRef< IDatasmithMeshElement > MeshElement )
	{
		TArray< UDatasmithMesh* > Result;

		TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileReader(MeshElement->GetFile()) );
		if ( !Archive.IsValid() )
		{
			return Result;
		}

		int32 NumMeshes;
		*Archive << NumMeshes;

		// Currently we only have 1 mesh per file. If there's a second mesh, it will be a CollisionMesh
		while ( NumMeshes-- > 0)
		{
			TArray< uint8 > Bytes;
			*Archive << Bytes;

			UDatasmithMesh* DatasmithMesh = NewObject< UDatasmithMesh >();

			FMemoryReader MemoryReader( Bytes, true );
			MemoryReader.ArIgnoreClassRef = false;
			MemoryReader.ArIgnoreArchetypeRef = false;
			MemoryReader.SetWantBinaryPropertySerialization(true);
			DatasmithMesh->Serialize( MemoryReader );

			Result.Emplace( DatasmithMesh );
		}

		return Result;
	}

	TOptional<FMeshDescription> ExtractMeshDescription(FDatasmithMeshSourceModel& DSSourceModel)
	{
		FRawMesh RawMesh;
		DSSourceModel.RawMeshBulkData.LoadRawMesh( RawMesh );
		if ( !RawMesh.IsValid() )
		{
			return {};
		}

		// RawMesh -> MeshDescription conversion requires an {mat_index: slot_name} map for its PolygonGroups.
		TMap<int32, FName> GroupNamePerGroupIndex;

		// There is no guaranty that incoming RawMesh.FaceMaterialIndices are sequential, but the conversion assumes so.
		// -> we remap materials identifiers to material indices
		// eg:
		//   incoming per-face mat identifier   5   5   1   1   1   99   99
		//   remapped per-face index            0   0   1   1   1   2    2
		//   per PolygonGroup FName:           "5" "5" "1" "1" "1" "99" "99"
		TSet<int32> MaterialIdentifiers;
		for (int32& MatIdentifier : RawMesh.FaceMaterialIndices)
		{
			bool bAlreadyIn = false;
			int32 IndexOfIdentifier = MaterialIdentifiers.Add(MatIdentifier, &bAlreadyIn).AsInteger();

			// identifier -> name association
			if (!bAlreadyIn)
			{
				FName MaterialSlotName = DatasmithMeshHelper::DefaultSlotName(MatIdentifier);
				GroupNamePerGroupIndex.Add(IndexOfIdentifier, MaterialSlotName);
			}

			// remap old identifier to material index
			MatIdentifier = IndexOfIdentifier;
		}

		FMeshDescription MeshDescription;
		DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);
		FMeshDescriptionOperations::ConvertFromRawMesh(RawMesh, MeshDescription, GroupNamePerGroupIndex);
		return MeshDescription;
	}

} // ns DatasmithNativeTranslatorImpl

bool FDatasmithNativeTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDatasmithNativeTranslator::LoadStaticMesh);

	using namespace DatasmithNativeTranslatorImpl;

	int32 ExtractionFailure = 0;
	for (UDatasmithMesh* DatasmithMesh : GetDatasmithMeshFromMeshElement( MeshElement ))
	{
		if (DatasmithMesh->bIsCollisionMesh)
		{
			for (FDatasmithMeshSourceModel& SourceModel : DatasmithMesh->SourceModels)
			{
				FRawMesh RawMesh;
				SourceModel.RawMeshBulkData.LoadRawMesh( RawMesh );
				if ( RawMesh.VertexPositions.Num() > 0 )
				{
					OutMeshPayload.CollisionPointCloud = MoveTemp(RawMesh.VertexPositions);
					break;
				}
				ExtractionFailure++;
			}
		}
		else
		{
			for (FDatasmithMeshSourceModel& SourceModel : DatasmithMesh->SourceModels)
			{
				if (TOptional<FMeshDescription> Mesh = ExtractMeshDescription(SourceModel))
				{
					OutMeshPayload.LodMeshes.Add(MoveTemp(Mesh.GetValue()));
					continue;
				}
				ExtractionFailure++;
			}
		}

		// Do not wait until garbage collection to free memory of the models
		DatasmithMesh->SourceModels.Reset();
		
		// Object has been created on another thread, tell the garbage collector it can now be deleted.
		DatasmithMesh->ClearInternalFlags(EInternalObjectFlags::Async);
	}

	return ExtractionFailure == 0;
}

bool FDatasmithNativeTranslator::LoadLevelSequence(const TSharedRef<IDatasmithLevelSequenceElement> LevelSequenceElement, FDatasmithLevelSequencePayload& OutLevelSequencePayload)
{
	// #ueent_todo: this totally skip the payload system....
	// Parse the level sequences from file
	FDatasmithAnimationSerializer AnimSerializer;
	if (LevelSequenceElement->GetFile() && IFileManager::Get().FileExists(LevelSequenceElement->GetFile()))
	{
		AnimSerializer.Deserialize(LevelSequenceElement, LevelSequenceElement->GetFile());
	}
	return true;
}
