// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith facade.
#include "DatasmithFacadeActor.h"


class DATASMITHFACADE_API FDatasmithFacadeActorCamera :
	public FDatasmithFacadeActor
{
public:

	FDatasmithFacadeActorCamera(
		const TCHAR* InElementName, // Datasmith element name
		const TCHAR* InElementLabel // Datasmith element label
	);

	virtual ~FDatasmithFacadeActorCamera() {}

	// Set the world position of the Datasmith camera actor.
	void SetCameraPosition(
		float InX, // camera position on the X axis
		float InY, // camera position on the Y axis
		float InZ  // camera position on the Z axis
	);

	// Set the world rotation of the Datasmith camera actor with the camera world forward and up directions.
	void SetCameraRotation(
		float InForwardX, // camera forward direction on the X axis
		float InForwardY, // camera forward direction on the Y axis
		float InForwardZ, // camera forward direction on the Z axis
		float InUpX,      // camera up direction on the X axis
		float InUpY,      // camera up direction on the Y axis
		float InUpZ       // camera up direction on the Z axis
	);

	// Set the sensor width of the Datasmith camera.
	void SetSensorWidth(
		float InSensorWidth // camera sensor width (in millimeters)
	);

	// Set the aspect ratio of the Datasmith camera.
	void SetAspectRatio(
		float InAspectRatio // camera aspect ratio (width/height)
	);

	// Set the Datasmith camera focus distance with the current camera world position and a target world position.
	void SetFocusDistance(
		float InTargetX, // target position on the X axis
		float InTargetY, // target position on the Y axis
		float InTargetZ  // target position on the Z axis
	);

	// Set the Datasmith camera focus distance.
	void SetFocusDistance(
		float InFocusDistance // camera focus distance (in world units)
	);

	// Set the Datasmith camera focal length.
	void SetFocalLength(
		float InFocalLength // camera focal length (in millimeters)
	);

	// Set the Datasmith camera focal length.
	void SetFocalLength(
		float InFOV,         // camera field of view (in degrees)
		bool  bInVerticalFOV // whether or not the field of view value represents the camera view height
	);

	// Set the Datasmith camera look-at actor.
	void SetLookAtActor(
		const TCHAR* InActorName // look-at actor name
	);

	// Set the Datasmith camera look-at roll allowed state.
	void SetLookAtAllowRoll(
		bool bInAllow // allow roll when look-at is active
	);

#ifdef SWIG_FACADE
protected:
#endif

	// Create and initialize a Datasmith camera actor hierarchy.
	virtual TSharedPtr<IDatasmithActorElement> CreateActorHierarchy(
		TSharedRef<IDatasmithScene> IOSceneRef // Datasmith scene
	) const override;

private:

	// Datasmith camera world position.
	FVector WorldCameraPosition;

	// Datasmith camera sensor width (in millimeters).
	float SensorWidth;

	// Datasmith camera aspect ratio (width/height).
	float AspectRatio;

	// Datasmith camera focus distance (in centimeters).
	float FocusDistance;

	// Datasmith camera focal length (in millimeters).
	float FocalLength;

	// Datasmith camera look-at actor name.
	FString LookAtActorName;

	// Whether the Datasmith camera allows roll when look-at is active.
	bool bLookAtAllowRoll;
};
