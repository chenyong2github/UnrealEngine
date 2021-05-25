// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTechFileParser.h"

#include "HAL/FileManager.h"
#include "Templates/TypeHash.h"

namespace CADLibrary 
{
	uint32 GetSceneFileHash(const uint32 InSGHash, const FImportParameters& ImportParam)
	{
		uint32 FileHash = HashCombine(InSGHash, GetTypeHash(ImportParam.StitchingTechnique));
		return FileHash;
	}

	uint32 GetGeomFileHash(const uint32 InSGHash, const FImportParameters& ImportParam)
	{
		uint32 FileHash = InSGHash;
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.ChordTolerance));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.MaxEdgeLength));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.MaxNormalAngle));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.MetricUnit));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.ScaleFactor));
		FileHash = HashCombine(FileHash, GetTypeHash(ImportParam.StitchingTechnique));
		return FileHash;
	}

	FCoreTechFileParser::FCoreTechFileParser(const FImportParameters& ImportParams, const FString& EnginePluginsPath, const FString& InCachePath)
		: CachePath(InCachePath)
		, ImportParameters(ImportParams)
	{
		CTKIO_InitializeKernel(*EnginePluginsPath);
	}

	bool FCoreTechFileParser::FindFile(FFileDescription& File)
	{
		FString FileName = File.Name;

		FString FilePath = FPaths::GetPath(File.Path);
		FString RootFilePath = File.MainCadFilePath;

		// Basic case: FilePath is, or is in a sub-folder of, RootFilePath
		if (FilePath.StartsWith(RootFilePath))
		{
			return IFileManager::Get().FileExists(*File.Path);
		}

		// Advance case: end of FilePath is in a upper-folder of RootFilePath
		// e.g.
		// FilePath = D:\\data temp\\Unstructured project\\Folder2\\Added_Object.SLDPRT
		//                                                 ----------------------------
		// RootFilePath = D:\\data\\CAD Files\\SolidWorks\\p033 - Unstructured project\\Folder1
		//                ------------------------------------------------------------
		// NewPath = D:\\data\\CAD Files\\SolidWorks\\p033 - Unstructured project\\Folder2\\Added_Object.SLDPRT
		TArray<FString> RootPaths;
		RootPaths.Reserve(30);
		do
		{
			RootFilePath = FPaths::GetPath(RootFilePath);
			RootPaths.Emplace(RootFilePath);
		} while (!FPaths::IsDrive(RootFilePath) && !RootFilePath.IsEmpty());

		TArray<FString> FilePaths;
		FilePaths.Reserve(30);
		FilePaths.Emplace(FileName);
		while (!FPaths::IsDrive(FilePath) && !FilePath.IsEmpty())
		{
			FString FolderName = FPaths::GetCleanFilename(FilePath);
			FilePath = FPaths::GetPath(FilePath);
			FilePaths.Emplace(FPaths::Combine(FolderName, FilePaths.Last()));
		};

		for(int32 IndexFolderPath = 0; IndexFolderPath < RootPaths.Num(); IndexFolderPath++)
		{
			for (int32 IndexFilePath = 0; IndexFilePath < FilePaths.Num(); IndexFilePath++)
			{
				FString NewFilePath = FPaths::Combine(RootPaths[IndexFolderPath], FilePaths[IndexFilePath]);
				if(IFileManager::Get().FileExists(*NewFilePath))
				{
					File.Path = NewFilePath;
					return true;
				};
			}
		}

		// Last case: the FilePath is elsewhere and the file exist
		// A Warning is launch because the file could be expected to not be loaded
		if(IFileManager::Get().FileExists(*File.Path))
		{
			return true;
		}

		WarningMessages.Add(FString::Printf(TEXT("File %s cannot be found."), *File.Path));
		return false;
	}

	ECoreTechParsingResult FCoreTechFileParser::ProcessFile(const FFileDescription& InFileDescription)
	{
		FileDescription = InFileDescription;

		if (!FindFile(FileDescription))
		{
			return ECoreTechParsingResult::FileNotFound;
		}

		if (CachePath.IsEmpty())
		{
			return CTKIO_LoadFile(FileDescription, ImportParameters, CachePath, SceneGraphArchive, WarningMessages, BodyMeshes);
		}

		uint32 FileHash = FileDescription.GetFileHash();
		FString CTFileName = FString::Printf(TEXT("UEx%08x"), FileHash);
		FString CTFilePath = FPaths::Combine(CachePath, TEXT("cad"), CTFileName + TEXT(".ct"));

		uint32 SceneFileHash = GetSceneFileHash(FileHash, ImportParameters);
		SceneGraphArchive.ArchiveFileName = FString::Printf(TEXT("UEx%08x"), SceneFileHash);

		FString SceneGraphArchiveFilePath = FPaths::Combine(CachePath, TEXT("scene"), SceneGraphArchive.ArchiveFileName + TEXT(".sg"));

		uint32 MeshFileHash = GetGeomFileHash(SceneFileHash, ImportParameters);
		MeshArchiveFile = FString::Printf(TEXT("UEx%08x"), MeshFileHash);
		MeshArchiveFilePath = FPaths::Combine(CachePath, TEXT("mesh"), MeshArchiveFile + TEXT(".gm"));

		bool bNeedToProceed = true;

		if (ImportParameters.bEnableCacheUsage && IFileManager::Get().FileExists(*CTFilePath))
		{
			if (IFileManager::Get().FileExists(*MeshArchiveFilePath)) // the file has been proceed with same meshing parameters
			{
				bNeedToProceed = false;
			}
			else // the file has been converted into CT file but meshed with different parameters
			{
				FileDescription.ReplaceByKernelIOBackup(CTFilePath);
			}
		}

		if (!bNeedToProceed)
		{
			// The file has been yet proceed, get ExternalRef
			LoadSceneGraphArchive(SceneGraphArchiveFilePath);
			return ECoreTechParsingResult::ProcessOk;
		}

		// Process the file
		ECoreTechParsingResult Result = CTKIO_LoadFile(FileDescription, ImportParameters, CachePath, SceneGraphArchive, WarningMessages, BodyMeshes);
		if (ECoreTechParsingResult::ProcessOk == Result)
		{
			ExportSceneGraphFile();
			ExportMeshArchiveFile();
		}

		return Result;
	}

	void FCoreTechFileParser::LoadSceneGraphArchive(const FString& SGFile)
	{
		SceneGraphArchive.DeserializeMockUpFile(*SGFile);
	}

	void FCoreTechFileParser::ExportSceneGraphFile()
	{
		SceneGraphArchive.SerializeMockUp(*FPaths::Combine(CachePath, TEXT("scene"), SceneGraphArchive.ArchiveFileName + TEXT(".sg")));
	}

	void FCoreTechFileParser::ExportMeshArchiveFile()
	{
		SerializeBodyMeshSet(*MeshArchiveFilePath, BodyMeshes);
	}
}
