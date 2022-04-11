// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADFileReader.h"

#include "CADFileParser.h"
#include "CADOptions.h"
#include "HAL/FileManager.h"
#include "Templates/TypeHash.h"

#ifdef USE_KERNEL_IO_SDK
#include "CoreTechFileParser.h"
#endif

#ifdef USE_TECHSOFT_SDK
#include "TechSoftFileParser.h"
#include "TechSoftFileParserCADKernelTessellator.h"
#endif

namespace CADLibrary
{
	FCADFileReader::FCADFileReader(const FImportParameters& ImportParams, FFileDescriptor& InFile, const FString& EnginePluginsPath, const FString& InCachePath)
		: CADFileData(ImportParams, InFile, InCachePath)
	{
#ifdef USE_KERNEL_IO_SDK
		if (FImportParameters::GCADLibrary == TEXT("KernelIO"))
		{
			CADParser = MakeUnique<FCoreTechFileParser>(CADFileData, EnginePluginsPath);
		}
#endif
#if defined(USE_KERNEL_IO_SDK) && defined(USE_TECHSOFT_SDK)
		else
#endif
#ifdef USE_TECHSOFT_SDK
		if (FImportParameters::GCADLibrary == TEXT("TechSoft"))
		{
			if (FImportParameters::bGDisableCADKernelTessellation)
			{
				CADParser = MakeUnique<FTechSoftFileParser>(CADFileData, EnginePluginsPath);
			}
			else
			{
				CADParser = MakeUnique<FTechSoftFileParserCADKernelTessellator>(CADFileData, EnginePluginsPath);
			}
		}
#endif
	}

	bool FCADFileReader::FindFile(FFileDescriptor& File)
	{
		const FString& FileName = File.GetFileName();

		FString FilePath = FPaths::GetPath(File.GetSourcePath());
		FString RootFilePath = File.GetRootFolder();

		// Basic case: File exists at the initial path
		if (IFileManager::Get().FileExists(*File.GetSourcePath()))
		{
			return true;
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

		for (int32 IndexFolderPath = 0; IndexFolderPath < RootPaths.Num(); IndexFolderPath++)
		{
			for (int32 IndexFilePath = 0; IndexFilePath < FilePaths.Num(); IndexFilePath++)
			{
				FString NewFilePath = FPaths::Combine(RootPaths[IndexFolderPath], FilePaths[IndexFilePath]);
				if (IFileManager::Get().FileExists(*NewFilePath))
				{
					File.SetSourceFilePath(NewFilePath);
					return true;
				};
			}
		}

		// Last case: the FilePath is elsewhere and the file exist
		// A Warning is launch because the file could be expected to not be loaded
		if (IFileManager::Get().FileExists(*File.GetSourcePath()))
		{
			return true;
		}

		CADFileData.AddWarningMessages(FString::Printf(TEXT("File %s cannot be found."), *File.GetFileName()));
		return false;
	}

	ECADParsingResult FCADFileReader::ProcessFile()
	{
		if (!CADParser.IsValid())
		{
			return ECADParsingResult::ProcessFailed;
		}

		if (!FindFile(CADFileData.GetCADFileDescription()))
		{
			return ECADParsingResult::FileNotFound;
		}

		if (FImportParameters::bGEnableCADCache)
		{
			CADFileData.SetArchiveNames();

			bool bNeedToProceed = true;

			FString CADFileCachePath = CADFileData.GetCADCachePath();
			if (!FImportParameters::bGOverwriteCache && IFileManager::Get().FileExists(*CADFileCachePath))
			{
				FString MeshArchiveFilePath = CADFileData.GetMeshArchiveFilePath();
				if (IFileManager::Get().FileExists(*MeshArchiveFilePath)) // the file has been proceed with same meshing parameters
				{
					bNeedToProceed = false;
				}
				else // the file has been converted into CT file but meshed with different parameters
				{
					CADFileData.GetCADFileDescription().SetCacheFile(CADFileCachePath);
				}
			}

			if (!bNeedToProceed)
			{
				// The file has been yet proceed, get ExternalRef
				CADFileData.LoadSceneGraphArchive();
				return ECADParsingResult::ProcessOk;
			}
		}

		// Process the file
		ECADParsingResult Result = CADParser->Process();

		if (FImportParameters::bGEnableCADCache)
		{
			if (ECADParsingResult::ProcessOk == Result)
			{
				CADFileData.ExportSceneGraphFile();
				CADFileData.ExportMeshArchiveFile();
			}
		}

		return Result;
	}
}
