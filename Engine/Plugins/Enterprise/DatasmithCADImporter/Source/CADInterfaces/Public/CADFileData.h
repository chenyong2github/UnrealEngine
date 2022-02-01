// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"

namespace CADLibrary
{

class FCADFileData
{
public:
	FCADFileData(const FImportParameters& InImportParameters, const FFileDescriptor& InFileDescription, const FString& InCachePath)
		: ImportParameters(InImportParameters)
		, CachePath(InCachePath)
		, bIsCacheDefined(!InCachePath.IsEmpty())
		, FileDescription(InFileDescription)
	{
		SceneGraphArchive.FullPath = FileDescription.GetSourcePath();
		SceneGraphArchive.CADFileName = FileDescription.GetFileName();
	}

	uint32 GetSceneFileHash() const
	{
		if (!SceneFileHash)
		{
			SceneFileHash = HashCombine(FileDescription.GetDescriptorHash(), ::GetTypeHash(ImportParameters.GetStitchingTechnique()));
			SceneFileHash = HashCombine(SceneFileHash, ::GetTypeHash(CADLibrary::FImportParameters::GCADLibrary));
		}
		return SceneFileHash;
	}

	uint32 GetGeomFileHash() const
	{
		if (!GeomFileHash)
		{
			GeomFileHash = GetSceneFileHash();
			GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters));
		}
		return GeomFileHash;
	}

	void SetArchiveNames()
	{
		SceneGraphArchive.ArchiveFileName = FString::Printf(TEXT("UEx%08x"), GetSceneFileHash());
		MeshArchiveFile = FString::Printf(TEXT("UEx%08x"), GetGeomFileHash());
	}

	const FString GetSceneGraphFilePath() const
	{
		if (IsCacheDefined())
		{
			return FPaths::Combine(CachePath, TEXT("scene"), SceneGraphArchive.ArchiveFileName + TEXT(".sg"));
		}
		return FString();
	}

	const FString GetMeshArchiveFilePath() const
	{
		if (IsCacheDefined())
		{
			return FPaths::Combine(CachePath, TEXT("mesh"), MeshArchiveFile + TEXT(".gm"));
		}
		return FString();
	}

	const FString GetBodyCachePath(uint32 BodyHash) const
	{
		return CADLibrary::BuildCacheFilePath(*CachePath, TEXT("body"), BodyHash);
	}

	/**
	 * @return the path of CAD cache file
	 */
	const FString GetCADCachePath() const
	{
		if (IsCacheDefined())
		{
			return CADLibrary::BuildCacheFilePath(*CachePath, TEXT("cad"), FileDescription.GetDescriptorHash());
		}
		return FString();
	}

	const FString GetCachePath() const
	{
		if (IsCacheDefined())
		{
			return CachePath;
		}
		return FString();
	}

	bool IsCacheDefined() const
	{
		return bIsCacheDefined;
	}

	void AddWarningMessages(const FString& Message)
	{
		WarningMessages.Add(Message);
	}

	void LoadSceneGraphArchive()
	{
		FString SceneGraphFilePath = GetSceneGraphFilePath();
		SceneGraphArchive.DeserializeMockUpFile(*SceneGraphFilePath);
	}

	void ExportSceneGraphFile()
	{
		FString SceneGraphFilePath = GetSceneGraphFilePath();
		SceneGraphArchive.SerializeMockUp(*SceneGraphFilePath);
	}

	bool HasComponentOfId(FCadId ComponentId) const
	{
		return SceneGraphArchive.CADIdToComponentIndex.Find(ComponentId) != nullptr;
	}

	int32 ComponentCount()
	{
		return SceneGraphArchive.Components.Num();
	}

	int32 AddComponent(FCadId ComponentId)
	{
		ensure(SceneGraphArchive.Components.Num() < SceneGraphArchive.Components.Max());

		int32 Index = SceneGraphArchive.Components.Emplace(ComponentId);
		SceneGraphArchive.CADIdToComponentIndex.Add(ComponentId, Index);
		return Index;
	}

	FArchiveComponent& GetComponentAt(int32 Index)
	{
		return SceneGraphArchive.Components[Index];
	}


	bool HasInstanceOfId(FCadId InstanceId) const
	{
		return SceneGraphArchive.CADIdToInstanceIndex.Find(InstanceId) != nullptr;
	}

	int32 AddInstance(FCadId InstanceId)
	{
		ensure(SceneGraphArchive.Instances.Num() < SceneGraphArchive.Instances.Max());

		int32 Index = SceneGraphArchive.Instances.Emplace(InstanceId);
		SceneGraphArchive.CADIdToInstanceIndex.Add(InstanceId, Index);
		return Index;
	}

	FArchiveInstance& GetInstanceAt(int32 Index)
	{
		return SceneGraphArchive.Instances[Index];
	}

	int32 GetInstanceIndexFromId(FCadId InstanceId)
	{
		int32* IndexPtr = SceneGraphArchive.CADIdToInstanceIndex.Find(InstanceId);

		return IndexPtr ? *IndexPtr : INDEX_NONE;
	}


	bool HasBodyOfId(FCadId BodyId) const
	{
		return SceneGraphArchive.CADIdToBodyIndex.Find(BodyId) != nullptr;
	}

	int32 AddBody(FCadId BodyId)
	{
		ensure(SceneGraphArchive.Bodies.Num() < SceneGraphArchive.Bodies.Max());

		int32 Index = SceneGraphArchive.Bodies.Emplace(BodyId);
		SceneGraphArchive.CADIdToBodyIndex.Add(BodyId, Index);
		return Index;
	}

	FArchiveBody& GetBodyAt(int32 Index)
	{
		return SceneGraphArchive.Bodies[Index];
	}


	int32* FindUnloadedComponentOfId(FCadId ComponentId)
	{
		return SceneGraphArchive.CADIdToUnloadedComponentIndex.Find(ComponentId);
	}

	bool HasUnloadedComponentOfId(FCadId ComponentId)
	{
		return FindUnloadedComponentOfId(ComponentId) != nullptr;
	}

	int32 AddUnloadedComponent(FCadId ComponentId)
	{
		ensure(SceneGraphArchive.UnloadedComponents.Num() < SceneGraphArchive.UnloadedComponents.Max());

		int32 Index = SceneGraphArchive.UnloadedComponents.Emplace(ComponentId);
		SceneGraphArchive.CADIdToUnloadedComponentIndex.Add(ComponentId, Index);
		return Index;
	}

	FArchiveUnloadedComponent& GetUnloadedComponentAt(int32 Index)
	{
		return SceneGraphArchive.UnloadedComponents[Index];
	}

	FFileDescriptor& GetExternalReferences(int32 Index)
	{
		return SceneGraphArchive.ExternalReferences[Index];
	}

	FFileDescriptor& AddExternalRef(const TCHAR* InFilePath, const TCHAR* InConfiguration, const TCHAR* InRootFilePath)
	{
		return SceneGraphArchive.ExternalReferences.Emplace_GetRef(InFilePath, InConfiguration, InRootFilePath);
	}

	FFileDescriptor& AddExternalRef(const FFileDescriptor& InFileDescription)
	{
		return SceneGraphArchive.ExternalReferences.Emplace_GetRef(InFileDescription);
	}

	/** return a unique value that will be used to define the static mesh name */
	uint32 GetStaticMeshHash(const int32 BodyId)
	{
		return HashCombine(GetSceneFileHash(), ::GetTypeHash(BodyId));
	}

	FBodyMesh& AddBodyMesh(FCadId BodyId, FArchiveBody& Body)
	{
		FBodyMesh& BodyMesh = BodyMeshes.Emplace_GetRef(BodyId);
		BodyMesh.MeshActorName = GetStaticMeshHash(BodyId);
		Body.MeshActorName = BodyMesh.MeshActorName;
		return BodyMesh;
	}

	void ExportMeshArchiveFile()
	{
		FString MeshArchiveFilePath = GetMeshArchiveFilePath();
		SerializeBodyMeshSet(*MeshArchiveFilePath, BodyMeshes);
	}

	const TArray<FFileDescriptor>& GetExternalRefSet() const
	{
		return SceneGraphArchive.ExternalReferences;
	}

	const FString& GetSceneGraphFileName() const
	{
		return SceneGraphArchive.ArchiveFileName;
	}

	const FString& GetMeshFileName() const
	{
		return MeshArchiveFile;
	}

	const TArray<FString>& GetWarningMessages() const
	{
		return WarningMessages;
	}

	const FArchiveSceneGraph& GetSceneGraphArchive() const
	{
		return SceneGraphArchive;
	}

	FArchiveSceneGraph& GetSceneGraphArchive()
	{
		return SceneGraphArchive;
	}

	FArchiveMaterial* FindMaterial(uint32 MaterialId)
	{
		return SceneGraphArchive.MaterialHIdToMaterial.Find(MaterialId);
	}

	FArchiveMaterial& AddMaterial(uint32 MaterialId)
	{
		return SceneGraphArchive.MaterialHIdToMaterial.Emplace(MaterialId, MaterialId);
	}

	FArchiveColor* FindColor(uint32 ColorId)
	{
		return SceneGraphArchive.ColorHIdToColor.Find(ColorId);
	}

	FArchiveColor& AddColor(uint32 ColorId)
	{
		return SceneGraphArchive.ColorHIdToColor.Emplace(ColorId, ColorId);
	}

	const TArray<FBodyMesh>& GetBodyMeshes() const
	{
		return BodyMeshes;
	}

	TArray<FBodyMesh>& GetBodyMeshes()
	{
		return BodyMeshes;
	}

	const FFileDescriptor& GetCADFileDescription() const
	{
		return FileDescription;
	}

	FFileDescriptor& GetCADFileDescription()
	{
		return FileDescription;
	}

	void ReserveBodyMeshes(int32 MaxBodyCount)
	{
		BodyMeshes.Reserve(MaxBodyCount);
	}

	const FImportParameters& GetImportParameters() const
	{
		return ImportParameters;
	}

private:
	const FImportParameters ImportParameters;
	const FString CachePath;
	const bool bIsCacheDefined;

	FFileDescriptor FileDescription;

	FString MeshArchiveFile;

	FArchiveSceneGraph SceneGraphArchive;
	TArray<FBodyMesh> BodyMeshes;

	TArray<FString> WarningMessages;

	mutable uint32 SceneFileHash = 0;
	mutable uint32 GeomFileHash = 0;
};
}