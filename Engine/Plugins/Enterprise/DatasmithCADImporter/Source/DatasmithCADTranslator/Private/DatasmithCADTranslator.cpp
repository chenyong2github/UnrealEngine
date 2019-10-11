// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslator.h"

#include "CADLibraryOptions.h"
#include "CoreTechParametricSurfaceExtension.h"
#include "DatasmithCADTranslatorModule.h"
#include "DatasmithImportOptions.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


FDatasmithCADTranslator::FDatasmithCADTranslator()
	: Translator(nullptr)
{
}

void FDatasmithCADTranslator::Initialize(FDatasmithTranslatorCapabilities& OutCapabilities)
{
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
}

bool FDatasmithCADTranslator::IsSourceSupported(const FDatasmithSceneSource& Source)
{
	return true;
}

bool FDatasmithCADTranslator::LoadScene(TSharedRef<IDatasmithScene> DatasmithScene)
{
	FString CachePath = FPaths::ConvertRelativePathToFull(FDatasmithCADTranslatorModule::Get().GetCacheDir());

	double FileMetricUnit = 0.001;
	double ScaleFactor = 0.1;

	FString FileExtension = GetSource().GetSourceFileExtension();
	if (FileExtension == TEXT("jt"))
	{
		FileMetricUnit = 1.;
		ScaleFactor = 100.;
	}

	Translator = MakeShared<FDatasmithCADTranslatorImpl>(GetSource(), DatasmithScene, CachePath, FileMetricUnit, ScaleFactor);
	if (!Translator)
	{
		return false;
	}

	FString OutputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FDatasmithCADTranslatorModule::Get().GetTempDir(), TEXT("Cache"), GetSource().GetSceneName()));
	IFileManager::Get().MakeDirectory(*OutputPath, true);

	Translator->SetOutputPath(OutputPath);
	Translator->SetTessellationOptions(TessellationOptions);

	return Translator->Read();

	return true;
}

void FDatasmithCADTranslator::UnloadScene()
{
	Translator->UnloadScene();
}

bool FDatasmithCADTranslator::LoadStaticMesh(const TSharedRef<IDatasmithMeshElement> MeshElement, FDatasmithMeshElementPayload& OutMeshPayload)
{
	CADLibrary::FMeshParameters MeshParameters;
	MeshParameters.ModelCoordSys = FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded;

	if (TOptional< FMeshDescription > Mesh = Translator->GetMeshDescription(MeshElement, MeshParameters))
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
				CoreTechData->SceneParameters.ModelCoordSys = uint8(FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded);
				CoreTechData->SceneParameters.MetricUnit = 0.001;
				CoreTechData->SceneParameters.ScaleFactor= 0.1;

				CoreTechData->MeshParameters.bNeedSwapOrientation = MeshParameters.bNeedSwapOrientation;
				CoreTechData->MeshParameters.bIsSymmetric = MeshParameters.bIsSymmetric;
				CoreTechData->MeshParameters.SymmetricNormal = MeshParameters.SymmetricNormal;
				CoreTechData->MeshParameters.SymmetricOrigin = MeshParameters.SymmetricOrigin;

				CoreTechData->LastTessellationOptions = TessellationOptions;
				OutMeshPayload.AdditionalData.Add(CoreTechData);
			}
		}
	}
	return OutMeshPayload.LodMeshes.Num() > 0;
}

void FDatasmithCADTranslator::GetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	TStrongObjectPtr<UDatasmithCommonTessellationOptions> TessellationOptionsPtr = Datasmith::MakeOptions<UDatasmithCommonTessellationOptions>();
	Options.Add(TessellationOptionsPtr);
}

void FDatasmithCADTranslator::SetSceneImportOptions(TArray<TStrongObjectPtr<UObject>>& Options)
{
	for (const TStrongObjectPtr<UObject>& OptionPtr : Options)
	{
		if (UObject* Option = OptionPtr.Get())
		{
			if (UDatasmithCommonTessellationOptions* TessellationOptionsObject = Cast<UDatasmithCommonTessellationOptions>(Option))
			{
				TessellationOptions = TessellationOptionsObject->Options;
				if (Translator)
				{
					Translator->SetTessellationOptions(TessellationOptions);
				}
			}
		}
	}
}




