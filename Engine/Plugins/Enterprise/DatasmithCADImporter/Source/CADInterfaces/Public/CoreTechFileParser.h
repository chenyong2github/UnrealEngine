// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "CoreTechTypes.h"

namespace CADLibrary
{
	class CADINTERFACES_API FCoreTechFileParser
	{
	public:
		/**
		 * @param ImportParams Parameters that setting import data like mesh SAG...
		 * @param EnginePluginsPath Full Path of EnginePlugins. Mandatory to set KernelIO to import DWG, or DGN files
		 * @param InCachePath Full path of the cache in which the data will be saved
		 */
		FCoreTechFileParser(const FImportParameters& ImportParams, const FString& EnginePluginsPath = TEXT(""), const FString& InCachePath = TEXT(""));

		ECoreTechParsingResult ProcessFile(const FFileDescription& InCTFileDescription);

		TArray<FFileDescription>& GetExternalRefSet()
		{
			return SceneGraphArchive.ExternalRefSet;
		}

		const FString& GetSceneGraphFile() const
		{
			return SceneGraphArchive.ArchiveFileName;
		}

		const FString& GetMeshFileName() const
		{
			return MeshArchiveFile;
		}

		const CADLibrary::FFileDescription& GetCADFileDescription() const
		{
			return FileDescription;
		}

		const TArray<FString>& GetWarningMessages() const 
		{
			return WarningMessages;
		}

		FArchiveSceneGraph& GetSceneGraphArchive() { return SceneGraphArchive; }

		TArray<FBodyMesh>& GetBodyMeshes() { return BodyMeshes; }

	private:
		bool FindFile(FFileDescription& File);

		void LoadSceneGraphArchive(const FString& SceneGraphFilePath);

		void ExportSceneGraphFile();
		void ExportMeshArchiveFile();

	protected:
		FString CachePath;

		CADLibrary::FFileDescription FileDescription;

		FArchiveSceneGraph SceneGraphArchive;
		TArray<FString> WarningMessages;

		FString MeshArchiveFilePath;
		FString MeshArchiveFile;

		TArray<FBodyMesh> BodyMeshes;

		const FImportParameters& ImportParameters;
	};
} // ns CADLibrary
