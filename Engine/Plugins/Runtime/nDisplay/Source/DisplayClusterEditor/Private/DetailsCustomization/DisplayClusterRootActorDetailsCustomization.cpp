// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomization/DisplayClusterRootActorDetailsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"

#include "SSearchableComboBox.h"
#include "Widgets/Text/STextBlock.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationStrings.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"

// TODO: Macros duplicated from DisplayClusterConfiguratorDetailCustomization.h. Eventually, we will want to move the RootActor details customization into the same module
// as the other details customizations to unify this and remove these duplicate macros.
#define BEGIN_CATEGORY(CategoryName) { \
	IDetailCategoryBuilder& CurrentCategory = InLayoutBuilder.EditCategory(CategoryName);

#define END_CATEGORY() }

#define ADD_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle); \
}

#define ADD_EXPANDED_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle).ShouldAutoExpand(true); \
}

#define ADD_CUSTOM_PROPERTY(FilterText) CurrentCategory.AddCustomRow(FilterText)

#define REPLACE_PROPERTY_WITH_CUSTOM(ClassName, PropertyName, Widget) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	InLayoutBuilder.HideProperty(PropertyHandle); \
	CurrentCategory.AddCustomRow(PropertyHandle->GetPropertyDisplayName()).NameContent()[PropertyHandle->CreatePropertyNameWidget()].ValueContent()[Widget]; \
}

#define LOCTEXT_NAMESPACE "DisplayClusterRootActorDetailsCustomization"


TSharedRef<IDetailCustomization> FDisplayClusterRootActorDetailsCustomization::MakeInstance()
{
	return MakeShared<FDisplayClusterRootActorDetailsCustomization>();
}

void FDisplayClusterRootActorDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
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

	// Lay out all of the root actor properties into their correct orders and categories. Because we are adding custom properties,
	// most of the actor's properties will need to be laid out manually here even if they aren't being customized.
	BuildLayout(InLayoutBuilder);

	// Update the selected item in the NodeId combo box to match the current value on the root actor
	UpdateNodeIdSelection();
}

void FDisplayClusterRootActorDetailsCustomization::BuildLayout(IDetailLayoutBuilder& InLayoutBuilder)
{
	// Force a particular order that the property categories show up in the details panel
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::DefaultCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::ConfigurationCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::ClusterCategory);
	InLayoutBuilder.EditCategory(DisplayClusterConfigurationStrings::categories::PreviewCategory);

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

void FDisplayClusterRootActorDetailsCustomization::OnNodeIdSelected(TSharedPtr<FString> PreviewNodeId, ESelectInfo::Type SelectInfo)
{
	const FString NewValue = (PreviewNodeId.IsValid() ? *PreviewNodeId : *NodeIdOptionNone);
	PropertyNodeId->SetValue(NewValue);
}

FText FDisplayClusterRootActorDetailsCustomization::GetSelectedNodeIdText() const
{
	TSharedPtr<FString> CurSelection = NodeIdComboBox->GetSelectedItem();
	return FText::FromString(CurSelection.IsValid() ? *CurSelection : *NodeIdOptionNone);
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

#undef LOCTEXT_NAMESPACE
