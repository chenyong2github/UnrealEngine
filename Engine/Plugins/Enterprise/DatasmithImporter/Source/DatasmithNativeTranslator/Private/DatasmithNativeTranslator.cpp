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
#include "Serialization/MemoryReader.h"
#include "StaticMeshOperations.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"


FString FDatasmithNativeTranslator::ResolveFilePath(const FString& FilePath, const TArray<FString>& ResourcePaths)
{
	if (FPaths::IsRelative(FilePath) && !FPaths::FileExists(FilePath))
	{
		for (const FString& ResourcePath : ResourcePaths)
		{
			if (ResourcePath.IsEmpty())
			{
				continue;
			}

			FString NewFilePath = ResourcePath / FilePath;
			if (FPaths::FileExists(NewFilePath))
			{
				return NewFilePath;
			}
		}
	}
	return FilePath;
}

void FDatasmithNativeTranslator::ResolveSceneFilePaths(TSharedRef<IDatasmithScene> Scene, const TArray<FString>& ResourcePaths)
{
	for (int32 Index = 0; Index < Scene->GetMeshesCount(); ++Index)
	{
		const TSharedPtr<IDatasmithMeshElement>& Mesh = Scene->GetMesh(Index);
		const TCHAR* Path = Mesh->GetFile();
		Mesh->SetFile(*ResolveFilePath(Path, ResourcePaths));
	}

	for (int32 Index = 0; Index < Scene->GetTexturesCount(); ++Index)
	{
		const TSharedPtr<IDatasmithTextureElement>& Tex = Scene->GetTexture(Index);
		const TCHAR* Path = Tex->GetFile();
		Tex->SetFile(*ResolveFilePath(Path, ResourcePaths));
	}

	for (int32 Index = 0; Index < Scene->GetLevelSequencesCount(); ++Index)
	{
		const TSharedPtr<IDatasmithLevelSequenceElement>& Sequence = Scene->GetLevelSequence(Index);
		const TCHAR* Path = Sequence->GetFile();
		Sequence->SetFile(*ResolveFilePath(Path, ResourcePaths));
	}

	TFunction<void (const TSharedPtr<IDatasmithActorElement> Actor)> VisitActorTree;
	VisitActorTree = [&](const TSharedPtr<IDatasmithActorElement> Actor) -> void
	{
		if (Actor->IsA(EDatasmithElementType::Landscape))
		{
			const TSharedPtr<IDatasmithLandscapeElement>& LandscapeActor = StaticCastSharedPtr<IDatasmithLandscapeElement>(Actor);
			const TCHAR* Path = LandscapeActor->GetHeightmap();
			LandscapeActor->SetHeightmap(*ResolveFilePath(Path, ResourcePaths));
		}
		else if (Actor->IsA(EDatasmithElementType::Light))
		{
			const TSharedPtr<IDatasmithLightActorElement>& Light = StaticCastSharedPtr<IDatasmithLightActorElement>(Actor);
			const TCHAR* Path = Light->GetIesFile();
			Light->SetIesFile(*ResolveFilePath(Path, ResourcePaths));
		}

		for (int32 ChildIndex = 0; ChildIndex < Actor->GetChildrenCount(); ++ChildIndex)
		{
			VisitActorTree(Actor->GetChild(ChildIndex));
		}
	};

	for (int32 Index = 0; Index < Scene->GetActorsCount(); ++Index)
	{
		VisitActorTree(Scene->GetActor(Index));
	}
}

void FDatasmithNativeTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
	OutCapabilities.SupportedFileFormats.Emplace(TEXT("udatasmith"), TEXT("Datasmith files"));
	OutCapabilities.bParallelLoadStaticMeshSupported = true;
}

bool FDatasmithNativeTranslator::LoadScene(TSharedRef<IDatasmithScene> OutScene)
{
	FDatasmithSceneXmlReader XmlParser;

	bool bParsingResult = XmlParser.ParseFile(GetSource().GetSourceFile(), OutScene);
	if (bParsingResult)
	{
		TArray<FString> ResourcePaths;
		FString ResourcePath = OutScene->GetResourcePath();
		ResourcePath.ParseIntoArray(ResourcePaths, TEXT(";"));

		FString ProjectPath = FPaths::GetPath(GetSource().GetSourceFile());
		ResourcePaths.Insert(ProjectPath, 0);

		ResolveSceneFilePaths(OutScene, ResourcePaths);
	}

	return bParsingResult;
}

namespace DatasmithNativeTranslatorImpl
{
	struct FDatasmithMeshInternal
	{
		bool bIsCollisionMesh = false;
		TArray< FDatasmithMeshSourceModel > SourceModels;
	};

	TArray< FDatasmithMeshInternal > GetDatasmithMeshFromMeshPath( const TCHAR* MeshPath )
	{
		TArray< FDatasmithMeshInternal > Result;

		TUniquePtr<FArchive> Archive( IFileManager::Get().CreateFileReader(MeshPath) );
		if ( !Archive.IsValid() )
		{
			return Result;
		}

		int32 NumMeshes = 0;
		*Archive << NumMeshes;

		UDatasmithMesh* DatasmithMesh = nullptr;
		{
			// Make sure the new UDatasmithMesh object is not created while a garbage collection is performed
			FGCScopeGuard GCGuard;
			// Setting the RF_Standalone bitmask on the new UDatasmithMesh object, to make sure it is not garbage collected
			// while loading and processing the udsmesh file. This can happen on very big meshes (5M+ triangles)
			DatasmithMesh = NewObject< UDatasmithMesh >(GetTransientPackage(), NAME_None, RF_Standalone);
		}

		// Currently we only have 1 mesh per file. If there's a second mesh, it will be a CollisionMesh
		while ( NumMeshes-- > 0)
		{
			TArray< uint8 > Bytes;
			*Archive << Bytes;

			FMemoryReader MemoryReader( Bytes, true );
			MemoryReader.ArIgnoreClassRef = false;
			MemoryReader.ArIgnoreArchetypeRef = false;
			MemoryReader.SetWantBinaryPropertySerialization(true);
			DatasmithMesh->Serialize( MemoryReader );

			FDatasmithMeshInternal& SourceModels = Result.AddDefaulted_GetRef();

			SourceModels.bIsCollisionMesh = DatasmithMesh->bIsCollisionMesh;
			SourceModels.SourceModels = MoveTemp(DatasmithMesh->SourceModels);
		}

		// Tell the garbage collector DatasmithMesh can now be deleted.
		DatasmithMesh->ClearInternalFlags(EInternalObjectFlags::Async);
		DatasmithMesh->ClearFlags(RF_Standalone);

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

		// Do not compute normals and tangents during conversion since we have other operations to apply
		// on the mesh that might invalidate them anyway and we must also validate the mesh to detect
		// vertex positions containing NaN before doing computation as MikkTSpace crashes on NaN values.
		const bool bSkipNormalsAndTangents = true;
		FStaticMeshOperations::ConvertFromRawMesh(RawMesh, MeshDescription, GroupNamePerGroupIndex, bSkipNormalsAndTangents);
		return MeshDescription;
	}

} // ns DatasmithNativeTranslatorImpl

bool FDatasmithNativeTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDatasmithNativeTranslator::LoadStaticMesh);

	using namespace DatasmithNativeTranslatorImpl;

	FString FilePath = MeshElement->GetFile();
	if (!FPaths::FileExists(FilePath))
	{
		return false;
	}

	int32 ExtractionFailure = 0;
	for (FDatasmithMeshInternal& DatasmithMesh : GetDatasmithMeshFromMeshPath( *FilePath ))
	{
		if (DatasmithMesh.bIsCollisionMesh)
		{
			for (FDatasmithMeshSourceModel& SourceModel : DatasmithMesh.SourceModels)
			{
				FRawMesh RawMesh;
				SourceModel.RawMeshBulkData.LoadRawMesh( RawMesh );
				if ( RawMesh.VertexPositions.Num() > 0 )
				{
					OutMeshPayload.CollisionPointCloud = LWC::ConvertArrayType<FVector>(RawMesh.VertexPositions);	// LWC_TODO: Perf pessimization. Was MoveTemp.
					break;
				}
				ExtractionFailure++;
			}
		}
		else
		{
			for (FDatasmithMeshSourceModel& SourceModel : DatasmithMesh.SourceModels)
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
		DatasmithMesh.SourceModels.Reset();
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
		return AnimSerializer.Deserialize(LevelSequenceElement, LevelSequenceElement->GetFile());
	}
	return false;
}
