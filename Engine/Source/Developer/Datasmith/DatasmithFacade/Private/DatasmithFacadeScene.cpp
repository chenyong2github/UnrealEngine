// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeScene.h"

// Datasmith facade.
#include "DatasmithFacadeElement.h"
#include "DatasmithFacadeMaterial.h"

// Datasmith SDK.
#include "DatasmithExporterManager.h"
#include "DatasmithSceneExporter.h"

#include "Misc/Paths.h"

FDatasmithFacadeScene::FDatasmithFacadeScene(
	const TCHAR* InApplicationHostName,
	const TCHAR* InApplicationVendorName,
	const TCHAR* InApplicationProductName,
	const TCHAR* InApplicationProductVersion
) :
	ApplicationHostName(InApplicationHostName),
	ApplicationVendorName(InApplicationVendorName),
	ApplicationProductName(InApplicationProductName),
	ApplicationProductVersion(InApplicationProductVersion),
	SceneRef(FDatasmithSceneFactory::CreateScene(TEXT("")))
{
}

void FDatasmithFacadeScene::AddElement(
	FDatasmithFacadeElement* InElementPtr
)
{
	SceneElementArray.Add(TSharedPtr<FDatasmithFacadeElement>(InElementPtr));
}

void FDatasmithFacadeScene::Optimize()
{
	for (int32 ElementNo = SceneElementArray.Num() - 1; ElementNo >= 0; ElementNo--)
	{
		// Optimize the Datasmith scene element.
		TSharedPtr<FDatasmithFacadeElement> ElementPtr = SceneElementArray[ElementNo]->Optimize(SceneElementArray[ElementNo]);

		if (ElementPtr.IsValid())
		{
			SceneElementArray[ElementNo] = ElementPtr;
		}
		else
		{
			SceneElementArray.RemoveAt(ElementNo);
		}
	}
}

void FDatasmithFacadeScene::BuildAssets()
{
	// Build the Datasmith scene element assets.
	for (TSharedPtr<FDatasmithFacadeElement> ElementPtr : SceneElementArray)
	{
		ElementPtr->BuildAsset();
	}
}

void FDatasmithFacadeScene::ExportAssets(
	const TCHAR* InAssetFolder
)
{
	FString AssetFolder = InAssetFolder;

	// Build and export the Datasmith scene element assets.
	for (TSharedPtr<FDatasmithFacadeElement> ElementPtr : SceneElementArray)
	{
		ElementPtr->ExportAsset(AssetFolder);
	}
}

void FDatasmithFacadeScene::BuildScene(
	const TCHAR* InSceneName
)
{
	// Initialize the Datasmith scene.
	SceneRef->SetName(InSceneName);
	SceneRef->Reset();

	// Set the name of the host application used to build the scene.
	SceneRef->SetHost(*ApplicationHostName);

	// Set the vendor name of the application used to build the scene.
	SceneRef->SetVendor(*ApplicationVendorName);

	// Set the product name of the application used to build the scene.
	SceneRef->SetProductName(*ApplicationProductName);

	// Set the product version of the application used to build the scene.
	SceneRef->SetProductVersion(*ApplicationProductVersion);

	// Initialize the set of built Datasmith textures.
	FDatasmithFacadeMaterial::ClearBuiltTextureSet();

	// Build the collected scene elements and add them to the Datasmith scene.
	for (TSharedPtr<FDatasmithFacadeElement> ElementPtr : SceneElementArray)
	{
		ElementPtr->BuildScene(SceneRef);
	}

	// Clear the set of built Datasmith textures.
	FDatasmithFacadeMaterial::ClearBuiltTextureSet();
}

void FDatasmithFacadeScene::ExportScene(
	const TCHAR* InOutputPath
)
{
	FString OutputPath = InOutputPath;

	// Initialize the Datasmith exporter module.
	FDatasmithExporterManager::Initialize();

	// Create a Datasmith scene exporter.
	TSharedRef<FDatasmithSceneExporter> SceneExporterRef = MakeShared<FDatasmithSceneExporter>();

	// Start measuring the time taken to export the scene.
	SceneExporterRef->PreExport();

	// Set the name of the scene to export and let Datasmith sanitize it when required.
	FString SceneName = FPaths::GetBaseFilename(OutputPath);
	SceneExporterRef->SetName(*SceneName);

	// Set the output folder where this scene will be exported.
	FString SceneFolder = FPaths::GetPath(OutputPath);
	SceneExporterRef->SetOutputPath(*SceneFolder);

	// Build and export the Datasmith scene element assets.
	ExportAssets(SceneExporterRef->GetAssetsOutputPath());

	// Build the Datasmith scene instance.
	BuildScene(*SceneName);

	// Export the Datasmith scene instance into its file.
	SceneExporterRef->Export(SceneRef);
}

void FDatasmithFacadeScene::AddElement(
	TSharedPtr<FDatasmithFacadeElement> InElementPtr
)
{
	SceneElementArray.Add(InElementPtr);
}

TSharedRef<IDatasmithScene> FDatasmithFacadeScene::GetScene() const
{
	return SceneRef;
}
