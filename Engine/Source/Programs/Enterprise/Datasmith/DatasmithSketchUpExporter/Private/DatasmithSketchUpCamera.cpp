// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpCamera.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpString.h"
#include "DatasmithSketchUpSummary.h"

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/model/camera.h"
#include "SketchUpAPI/model/entity.h"
#include "SketchUpAPI/model/model.h"
#include "SketchUpAPI/model/scene.h"
#include "DatasmithSketchUpSDKCeases.h"

// Datasmith SDK.
#include "Containers/Array.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "Math/RotationMatrix.h"
#include "Math/UnrealMathUtility.h"


TMap<int32, TSharedPtr<FDatasmithSketchUpCamera>> FDatasmithSketchUpCamera::CameraDefinitionMap;

void FDatasmithSketchUpCamera::InitCameraDefinitionMap(
	SUModelRef InSModelRef
)
{
	// Get the number of scenes in the SketchUp model.
	size_t SSceneCount = 0;
	SUModelGetNumScenes(InSModelRef, &SSceneCount); // we can ignore the returned SU_RESULT

	if (SSceneCount > 0)
	{
		// Retrieve the scenes in the SketchUp model.
		TArray<SUSceneRef> SScenes;
		SScenes.Init(SU_INVALID, SSceneCount);
		SUResult SResult = SUModelGetScenes(InSModelRef, SSceneCount, SScenes.GetData(), &SSceneCount);
		SScenes.SetNum(SSceneCount);
		// Make sure the SketchUp model has scenes to retrieve (no SU_ERROR_NO_DATA).
		if (SResult == SU_ERROR_NONE)
		{
			for (SUSceneRef SSceneRef : SScenes)
			{
				// Make sure the SketchUp scene uses a camera.
				bool bSSceneUseCamera = false;
				SUSceneGetUseCamera(SSceneRef, &bSSceneUseCamera); // we can ignore the returned SU_RESULT

				if (bSSceneUseCamera)
				{
					// Add the SketchUp scene camera into our dictionary of camera definitions.
					TSharedPtr<FDatasmithSketchUpCamera> CameraPtr = TSharedPtr<FDatasmithSketchUpCamera>(new FDatasmithSketchUpCamera(SSceneRef));
					CameraDefinitionMap.Add(CameraPtr->SSourceID, CameraPtr);
				}
			}
		}
	}

	// Retrieve the active scene of the SketchUp model.
	SUSceneRef SActiveSceneRef = SU_INVALID;
	SUResult SResult = SUModelGetActiveScene(InSModelRef, &SActiveSceneRef);
	// Make sure the SketchUp model has an active scene to retrieve (no SU_ERROR_NO_DATA).
	if (SResult == SU_ERROR_NONE)
	{
		// Get the camera ID of the SketckUp active scene.
		int32 SActiveCameraID = GetCameraID(SActiveSceneRef);

		// Make sure the SketchUp active scene camera exists in our dictionary of camera definitions.
		if (CameraDefinitionMap.Contains(SActiveCameraID))
		{
			// Set the flag indicating this is the SketchUp active scene camera definition.
			CameraDefinitionMap[SActiveCameraID]->bActiveCamera = true;
		}
	}
}

void FDatasmithSketchUpCamera::ClearCameraDefinitionMap()
{
	// Remove all entries from our dictionary of camera definitions.
	CameraDefinitionMap.Empty();
}

void FDatasmithSketchUpCamera::ExportDefinitions(
	TSharedRef<IDatasmithScene> IODSceneRef
)
{
	// Export the camera definitions into the Datasmith scene.
	for (auto const& CameraDefinitionEntry : CameraDefinitionMap)
	{
		// Export the camera definition into a Datasmith camera actor.
		CameraDefinitionEntry.Value->ExportCamera(IODSceneRef);
	}
}

int32 FDatasmithSketchUpCamera::GetCameraID(
	SUSceneRef InSSceneRef
)
{
	// Get the SketckUp scene ID and use it as the camera ID.
	int32 SSceneID = 0;
	SUEntityGetID(SUSceneToEntity(InSSceneRef), &SSceneID); // we can ignore the returned SU_RESULT

	return SSceneID;
}

FDatasmithSketchUpCamera::FDatasmithSketchUpCamera(
	SUSceneRef InSSceneRef
):
	SSourceCameraRef(SU_INVALID),
	SSourceAspectRatio(16.0/9.0), // default aspect ratio of 16:9
	bSSourceFOVForHeight(true),
	SSourceFOV(60.0),             // default vertical field of view of 60 degrees
	SSourceImageWidth(36.0),      // default image width of 36 mm (from Datasmith)
	bActiveCamera(false)
{
	SUResult SResult = SU_ERROR_NONE;

	// Retrieve the SketchUp scene camera.
	SUSceneGetCamera(InSSceneRef, &SSourceCameraRef); // we can ignore the returned SU_RESULT

	// Get the camera ID of the SketckUp scene.
	SSourceID = GetCameraID(InSSceneRef);

	// Retrieve the SketchUp scene name and use it as the camera name.
	SU_GET_STRING(SUSceneGetName, InSSceneRef, SSourceName);

	// Retrieve the SketckUp camera orientation.
	SUCameraGetOrientation(SSourceCameraRef, &SSourcePosition, &SSourceTarget, &SSourceUpVector); // we can ignore the returned SU_RESULT

	// Get the SketchUp camera aspect ratio.
	double SCameraAspectRatio = 0.0;
	SResult = SUCameraGetAspectRatio(SSourceCameraRef, &SCameraAspectRatio);
	// Keep the default aspect ratio when the camera uses the screen aspect ratio (SU_ERROR_NO_DATA).
	if (SResult == SU_ERROR_NONE)
	{
		SSourceAspectRatio = SCameraAspectRatio;
	}

	// Get the flag indicating whether or not the SketchUp scene camera is a perspective camera.
	bool bSCameraIsPerspective = false;
	SUCameraGetPerspective(SSourceCameraRef, &bSCameraIsPerspective); // we can ignore the returned SU_RESULT

	// Get the flag indicating whether or not the SketchUp scene camera is a two dimensional camera.
	bool bSCameraIs2D = false;
	SUCameraGet2D(SSourceCameraRef, &bSCameraIs2D); // we can ignore the returned SU_RESULT

	if (bSCameraIsPerspective && !bSCameraIs2D)
	{
		// Get the flag indicating whether or not the SketchUp camera field of view value represents the camera view height.
		SUCameraGetFOVIsHeight(SSourceCameraRef, &bSSourceFOVForHeight); // we can ignore the returned SU_RESULT

		// Get the SketchUp camera field of view (in degrees).
		SUCameraGetPerspectiveFrustumFOV(SSourceCameraRef, &SSourceFOV); // we can ignore the returned SU_RESULT

		// Get the SketchUp camera image width (in millimeters).
		double SCameraImageWidth = 0.0;
		SResult = SUCameraGetImageWidth(SSourceCameraRef, &SCameraImageWidth);
		// Keep the default image width when the camera does not have an image width.
		if (SCameraImageWidth > 0.0)
		{
			SSourceImageWidth = SCameraImageWidth;
		}
	}
}

void FDatasmithSketchUpCamera::ExportCamera(
	TSharedRef<IDatasmithScene> IODSceneRef
) const
{
	FString ActorName  = FDatasmithUtils::SanitizeObjectName(SSourceName);
	FString ActorLabel = ActorName;

	// Create a Datasmith camera actor for the camera definition.
	TSharedRef<IDatasmithCameraActorElement> DCameraActorPtr = FDatasmithSceneFactory::CreateCameraActor(*ActorName);

	// Set the camera actor label used in the Unreal UI.
	DCameraActorPtr->SetLabel(*ActorLabel);

	// Set the Datasmith camera actor world transform.
	SetActorTransform(DCameraActorPtr);

	// Set the Datasmith camera aspect ratio.
	DCameraActorPtr->SetSensorAspectRatio(float(SSourceAspectRatio));

	// Set the Datasmith camera sensor width (in millimeters).
	DCameraActorPtr->SetSensorWidth(float(SSourceImageWidth));

	// Set the Datasmith camera focal length (in millimeters).
	double FocalLength = (bSSourceFOVForHeight ? (SSourceImageWidth / SSourceAspectRatio) : SSourceImageWidth) / (2.0 * tan(FMath::DegreesToRadians<double>(SSourceFOV) / 2.0));
	DCameraActorPtr->SetFocalLength(float(FocalLength));

	// Set the Datasmith camera focus distance (in centimeters).
	// SketchUp uses inches as internal system unit for all 3D coordinates in the model while Unreal uses centimeters.
	const float UnitScale = 2.54; // centimeters per inch
	FVector DistanceVector(float(SSourceTarget.x - SSourcePosition.x), float(SSourceTarget.y - SSourcePosition.y), float(SSourceTarget.z - SSourcePosition.z));
	DCameraActorPtr->SetFocusDistance(DistanceVector.Size() * UnitScale);

	// Add the camera actor to the Datasmith scene.
	IODSceneRef->AddActor(DCameraActorPtr);
}

void FDatasmithSketchUpCamera::SetActorTransform(
	TSharedPtr<IDatasmithCameraActorElement> IODCameraActorPtr
) const
{
	// Convert the SketchUp right-handed camera orientation into an Unreal left-handed look-at rotation quaternion.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	SUVector3D SLookAtVector = { SSourceTarget.x - SSourcePosition.x, SSourceTarget.y - SSourcePosition.y, SSourceTarget.z - SSourcePosition.z };
	FVector XAxis(float(SLookAtVector.x),   float(-SLookAtVector.y),   float(SLookAtVector.z));
	FVector ZAxis(float(SSourceUpVector.x), float(-SSourceUpVector.y), float(SSourceUpVector.z));
	FQuat Rotation(FRotationMatrix::MakeFromXZ(XAxis, ZAxis)); // axis vectors do not need to be normalized

	// Convert the SketchUp right-handed Z-up coordinate translation into an Unreal left-handed Z-up coordinate translation.
	// To avoid perturbating X, which is forward in Unreal, the handedness conversion is done by flipping the side vector Y.
	// SketchUp uses inches as internal system unit for all 3D coordinates in the model while Unreal uses centimeters.
	const float UnitScale = 2.54; // centimeters per inch
	FVector Translation(float(SSourcePosition.x * UnitScale), float(-SSourcePosition.y * UnitScale), float(SSourcePosition.z * UnitScale));

	// Set the world transform of the Datasmith camera actor.
	IODCameraActorPtr->SetRotation(Rotation);
	IODCameraActorPtr->SetTranslation(Translation);
}
