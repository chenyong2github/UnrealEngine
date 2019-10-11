// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpExporter.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpCamera.h"
#include "DatasmithSketchUpComponent.h"
#include "DatasmithSketchUpMaterial.h"
#include "DatasmithSketchUpMesh.h"
#include "DatasmithSketchUpMetadata.h"
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/initialize.h"
#include "SketchUpAPI/import_export/pluginprogresscallback.h"
#include "SketchUpAPI/model/layer.h"
#include "SketchUpAPI/model/model.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/StringConv.h"
#include "DatasmithExporterManager.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "Misc/Paths.h"


bool FDatasmithSketchUpExporter::Convert(
	std::string const&              InInputPath,
	std::string const&              InOutputPath,
	SketchUpPluginProgressCallback* InProgressCallback
)
{
	// Clear the summary of the export process.
	FDatasmithSketchUpSummary::GetSingleton().ClearSummary();

	bool bSuccess = false;

	// Initialize the SketchUp API.
	SUInitialize();

	// Get the SketchUp product version.
	TCHAR const* SketchUpProductVersion = SKETCHUP_PRODUCT_VERSION;
#if defined SKP_SDK_2019
	SketchUpProductVersion = TEXT("2019");
#elif defined SKP_SDK_2018
	SketchUpProductVersion = TEXT("2018");
#elif defined SKP_SDK_2017
	SketchUpProductVersion = TEXT("2017");
#endif

	SUModelRef SModelRef = SU_INVALID;

	try
	{
		SetProgress(InProgressCallback, 0.0, "Exporting to Datasmith");

		// Load the model from the SketchUp file.
		SUResult SResult = SUModelCreateFromFile(&SModelRef, InInputPath.c_str());
		// Make sure the SketchUp model was loaded properly (no SU_ERROR_SERIALIZATION, SU_ERROR_MODEL_INVALID or SU_ERROR_MODEL_VERSION).
		if (SResult != SU_ERROR_NONE)
		{
			throw std::runtime_error("Cannot load the SketchUp model file");
		}

		// Retrieve the SketchUp model name.
		FString SModelName;
		SU_GET_STRING(SUModelGetName, SModelRef, SModelName);

		if (SModelName.IsEmpty())
		{
			// Use a default name (UTF-8 encoded) as SketchUp model name.
			SUModelSetName(SModelRef, "SketchUp_Model"); // we can ignore the returned SU_RESULT
		}

#ifndef SKP_SDK_2017
		// Fix any errors found in the SketchUp model.
		// SUModelFixErrors is available since SketchUp 2018, API 6.0.
		SUModelFixErrors(SModelRef); // we can ignore the returned SU_RESULT
#endif

		// Initialize the Datasmith exporter module.
		FDatasmithExporterManager::Initialize();

		// Create a Datasmith scene exporter.
		TSharedRef<FDatasmithSceneExporter> DSceneExporterRef = MakeShared<FDatasmithSceneExporter>();

		// Start measuring the time taken to export the scene.
		DSceneExporterRef->PreExport();

		// Set the name of the scene to export and let Datasmith sanitize it when required.
		FString OutputPath = UTF8_TO_TCHAR(InOutputPath.c_str());
		FString DSceneName = FPaths::GetBaseFilename(OutputPath);
		DSceneExporterRef->SetName(*DSceneName);

		// Set the output folder where this scene will be exported.
		DSceneExporterRef->SetOutputPath(*FPaths::GetPath(OutputPath));

		// Create an empty Datasmith scene.
		TSharedRef<IDatasmithScene> DSceneRef = FDatasmithSceneFactory::CreateScene(*DSceneName);

		// Set the name of the host application used to export the scene.
		DSceneRef->SetHost(SKETCHUP_HOST_NAME);

		// Set the vendor name of the application used to export the scene.
		DSceneRef->SetVendor(SKETCHUP_VENDOR_NAME);

		// Set the product name of the application used to export the scene.
		DSceneRef->SetProductName(SKETCHUP_PRODUCT_NAME);

		// Set the product version of the application used to export the scene.
		DSceneRef->SetProductVersion(SketchUpProductVersion);

		SetProgress(InProgressCallback, 5.0, "Retrieving layers, materials, cameras");

		// Retrieve the default layer in the SketchUp model.
		SULayerRef SDefaultLayerRef = SU_INVALID;
		SUModelGetDefaultLayer(SModelRef, &SDefaultLayerRef); // we can ignore the returned SU_RESULT

		// Retrieve the SketchUp default layer name.
		FString SDefaultLayerName;
		SU_GET_STRING(SULayerGetName, SDefaultLayerRef, SDefaultLayerName);

		// Initialize our dictionary of SketchUp material definitions.
		FDatasmithSketchUpMaterial::InitMaterialDefinitionMap(SModelRef);

		// Initialize our dictionary of SketchUp camera definitions.
		FDatasmithSketchUpCamera::InitCameraDefinitionMap(SModelRef);

		// Initialize our dictionary of SketchUp component definitions.
		SetProgress(InProgressCallback, 15.0, "Retrieving components and groups");
		FDatasmithSketchUpComponent::InitComponentDefinitionMap(SModelRef);

		// Retrieve the SketchUp model hierarchy.
		SetProgress(InProgressCallback, 25.0, "Retrieving model hierarchy");
		FDatasmithSketchUpComponent ModelComponent(SModelRef);

		SetProgress(InProgressCallback, 45.0, "Building actor hierarchy");

		// Create a temporary Datasmith model actor as Datasmith scene root placeholder.
		TSharedPtr<IDatasmithActorElement> DModelActorPtr = FDatasmithSceneFactory::CreateActor(TEXT("SU"));

		// Set the Datasmith model actor label used in the Unreal UI.
		DModelActorPtr->SetLabel(TEXT("Model"));

		// Set the Datasmith model actor layer name.
		DModelActorPtr->SetLayer(*FDatasmithUtils::SanitizeObjectName(SDefaultLayerName));

		// Convert the SketchUp model hierarchy into a Datasmith actor hierarchy.
		SUTransformation SWorldTransform = { 1.0, 0.0, 0.0, 0.0,
		                                     0.0, 1.0, 0.0, 0.0,
		                                     0.0, 0.0, 1.0, 0.0,
		                                     0.0, 0.0, 0.0, 1.0 };
		ModelComponent.ConvertEntities(0, SWorldTransform, SDefaultLayerRef, FDatasmithSketchUpMaterial::INHERITED_MATERIAL_ID, DSceneRef, DModelActorPtr);

		// Detete the temporary Datasmith model actor.
		DModelActorPtr.Reset();

		// Add the camera actors into the Datasmith scene.
		FDatasmithSketchUpCamera::ExportDefinitions(DSceneRef);

		// Add the mesh elements into the Datasmith scene.
		SetProgress(InProgressCallback, 65.0, "Adding mesh elements");
		FDatasmithSketchUpMesh::ExportDefinitions(DSceneRef, DSceneExporterRef->GetAssetsOutputPath());

		// Add the material elements into the Datasmith scene.
		SetProgress(InProgressCallback, 75.0, "Adding material elements");
		FDatasmithSketchUpMaterial::ExportDefinitions(DSceneRef, DSceneExporterRef->GetAssetsOutputPath());

		// Export the Datasmith scene into its file.
		SetProgress(InProgressCallback, 85.0, "Writing Datasmith scene file");
		DSceneExporterRef->Export(DSceneRef);

		SetProgress(InProgressCallback, 95.0, "Cleaning up exporter memory");

		// Clear our list of mesh definitions.
		FDatasmithSketchUpMesh::ClearMeshDefinitionList();

		// Clear our dictionary of component definitions.
		FDatasmithSketchUpComponent::ClearComponentDefinitionMap();

		// Clear our dictionary of material definitions.
		FDatasmithSketchUpMaterial::ClearMaterialDefinitionMap();

		// Clear our dictionary of camera definitions.
		FDatasmithSketchUpCamera::ClearCameraDefinitionMap();

		// Clear our dictionary of metadata definitions.
		FDatasmithSketchUpMetadata::ClearMetadataDefinitionMap();

		SetProgress(InProgressCallback, 100.0, "Export completed");

		// Log the summary of the export process into a log file alongside the Datasmith scene file.
		FString LogFilePath = FPaths::Combine(DSceneExporterRef->GetOutputPath(), DSceneName + TEXT(".log"));
		// FDatasmithSketchUpSummary::GetSingleton().LogSummary(LogFilePath);

		bSuccess = true;
	}
	catch (std::exception const& Exception)
	{
		// TODO: Append Exception.what() to the export summary.
		ADD_SUMMARY_LINE(TEXT("Specific exception: %s"), Exception.what());
	}
	catch (...)
	{
		// TODO: Append generic exception info to the export summary.
		ADD_SUMMARY_LINE(TEXT("Unknown exception"));
	}

	// Release the loaded SketchUp model and its associated resources.
	SUModelRelease(&SModelRef); // we can ignore the returned SU_RESULT

	// Signals termination of use of the SketchUp API.
	SUTerminate();

	return bSuccess;
}

void FDatasmithSketchUpExporter::SetProgress(
	SketchUpPluginProgressCallback* InProgressCallback,
	double                          InPercentDone,
	std::string const&              InMessage
)
{
	if (InProgressCallback != nullptr)
	{
		// Check if the user has canceled the export.
		if (InProgressCallback->HasBeenCancelled())
		{
			throw std::runtime_error("Export canceled by the user");
		}

		InProgressCallback->SetPercentDone(InPercentDone);
		InProgressCallback->SetProgressMessage(InMessage);
	}
}
