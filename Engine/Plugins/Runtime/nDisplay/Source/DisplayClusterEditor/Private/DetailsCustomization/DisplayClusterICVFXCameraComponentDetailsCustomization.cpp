// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomization/DisplayClusterICVFXCameraComponentDetailsCustomization.h"

#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "CineCameraActor.h"
#include "UObject/SoftObjectPtr.h"

#include "DetailLayoutBuilder.h"


TSharedRef<IDetailCustomization> FDisplayClusterICVFXCameraComponentDetailsCustomization::MakeInstance()
{
	return MakeShared<FDisplayClusterICVFXCameraComponentDetailsCustomization>();
}

void FDisplayClusterICVFXCameraComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
	DetailLayout = &Builder;

	if (!EditedObject.IsValid())
	{
		TArray<TWeakObjectPtr<UObject>> Objects;
		Builder.GetObjectsBeingCustomized(Objects);

		for (TWeakObjectPtr<UObject> Object : Objects)
		{
			if (Object->IsA<UDisplayClusterICVFXCameraComponent>())
			{
				EditedObject = Cast<UDisplayClusterICVFXCameraComponent>(Object.Get());
			}
		}
	}

	// Hide some groups if an external CineCameraActor is set
	if (EditedObject.IsValid() && EditedObject->ExternalCameraActor.IsValid())
	{
		Builder.HideCategory(TEXT("Current Camera Settings"));
		Builder.HideCategory(TEXT("CameraOptions"));
		Builder.HideCategory(TEXT("Camera"));
		Builder.HideCategory(TEXT("PostProcess"));
		Builder.HideCategory(TEXT("Lens"));
		Builder.HideCategory(TEXT("LOD"));
		Builder.HideCategory(TEXT("ColorGrading"));
		Builder.HideCategory(TEXT("RenderingFeatures"));
		Builder.HideCategory(TEXT("Tags"));
		Builder.HideCategory(TEXT("ComponentTick"));
		Builder.HideCategory(TEXT("Color Grading"));
		Builder.HideCategory(TEXT("Rendering Features"));
		Builder.HideCategory(TEXT("Activation"));
	}

	// Always hide the following categories
	Builder.HideCategory(TEXT("AssetUserData"));
	Builder.HideCategory(TEXT("Collision"));
	Builder.HideCategory(TEXT("Cooking"));
	Builder.HideCategory(TEXT("ComponentReplication"));
	Builder.HideCategory(TEXT("Events"));
	Builder.HideCategory(TEXT("Physics"));
	Builder.HideCategory(TEXT("Sockets"));
}

#undef LOCTEXT_NAMESPACE
