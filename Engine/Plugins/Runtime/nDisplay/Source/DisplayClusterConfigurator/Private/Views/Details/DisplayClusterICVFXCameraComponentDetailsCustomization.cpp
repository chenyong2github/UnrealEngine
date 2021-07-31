// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterICVFXCameraComponentDetailsCustomization.h"

#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfiguratorDetailCustomizationUtils.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "CineCameraActor.h"
#include "DisplayClusterRootActor.h"
#include "UObject/SoftObjectPtr.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"

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
			DisplayClusterConfigurationStrings::categories::CameraColorGradingCategory,
			DisplayClusterConfigurationStrings::categories::OCIOCategory,
			DisplayClusterConfigurationStrings::categories::ChromaKeyCategory,
			DisplayClusterConfigurationStrings::categories::OverrideCategory,
			DisplayClusterConfigurationStrings::categories::ConfigurationCategory
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
	if (EditedObject.IsValid() && EditedObject->CameraSettings.ExternalCameraActor.IsValid())
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

	bool bIsCDO = false;
	if (EditedObject.IsValid())
	{
		ADisplayClusterRootActor* RootActor = static_cast<ADisplayClusterRootActor*>(EditedObject->GetOwner());
		bIsCDO = (RootActor == nullptr) || RootActor->IsTemplate(RF_ClassDefaultObject);
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
	InLayoutBuilder.HideCategory(TEXT("Activation"));
	InLayoutBuilder.HideCategory(TEXT("Tags"));
	InLayoutBuilder.HideCategory(TEXT("Component Tick"));


	InLayoutBuilder.SortCategories(DisplayClusterICVFXCameraComponentDetailsCustomizationUtils::SortCategories);

	FDisplayClusterConfiguratorNestedPropertyHelper NestedPropertyHelper(InLayoutBuilder);

	GET_MEMBER_NAME_CHECKED(UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable);

	CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(CameraEnabledEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable);

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ConfigurationCategory)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.RenderOrder, CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.CustomFrameSize, CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.AdvancedRenderSettings.RenderTargetRatio, CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.AdvancedRenderSettings.GPUIndex, CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.AdvancedRenderSettings.StereoGPUIndex, CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.AdvancedRenderSettings.StereoMode, CameraEnabledEditCondition)
	END_CATEGORY();

	BEGIN_LABELED_CATEGORY(DisplayClusterConfigurationStrings::categories::ICVFXCategory, LOCTEXT("ICVFXCategoryLabel", "In-Camera VFX"))
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.ExternalCameraActor)

		// TODO: Screen Percentage Multiplier
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.BufferRatio)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.FieldOfViewMultiplier)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.SoftEdge)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.FrustumRotation)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.FrustumOffset)

		ADD_NESTED_PROPERTY_ENABLE_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips, CameraEnabledEditCondition)
		HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.MipsAddressU)
		HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.MipsAddressV)
		
		if (!bIsCDO)
		{
			HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.bEnabledMaxNumMips)
			HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.MaxNumMips)
		}

		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraMotionBlur, CameraEnabledEditCondition)

		BEGIN_GROUP_WITH_TOOLTIP(TEXT("HiddenContentGroup"), LOCTEXT("HiddenContentGroupLabel", "Content Hidden from Inner Frustum"), LOCTEXT("HiddenContentGroupTooltip", "Content specified here will not appear in the inner frustum, but can appear in the nDisplay viewports."))
		
			ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.ActorLayers, LOCTEXT("HiddenContentLayersTooltip", "Layers hidden from the inner frustum."), CameraEnabledEditCondition)
			ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.Actors, LOCTEXT("HiddenContentActorsTooltip", "Actors hidden from the inner frustum."), CameraEnabledEditCondition)

			if (bIsCDO)
			{
				ADD_GROUP_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.RootActorComponentNames, CameraEnabledEditCondition)
			}
		END_GROUP();

	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ChromaKeyCategory)
		CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(CameraAndChromakeyEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.Chromakey.bEnable);

		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.bEnable, CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.ChromakeyColor, CameraAndChromakeyEditCondition)
		ADD_EXPANDED_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.ChromakeyMarkers, CameraAndChromakeyEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.Chromakey.ChromakeyRenderTexture, CameraAndChromakeyEditCondition)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::OCIOCategory)
		CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(OCIOEnabledEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesOCIOConfiguration.bIsEnabled);

		RENAME_NESTED_PROPERTY_AND_TOOLTIP(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesOCIOConfiguration.bIsEnabled, LOCTEXT("bEnableInnerFrustumOCIOLabel", "Enable Inner Frustum OCIO"), LOCTEXT("bEnableInnerFrustumOCIOTooltip", "Enable the application of an OpenColorIO configuration to all nodes."))
		RENAME_NESTED_PROPERTY_AND_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesOCIOConfiguration.OCIOConfiguration.ColorConfiguration, LOCTEXT("AllNodesColorConfigurationGroupLabel", "All Nodes Color Configuration"), LOCTEXT("AllNodesColorConfigurationGroupTooltip", "Apply this OpenColorIO configuration to all nodes."), OCIOEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.PerNodeOCIOProfiles, OCIOEnabledEditCondition)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::CameraColorGradingCategory)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesColorGrading.bEnableInnerFrustumAllNodesColorGrading);

		RENAME_NESTED_CONDITIONAL_PROPERTY(NestedPropertyHelper,
			UDisplayClusterICVFXCameraComponent,
			CameraSettings.AllNodesColorGrading,
			LOCTEXT("AllNodesColorGradingLabel", "All Nodes"),
			CameraSettings.AllNodesColorGrading.bEnableInnerFrustumAllNodesColorGrading)
		ADD_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.PerNodeColorGrading);
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::OverrideCategory)
		CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(CameraAndOverrideEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.RenderSettings.Replace.bAllowReplace);
		CREATE_NESTED_PROPERTY_EDITCONDITION_3ARG(TextureRegionEditCondition,     NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.RenderSettings.Replace.bAllowReplace, CameraSettings.RenderSettings.Replace.bShouldUseTextureRegion);

		RENAME_NESTED_PROPERTY_AND_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Replace.bAllowReplace, LOCTEXT("bAllowReplaceLabel", "Enable Inner Frustum Texture Replacement"), LOCTEXT("bAllowReplaceLabelTooltip", "Set to True to replace the entire inner frustum with the specified texture."),  CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Replace.SourceTexture, CameraAndOverrideEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Replace.bShouldUseTextureRegion, CameraAndOverrideEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Replace.TextureRegion, TextureRegionEditCondition)
	END_CATEGORY();
}

#undef LOCTEXT_NAMESPACE
