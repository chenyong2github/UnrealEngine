// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith SDK.
#include "DatasmithSceneFactory.h"

// Datasmith facade classes.
class FDatasmithFacadeElement;


class DATASMITHFACADE_API FDatasmithFacadeScene
{
public:

	FDatasmithFacadeScene(
		const TCHAR* InApplicationHostName,      // name of the host application used to build the scene
		const TCHAR* InApplicationVendorName,    // vendor name of the application used to build the scene
		const TCHAR* InApplicationProductName,   // product name of the application used to build the scene
		const TCHAR* InApplicationProductVersion // product version of the application used to build the scene
	);

	// Collect an element for the Datasmith scene to build.
	void AddElement(
		FDatasmithFacadeElement* InElementPtr // Datasmith scene element
	);

	// Optimize the Datasmith scene.
	void Optimize();

	// Build the Datasmith scene element assets.
	void BuildAssets();

	// Build and export the Datasmith scene element assets.
	// This must be done before building a Datasmith scene instance.
	void ExportAssets(
		const TCHAR* InAssetFolder // Datasmith asset folder path
	);

	// Build a Datasmith scene instance.
	void BuildScene(
		const TCHAR* InSceneName // Datasmith scene name
	);

	// Build and export a Datasmith scene instance and its scene element assets.
	void ExportScene(
		const TCHAR* InOutputPath // Datasmith scene output file path
	);

#ifdef SWIG_FACADE
protected:
#endif

	// Collect an element for the Datasmith scene to build.
	void AddElement(
		TSharedPtr<FDatasmithFacadeElement> InElementPtr // Datasmith scene element
	);

	// Return the build Datasmith scene instance.
	TSharedRef<IDatasmithScene> GetScene() const;

private:

	// Name of the host application used to build the scene.
	FString ApplicationHostName;

	// Vendor name of the application used to build the scene.
	FString ApplicationVendorName;

	// Product name of the application used to build the scene.
	FString ApplicationProductName;

	// Product version of the application used to build the scene.
	FString ApplicationProductVersion;

	// Array of collected elements for the Datasmith scene to build.
	TArray<TSharedPtr<FDatasmithFacadeElement>> SceneElementArray;

	// Datasmith scene instance built with the collected elements.
	TSharedRef<IDatasmithScene> SceneRef;
};
