// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Details/SDisplayClusterConfiguratorViewDetails.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Views/Details/DisplayClusterConfiguratorDetailCustomization.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewDetails"


SDisplayClusterConfiguratorViewDetails::SDisplayClusterConfiguratorViewDetails()
	: bRefreshOnTick(false)
{
}

SDisplayClusterConfiguratorViewDetails::~SDisplayClusterConfiguratorViewDetails()
{
	// Unregister UCLASS customization 
	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationData::StaticClass());

	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationScene::StaticClass());
	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationCluster::StaticClass());
	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationInput::StaticClass());

	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationViewport::StaticClass());

	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentXform::StaticClass());
	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentScreen::StaticClass());
	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentCamera::StaticClass());
	PropertyView->UnregisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentMesh::StaticClass());

	// Unregister USTRUCT customization 
	PropertyView->UnregisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationClusterSync::StaticStruct()->GetFName());
	PropertyView->UnregisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationRenderSyncPolicy::StaticStruct()->GetFName());
	PropertyView->UnregisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationInputSyncPolicy::StaticStruct()->GetFName());
	PropertyView->UnregisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationProjection::StaticStruct()->GetFName());
}

void SDisplayClusterConfiguratorViewDetails::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	ToolkitPtr = InToolkit;

	// Delegates
	InToolkit->RegisterOnObjectSelected(IDisplayClusterConfiguratorToolkit::FOnObjectSelectedDelegate::CreateSP(this, &SDisplayClusterConfiguratorViewDetails::OnObjectSelected));
	
	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FNotifyHook* NotifyHook = this;
	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ true,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ NotifyHook,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;

	PropertyView = EditModule.CreateDetailView(DetailsViewArgs);

	// Detail Customization Registration

	// Main configuration data container
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationData::StaticClass(), 
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorDataDetailCustomization>, ToolkitPtr));

	// Main configuration Items  
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationScene::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorSceneDetailCustomization>, ToolkitPtr));
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationCluster::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorClusterDetailCustomization>, ToolkitPtr));
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationInput::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorInputDetailCustomization>, ToolkitPtr));

	// Cluster nodes
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationViewport::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorViewportDetailCustomization>, ToolkitPtr));

	// Scene components
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentXform::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorSceneComponentXformDetailCustomization>, ToolkitPtr));
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentScreen::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorSceneComponentScreenDetailCustomization>, ToolkitPtr));
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentCamera::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorSceneComponentCameraDetailCustomization>, ToolkitPtr));
	PropertyView->RegisterInstancedCustomPropertyLayout(UDisplayClusterConfigurationSceneComponentMesh::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorDetailCustomization::MakeInstance<FDisplayClusterConfiguratorSceneComponentMeshDetailCustomization>, ToolkitPtr));

	// USTRUCT customization
	FOnGetPropertyTypeCustomizationInstance ClusterSyncTypeCustomization = FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorClusterSyncTypeCustomization>, ToolkitPtr);
	PropertyView->RegisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationClusterSync::StaticStruct()->GetFName(), ClusterSyncTypeCustomization);
	FOnGetPropertyTypeCustomizationInstance RenderSyncPolicyTypeCustomization = FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorRenderSyncPolicyCustomization>, ToolkitPtr);
	PropertyView->RegisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationRenderSyncPolicy::StaticStruct()->GetFName(), RenderSyncPolicyTypeCustomization);
	FOnGetPropertyTypeCustomizationInstance InputSyncPolicyTypeCustomization = FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorInputSyncPolicyCustomization>, ToolkitPtr);
	PropertyView->RegisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationInputSyncPolicy::StaticStruct()->GetFName(), InputSyncPolicyTypeCustomization);
	FOnGetPropertyTypeCustomizationInstance ProjectionTypeCustomization = FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDisplayClusterConfiguratorTypeCustomization::MakeInstance<FDisplayClusterConfiguratorProjectionCustomization>, ToolkitPtr);
	PropertyView->RegisterInstancedCustomPropertyTypeLayout(FDisplayClusterConfigurationProjection::StaticStruct()->GetFName(), ProjectionTypeCustomization);

	SDisplayClusterConfiguratorViewBase::Construct(
		SDisplayClusterConfiguratorViewBase::FArguments()
		.Padding(0.0f)
		.Content()
		[
			PropertyView.ToSharedRef()
		],
		InToolkit);
}

void SDisplayClusterConfiguratorViewDetails::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshOnTick)
	{
		UpdateFromObjects(RefreshPropertyObjects);
		RefreshPropertyObjects.Empty();
		bRefreshOnTick = false;
	}
}

void SDisplayClusterConfiguratorViewDetails::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
}

void SDisplayClusterConfiguratorViewDetails::ShowDetailsObjects(const TArray<UObject*>& Objects)
{
	bRefreshOnTick = true;
	RefreshPropertyObjects.Empty();

	RefreshPropertyObjects.Append(Objects);
}

const TArray<UObject*>& SDisplayClusterConfiguratorViewDetails::GetSelectedObjects() const
{
	return ToolkitPtr.Pin()->GetSelectedObjects();
}

void SDisplayClusterConfiguratorViewDetails::UpdateFromObjects(const TArray<UObject*>& PropertyObjects)
{
	PropertyView->SetObjects(PropertyObjects);
}

void SDisplayClusterConfiguratorViewDetails::OnObjectSelected()
{
	ShowDetailsObjects(GetSelectedObjects());
}

#undef LOCTEXT_NAMESPACE
