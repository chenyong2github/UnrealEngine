// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomization/DisplayClusterRootActorDetailsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"

#include "SSearchableComboBox.h"
#include "Widgets/Text/STextBlock.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"


#define LOCTEXT_NAMESPACE "DisplayClusterRootActorDetailsCustomization"


TSharedRef<IDetailCustomization> FDisplayClusterRootActorDetailsCustomization::MakeInstance()
{
	return MakeShared<FDisplayClusterRootActorDetailsCustomization>();
}

void FDisplayClusterRootActorDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	LayoutBuilder = &InLayoutBuilder;

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

	// Store preview category
	CategoryPreview = &InLayoutBuilder.EditCategory("Preview (Editor only)");
	check(CategoryPreview);

	// Finally, do the customization
	BuildLayout();
}

void FDisplayClusterRootActorDetailsCustomization::BuildLayout()
{
	// We need to know when the preview config file is changed
	TSharedRef<IPropertyHandle> PropertyConfigFile = LayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, PreviewConfigPath), ADisplayClusterRootActor::StaticClass());
	check(PropertyConfigFile->IsValidHandle());
	
	PropertyConfigFile->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDisplayClusterRootActorDetailsCustomization::OnPreviewConfigChanged));
	
	// Setup preivew cluster node ID combobox
	AddNodeIdRow();
	// Setup preview default camera ID combobox
	AddDefaultCameraRow();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Preview node ID
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterRootActorDetailsCustomization::AddNodeIdRow()
{
	// Hide PreviewNodeId property, it will be replaced with a combobox
	PropertyNodeId = LayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, PreviewNodeId), ADisplayClusterRootActor::StaticClass());
	check(PropertyNodeId->IsValidHandle());
	LayoutBuilder->HideProperty(PropertyNodeId);

	if (!RebuildNodeIdOptionsList())
	{
		return;
	}

	// Create GUI representation
	{
		CategoryPreview->AddCustomRow(PropertyNodeId->GetPropertyDisplayName())
			.NameContent()
			[
				PropertyNodeId->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SAssignNew(NodeIdComboBox, SSearchableComboBox)
				.OptionsSource(&NodeIdOptions)
				.OnGenerateWidget(this, &FDisplayClusterRootActorDetailsCustomization::CreateComboWidget)
				.OnSelectionChanged(this, &FDisplayClusterRootActorDetailsCustomization::OnNodeIdSelected)
				.ContentPadding(2)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &FDisplayClusterRootActorDetailsCustomization::GetSelectedNodeIdText)
				]
			];
	}

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

bool FDisplayClusterRootActorDetailsCustomization::RebuildNodeIdOptionsList()
{
	// Get current configuration data
	const UDisplayClusterConfigurationData* ConfigurationData = EditedObject->GetConfigData();
	if (!ConfigurationData || !CategoryPreview)
	{
		return false;
	}

	// Initialize special options
	NodeIdOptionNone = MakeShared<FString>(ADisplayClusterRootActor::PreviewNodeNone);
	NodeIdOptionAll  = MakeShared<FString>(ADisplayClusterRootActor::PreviewNodeAll);

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
	// Hide PreviewDefaultCameraId property, it will be replaced with a combobox
	PropertyDefaultCamera = LayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(ADisplayClusterRootActor, PreviewDefaultCameraId), ADisplayClusterRootActor::StaticClass());
	check(PropertyDefaultCamera->IsValidHandle());
	LayoutBuilder->HideProperty(PropertyDefaultCamera);

	if (!RebuildDefaultCameraOptionsList())
	{
		return;
	}

	// Create GUI representation
	{
		CategoryPreview->AddCustomRow(PropertyDefaultCamera->GetPropertyDisplayName())
			.NameContent()
			[
				PropertyDefaultCamera->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SAssignNew(DefaultCameraComboBox, SSearchableComboBox)
				.OptionsSource(&DefaultCameraOptions)
				.OnGenerateWidget(this, &FDisplayClusterRootActorDetailsCustomization::CreateComboWidget)
				.OnSelectionChanged(this, &FDisplayClusterRootActorDetailsCustomization::OnDefaultCameraSelected)
				.ContentPadding(2)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &FDisplayClusterRootActorDetailsCustomization::GetSelectedDefaultCameraText)
				]
			];
	}

	// Set combobox selected item (options list is not empty here)
	DefaultCameraComboBox->SetSelectedItem(DefaultCameraOptions[0]);
}

bool FDisplayClusterRootActorDetailsCustomization::RebuildDefaultCameraOptionsList()
{
	// Get current configuration data
	const UDisplayClusterConfigurationData* ConfigurationData = EditedObject->GetConfigData();
	if (!ConfigurationData || !CategoryPreview)
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
