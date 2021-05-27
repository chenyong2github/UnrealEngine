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
	InLayoutBuilder.HideCategory(TEXT("Rendering"));
	InLayoutBuilder.HideCategory(TEXT("Replication"));
	InLayoutBuilder.HideCategory(TEXT("Collision"));
	InLayoutBuilder.HideCategory(TEXT("Input"));
	InLayoutBuilder.HideCategory(TEXT("Actor"));
	InLayoutBuilder.HideCategory(TEXT("LOD"));
	InLayoutBuilder.HideCategory(TEXT("Cooking"));
	
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
	PropertyNodeId = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, PreviewNodeId), ADisplayClusterRootActor::StaticClass());
	check(PropertyNodeId->IsValidHandle());

	if (EditedObject.IsValid() && !EditedObject->IsTemplate(RF_ClassDefaultObject))
	{
		InLayoutBuilder.HideCategory(TEXT("NDisplay"));
		InLayoutBuilder.HideCategory(TEXT("NDisplay Configuration"));
		InLayoutBuilder.HideCategory(TEXT("NDisplay Cluster"));
		InLayoutBuilder.HideCategory(TEXT("NDisplay Cluster Configuration"));
	}
	
	// Lay out all of the root actor properties into their correct orders and categories. Because we are adding custom properties,
	// most of the actor's properties will need to be laid out manually here even if they aren't being customized.
	BuildLayout(InLayoutBuilder);

	// Update the selected item in the NodeId combo box to match the current value on the root actor
	UpdateNodeIdSelection();
}

void FDisplayClusterRootActorDetailsCustomization::BuildLayout(IDetailLayoutBuilder& InLayoutBuilder)
{
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::DefaultCategory);
	
	FDisplayClusterConfiguratorNestedPropertyHelper NestedPropertyHelper(InLayoutBuilder);
	
	if (EditedObject.IsValid() && !EditedObject->IsTemplate(RF_ClassDefaultObject) && EditedObject->CurrentConfigData)
	{
		// Build ICVFX category.
		
		TArray<FString> ViewportNames;
		NestedPropertyHelper.GetNestedPropertyKeys(TEXT("CurrentConfigData.Cluster.Nodes.Viewports"), ViewportNames);

		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ICVFXCategory)

			const TSharedRef<IPropertyHandle> EnableInnerFrustumPropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, EnableInnerFrustum));
			{
				check(EnableInnerFrustumPropertyHandle->IsValidHandle());
				CurrentCategory.AddProperty(EnableInnerFrustumPropertyHandle);
			}

			const TAttribute<bool> AllowICVFXEditCondition = TAttribute<bool>::Create([this, EnableInnerFrustumPropertyHandle]()
			{
				bool bFrustumEnabled = false;
				EnableInnerFrustumPropertyHandle->GetValue(bFrustumEnabled);
				return bFrustumEnabled;
			});
		
			if (ViewportNames.Num() > 0)
			{
				TArray<TSharedPtr<IPropertyHandle>> AllowICVFXHandles;
				NestedPropertyHelper.GetNestedProperties(TEXT("CurrentConfigData.Cluster.Nodes.Viewports.ICVFX.bAllowICVFX"), AllowICVFXHandles);

				BEGIN_GROUP("InnerFrustumEnabledInViewports", LOCTEXT("InnerFrustumEnabledInViewports", "Inner Frustum Enabled in Viewports"))
					for (int32 VPIdx = 0; VPIdx < AllowICVFXHandles.Num(); ++VPIdx)
					{
						TSharedPtr<IPropertyHandle>& Handle = AllowICVFXHandles[VPIdx];

						Handle->SetPropertyDisplayName(FText::FromString(ViewportNames[VPIdx]));
						IDetailPropertyRow& PropertyRow = CurrentGroup.AddPropertyRow(Handle.ToSharedRef());
						PropertyRow.EditCondition(AllowICVFXEditCondition, nullptr);
					}
				END_GROUP();

				ADD_PROPERTY(ADisplayClusterRootActor, InnerFrustumPriority);
				ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->RenderFrameSettings.ClusterRenderTargetRatioMult)
				
				TArray<TSharedPtr<IPropertyHandle>> ScreenPercentageHandles;
				NestedPropertyHelper.GetNestedProperties(TEXT("CurrentConfigData.Cluster.Nodes.Viewports.RenderSettings.BufferRatio"), ScreenPercentageHandles);

				BEGIN_GROUP("OuterViewportScreenPercentage", LOCTEXT("OuterViewportScreenPercentage", "Outer Viewport Screen Percentage"))
					for (int32 VPIdx = 0; VPIdx < ScreenPercentageHandles.Num(); ++VPIdx)
					{
						TSharedPtr<IPropertyHandle>& Handle = ScreenPercentageHandles[VPIdx];

						Handle->SetPropertyDisplayName(FText::FromString(ViewportNames[VPIdx]));
						CurrentGroup.AddPropertyRow(Handle.ToSharedRef());
					}
				END_GROUP();
			}

			BEGIN_GROUP(TEXT("HiddenContentGroup"), LOCTEXT("HiddenContentGroupLabel", "Content Hidden from nDisplay"))
				ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.HideList.ActorLayers)
				ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.HideList.Actors)
			END_GROUP();
		END_CATEGORY();

		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::ChromaKeyCategory)
			ADD_EXPANDED_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Chromakey)
		END_CATEGORY();
		
		BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::LightcardCategory)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.bEnable)
			ADD_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.Blendingmode)

			BEGIN_GROUP(TEXT("LightCardActorsGroup"), LOCTEXT("LightCardActorsGroupLabel", "Content Visibile Only in Outer Viewports"))
				ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.ShowOnlyList.ActorLayers)
				ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ADisplayClusterRootActor, CurrentConfigData->StageSettings.Lightcard.ShowOnlyList.Actors)
			END_GROUP();
		END_CATEGORY();
	}
	
	// Force a particular order that the property categories show up in the details panel

	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::ConfigurationCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::ClusterCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::ClusterPostprocessCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::PreviewCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::AdvancedCategory).SetCategoryVisibility(false); // Re-enable for access to more adv control.
	
	// Add custom properties and lay out/order properties into their correct categories.
	BEGIN_CATEGORY(DisplayClusterConfigurationStrings::categories::PreviewCategory)
		if (RebuildNodeIdOptionsList())
		{
			REPLACE_PROPERTY_WITH_CUSTOM(ADisplayClusterRootActor, PreviewNodeId, CreateCustomNodeIdWidget());
		}
	END_CATEGORY();
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
