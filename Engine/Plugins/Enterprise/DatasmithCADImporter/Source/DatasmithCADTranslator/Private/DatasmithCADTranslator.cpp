// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslator.h"

#ifdef CAD_LIBRARY
#include "CoreTechParametricSurfaceExtension.h"
#include "DatasmithCADTranslatorModule.h"
#include "DatasmithDispatcher.h"
#include "DatasmithMeshBuilder.h"
#include "DatasmithSceneGraphBuilder.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"


#define CAD_CACHE_VERSION 1

void FDatasmithCADTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
#ifndef CAD_TRANSLATOR_DEBUG
	OutCapabilities.bParallelLoadStaticMeshSupported = true;
#endif
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("CATPart"), TEXT("CATIA Part files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("CATProduct"), TEXT("CATIA Product files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("cgr"), TEXT("CATIA Graphical Representation V5 files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("3dxml"), TEXT("CATIA files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("3drep"), TEXT("CATIA files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("asm.*"), TEXT("Pro/Engineer Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("asm"), TEXT("Pro/Engineer Assembly, NX files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("creo.*"), TEXT("Pro/Engineer Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("creo"), TEXT("Pro/Engineer Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("neu"), TEXT("Pro/Engineer Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("prt.*"), TEXT("Pro/Engineer Part files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("prt"), TEXT("Pro/Engineer, Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("iam"), TEXT("Inventor Assembly files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("ipt"), TEXT("Inventor Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("iges"), TEXT("IGES files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("igs"), TEXT("IGES files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("jt"), TEXT("JT Open files") });
	
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("sat"), TEXT("3D ACIS model files") });
	
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("SLDASM"), TEXT("SolidWorks Product files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("SLDPRT"), TEXT("SolidWorks Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("step"), TEXT("Step files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("stp"), TEXT("Step files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("x_t"), TEXT("Parasolid files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("asm"), TEXT("Unigraphics Assembly, NX files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("prt"), TEXT("Unigraphics, NX Part files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("dwg"), TEXT("AutoCAD, Model files") });
	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("dgn"), TEXT("MicroStation files") });

	OutCapabilities.SupportedFileFormats.Add(FFileFormatInfo{ TEXT("ct"), TEXT("Kernel_IO files") });
}

bool FDatasmithCADTranslator::LoadScene(TSharedRef<IDatasmithScene> DatasmithScene)
{
	ImportParameters.MetricUnit = 0.001;
	ImportParameters.ScaleFactor = 0.1;
	const FDatasmithTessellationOptions& TesselationOptions = GetCommonTessellationOptions();
	ImportParameters.ChordTolerance = TesselationOptions.ChordTolerance;
	ImportParameters.MaxEdgeLength = TesselationOptions.MaxEdgeLength;
	ImportParameters.MaxNormalAngle = TesselationOptions.NormalTolerance;
	ImportParameters.StitchingTechnique = (CADLibrary::EStitchingTechnique) TesselationOptions.StitchingTechnique;

	FString FileExtension = GetSource().GetSourceFileExtension();
	if (FileExtension == TEXT("jt"))
	{
		ImportParameters.MetricUnit = 1.;
		ImportParameters.ScaleFactor = 100.;
	}

	ImportParameters.ModelCoordSys = CADLibrary::EModelCoordSystem::ZUp_RightHanded;
	if (FileExtension == TEXT("sldprt") || FileExtension == TEXT("sldasm") || // Solidworks
		FileExtension == TEXT("iam") || FileExtension == TEXT("ipt") || // Inventor
		FileExtension.StartsWith(TEXT("asm")) || FileExtension.StartsWith(TEXT("creo")) || FileExtension.StartsWith(TEXT("prt")) // Creo
		)
	{
		ImportParameters.ModelCoordSys = CADLibrary::EModelCoordSystem::YUp_RightHanded;
		ImportParameters.DisplayPreference = CADLibrary::EDisplayPreference::ColorOnly;
		ImportParameters.Propagation = CADLibrary::EDisplayDataPropagationMode::BodyOnly;
	}

	FString CachePath = FPaths::ConvertRelativePathToFull(FDatasmithCADTranslatorModule::Get().GetCacheDir());

	TMap<FString, FString> CADFileToUE4FileMap;
	int32 NumCores = FPlatformMisc::NumberOfCores();
	{
		DatasmithDispatcher::FDatasmithDispatcher Dispatcher(ImportParameters, CachePath, NumCores, CADFileToUE4FileMap, CADFileToUE4GeomMap);
		Dispatcher.AddTask(FPaths::ConvertRelativePathToFull(GetSource().GetSourceFile()));

		bool bWithProcessor = true;

#ifdef CAD_TRANSLATOR_DEBUG
		bWithProcessor = false;
#endif //CAD_TRANSLATOR_DEBUG
		
		Dispatcher.Process(bWithProcessor);
	}

	FDatasmithSceneGraphBuilder SceneGraphBuilder(CADFileToUE4FileMap, CachePath, DatasmithScene, GetSource(), ImportParameters);
	SceneGraphBuilder.Build();

	MeshBuilderPtr = MakeUnique<FDatasmithMeshBuilder>(CADFileToUE4GeomMap, CachePath, ImportParameters);

	return true;
}

void FDatasmithCADTranslator::UnloadScene()
{
	MeshBuilderPtr = nullptr;

	CADFileToUE4GeomMap.Empty();
}

bool FDatasmithCADTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
	if (!MeshBuilderPtr.IsValid())
	{
		return false;
	}

	CADLibrary::FMeshParameters MeshParameters;

	if (TOptional< FMeshDescription > Mesh = MeshBuilderPtr->GetMeshDescription(MeshElement, MeshParameters))
	{
		OutMeshPayload.LodMeshes.Add(MoveTemp(Mesh.GetValue()));

		// Store CoreTech additional data if provided
		const TCHAR* CoretechFile = MeshElement->GetFile();
		if (FPaths::FileExists(CoretechFile))
		{
			TArray<uint8> ByteArray;
			if (FFileHelper::LoadFileToArray(ByteArray, CoretechFile))
			{
				UCoreTechParametricSurfaceData* CoreTechData = Datasmith::MakeAdditionalData<UCoreTechParametricSurfaceData>();
				CoreTechData->SourceFile = CoretechFile;
				CoreTechData->RawData = MoveTemp(ByteArray);
				CoreTechData->SceneParameters.ModelCoordSys = uint8(ImportParameters.ModelCoordSys);
				CoreTechData->SceneParameters.MetricUnit = ImportParameters.MetricUnit;
				CoreTechData->SceneParameters.ScaleFactor = ImportParameters.ScaleFactor;

				CoreTechData->MeshParameters.bNeedSwapOrientation = MeshParameters.bNeedSwapOrientation;
				CoreTechData->MeshParameters.bIsSymmetric = MeshParameters.bIsSymmetric;
				CoreTechData->MeshParameters.SymmetricNormal = MeshParameters.SymmetricNormal;
				CoreTechData->MeshParameters.SymmetricOrigin = MeshParameters.SymmetricOrigin;

				CoreTechData->LastTessellationOptions = GetCommonTessellationOptions();
				OutMeshPayload.AdditionalData.Add(CoreTechData);
			}
		}
	}
	return OutMeshPayload.LodMeshes.Num() > 0;
}

void FDatasmithCADTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	FDatasmithCoreTechTranslator::SetSceneImportOptions(Options);
}
#endif




