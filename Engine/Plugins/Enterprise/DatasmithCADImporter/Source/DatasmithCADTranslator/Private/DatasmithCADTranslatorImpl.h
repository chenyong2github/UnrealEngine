// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "CTSession.h"
#include "DatasmithImportOptions.h"
#include "IDatasmithSceneElements.h"
#include "Misc/Paths.h"


using namespace CADLibrary;

class FDatasmithSceneSource;
class IDatasmithActorElement;
class IDatasmithMeshElement;
class IDatasmithMaterialIDElement;

#ifdef USE_CORETECH_MT_PARSER
class FCoreTechParserMT;
class FDatasmithSceneGraphBuilder;
class FDatasmithMeshBuilder;
#else
class FCoreTechParser;
#endif

class FDatasmithCADTranslatorImpl 
{
public:
	FDatasmithCADTranslatorImpl(const FDatasmithSceneSource& InSceneSource, TSharedRef<IDatasmithScene> InScene, FString& CachePath, double FileMetricUnit, double ScaleFactor);

	bool Read();
	void UnloadScene();

	TOptional<FMeshDescription> GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters);

	void SetTessellationOptions(const FDatasmithTessellationOptions& Options);
	void SetOutputPath(const FString& Path) { OutputPath = Path; }
	void SetCachePath(const FString& Path) { CachePath = Path; }
	double GetMetricUnit() const { return 0.01; }

private:

	struct FDagNodeInfo
	{
		FString UEuuid;  // Use for actor name
		FString Label;
		TSharedPtr< IDatasmithActorElement > ActorElement;
	};


private:
	TSharedRef<IDatasmithScene> DatasmithScene;
	const FDatasmithSceneSource& SceneSource;

	FString OutputPath;
	FString CachePath;

	FDatasmithTessellationOptions TessellationOptions;
	uint32 TessellationOptionsHash;

	
#ifdef USE_CORETECH_MT_PARSER
	TMap<FString, FString> CADFileToUE4FileMap;
	TMap<FString, FString> CADFileToUE4GeomMap;
	TMap< TSharedPtr< IDatasmithMeshElement >, uint32 > MeshElementToCTBodyUuidMap;

	TSharedRef < FCoreTechParserMT > CTParser;
	TSharedRef < FDatasmithSceneGraphBuilder > SceneGraphBuilder;
	TSharedRef < FDatasmithMeshBuilder> MeshBuilder;

#else
	TSharedRef < FCoreTechParser > CTParser;
	TMap< IDatasmithMeshElement*, CT_OBJECT_ID* > MeshElementToCTBodyIdMap;
#endif
};
