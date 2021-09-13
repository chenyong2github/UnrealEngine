// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"

namespace CADLibrary
{
	enum class ECADImportSdk
	{
		None = 0,
		KernelIO, 
		TechSoft
	};

	class FCADFileData
	{
	public:
		FCADFileData(const FImportParameters& InImportParameters, const FFileDescription& InFileDescription, const FString& InCachePath)
			: CADImportSdk(ECADImportSdk::KernelIO)
			, ImportParameters(InImportParameters)
			, CachePath(InCachePath)
			, BodyCacheExt(InImportParameters.bDisableCADKernelTessellation ? TEXT(".ct") : TEXT(".ugeom"))
			, bIsCacheDefined(!InCachePath.IsEmpty())
			, FileDescription(InFileDescription)
		{
			SceneGraphArchive.FullPath = FileDescription.OriginalPath;
			SceneGraphArchive.CADFileName = FileDescription.Name;
		}

		uint32 GetSceneFileHash()
		{
			if (!SceneFileHash)
			{
				SceneFileHash = HashCombine(FileDescription.GetFileHash(), GetTypeHash(ImportParameters.StitchingTechnique));
				SceneFileHash = HashCombine(SceneFileHash, GetTypeHash(CADImportSdk));
			}
			return SceneFileHash;
		}

		uint32 GetGeomFileHash()
		{
			if (!GeomFileHash)
			{
				GeomFileHash = GetSceneFileHash();
				GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters.ChordTolerance));
				GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters.MaxEdgeLength));
				GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters.MaxNormalAngle));
				GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters.MetricUnit));
				GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters.ScaleFactor));
				GeomFileHash = HashCombine(GeomFileHash, GetTypeHash(ImportParameters.StitchingTechnique));
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
			if (IsCacheDefined())
			{
				FString BodyFileName = FString::Printf(TEXT("UEx%08x"), BodyHash);
				return FPaths::Combine(CachePath, TEXT("body"), BodyFileName + BodyCacheExt);
			}
			return FString();
		}

		const FString GetCADCachePath() const
		{
			if (IsCacheDefined())
			{
				FString CacheFileName = FString::Printf(TEXT("UEx%08x"), FileDescription.GetFileHash());
				return FPaths::Combine(CachePath, TEXT("cad"), CacheFileName + TEXT(".ct"));
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


		int32 AddComponent(FCadId ComponentId)
		{
			int32 Index = SceneGraphArchive.ComponentSet.Emplace(ComponentId);
			SceneGraphArchive.CADIdToComponentIndex.Add(ComponentId, Index);
			return Index;
		}

		FArchiveComponent& GetComponentAt(int32 Index)
		{
			return SceneGraphArchive.ComponentSet[Index];
		}


		bool HasInstanceOfId(FCadId InstanceId) const
		{
			return SceneGraphArchive.CADIdToInstanceIndex.Find(InstanceId) != nullptr;
		}

		int32 AddInstance(FCadId InstanceId)
		{
			int32 Index = SceneGraphArchive.Instances.Emplace(InstanceId);
			SceneGraphArchive.CADIdToInstanceIndex.Add(InstanceId, Index);
			return Index;
		}

		FArchiveInstance& GetInstanceAt(int32 Index)
		{
			return SceneGraphArchive.Instances[Index];
		}


		bool HasBodyOfId(FCadId BodyId) const
		{
			return SceneGraphArchive.CADIdToBodyIndex.Find(BodyId) != nullptr;
		}

		int32 AddBody(FCadId BodyId)
		{
			int32 Index = SceneGraphArchive.BodySet.Emplace(BodyId);
			SceneGraphArchive.CADIdToBodyIndex.Add(BodyId, Index);
			return Index;
		}

		FArchiveBody& GetBodyAt(int32 Index)
		{
			return SceneGraphArchive.BodySet[Index];
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
			int32 Index = SceneGraphArchive.UnloadedComponentSet.Emplace(ComponentId);
			SceneGraphArchive.CADIdToUnloadedComponentIndex.Add(ComponentId, Index);
			return Index;
		}

		FArchiveUnloadedComponent& GetUnloadedComponentAt(int32 Index)
		{
			return SceneGraphArchive.UnloadedComponentSet[Index];
		}

		FFileDescription& GetExternalReferences(int32 Index)
		{
			return SceneGraphArchive.ExternalRefSet[Index];
		}

		FFileDescription& AddExternalRef(const TCHAR* InFilePath, const TCHAR* InConfiguration, const TCHAR* InRootFilePath)
		{
			return SceneGraphArchive.ExternalRefSet.Emplace_GetRef(InFilePath, InConfiguration, InRootFilePath);
		}

		FFileDescription& AddExternalRef(const FFileDescription& InFileDescription)
		{
			return SceneGraphArchive.ExternalRefSet.Emplace_GetRef(InFileDescription);
		}

		FBodyMesh& AddBodyMesh(FCadId BodyId)
		{
			return BodyMeshes.Emplace_GetRef(BodyId);
		}


		void ExportMeshArchiveFile()
		{
			FString MeshArchiveFilePath = GetMeshArchiveFilePath();
			SerializeBodyMeshSet(*MeshArchiveFilePath, BodyMeshes);
		}

		const TArray<FFileDescription>& GetExternalRefSet() const 
		{
			return SceneGraphArchive.ExternalRefSet;
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

		const FFileDescription& GetCADFileDescription() const
		{
			return FileDescription;
		}

		FFileDescription& GetCADFileDescription()
		{
			return FileDescription;
		}

		bool HasConfiguration() const
		{
			return !FileDescription.Configuration.IsEmpty();
		}

		const FString& GetConfiguration() const
		{
			return FileDescription.Configuration;
		}

		const FString& FileExtension() const
		{
			return FileDescription.Extension;
		}

		const FString& FilePath() const
		{
			return FileDescription.Path;
		}

		const FString& FileName() const
		{
			return FileDescription.Name;
		}

		void ReplaceFileByCacheBackup(const FString& CacheFilePath)
		{
			FileDescription.ReplaceByKernelIOBackup(CacheFilePath);
		}

		void ReserveBodyMeshes(int32 MaxBodyCount)
		{
			BodyMeshes.Reserve(MaxBodyCount);
		}

		const FImportParameters& GetImportParameters() const
		{
			return ImportParameters;
		}

		double MetricUnit() const
		{
			return ImportParameters.MetricUnit;
		}

		const EStitchingTechnique& GetStitchingTechnique() const
		{
			return ImportParameters.StitchingTechnique;
		}

		bool IsCADKernelTessellation() const
		{
			return ImportParameters.bDisableCADKernelTessellation;
		}

		bool IsSequentialImport() const
		{
			return ImportParameters.bEnableSequentialImport;
		}

		bool NeedUVMapScaling() const
		{
			return ImportParameters.bScaleUVMap;
		}

		double GetScaleFactor() const
		{
			return ImportParameters.ScaleFactor;
		}

		double GetMetricUnit() const
		{
			return ImportParameters.MetricUnit;
		}

	private:
		ECADImportSdk CADImportSdk = ECADImportSdk::KernelIO;
		const FImportParameters ImportParameters;
		const FString CachePath;
		const FString BodyCacheExt;
		const bool bIsCacheDefined;

		FFileDescription FileDescription;

		FString MeshArchiveFile;

		FArchiveSceneGraph SceneGraphArchive;
		TArray<FBodyMesh> BodyMeshes;

		TArray<FString> WarningMessages;

		uint32 SceneFileHash = 0;
		uint32 GeomFileHash = 0;
	};
}