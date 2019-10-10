// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeActorCamera.h"


FDatasmithFacadeActorCamera::FDatasmithFacadeActorCamera(
	const TCHAR* InElementName,
	const TCHAR* InElementLabel
) :
	FDatasmithFacadeActor(InElementName, InElementLabel),
	WorldCameraPosition(0.0, 0.0, 0.0), // default camera position at world origin
	SensorWidth(36.0),                  // default Datasmith sensor width of 36 mm
	AspectRatio(16.0 / 9.0),            // default Datasmith aspect ratio of 16:9
	FocusDistance(1000.0),              // default Datasmith focus distance of 1000 centimeters
	FocalLength(35.0),                  // default Datasmith focal length of 35 millimeters
	LookAtActorName(),					// default Datasmith look-at actor name
	bLookAtAllowRoll(false)				// default Datasmith look-at allow roll state
{
	// Prevent the Datasmith camera actor from being removed by optimization.
	KeepActor();
}

void FDatasmithFacadeActorCamera::SetCameraPosition(
	float InX,
	float InY,
	float InZ
)
{
	// Scale and convert the camera position into a Datasmith actor world translation.
	WorldTransform.SetTranslation(ConvertPosition(InX, InY, InZ));
}

void FDatasmithFacadeActorCamera::SetCameraRotation(
	float InForwardX,
	float InForwardY,
	float InForwardZ,
	float InUpX,
	float InUpY,
	float InUpZ
)
{
	// Convert the camera orientation into a Datasmith actor world look-at rotation quaternion.
	FVector XAxis = ConvertDirection(InForwardX, InForwardY, InForwardZ);
	FVector ZAxis = ConvertDirection(InUpX, InUpY, InUpZ);
	WorldTransform.SetRotation(FQuat(FRotationMatrix::MakeFromXZ(XAxis, ZAxis))); // axis vectors do not need to be normalized
}

void FDatasmithFacadeActorCamera::SetSensorWidth(
	float InSensorWidth
)
{
	SensorWidth = InSensorWidth;
}

void FDatasmithFacadeActorCamera::SetAspectRatio(
	float InAspectRatio
)
{
	AspectRatio = InAspectRatio;
}

void FDatasmithFacadeActorCamera::SetFocusDistance(
	float InTargetX,
	float InTargetY,
	float InTargetZ
)
{
	// Set the Datasmith camera focus distance (in centimeters).
	FVector DistanceVector(InTargetX - WorldCameraPosition.X, InTargetY - WorldCameraPosition.Y, InTargetZ - WorldCameraPosition.Z);
	FocusDistance = DistanceVector.Size() * WorldUnitScale;
}

void FDatasmithFacadeActorCamera::SetFocusDistance(
	float InFocusDistance
)
{
	// Set the Datasmith camera focus distance (in centimeters).
	FocusDistance = InFocusDistance * WorldUnitScale;
}

void FDatasmithFacadeActorCamera::SetFocalLength(
	float InFocalLength
)
{
	FocalLength = InFocalLength;
}

void FDatasmithFacadeActorCamera::SetFocalLength(
	float InFOV,
	bool  bInVerticalFOV
)
{
	FocalLength = float((bInVerticalFOV ? (SensorWidth / AspectRatio) : SensorWidth) / (2.0 * tan(FMath::DegreesToRadians<double>(InFOV) / 2.0)));
}

void FDatasmithFacadeActorCamera::SetLookAtActor(
	const TCHAR* InActorName
)
{
	LookAtActorName = InActorName;
}

void FDatasmithFacadeActorCamera::SetLookAtAllowRoll(
	bool bInAllow
)
{
	bLookAtAllowRoll = bInAllow;
}

TSharedPtr<IDatasmithActorElement> FDatasmithFacadeActorCamera::CreateActorHierarchy(
	TSharedRef<IDatasmithScene> IOSceneRef
) const
{
	// Create a Datasmith camera actor element.
	TSharedPtr<IDatasmithCameraActorElement> CameraActorPtr = FDatasmithSceneFactory::CreateCameraActor(*ElementName);

	// Set the Datasmith camera actor base properties.
	SetActorProperties(IOSceneRef, CameraActorPtr);

	// Set the Datasmith camera sensor width.
	CameraActorPtr->SetSensorWidth(SensorWidth);

	// Set the Datasmith camera aspect ratio.
	CameraActorPtr->SetSensorAspectRatio(AspectRatio);

	// Set the Datasmith camera focus method.
	CameraActorPtr->SetEnableDepthOfField(false); // no depth of field

	// Set the Datasmith camera focus distance.
	CameraActorPtr->SetFocusDistance(FocusDistance);

	// Set the Datasmith camera f-stop.
	CameraActorPtr->SetFStop(5.6); // default Datasmith f-stop of f/5.6

	// Set the Datasmith camera focal length.
	CameraActorPtr->SetFocalLength(FocalLength);

	// Set the Datasmith camera look-at actor.
	if (!LookAtActorName.IsEmpty())
	{
		CameraActorPtr->SetLookAtActor(*LookAtActorName);
	}

	// Set the Datasmith camera look-at allow roll state.
	CameraActorPtr->SetLookAtAllowRoll(bLookAtAllowRoll);

	// Add the hierarchy of children to the Datasmith actor.
	AddActorChildren(IOSceneRef, CameraActorPtr);

	return CameraActorPtr;
}
