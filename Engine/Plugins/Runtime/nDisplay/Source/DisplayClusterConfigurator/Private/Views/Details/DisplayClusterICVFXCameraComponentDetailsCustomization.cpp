// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterICVFXCameraComponentDetailsCustomization.h"

#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfiguratorDetailCustomizationUtils.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "CineCameraActor.h"
#include "UObject/SoftObjectPtr.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"

#define LOCTEXT_NAMESPACE "DisplayClusterICVFXCameraComponentDetailsCustomization"

namespace DisplayClusterICVFXCameraComponentDetailsCustomizationUtils
{
	void SortCategories(const TMap<FName, IDetailCategoryBuilder*>& AllCategoryMap)
	{
		static const TArray<FName> CategoryOrder =
		{
			TEXT("Variable"),
			TEXT("TransformCommon"),
			DisplayClusterConfigurationStrings::categories::ICVFXCategory,
			DisplayClusterConfigurationStrings::categories::ChromaKeyCategory,
			DisplayClusterConfigurationStrings::categories::OCIOCategory,
			DisplayClusterConfigurationStrings::categories::OverrideCategory
		};

		for (const TPair<FName, IDetailCategoryBuilder*>& Pair : AllCategoryMap)
		{
			int32 CurrentSortOrder = Pair.Value->GetSortOrder();

			int32 DesiredSortOrder;
			if (CategoryOrder.Find(Pair.Key, DesiredSortOrder))
			{
				CurrentSortOrder = DesiredSortOrder;
			}
			else
			{
				CurrentSortOrder += CategoryOrder.Num();
			}

			Pair.Value->SetSortOrder(CurrentSortOrder);
		}
	}
}

TSharedRef<IDetailCustomization> FDisplayClusterICVFXCameraComponentDetailsCustomization::MakeInstance()
{
	return MakeShared<FDisplayClusterICVFXCameraComponentDetailsCustomization>();
}

void FDisplayClusterICVFXCameraComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	DetailLayout = &InLayoutBuilder;

	if (!EditedObject.IsValid())
	{
		TArray<TWeakObjectPtr<UObject>> Objects;
		InLayoutBuilder.GetObjectsBeingCustomized(Objects);

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
		InLayoutBuilder.HideCategory(TEXT("TransformCommon"));
		InLayoutBuilder.HideCategory(TEXT("Current Camera Settings"));
		InLayoutBuilder.HideCategory(TEXT("CameraOptions"));
		InLayoutBuilder.HideCategory(TEXT("Camera"));
		InLayoutBuilder.HideCategory(TEXT("PostProcess"));
		InLayoutBuilder.HideCategory(TEXT("Lens"));
		InLayoutBuilder.HideCategory(TEXT("LOD"));
		InLayoutBuilder.HideCategory(TEXT("ColorGrading"));
		InLayoutBuilder.HideCategory(TEXT("RenderingFeatures"));
		InLayoutBuilder.HideCategory(TEXT("Tags"));
		InLayoutBuilder.HideCategory(TEXT("ComponentTick"));
		InLayoutBuilder.HideCategory(TEXT("Color Grading"));
		InLayoutBuilder.HideCategory(TEXT("Rendering Features"));
		InLayoutBuilder.HideCategory(TEXT("Activation"));
	}

	// Always hide the following categories
	InLayoutBuilder.HideCategory(TEXT("AssetUserData"));
	InLayoutBuilder.HideCategory(TEXT("Collision"));
	InLayoutBuilder.HideCategory(TEXT("Cooking"));
	InLayoutBuilder.HideCategory(TEXT("ComponentReplication"));
	InLayoutBuilder.HideCategory(TEXT("Events"));
	InLayoutBuilder.HideCategory(TEXT("Physics"));
	InLayoutBuilder.HideCategory(TEXT("Sockets"));
	InLayoutBuilder.HideCategory(TEXT("NDisplay"));

	InLayoutBuilder.SortCategories(DisplayClusterICVFXCameraComponentDetailsCustomizationUtils::SortCategories);

	FDisplayClusterConfiguratorNestedPropertyHelper NestedPropertyHelper(InLayoutBuilder);

	GET_MEMBER_NAME_CHECKED(UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable);

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ICVFXCategory)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable)
		ADD_PROPERTY(UDisplayClusterICVFXCameraComponent, ExternalCameraActor)
		// TODO: Screen Percentage Multiplier
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.BufferRatio)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.FieldOfViewMultiplier)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.SoftEdge)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.FrustumRotation)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.FrustumOffset)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraMotionBlur)

		ADD_ADVANCED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.RenderOrder)
		ADD_ADVANCED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.CustomFrameSize)
		ADD_ADVANCED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.PostprocessBlur)
		ADD_ADVANCED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips)
		ADD_ADVANCED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.AdvancedRenderSettings)

		BEGIN_GROUP(TEXT("HiddenContentGroup"), LOCTEXT("HiddenContentGroupLabel", "Content Hidden from Camera"))
			ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.ActorLayers)
			ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.Actors)
		END_GROUP();

	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ClusterPostprocessCategory)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.bUseCameraComponentPostprocess)
		ADD_EXPANDED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.PostProcessSettings)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ChromaKeyCategory)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.bEnable)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.ChromakeyColor)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.ChromakeyRenderTexture)
		ADD_EXPANDED_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.ChromakeyMarkers)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::OCIOCategory)
		// TODO: Refactor ICVFX camera OCIO to match the DCRA OCIO refactor
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.OCIO_Configuration)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnableInnerFrustumOCIO)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.InnerFrustumOCIOConfigurations)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::OverrideCategory)
		RENAME_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.bAllowOverride, LOCTEXT("bAllowOverrideLabel", "Enable Inner Frustum Override"))
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.SourceTexture)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.bShouldUseTextureRegion)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.TextureRegion)
	END_CATEGORY();
}

#undef LOCTEXT_NAMESPACE
