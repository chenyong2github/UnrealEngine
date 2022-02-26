// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADModelToTechSoftConverterBase.h"


#include "CADData.h"
#include "MeshDescriptionHelper.h"
#include "ParametricSurfaceTranslator.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TechSoftInterface.h"
#include "TechSoftUtils.h"
#include "TUniqueTechSoftObj.h"

bool FCADModelToTechSoftConverterBase::RepairTopology()
{
#ifdef USE_TECHSOFT_SDK
	// Apply stitching if applicable
	if (ImportParameters.GetStitchingTechnique() != CADLibrary::StitchingNone)
	{
		CADLibrary::TUniqueTSObj<A3DSewOptionsData> SewOptionsData;
		SewOptionsData->m_bComputePreferredOpenShellOrientation = false;

		A3DRiBrepModel** OutNewBReps;
		uint32 OutNewBRepCount;
		CADLibrary::TechSoftInterface::SewBReps((A3DRiBrepModel**) RiRepresentationItems.GetData(), RiRepresentationItems.Num(), 0.01, SewOptionsData.GetPtr(), &OutNewBReps, OutNewBRepCount);

		RiRepresentationItems.Empty(OutNewBRepCount);
		for (uint32 Index = 0; Index < OutNewBRepCount; ++Index)
		{
			RiRepresentationItems.Add(OutNewBReps[Index]);
		}
	}
#endif
	return true;
}

void FCADModelToTechSoftConverterBase::InitializeProcess(double InMetricUnit)
{
	RiRepresentationItems.Empty();
	ModelFile.Reset();
}


bool FCADModelToTechSoftConverterBase::SaveModel(const TCHAR* InFolderPath, TSharedRef<IDatasmithMeshElement>& MeshElement)
{
#ifdef USE_TECHSOFT_SDK
	FString FilePath = FPaths::Combine(InFolderPath, MeshElement->GetName()) + TEXT(".prc");

	FString JsonString;
	{
		// Save file unit and default color and material attributes in a json string
		// This will be used when the file is reloaded
		TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		JsonObject->SetNumberField(JSON_ENTRY_FILE_UNIT, 1);
		JsonObject->SetNumberField(JSON_ENTRY_COLOR_NAME, 0);
		JsonObject->SetNumberField(JSON_ENTRY_MATERIAL_NAME, 0);

		TSharedRef< TJsonWriter< TCHAR, TPrettyJsonPrintPolicy<TCHAR> > > JsonWriter = TJsonWriterFactory< TCHAR, TPrettyJsonPrintPolicy<TCHAR> >::Create(&JsonString);

		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
	}

	ModelFile = CADLibrary::TechSoftUtils::SaveBodiesToPrcFile(RiRepresentationItems.GetData(), RiRepresentationItems.Num(), FilePath, JsonString);

	MeshElement->SetFile(*FilePath);
#endif
	return true;
}

bool FCADModelToTechSoftConverterBase::Tessellate(const CADLibrary::FMeshParameters& InMeshParameters, FMeshDescription& OutMeshDescription)
{
	CADLibrary::FBodyMesh BodyMesh;
	BodyMesh.BodyID = 1;

#ifdef USE_TECHSOFT_SDK
	for (A3DRiRepresentationItem* Representation : RiRepresentationItems)
	{
		CADLibrary::TechSoftUtils::FillBodyMesh(Representation, ImportParameters, 1.0, BodyMesh);
	}
#endif

	if (BodyMesh.Faces.Num() == 0)
	{
		return false;
	}

	if (!CADLibrary::ConvertBodyMeshToMeshDescription(ImportParameters, InMeshParameters, BodyMesh, OutMeshDescription))
	{
		ensureMsgf(false, TEXT("Error during mesh conversion"));
		return false;
	}
	return true;
}

void FCADModelToTechSoftConverterBase::AddSurfaceDataForMesh(const TCHAR* InFilePath, const CADLibrary::FMeshParameters& InMeshParameters, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload) const
{
	ParametricSurfaceUtils::AddSurfaceData(InFilePath, ImportParameters, InMeshParameters, InTessellationOptions, OutMeshPayload);
}

