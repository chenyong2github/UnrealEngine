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

		CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(CameraAndMipsEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.RenderSettings.GenerateMips.bAutoGenerateMips);
	
		ADD_NESTED_PROPERTY_ENABLE_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips, CameraAndMipsEditCondition)
		HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.bAutoGenerateMips)
		HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.MipsAddressU)
		HIDE_NESTED_PROPERTY(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.GenerateMips.MipsAddressV)

		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraMotionBlur, CameraEnabledEditCondition)

		BEGIN_GROUP(TEXT("HiddenContentGroup"), LOCTEXT("HiddenContentGroupLabel", "Content Hidden from Inner Frustum"))
		
			ADD_GROUP_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.ActorLayers, CameraEnabledEditCondition)
			ADD_GROUP_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.CameraHideList.Actors, CameraEnabledEditCondition)

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
		CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(CameraAndOCIOEnabledEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.AllNodesOCIOConfiguration.bIsEnabled);

		RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesOCIOConfiguration.bIsEnabled, LOCTEXT("bEnableInnerFrustumOCIOLabel", "Enable Inner Frustum OCIO"), CameraEnabledEditCondition)
		RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesOCIOConfiguration.ColorConfiguration, LOCTEXT("AllNodesColorConfigurationGroupLabel", "All Nodes Color Configuration"), CameraAndOCIOEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.PerNodeOCIOProfiles, CameraAndOCIOEnabledEditCondition)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::CameraColorGradingCategory)
		CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(CameraAndColorGradingEnabledEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.AllNodesColorGradingConfiguration.bIsEnabled);

		RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesColorGradingConfiguration.bIsEnabled, LOCTEXT("bEnableCameraColorGradingLabel", "Enable Inner Frustum Color Grading"), CameraEnabledEditCondition)
		RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesColorGradingConfiguration.bExcludeFromOverallClusterPostProcess, LOCTEXT("bExcludeAllNodesColorConfigurationGroupLabel", "Ignore Entire Cluster Color Grading"), CameraAndColorGradingEnabledEditCondition)
		RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.AllNodesColorGradingConfiguration.PostProcessSettings, LOCTEXT("AllNodesColorGradingGroupLabel", "All Nodes Color Grading"), CameraAndColorGradingEnabledEditCondition)

		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.PerNodeColorGradingProfiles, CameraAndColorGradingEnabledEditCondition)
	END_CATEGORY();

	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::OverrideCategory)
		CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(CameraAndOverrideEditCondition, NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.RenderSettings.Override.bAllowOverride);
		CREATE_NESTED_PROPERTY_EDITCONDITION_3ARG(TextureRegionEditCondition,     NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.bEnable, CameraSettings.RenderSettings.Override.bAllowOverride, CameraSettings.RenderSettings.Override.bShouldUseTextureRegion);

		RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.bAllowOverride, LOCTEXT("bAllowOverrideLabel", "Enable Inner Frustum Override"), CameraEnabledEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.SourceTexture, CameraAndOverrideEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.bShouldUseTextureRegion, CameraAndOverrideEditCondition)
		ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, UDisplayClusterICVFXCameraComponent, CameraSettings.RenderSettings.Override.TextureRegion, TextureRegionEditCondition)
	END_CATEGORY();
}

#undef LOCTEXT_NAMESPACE
