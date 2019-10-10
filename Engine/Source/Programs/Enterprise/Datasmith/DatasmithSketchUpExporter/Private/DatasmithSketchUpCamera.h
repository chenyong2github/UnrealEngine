// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/geometry.h"
#include "SketchUpAPI/model/defs.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class IDatasmithCameraActorElement;
class IDatasmithScene;


class FDatasmithSketchUpCamera
{
public:

	// Initialize the dictionary of camera definitions.
	static void InitCameraDefinitionMap(
		SUModelRef InSModelRef // model containing SketchUp camera definitions
	);

	// Clear the dictionary of camera definitions.
	static void ClearCameraDefinitionMap();

	// Export the camera definitions into the Datasmith scene.
	static void ExportDefinitions(
		TSharedRef<IDatasmithScene> IODSceneRef // Datasmith scene to populate
	);

private:

	// Get the camera ID of a SketckUp scene.
	static int32 GetCameraID(
		SUSceneRef InSSceneRef // valid SketckUp scene
	);

	FDatasmithSketchUpCamera(
		SUSceneRef InSSceneRef // valid SketckUp scene
	);

	// No copying or copy assignment allowed for this class.
	FDatasmithSketchUpCamera(FDatasmithSketchUpCamera const&) = delete;
	FDatasmithSketchUpCamera& operator=(FDatasmithSketchUpCamera const&) = delete;

	// Export the camera definition into a Datasmith camera actor.
	void ExportCamera(
		TSharedRef<IDatasmithScene> IODSceneRef // Datasmith scene to populate
	) const;

	// Set the world transform of a Datasmith camera actor.
	void SetActorTransform(
		TSharedPtr<IDatasmithCameraActorElement> IODCameraActorPtr // Datasmith camera actor to transform
	) const;

private:

	// Dictionary of camera definitions indexed by the SketchUp camera IDs.
	static TMap<int32, TSharedPtr<FDatasmithSketchUpCamera>> CameraDefinitionMap;

	// Source SketchUp camera.
	SUCameraRef SSourceCameraRef;

	// Source SketchUp camera ID.
	int32 SSourceID;

	// Source SketchUp camera name.
	FString SSourceName;

	// Source SketchUp camera position.
	SUPoint3D SSourcePosition;

	// Source SketchUp camera target.
	SUPoint3D SSourceTarget;

	// Source SketchUp camera up-vector.
	SUVector3D SSourceUpVector;

	// Source SketchUp camera aspect ratio.
	double SSourceAspectRatio;

	// Whether or not the source SketchUp camera field of view value represents the camera view height.
	bool bSSourceFOVForHeight;

	// Source SketchUp camera field of view (in degrees).
	double SSourceFOV;

	// Source SketchUp camera image width (in millimeters).
	double SSourceImageWidth;

	// Whether or not this is the active camera definition.
	bool bActiveCamera;
};
