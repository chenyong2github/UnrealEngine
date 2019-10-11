// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class CameraObject;
class IDatasmithCameraActorElement;
class IDatasmithPostProcessVolumeElement;
class INode;
class ToneOperator;

/**
 * Export both the cameras and the tone operator as they share a lot of parameters.
 */
class FDatasmithMaxCameraExporter
{
public:
	static bool ExportCamera( INode& Node, TSharedRef< IDatasmithCameraActorElement > CameraElement );
	static bool ExportToneOperator( ToneOperator& ToneOp, TSharedRef< IDatasmithPostProcessVolumeElement > PostProcessVolumeElement );

private:
	static bool ExportPhysicalCamera( CameraObject& Camera, TSharedRef< IDatasmithCameraActorElement > CameraElement );
	static bool ExportVRayPhysicalCamera( CameraObject& Camera, TSharedRef< IDatasmithCameraActorElement > CameraElement );

	static bool ExportPhysicalExposureControl( ToneOperator& ToneOp, TSharedRef< IDatasmithPostProcessVolumeElement > PostProcessVolumeElement );
	static bool ExportVRayExposureControl( ToneOperator& ToneOp, TSharedRef< IDatasmithPostProcessVolumeElement > PostProcessVolumeElement );
};
