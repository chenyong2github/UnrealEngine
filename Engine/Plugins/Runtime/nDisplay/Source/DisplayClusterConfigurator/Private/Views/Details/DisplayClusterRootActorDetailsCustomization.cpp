// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRootActorDetailsCustomization.h"
#include "DisplayClusterConfiguratorDetailCustomizationUtils.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"

#include "EditorSupportDelegates.h"
#include "SSearchableComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "IDetailGroup.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"


#define LOCTEXT_NAMESPACE "DisplayClusterRootActorDetailsCustomization"

namespace DisplayClusterRootActorDetailsCustomizationUtils
{
	void SortCategories(const TMap<FName, IDetailCategoryBuilder*>& AllCategoryMap)
	{
		static const TArray<FName> CategoryOrder =
		{
			TEXT("TransformCommon"),
			DisplayClusterConfigurationStrings::categories::ViewportsCategory,
			DisplayClusterConfigurationStrings::categories::ICVFXCategory,
			DisplayClusterConfigurationStrings::categories::ColorGradingCategory,
			DisplayClusterConfigurationStrings::categories::OCIOCategory,
			DisplayClusterConfigurationStrings::categories::LightcardCategory,
			DisplayClusterConfigurationStrings::categories::OverrideCategory,
			TEXT("Rendering"),
			DisplayClusterConfigurationStrings::categories::PreviewCategory,
			DisplayClusterConfigurationStrings::categories::ConfigurationCategory,
			DisplayClusterConfigurationStrings::categories::AdvancedCategory,
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

TSharedRef<IDetailCustomization> FDisplayClusterRootActorDetailsCustomization::MakeInstance()
{
	return MakeShared<FDisplayClusterRootActorDetailsCustomization>();
}

FDisplayClusterRootActorDetailsCustomization::~FDisplayClusterRootActorDetailsCustomization()
{
	FEditorSupportDelegates::ForcePropertyWindowRebuild.Remove(ForcePropertyWindowRebuildHandle);
}

void FDisplayClusterRootActorDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	LayoutBuilder = &InLayoutBuilder;
	ForcePropertyWindowRebuildHandle = FEditorSupportDelegates::ForcePropertyWindowRebuild.AddSP(this, &FDisplayClusterRootActorDetailsCustomization::OnForcePropertyWindowRebuild);

	// Hide the following categories, we don't really need them
	InLayoutBuilder.HideCategory(TEXT("Replication"));
	InLayoutBuilder.HideCategory(TEXT("Collision"));
	InLayoutBuilder.HideCategory(TEXT("Input"));
	InLayoutBuilder.HideCategory(TEXT("Actor"));
	InLayoutBuilder.HideCategory(TEXT("LOD"));
	InLayoutBuilder.HideCategory(TEXT("Cooking"));
	InLayoutBuilder.HideCategory(TEXT("Physics"));
	InLayoutBuilder.HideCategory(TEXT("Activation"));
	InLayoutBuilder.HideCategory(TEXT("Asset User Data"));
	InLayoutBuilder.HideCategory(TEXT("Actor Tick"));

	// Hide the auto-generated category. Currently, you can see "Advanced" and "Default". Fix in a different way.
	InLayoutBuilder.HideCategory(TEXT("Advanced"));

	// Only single selection is allowed
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	// Store the object we're working with
	EditedObject = Cast<ADisplayClusterRootActor>(SelectedObjects[0].Get());
	if (!EditedObject.IsValid())
	{
		return;
	}

	// Store the NodeId property handle to use later
	PropertyNodeId = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_STRING_CHECKED(ADisplayClusterRootActor, PreviewNodeId), ADisplayClusterRootActor::StaticClass());
	check(PropertyNodeId.IsValid());
	check(PropertyNodeId->IsValidHandle());

	InLayoutBuilder.HideCategory(TEXT("NDisplay"));
	InLayoutBuilder.HideCategory(TEXT("NDisplay Configuration"));
	InLayoutBuilder.HideCategory(TEXT("NDisplay Cluster"));
	InLayoutBuilder.HideCategory(TEXT("NDisplay Cluster Configuration"));

	InLayoutBuilder.SortCategories(DisplayClusterRootActorDetailsCustomizationUtils::SortCategories);
	
	// Lay out all of the root actor properties into their correct orders and categories. Because we are adding custom properties,
	// most of the actor's properties will need to be laid out manually here even if they aren't being customized.
	BuildLayout(InLayoutBuilder);

	// Update the selected item in the NodeId combo box to match the current value on the root actor
	UpdateNodeIdSelection();
}

void FDisplayClusterRootActorDetailsCustomization::BuildLayout(IDetailLayoutBuilder& InLayoutBuilder)
{
	
	FDisplayClusterConfiguratorNestedPropertyHelper NestedPropertyHelper(InLayoutBuilder);
	
	if (EditedObject.IsValid() && EditedObject->CurrentConfigData)
	{
		const bool bIsCDO = EditedObject->IsTemplate(RF_ClassDefaultObject);

		// Only get the viewport names if we are not editing the CDO object in this details panel
		// (i.e. only level instance details panels should show viewport groupings)
		TArray<FString> ViewportNames;
		if (!bIsCDO)
		{
			NestedPropertyHelper.GetNestedPropertyKeys(TEXT("CurrentConfigData.Cluster.Nodes.Viewports"), ViewportNames);
		}

		// Manually add the transform properties' data to the layout builder's property in order to generate property handles for them.
		InLayoutBuilder.AddObjectPropertyData({EditedObject->DisplayClusterRootComponent}, USceneComponent::GetRelativeLocationPropertyName());
		InLayoutBuilder.AddObjectPropertyData({EditedObject->DisplayClusterRootComponent}, USceneComponent::GetRelativeRotationPropertyName());
		InLayoutBuilder.AddObjectPropertyData({EditedObject->DisplayClusterRootComponent}, USceneComponent::GetRelativeScale3DPropertyName());

		CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(EnableICVFXEditCondition, NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.bEnableInnerFrustums);

		// Manually labeling the ICVFX category because UE4 will automatically put a space after the dash if the label is generated automatically
		BEGIN_LABELED_CATEGORY(DisplayClusterConfigurationStrings::categories::ICVFXCategory, LOCTEXT("ICVFXCategoryLabel", "In-Camera VFX"))
			if (bIsCDO)
			{
				ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.bEnableInnerFrustums);
			}
			else
			{
				ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.bEnableInnerFrustums)

				if (ViewportNames.Num() > 0)
				{
					TArray<TSharedPtr<IPropertyHandle>> AllowICVFXHandles;
					NestedPropertyHelper.GetNestedProperties(TEXT("CurrentConfigData.Cluster.Nodes.Viewports.ICVFX.bAllowInnerFrustum"), AllowICVFXHandles);

					BEGIN_GROUP_WITH_TOOLTIP("InnerFrustumEnabledInViewports", LOCTEXT("InnerFrustumEnabledInViewports", "Inner Frustum Visible in Viewports"), LOCTEXT("InnerFrustumEnabledInViewportsTooltip", "Enable/disable inner frustum rendering on each individual viewport for all ICVFX cameras."))
						for (int32 VPIdx = 0; VPIdx < AllowICVFXHandles.Num(); ++VPIdx)
						{
							TSharedPtr<IPropertyHandle>& Handle = AllowICVFXHandles[VPIdx];

							Handle->SetPropertyDisplayName(FText::FromString(ViewportNames[VPIdx]));
							Handle->SetToolTipText(FText::FromString(ViewportNames[VPIdx]));

							IDetailPropertyRow& PropertyRow = CurrentGroup.AddPropertyRow(Handle.ToSharedRef());
							PropertyRow.EditCondition(EnableICVFXEditCondition, nullptr);
						}
					END_GROUP();

				}

				ADD_PROPERTY(ADisplayClusterRootActor, InnerFrustumPriority);
			}
		END_CATEGORY();

		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ViewportsCategory)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->RenderFrameSettings.ClusterICVFXOuterViewportBufferRatioMult)

			if (ViewportNames.Num() > 0)
			{
				TArray<TSharedPtr<IPropertyHandle>> ScreenPercentageHandles;
				NestedPropertyHelper.GetNestedProperties(TEXT("CurrentConfigData.Cluster.Nodes.Viewports.RenderSettings.BufferRatio"), ScreenPercentageHandles);

				BEGIN_GROUP_WITH_TOOLTIP("OuterViewportScreenPercentage", LOCTEXT("OuterViewportScreenPercentage", "Viewport Screen Percentage"), LOCTEXT("OuterViewportScreenPercentageTooltip", "Adjust resolution scaling for an individual viewport.  Viewport Screen Percentage Multiplier is applied to this value."))
					for (int32 VPIdx = 0; VPIdx < ScreenPercentageHandles.Num(); ++VPIdx)
					{
						TSharedPtr<IPropertyHandle>& Handle = ScreenPercentageHandles[VPIdx];

						Handle->SetPropertyDisplayName(FText::FromString(ViewportNames[VPIdx]));
						Handle->SetToolTipText(FText::FromString(ViewportNames[VPIdx]));

						CurrentGroup.AddPropertyRow(Handle.ToSharedRef());
					}
				END_GROUP();

				TArray<TSharedPtr<IPropertyHandle>> OverscanHandles;
				NestedPropertyHelper.GetNestedProperties(TEXT("CurrentConfigData.Cluster.Nodes.Viewports.RenderSettings.Overscan"), OverscanHandles);

				BEGIN_GROUP_WITH_TOOLTIP("OuterViewportOverscan", LOCTEXT("OuterViewportOverscan", "Viewport Overscan"), LOCTEXT("OuterViewportOverscanTooltip", "Render a larger frame than specified in the configuration to achieve continuity across displays when using post-processing effects."))
					for (int32 VPIdx = 0; VPIdx < OverscanHandles.Num(); ++VPIdx)
					{
						TSharedPtr<IPropertyHandle>& Handle = OverscanHandles[VPIdx];

						Handle->SetPropertyDisplayName(FText::FromString(ViewportNames[VPIdx]));
						Handle->SetToolTipText(FText::FromString(ViewportNames[VPIdx]));

						CurrentGroup.AddPropertyRow(Handle.ToSharedRef());
					}
				END_GROUP();
			}

			BEGIN_GROUP_WITH_TOOLTIP(TEXT("HiddenContentGroup"), LOCTEXT("HiddenContentGroupLabel", "Content Hidden from Entire Cluster"), LOCTEXT("HiddenContentGroupTooltip", "Content specified here will not appear anywhere in the nDisplay cluster."))

				ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.HideList.ActorLayers, LOCTEXT("HiddenContentLayersTooltip", "Layers hidden from the entire nDisplay cluster."))
				ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.HideList.Actors, LOCTEXT("HiddenContentActorsTooltip", "Actors hidden from the entire nDisplay cluster."))

				if (bIsCDO)
				{
					ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.HideList.RootActorComponentNames)
				}
			END_GROUP();

			BEGIN_GROUP_WITH_TOOLTIP(TEXT("HiddenOuterViewportsGroup"), LOCTEXT("HiddenOuterViewportsLabel", "Content Hidden from Viewports"), LOCTEXT("HiddenOuterViewportsTooltip", "Content specified here will not appear in the nDisplay viewports, but can appear in the inner frustum."))
				ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.OuterViewportHideList.ActorLayers, LOCTEXT("HiddenViewportsLayersTooltip", "Layers hidden from the nDisplay viewports."))
				ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.OuterViewportHideList.Actors, LOCTEXT("HiddenViewportsActorsTooltip", "Actors hidden from the nDisplay viewports."))

				if (bIsCDO)
				{
					ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.OuterViewportHideList.RootActorComponentNames)
				}
			END_GROUP();
		END_CATEGORY();

		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::OCIOCategory)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.bUseOverallClusterOCIOConfiguration)
			RENAME_NESTED_CONDITIONAL_PROPERTY_AND_TOOLTIP(NestedPropertyHelper,
				ADisplayClusterRootActor,
				CurrentConfigData->StageSettings.AllViewportsOCIOConfiguration.OCIOConfiguration.ColorConfiguration,
				LOCTEXT("AllViewportsColorConfigLabel", "All Viewports Color Configuration"),
				LOCTEXT("AllViewportsColorConfigTooltip", "Apply this OpenColorIO configuration to all viewports."),
				CurrentConfigData->StageSettings.bUseOverallClusterOCIOConfiguration)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.PerViewportOCIOProfiles)
		END_CATEGORY();

		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ColorGradingCategory)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.EntireClusterColorGrading.bEnableEntireClusterColorGrading);
			RENAME_NESTED_CONDITIONAL_PROPERTY(NestedPropertyHelper,
				ADisplayClusterRootActor,
				CurrentConfigData->StageSettings.EntireClusterColorGrading.ColorGradingSettings,
				LOCTEXT("EntireClusterColorGradingLabel", "Entire Cluster"),
				CurrentConfigData->StageSettings.EntireClusterColorGrading.bEnableEntireClusterColorGrading)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.PerViewportColorGrading);
		END_CATEGORY();

		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::LightcardCategory)
			CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(ICVFXLightCardEnabledEditCondition, NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.bEnable);

			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.bEnable)
			ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.Blendingmode, ICVFXLightCardEnabledEditCondition)

			BEGIN_GROUP_WITH_TOOLTIP(TEXT("LightCardActorsGroup"), LOCTEXT("LightCardActorsGroupLabel", "Light Cards Content"), LOCTEXT("LightCardActorsGroupTooltip", "Content specified here will be treated as a Light Card and adhere to the Blending Mode setting."))

				ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.ShowOnlyList.ActorLayers, LOCTEXT("LightCardLayersTooltip", "Layers containing Light Cards."), ICVFXLightCardEnabledEditCondition)
				ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.ShowOnlyList.Actors, LOCTEXT("LightCardActorsTooltip", "Light Card Actors"), ICVFXLightCardEnabledEditCondition)

				if (bIsCDO)
				{
					ADD_GROUP_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.ShowOnlyList.RootActorComponentNames, ICVFXLightCardEnabledEditCondition)
				}
			END_GROUP();
		END_CATEGORY();

		// Add custom properties and lay out/order properties into their correct categories.
		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::PreviewCategory)
			ADD_PROPERTY(ADisplayClusterRootActor, bPreviewEnable)
			if (RebuildNodeIdOptionsList())
			{
				CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(PreviewEnabledEditCondition, NestedPropertyHelper, ADisplayClusterRootActor, bPreviewEnable);
				REPLACE_PROPERTY_WITH_CUSTOM_ENABLE_CONDITION(ADisplayClusterRootActor, PreviewNodeId, CreateCustomNodeIdWidget(), PreviewEnabledEditCondition);
			}
		END_CATEGORY();

		// Hide unwanted properties from "Rendering" category
		{
			IDetailCategoryBuilder& RenderingCategory = InLayoutBuilder.EditCategory(TEXT("Rendering"));

			TArray<TSharedRef<IPropertyHandle>> DefaultProperties;
			RenderingCategory.GetDefaultProperties(DefaultProperties);

			for (TSharedRef<IPropertyHandle>& PropertyHandle : DefaultProperties)
			{
				if (const FProperty* Property = PropertyHandle->GetProperty())
				{
					if (Property->GetFName() != TEXT("bHidden")) // "Actor Hidden In Game"
					{
						PropertyHandle->MarkHiddenByCustomization();
					}
				}
			}
		}

		// Hide the Configuration category from level instances, will only be allowed to edit them in the config editor
		if (!bIsCDO)
		{
			InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::ConfigurationCategory).SetCategoryVisibility(false);
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Preview node ID
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedRef<SWidget> FDisplayClusterRootActorDetailsCustomization::CreateCustomNodeIdWidget()
{
	if (NodeIdComboBox.IsValid())
	{
		return NodeIdComboBox.ToSharedRef();
	}

	return SAssignNew(NodeIdComboBox, SSearchableComboBox)
		.OptionsSource(&NodeIdOptions)
		.OnGenerateWidget(this, &FDisplayClusterRootActorDetailsCustomization::CreateComboWidget)
		.OnSelectionChanged(this, &FDisplayClusterRootActorDetailsCustomization::OnNodeIdSelected)
		.ContentPadding(2)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FDisplayClusterRootActorDetailsCustomization::GetSelectedNodeIdText)
		];
}

bool FDisplayClusterRootActorDetailsCustomization::RebuildNodeIdOptionsList()
{
	// Get current configuration data
	const UDisplayClusterConfigurationData* ConfigurationData = EditedObject->GetConfigData();
	if (!ConfigurationData)
	{
		return false;
	}

	// Initialize special options
	NodeIdOptionAll  = MakeShared<FString>(DisplayClusterConfigurationStrings::gui::preview::PreviewNodeAll);
	NodeIdOptionNone = MakeShared<FString>(DisplayClusterConfigurationStrings::gui::preview::PreviewNodeNone);

	// Fill combobox with the options
	NodeIdOptions.Reset();
	NodeIdOptions.Emplace(NodeIdOptionNone);
	NodeIdOptions.Emplace(NodeIdOptionAll);
	for (const TPair<FString, UDisplayClusterConfigurationClusterNode*>& Node : ConfigurationData->Cluster->Nodes)
	{
		if (!Node.Key.IsEmpty())
		{
			NodeIdOptions.Emplace(MakeShared<FString>(Node.Key));
		}
	}

	// Set 'None' each time we update the preview config file
	if (NodeIdComboBox.IsValid())
	{
		NodeIdComboBox->SetSelectedItem(NodeIdOptionNone);
	}

	// Make sure we've got at least one cluster node in the config
	// (None+node or None+All+node1+node2+...)
	return NodeIdOptions.Num() >= 3;
}

void FDisplayClusterRootActorDetailsCustomization::UpdateNodeIdSelection()
{
	if (NodeIdComboBox.IsValid())
	{
		const FString& CurrentPreviewNode = EditedObject->PreviewNodeId;
		TSharedPtr<FString>* FoundItem = NodeIdOptions.FindByPredicate([CurrentPreviewNode](const TSharedPtr<FString>& Item)
		{
			return Item->Equals(CurrentPreviewNode, ESearchCase::IgnoreCase);
		});

		if (FoundItem)
		{
			NodeIdComboBox->SetSelectedItem(*FoundItem);
		}
		else
		{
			// Set combobox selected item (options list is not empty here)
			NodeIdComboBox->SetSelectedItem(NodeIdOptionAll);
		}
	}
}

void FDisplayClusterRootActorDetailsCustomization::OnNodeIdSelected(TSharedPtr<FString> PreviewNodeId, ESelectInfo::Type SelectInfo)
{
	const FString NewValue = (PreviewNodeId.IsValid() ? *PreviewNodeId : *NodeIdOptionNone);
	PropertyNodeId->SetValue(NewValue);
}

FText FDisplayClusterRootActorDetailsCustomization::GetSelectedNodeIdText() const
{
	if (NodeIdComboBox.IsValid())
	{
		TSharedPtr<FString> CurSelection = NodeIdComboBox->GetSelectedItem();
		return FText::FromString(CurSelection.IsValid() ? *CurSelection : *NodeIdOptionNone);
	}

	return FText::FromString(*NodeIdOptionNone);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Preview default camera ID
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterRootActorDetailsCustomization::AddDefaultCameraRow()
{
	//! remove 
}

bool FDisplayClusterRootActorDetailsCustomization::RebuildDefaultCameraOptionsList()
{
	// Get current configuration data
	const UDisplayClusterConfigurationData* ConfigurationData = EditedObject->GetConfigData();
	if (!ConfigurationData)
	{
		return false;
	}

	// Fill combobox with the options
	DefaultCameraOptions.Reset();
	for (const TPair<FString, UDisplayClusterConfigurationSceneComponentCamera*>& Node : ConfigurationData->Scene->Cameras)
	{
		if (!Node.Key.IsEmpty())
		{
			DefaultCameraOptions.Emplace(MakeShared<FString>(Node.Key));
		}
	}

	if (DefaultCameraOptions.Num() < 1)
	{
		return false;
	}

	// Set 'None' each time we update the preview config file
	if (DefaultCameraComboBox.IsValid())
	{
		DefaultCameraComboBox->SetSelectedItem(DefaultCameraOptions[0]);
	}

	return true;
}

void FDisplayClusterRootActorDetailsCustomization::OnDefaultCameraSelected(TSharedPtr<FString> CameraId, ESelectInfo::Type SelectInfo)
{
	const FString NewValue = (CameraId.IsValid() ? *CameraId : FString());
	PropertyDefaultCamera->SetValue(NewValue);
}

FText FDisplayClusterRootActorDetailsCustomization::GetSelectedDefaultCameraText() const
{
	TSharedPtr<FString> CurSelection = DefaultCameraComboBox->GetSelectedItem();
	return FText::FromString(CurSelection.IsValid() ? *CurSelection : *FString());
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Internals
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterRootActorDetailsCustomization::OnPreviewConfigChanged()
{
	// We need to update options for the comboboxes
	RebuildNodeIdOptionsList();
	RebuildDefaultCameraOptionsList();
}

TSharedRef<SWidget> FDisplayClusterRootActorDetailsCustomization::CreateComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

void FDisplayClusterRootActorDetailsCustomization::OnForcePropertyWindowRebuild(UObject* Object)
{
	if (LayoutBuilder && EditedObject.IsValid())
	{
		if (EditedObject->GetClass() == Object)
		{
			LayoutBuilder->ForceRefreshDetails();
		}
	}
}

#undef LOCTEXT_NAMESPACE
