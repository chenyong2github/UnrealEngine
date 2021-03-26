// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpCamera.h"

#include "DatasmithSketchUpCommon.h"
#include "DatasmithSketchUpUtils.h"

#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpExportContext.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/camera.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/scene.h"
#include "DatasmithSketchUpSDKCeases.h"

#include "DatasmithUtils.h"
#include "DatasmithSceneFactory.h"

using namespace DatasmithSketchUp;

TSharedPtr<FCamera> FCamera::Create(FExportContext& Context, SUCameraRef InCameraRef, const FString& Name)
{
	TSharedPtr<FCamera> Camera = MakeShared<FCamera>(InCameraRef);
	Camera->DatasmithCamera = FDatasmithSceneFactory::CreateCameraActor(TEXT(""));
	Context.DatasmithScene->AddActor(Camera->DatasmithCamera);
	Camera->Name = Name;
	Camera->Update(Context);
	return Camera;
}

TSharedPtr<FCamera> FCamera::Create(FExportContext& Context, SUSceneRef InSceneRef)
{
	SUCameraRef CameraRef;
	// Retrieve the SketchUp scene camera.
	SUSceneGetCamera(InSceneRef, &CameraRef);

	return Create(Context, CameraRef, SuGetString(SUSceneGetName, InSceneRef));
}

void FCamera::Update(FExportContext& Context)
{
	SUPoint3D SourcePosition;
	SUPoint3D SourceTarget;
	SUVector3D SourceUpVector;

	// Retrieve the SketckUp camera orientation.
	SUCameraGetOrientation(CameraRef, &SourcePosition, &SourceTarget, &SourceUpVector);

	// Get the SketchUp camera aspect ratio.
	double CameraAspectRatio = 0.0;
	SUResult Result = SUCameraGetAspectRatio(CameraRef, &CameraAspectRatio);
	double SourceAspectRatio = 16.0 / 9.0;

	// Keep the default aspect ratio when the camera uses the screen aspect ratio (SU_ERROR_NO_DATA).
	if (Result == SU_ERROR_NONE)
	{
		SourceAspectRatio = CameraAspectRatio;
	}

	// Get the flag indicating whether or not the SketchUp scene camera is a perspective camera.
	bool bCameraIsPerspective = false;
	SUCameraGetPerspective(CameraRef, &bCameraIsPerspective); // we can ignore the returned SU_RESULT

	// Get the flag indicating whether or not the SketchUp scene camera is a two dimensional camera.
	bool bCameraIs2D = false;
	SUCameraGet2D(CameraRef, &bCameraIs2D); // we can ignore the returned SU_RESULT

	bool bSourceFOVForHeight = true;
	double SourceFOV(60.0);             // default vertical field of view of 60 degrees
	double SourceImageWidth(36.0);      // default image width of 36 mm (from Datasmith)

	if (bCameraIsPerspective && !bCameraIs2D)
	{
		// Get the flag indicating whether or not the SketchUp camera field of view value represents the camera view height.
		SUCameraGetFOVIsHeight(CameraRef, &bSourceFOVForHeight); // we can ignore the returned SU_RESULT

		// Get the SketchUp camera field of view (in degrees).
		SUCameraGetPerspectiveFrustumFOV(CameraRef, &SourceFOV); // we can ignore the returned SU_RESULT

		// Get the SketchUp camera image width (in millimeters).
		double CameraImageWidth = 0.0;
		Result = SUCameraGetImageWidth(CameraRef, &CameraImageWidth);
		// Keep the default image width when the camera does not have an image width.
		if (CameraImageWidth > 0.0)
		{
			SourceImageWidth = CameraImageWidth;
		}
	}

	FString ActorName = FDatasmithUtils::SanitizeObjectName(Name);
	FString ActorLabel = ActorName;

	// Create a Datasmith camera actor for the camera definition.
	DatasmithCamera->SetName(*ActorName);
	// Set the camera actor label used in the Unreal UI.
	DatasmithCamera->SetLabel(*ActorLabel);

	// Convert the SketchUp right-handed camera orientation into an Unreal left-handed look-at rotation quaternion.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	SUVector3D SLookAtVector = { SourceTarget.x - SourcePosition.x, SourceTarget.y - SourcePosition.y, SourceTarget.z - SourcePosition.z };
	FVector XAxis = DatasmithSketchUpUtils::FromSketchUp::ConvertDirection(SLookAtVector);
	FVector ZAxis = DatasmithSketchUpUtils::FromSketchUp::ConvertDirection(SourceUpVector);
	FQuat Rotation(FRotationMatrix::MakeFromXZ(XAxis, ZAxis)); // axis vectors do not need to be normalized

	// Convert the SketchUp right-handed Z-up coordinate translation into an Unreal left-handed Z-up coordinate translation.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	// SketchUp uses inches as internal system unit for all 3D coordinates in the model while Unreal uses centimeters.

	FVector Translation = DatasmithSketchUpUtils::FromSketchUp::ConvertPosition(SourcePosition);

	// Set the world transform of the Datasmith camera actor.
	DatasmithCamera->SetRotation(Rotation);
	DatasmithCamera->SetTranslation(Translation);

	// Set the Datasmith camera aspect ratio.
	DatasmithCamera->SetSensorAspectRatio(float(SourceAspectRatio));

	// Set the Datasmith camera sensor width (in millimeters).
	DatasmithCamera->SetSensorWidth(float(SourceImageWidth));

	// Set the Datasmith camera focal length (in millimeters).
	double FocalLength = (bSourceFOVForHeight ? (SourceImageWidth / SourceAspectRatio) : SourceImageWidth) / (2.0 * tan(FMath::DegreesToRadians<double>(SourceFOV) / 2.0));
	DatasmithCamera->SetFocalLength(float(FocalLength));

	// Set the Datasmith camera focus distance (in centimeters).
	FVector DistanceVector = DatasmithSketchUpUtils::FromSketchUp::ConvertPosition(SourceTarget.x - SourcePosition.x, SourceTarget.y - SourcePosition.y, SourceTarget.z - SourcePosition.z);
	DatasmithCamera->SetFocusDistance(DistanceVector.Size());

}
