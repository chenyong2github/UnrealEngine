// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslatorImpl.h"

#include "CoreTechTypes.h"

#ifdef USE_CORETECH_MT_PARSER
#include "CoreTechParserMT.h"
#include "DatasmithSceneGraphBuilder.h"
#include "DatasmithMeshBuilder.h"
#else
#include "CoreTechParser.h"
#endif

#include "DatasmithImportOptions.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Translators\DatasmithTranslator.h"

void FDatasmithCADTranslatorImpl::SetTessellationOptions(const FDatasmithTessellationOptions& Options)
{
	TessellationOptions = Options;
	TessellationOptionsHash = TessellationOptions.GetHash();
}

bool FDatasmithCADTranslatorImpl::Read()
{
#ifdef USE_CORETECH_MT_PARSER
	CTParser->SetOutputPath(OutputPath);
	CTParser->Read();
	//CTParser->Clear();

	SceneGraphBuilder->Build();
	//SceneGraphBuilder->Clear();

	MeshBuilder->SetScaleFactor(CTParser->GetScaleFactor());
	MeshBuilder->LoadRawDataGeom();

#else
	CheckedCTError Result = CTParser->Read();
	if (Result != IO_OK)
	{
		return false;
	}
#endif

	// Force CoreTech to re-tessellate the model with the translator's tessellation parameters
	// This call has no effect on the load of the model
	CTParser->SetTessellationOptions(TessellationOptions);

	return true;
}

TOptional<FMeshDescription> FDatasmithCADTranslatorImpl::GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, CADLibrary::FMeshParameters& OutMeshParameters)
{
#ifdef USE_CORETECH_MT_PARSER
	return MeshBuilder->GetMeshDescription(OutMeshElement, OutMeshParameters);
#else
	return CTParser->GetMeshDescription(OutMeshElement, OutMeshParameters);
#endif
}

FDatasmithCADTranslatorImpl::FDatasmithCADTranslatorImpl(const FDatasmithSceneSource& InSceneSource, TSharedRef<IDatasmithScene> InScene, FString& InCachePath, double FileMetricUnit, double ScaleFactor)
	: DatasmithScene(InScene)
	, SceneSource(InSceneSource)
	, TessellationOptionsHash(0)
#ifdef USE_CORETECH_MT_PARSER
	, CTParser(MakeShared<FCoreTechParserMT>(InCachePath, SceneSource, CADFileToUE4FileMap, CADFileToUE4GeomMap, FileMetricUnit, ScaleFactor))
	, SceneGraphBuilder(MakeShared<FDatasmithSceneGraphBuilder>(InCachePath, InScene, SceneSource, CADFileToUE4FileMap, MeshElementToCTBodyUuidMap))
	, MeshBuilder(MakeShared<FDatasmithMeshBuilder>(InCachePath, CADFileToUE4GeomMap, MeshElementToCTBodyUuidMap))
#else
	, CTParser(MakeShared<FCoreTechParser>(DatasmithScene, SceneSource, FileMetricUnit, ScaleFactor))
#endif
{
}


void FDatasmithCADTranslatorImpl::UnloadScene()
{
	CTParser->UnloadScene();
}
