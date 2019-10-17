// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if defined(USE_CORETECH_MT_PARSER) && defined(CAD_LIBRARY)

#include "CoreMinimal.h"

#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "CoreTechTypes.h"
#include "CoreTechHelper.h"
#include "CTSession.h"
#include "DatasmithImportOptions.h"
#include "Misc/Paths.h"

class IDatasmithMeshElement;
class FDatasmithSceneSource;

#define COLORSETLINE    3
#define MATERIALSETLINE 4
#define EXTERNALREFLINE 7
#define MAPCTIDLINE     8

class FCoreTechParserMT
{
public:
	FCoreTechParserMT(const FString& InCachePath, const FDatasmithSceneSource& InSource, TMap<FString, FString>& SharedCADFileToUnrealFile, TMap<FString, FString>& SharedCADFileToGeomMap);

	bool Read();
	void UnloadScene();

	void SetTessellationOptions(const FDatasmithTessellationOptions& Options);
	void SetOutputPath(const FString& Path) 
	{ 
		OutputPath = Path; 
	}

	//double GetMetricUnit() const { return CurrentSession; }
	double GetScaleFactor() const 
	{ 
		return CurrentSession->GetImportParameters().ScaleFactor; 
	}

protected:

	bool ReadFileStack();
	void GetRawDataFileExternalRef(const FString& InRawDataFile);

	void AddFileToProcess(const FString& File);
	void GetNewFileToProcess(FString& OutFile);
	void LinkCTFileToUnrealSceneGraphFile(const FString& CTFile, const FString& UnrealFile);
	void LinkCTFileToUnrealGeomFile(const FString& CTFile, const FString& UnrealFile);

protected:
	const FDatasmithSceneSource& Source;
	FString OutputPath;
	FString CachePath;
	FString FilePath;

	TMap<FString, FString>& CADFileToUnrealFileMap;
	TMap<FString, FString>& CADFileToUnrealGeomMap;

	TQueue<FString> FileToRead;
	TSet<FString> FileToReadSet;

	TSet<FString> FileLoaded;
	TSet<FString> FileFailed;
	TSet<FString> FileNotFound;
	TSet<FString> FileProceed;

	uint32 TessellationOptionsHash;

	TSharedPtr<CADLibrary::CTSession> CurrentSession;
};



class FCoreTechFileParser
{
public:
	FCoreTechFileParser(const FString InCTFile, const FString InCTFullPath, const FString InSgFile, const FString InGmFile, const FString InCachePath, CADLibrary::FImportParameters& ImportParams);

	//void UnloadScene();

	double GetMetricUnit() const { return 0.01; }

	bool ReadFile();

	TSet<FString>& GetExternalRefSet()
	{
		return ExternalRefSet;
	}

protected:
	bool ReadNode(CT_OBJECT_ID NodeId);
	bool ReadInstance(CT_OBJECT_ID NodeId);
	bool ReadComponent(CT_OBJECT_ID NodeId);
	bool ReadUnloadedComponent(CT_OBJECT_ID NodeId);
	bool ReadBody(CT_OBJECT_ID NodeId);

	uint32 GetMaterialNum();
	void ReadColor();
	void ReadMaterial();

	void ReadNodeMetaDatas(CT_OBJECT_ID NodeId);

	CT_FLAGS SetCoreTechImportOption(const FString& MainFileExt);
	void GetAttributeValue(CT_ATTRIB_TYPE attrib_type, int ith_field, FString& value);

	void ExportFileSceneGraph();

protected:
	FString CADFile;
	FString FullPath;
	FString CachePath;
	FString OutSgFile;
	FString OutGmFile;

	TSet<FString> ExternalRefSet;

	TArray<FString> SceneGraphDescription;
	TMap<uint32, uint32> CTIdToRawLineMap;

	CADLibrary::FCTMaterialPartition Material2Partition;

	bool bNeedSaveCTFile;

	CADLibrary::FImportParameters& ImportParameters;
};

#endif //  USE_CORETECH_MT_PARSER
