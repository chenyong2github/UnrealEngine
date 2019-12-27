// Copyright Epic Games, Inc. All Rights Reserved.

#include "VREditorCameraWidgetComponent.h"
#include "UObject/ConstructorHelpers.h"

UVREditorCameraWidgetComponent::UVREditorCameraWidgetComponent()
	: Super()	
{	
	// Override this shader for VR camera viewfinders so that we get color-correct images.
	// This shader does an sRGB -> Linear conversion and doesn't apply the "UI Brightness" setting .
	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> OpaqueMaterial_OneSided_Finder(TEXT("/Engine/EngineMaterials/Widget3DCameraPassThrough_Opaque_OneSided"));
		OpaqueMaterial_OneSided = OpaqueMaterial_OneSided_Finder.Object;
	}
}

