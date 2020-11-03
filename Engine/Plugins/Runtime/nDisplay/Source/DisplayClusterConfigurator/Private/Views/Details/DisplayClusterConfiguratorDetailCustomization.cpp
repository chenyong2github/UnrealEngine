// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Details/DisplayClusterConfiguratorDetailCustomization.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Views/Details/Widgets/SDisplayClusterConfigurationSearchableComboBox.h"
#include "Views/Log/DisplayClusterConfiguratorViewLog.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorDetailCustomization"

//////////////////////////////////////////////////////////////////////////////////////////////
// Base UCLASS Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////

FDisplayClusterConfiguratorDetailCustomization::FDisplayClusterConfiguratorDetailCustomization(TWeakPtr<FDisplayClusterConfiguratorToolkit> InToolkitPtr)
	: ToolkitPtr(InToolkitPtr)
	, LayoutBuilder(nullptr)
	, NDisplayCategory(nullptr)
{}

void FDisplayClusterConfiguratorDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	LayoutBuilder = &InLayoutBuilder;
	NDisplayCategory = &LayoutBuilder->EditCategory("nDisplay", FText::GetEmpty());
	NDisplayCategory->InitiallyCollapsed(false);
}

void FDisplayClusterConfiguratorDetailCustomization::AddCustomInfoRow(IDetailCategoryBuilder* InCategory, TAttribute<FText> NameContentAttribute, TAttribute<FText> ValueContentAttribute)
{
	InCategory->AddCustomRow(NameContentAttribute.Get())
	.NameContent()
	[
		SNew( STextBlock )
		.Text(NameContentAttribute)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(STextBlock)
		.Text(ValueContentAttribute)
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Main configuration data container Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorDataDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);

	TSharedRef<IPropertyHandle> InfoHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationData, Info));
	check(InfoHandle->IsValidHandle());
	NDisplayCategory->AddProperty(InfoHandle).ShouldAutoExpand(true);

	TSharedRef<IPropertyHandle> DiagnosticsHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationData, Diagnostics));
	check(DiagnosticsHandle->IsValidHandle());
	NDisplayCategory->AddProperty(DiagnosticsHandle).ShouldAutoExpand(true);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Scene Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorSceneDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);
	ConfigurationScenePtr = nullptr;

	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		ConfigurationScenePtr = Cast<UDisplayClusterConfigurationScene>(SelectedObjects[0]);
	}
	check(ConfigurationScenePtr != nullptr);

	// Add Statistics
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentXformsName", "X forms"), FText::Format(LOCTEXT("ConfigurationSceneComponentXformsValue", "{0}"), FText::AsNumber(ConfigurationScenePtr->Xforms.Num())));
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentScreensName", "Screens"), FText::Format(LOCTEXT("ConfigurationSceneComponentScreesValue", "{0}"), FText::AsNumber(ConfigurationScenePtr->Screens.Num())));
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentCamerasName", "Cameras"), FText::Format(LOCTEXT("ConfigurationSceneComponentCamerasValue", "{0}"), FText::AsNumber(ConfigurationScenePtr->Cameras.Num())));
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentMeshesName", "Meshes"), FText::Format(LOCTEXT("ConfigurationSceneComponentMeshesValue", "{0}"), FText::AsNumber(ConfigurationScenePtr->Meshes.Num())));
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorClusterDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);

	NDisplayCategory->InitiallyCollapsed(false);

	TSharedPtr<IPropertyHandle> MasterNodeHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, MasterNode), UDisplayClusterConfigurationCluster::StaticClass());
	check(MasterNodeHandle->IsValidHandle());

	TSharedPtr<IPropertyHandle> SyncHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Sync), UDisplayClusterConfigurationCluster::StaticClass());
	check(SyncHandle->IsValidHandle());

	TSharedPtr<IPropertyHandle> NetworkHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Network), UDisplayClusterConfigurationCluster::StaticClass());
	check(NetworkHandle->IsValidHandle());

	NDisplayCategory->AddProperty(MasterNodeHandle).ShouldAutoExpand(true);
	NDisplayCategory->AddProperty(SyncHandle).ShouldAutoExpand(true);
	NDisplayCategory->AddProperty(NetworkHandle).ShouldAutoExpand(true);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Viewport Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorViewportDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);

	ConfigurationViewportPtr = nullptr;
	NoneOption = MakeShared<FString>("None");

	// Set config data pointer
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	UDisplayClusterConfigurationData* ConfigurationData = Toolkit->GetConfig();
	check(ConfigurationData != nullptr);
	ConfigurationDataPtr = ConfigurationData;

	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		ConfigurationViewportPtr = Cast<UDisplayClusterConfigurationViewport>(SelectedObjects[0]);
	}
	check(ConfigurationViewportPtr != nullptr);

	// Hide properties
	CameraHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationViewport, Camera), UDisplayClusterConfigurationViewport::StaticClass());
	check(CameraHandle->IsValidHandle());
	InLayoutBuilder.HideProperty(CameraHandle);

	ResetCameraOptions();
	AddCameraRow();
}

void FDisplayClusterConfiguratorViewportDetailCustomization::ResetCameraOptions()
{
	CameraOptions.Reset();

	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	UDisplayClusterConfigurationData* ConfigurationData = ConfigurationDataPtr.Get();
	check(ConfigurationData != nullptr);

	TMap<FString, UDisplayClusterConfigurationSceneComponentCamera*>& Cameras = ConfigurationData->Scene->Cameras;

	for (const TPair<FString, UDisplayClusterConfigurationSceneComponentCamera*>& CameraPair : Cameras)
	{
		if (!CameraPair.Key.Equals(ConfigurationViewport->Camera))
		{
			CameraOptions.Add(MakeShared<FString>(CameraPair.Key));
		}
	}

	// Add None option
	if (!ConfigurationViewport->Camera.IsEmpty())
	{
		CameraOptions.Add(NoneOption);
	}
}

void FDisplayClusterConfiguratorViewportDetailCustomization::AddCameraRow()
{
	if (CameraComboBox.IsValid())
	{
		return;
	}
	
	NDisplayCategory->AddCustomRow(CameraHandle->GetPropertyDisplayName())
	.NameContent()
	[
		CameraHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SAssignNew(CameraComboBox, SDisplayClusterConfigurationSearchableComboBox)
		.OptionsSource(&CameraOptions)
		.OnGenerateWidget(this, &FDisplayClusterConfiguratorViewportDetailCustomization::MakeCameraOptionComboWidget)
		.OnSelectionChanged(this, &FDisplayClusterConfiguratorViewportDetailCustomization::OnCameraSelected)
		.ContentPadding(2)
		.MaxListHeight(200.0f)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FDisplayClusterConfiguratorViewportDetailCustomization::GetSelectedCameraText)
		]
	];
}

TSharedRef<SWidget> FDisplayClusterConfiguratorViewportDetailCustomization::MakeCameraOptionComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

void FDisplayClusterConfiguratorViewportDetailCustomization::OnCameraSelected(TSharedPtr<FString> InCamera, ESelectInfo::Type SelectInfo)
{
	if (InCamera.IsValid())
	{
		UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
		check(ConfigurationViewport != nullptr);

		// Handle empty case
		if (InCamera->Equals(*NoneOption.Get()))
		{
			ConfigurationViewport->Camera = "";

		}
		else
		{
			ConfigurationViewport->Camera = *InCamera.Get();
		}

		// Reset available options
		ResetCameraOptions();
		CameraComboBox->ResetOptionsSource(&CameraOptions);

		CameraComboBox->SetIsOpen(false);
	}
}

FText FDisplayClusterConfiguratorViewportDetailCustomization::GetSelectedCameraText() const
{
	FString SelectedOption = ConfigurationViewportPtr.Get()->Camera;
	if (SelectedOption.IsEmpty())
	{
		SelectedOption = *NoneOption.Get();
	}

	return FText::FromString(SelectedOption);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Input Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorInputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);
	ConfigurationInputPtr = nullptr;

	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		ConfigurationInputPtr = Cast<UDisplayClusterConfigurationInput>(SelectedObjects[0]);
	}
	check(ConfigurationInputPtr != nullptr);

	// Add Statistics
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentAnalogDevicesName", "Analog Devices"), FText::Format(LOCTEXT("ConfigurationSceneComponentAnalogDevicesValue", "{0}"), FText::AsNumber(ConfigurationInputPtr->AnalogDevices.Num())));
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentButtonDevicesName", "Button Devices"), FText::Format(LOCTEXT("ConfigurationSceneComponentButtonDevicesValue", "{0}"), FText::AsNumber(ConfigurationInputPtr->ButtonDevices.Num())));
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentKeyboardDevicesName", "Keyboard Devices"), FText::Format(LOCTEXT("ConfigurationSceneComponentKeyboardDevicesValue", "{0}"), FText::AsNumber(ConfigurationInputPtr->KeyboardDevices.Num())));
	AddCustomInfoRow(NDisplayCategory, LOCTEXT("ConfigurationSceneComponentTrackerDevicesName", "Tracker Devices"), FText::Format(LOCTEXT("ConfigurationSceneComponentTrackerDevicesValue", "{0}"), FText::AsNumber(ConfigurationInputPtr->TrackerDevices.Num())));

	// Expand Array
	TSharedRef<IPropertyHandle> InputBindingHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationInput, InputBinding));
	NDisplayCategory->AddProperty(InputBindingHandle).ShouldAutoExpand(true);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Base Scene Component Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorSceneComponentDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);
	SceneComponenPtr = nullptr;
	NoneOption = MakeShared<FString>("None");

	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		SceneComponenPtr = Cast<UDisplayClusterConfigurationSceneComponent>(SelectedObjects[0]);
	}
	check(SceneComponenPtr != nullptr);
	
	// Hide properties
	TSharedRef<IPropertyHandle> ParentIdHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationSceneComponent, ParentId), UDisplayClusterConfigurationSceneComponent::StaticClass());
	check(ParentIdHandle->IsValidHandle());
	InLayoutBuilder.HideProperty(ParentIdHandle);

	TrackerIdHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationSceneComponent, TrackerId), UDisplayClusterConfigurationSceneComponent::StaticClass());
	check(TrackerIdHandle->IsValidHandle());
	InLayoutBuilder.HideProperty(TrackerIdHandle);

	ResetTrackerIdOptions();
	AddTrackerIdRow();

	// Hide Location and Rotation if the tracker has been selected
	TSharedRef<IPropertyHandle> LocationHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationSceneComponent, Location), UDisplayClusterConfigurationSceneComponent::StaticClass());
	NDisplayCategory->AddProperty(LocationHandle)
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorSceneComponentDetailCustomization::GetLocationAndRotationVisibility)));

	TSharedRef<IPropertyHandle> RotationHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationSceneComponent, Rotation), UDisplayClusterConfigurationSceneComponent::StaticClass());
	NDisplayCategory->AddProperty(RotationHandle)
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorSceneComponentDetailCustomization::GetLocationAndRotationVisibility)));
}

void FDisplayClusterConfiguratorSceneComponentDetailCustomization::ResetTrackerIdOptions()
{
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	UDisplayClusterConfigurationData* ConfigurationData = Toolkit->GetConfig();
	check(ConfigurationData != nullptr);
	
	UDisplayClusterConfigurationSceneComponent* SceneComponent = SceneComponenPtr.Get();
	check(SceneComponent != nullptr);

	// Get Parent
	UDisplayClusterConfigurationScene* Scene = ConfigurationData->Scene;
	check(Scene != nullptr);

	TrackerIdOptions.Empty();

	for (const TPair<FString, UDisplayClusterConfigurationInputDeviceTracker*>& InputDeviceTrackerPair : ConfigurationData->Input->TrackerDevices)
	{
		if (!InputDeviceTrackerPair.Key.Equals(SceneComponent->TrackerId))
		{
			TrackerIdOptions.Add(MakeShared<FString>(InputDeviceTrackerPair.Key));
		}
	}

	// Add None option
	if (!SceneComponent->TrackerId.IsEmpty())
	{
		TrackerIdOptions.Add(NoneOption);
	}
}

TSharedRef<SWidget> FDisplayClusterConfiguratorSceneComponentDetailCustomization::MakeTrackerIdOptionComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

void FDisplayClusterConfiguratorSceneComponentDetailCustomization::OnTrackerIdSelected(TSharedPtr<FString> InTrackerId, ESelectInfo::Type SelectInfo)
{
	if (InTrackerId.IsValid())
	{
		UDisplayClusterConfigurationSceneComponent* SceneComponent = SceneComponenPtr.Get();
		check(SceneComponent != nullptr);

		// Handle empty case
		if (InTrackerId->Equals(*NoneOption.Get()))
		{
			SceneComponent->TrackerId = "";

		}
		else
		{
			SceneComponent->TrackerId = *InTrackerId.Get();
		}

		// Reset available options
		ResetTrackerIdOptions();
		TrackerIdComboBox->ResetOptionsSource(&TrackerIdOptions);

		TrackerIdComboBox->SetIsOpen(false);
	}
}

void FDisplayClusterConfiguratorSceneComponentDetailCustomization::AddTrackerIdRow()
{
	if (TrackerIdComboBox.IsValid())
	{
		return;
	}
	
	NDisplayCategory->AddCustomRow(TrackerIdHandle->GetPropertyDisplayName())
	.NameContent()
	[
		TrackerIdHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SAssignNew(TrackerIdComboBox, SDisplayClusterConfigurationSearchableComboBox)
		.OptionsSource(&TrackerIdOptions)
		.OnGenerateWidget(this, &FDisplayClusterConfiguratorSceneComponentDetailCustomization::MakeTrackerIdOptionComboWidget)
		.OnSelectionChanged(this, &FDisplayClusterConfiguratorSceneComponentDetailCustomization::OnTrackerIdSelected)
		.ContentPadding(2)
		.MaxListHeight(200.0f)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FDisplayClusterConfiguratorSceneComponentDetailCustomization::GetSelectedTrackerIdText)
		]
	];
}

FText FDisplayClusterConfiguratorSceneComponentDetailCustomization::GetSelectedTrackerIdText() const
{
	FString SelectedOption = SceneComponenPtr.Get()->TrackerId;
	if (SelectedOption.IsEmpty())
	{
		SelectedOption = *NoneOption.Get();
	}
	
	return FText::FromString(SelectedOption);
}

EVisibility FDisplayClusterConfiguratorSceneComponentDetailCustomization::GetLocationAndRotationVisibility() const
{
	FString SelectedOption = SceneComponenPtr.Get()->TrackerId;
	if (SelectedOption.IsEmpty())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Scene Component Mesh Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorSceneComponentMeshDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);

	SceneComponentMeshPtr = nullptr;

	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		SceneComponentMeshPtr = Cast<UDisplayClusterConfigurationSceneComponentMesh>(SelectedObjects[0]);
	}
	check(SceneComponentMeshPtr != nullptr);

	// Load static mesh assets
	SceneComponentMeshPtr.Get()->LoadAssets();

	AssetHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationSceneComponentMesh, Asset));
	AssetHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDisplayClusterConfiguratorSceneComponentMeshDetailCustomization::OnAssetValueChanged));
}

void FDisplayClusterConfiguratorSceneComponentMeshDetailCustomization::OnAssetValueChanged()
{
	FString AssetObjectPath;
	AssetHandle->GetValueAsFormattedString(AssetObjectPath);

	if (AssetObjectPath.Len() > 0 && AssetObjectPath != TEXT("None"))
	{
		UDisplayClusterConfigurationSceneComponentMesh* SceneComponentMesh = SceneComponentMeshPtr.Get();
		check(SceneComponentMesh != nullptr);
		SceneComponentMesh->AssetPath = AssetObjectPath;

		TSharedRef<IDisplayClusterConfiguratorViewLog> ViewLog = ToolkitPtr.Pin()->GetViewLog();
		ViewLog->Log(FText::Format(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "SuccessUpdateStaticMesh", "Successfully updated a static mesh. PackageName: {0}"), FText::FromString(AssetObjectPath)));
	}
	else
	{
		TSharedRef<IDisplayClusterConfiguratorViewLog> ViewLog = ToolkitPtr.Pin()->GetViewLog();
		ViewLog->Log(NSLOCTEXT("FDisplayClusterConfiguratorViewLog", "ErrorUpdateStaticMesh", "Attempted to update a package with empty package name. PackageName"));
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorClusterSyncTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	RenderSyncPolicyHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationClusterSync, RenderSyncPolicy));
	InputSyncPolicyHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationClusterSync, InputSyncPolicy));
}

void FDisplayClusterConfiguratorClusterSyncTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	InChildBuilder
		.AddProperty(RenderSyncPolicyHandle.ToSharedRef())
		.ShouldAutoExpand(true);

	InChildBuilder
		.AddProperty(InputSyncPolicyHandle.ToSharedRef())
		.ShouldAutoExpand(true);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Render Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	TypeHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationPolymorphicEntity, Type));
	check(TypeHandle->IsValidHandle());
	ParametersHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationPolymorphicEntity, Parameters));
	check(ParametersHandle->IsValidHandle());

	// Create header row
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		InPropertyHandle->CreatePropertyValueWidget()
	];
}

void FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	ChildBuilder = &InChildBuilder;


}

//////////////////////////////////////////////////////////////////////////////////////////////
// Render Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

const FString FDisplayClusterConfiguratorRenderSyncPolicyCustomization::SwapGroupName = TEXT("SwapGroup");
const FString FDisplayClusterConfiguratorRenderSyncPolicyCustomization::SwapBarrierName = TEXT("SwapBarrier");

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	SwapGroupValue = SwapBarrierValue = 1;
	NvidiaOption = MakeShared<FString>("Nvidia");
	CustomOption = MakeShared<FString>("Custom");

	// Get the Editing object
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num())
	{
		ConfigurationClusterPtr = Cast<UDisplayClusterConfigurationCluster>(OuterObjects[0]);
	}
	check(ConfigurationClusterPtr != nullptr);

	// Set initial Nvidia option values
	{
		UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
		FString* SwapGroupParam = ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Find(SwapGroupName);
		if (SwapGroupParam != nullptr)
		{
			LexTryParseString<int32>(SwapGroupValue, **SwapGroupParam);
		}

		FString* SwapBarrierParam = ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Find(SwapBarrierName);
		if (SwapBarrierParam != nullptr)
		{
			LexTryParseString<int32>(SwapBarrierValue, **SwapBarrierParam);
		}
	}

	bIsCustomPolicy = IsCustomTypeInConfig();
	if (bIsCustomPolicy)
	{
		// Load default config
		UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
		CustomPolicy = ConfigurationCluster->Sync.RenderSyncPolicy.Type;
	}
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	// Hide properties 
	TypeHandle->MarkHiddenByCustomization();

	// Add custom rows
	ResetRenderSyncPolicyOptions();
	AddRenderSyncPolicyRow();
	AddNvidiaPolicyRows();
	AddCustomPolicyRow();

	// Add Parameters property with Visibility handler
	InChildBuilder
		.AddProperty(ParametersHandle.ToSharedRef())
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetCustomRowsVisibility)))
		.ShouldAutoExpand(true);
}

EVisibility FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetCustomRowsVisibility() const
{
	if (bIsCustomPolicy)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

EVisibility FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetNvidiaPolicyRowsVisibility() const
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	if (ConfigurationCluster->Sync.RenderSyncPolicy.Type.Equals(*NvidiaOption.Get()))
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::ResetRenderSyncPolicyOptions()
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	RenderSyncPolicyOptions.Reset();
	for (const FString& RenderSyncPolicy : UDisplayClusterConfiguratorEditorData::RenderSyncPolicies)
	{
		if (!ConfigurationCluster->Sync.RenderSyncPolicy.Type.Equals(RenderSyncPolicy))
		{
			RenderSyncPolicyOptions.Add(MakeShared<FString>(RenderSyncPolicy));
		}
	}

	// Add Custom option
	if (!bIsCustomPolicy)
	{
		RenderSyncPolicyOptions.Add(CustomOption);
	}
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::AddRenderSyncPolicyRow()
{
	if (RenderSyncPolicyComboBox.IsValid())
	{
		return;
	}
	
	ChildBuilder->AddCustomRow(TypeHandle->GetPropertyDisplayName())
	.NameContent()
	[
		TypeHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SAssignNew(RenderSyncPolicyComboBox, SDisplayClusterConfigurationSearchableComboBox)
		.OptionsSource(&RenderSyncPolicyOptions)
		.OnGenerateWidget(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::MakeRenderSyncPolicyOptionComboWidget)
		.OnSelectionChanged(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::OnRenderSyncPolicySelected)
		.ContentPadding(2)
		.MaxListHeight(200.0f)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetSelectedRenderSyncPolicyText)
		]
	];
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::AddNvidiaPolicyRows()
{
	ChildBuilder->AddCustomRow(TypeHandle->GetPropertyDisplayName())
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetNvidiaPolicyRowsVisibility)))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("SwapGroup", "Swap Group"))
	]
	.ValueContent()
	[
		SAssignNew(SwapGroupSpinBox,  SSpinBox<int32>)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.MinValue(1)
		.MaxValue(9)
		.Value_Lambda([this]() 
		{
			return SwapGroupValue;
		})
		.OnValueChanged_Lambda([this](int32 InValue)
		{
			UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
			check(ConfigurationCluster != nullptr);

			SwapGroupValue = InValue;
			ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Add(SwapGroupName, FString::FromInt(SwapGroupValue));
		})
	];

	ChildBuilder->AddCustomRow(TypeHandle->GetPropertyDisplayName())
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetNvidiaPolicyRowsVisibility)))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("SwapBarrier", "Swap Barrier"))
	]
	.ValueContent()
	[
		SAssignNew(SwapBarrierSpinBox, SSpinBox<int32>)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.MinValue(1)
		.MaxValue(9)
		.Value_Lambda([this]() 
		{
			return SwapBarrierValue;
		})
		.OnValueChanged_Lambda([this](int32 InValue)
		{
			UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
			check(ConfigurationCluster != nullptr);

			SwapBarrierValue = InValue;
			ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Add(SwapBarrierName, FString::FromInt(SwapBarrierValue));
		})
	];
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::AddCustomPolicyRow()
{
	if (CustomPolicyRow.IsValid())
	{
		return;
	}

	FText SyncProjectionName = LOCTEXT("SyncProjectionName", "Name");

	ChildBuilder->AddCustomRow(SyncProjectionName)
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetCustomRowsVisibility)))
	.NameContent()
	[
		SNew(STextBlock).Text(SyncProjectionName)
	]
	.ValueContent()
	[
		SAssignNew(CustomPolicyRow, SEditableTextBox)
			.Text(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetCustomPolicyText)
			.OnTextCommitted(this, &FDisplayClusterConfiguratorRenderSyncPolicyCustomization::OnTextCommittedInCustomPolicyText)
			.Font(IDetailLayoutBuilder::GetDetailFont())
	];
}

TSharedRef<SWidget> FDisplayClusterConfiguratorRenderSyncPolicyCustomization::MakeRenderSyncPolicyOptionComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::OnRenderSyncPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo)
{
	if (InPolicy.IsValid())
	{
		UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
		check(ConfigurationCluster != nullptr);

		FString SelectedPolicy = *InPolicy.Get();

		if (SelectedPolicy.Equals(*CustomOption.Get()))
		{
			bIsCustomPolicy = true;
			ConfigurationCluster->Sync.RenderSyncPolicy.Type = CustomPolicy;
		}
		else
		{
			bIsCustomPolicy = false;
			ConfigurationCluster->Sync.RenderSyncPolicy.Type = SelectedPolicy;
		}

		if (ConfigurationCluster->Sync.RenderSyncPolicy.Type.Equals(*NvidiaOption.Get()))
		{
			ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Add(SwapGroupName, FString::FromInt(SwapGroupValue));
			ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Add(SwapBarrierName, FString::FromInt(SwapBarrierValue));
		}
		else
		{
			ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Remove(SwapGroupName);
			ConfigurationCluster->Sync.RenderSyncPolicy.Parameters.Remove(SwapBarrierName);
		}

		// Reset available options
		ResetRenderSyncPolicyOptions();
		RenderSyncPolicyComboBox->ResetOptionsSource(&RenderSyncPolicyOptions);

		RenderSyncPolicyComboBox->SetIsOpen(false);
	}
}

FText FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetSelectedRenderSyncPolicyText() const
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	if (bIsCustomPolicy)
	{
		return FText::FromString(*CustomOption.Get());
	}

	return FText::FromString(ConfigurationCluster->Sync.RenderSyncPolicy.Type);
}

FText FDisplayClusterConfiguratorRenderSyncPolicyCustomization::GetCustomPolicyText() const
{
	return FText::FromString(CustomPolicy);
}

bool FDisplayClusterConfiguratorRenderSyncPolicyCustomization::IsCustomTypeInConfig() const
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	for (const FString& ProjectionPolicy : UDisplayClusterConfiguratorEditorData::ProjectionPoliсies)
	{
		if (ConfigurationCluster->Sync.RenderSyncPolicy.Type.Equals(ProjectionPolicy))
		{
			return false;
		}
	}

	return true;
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType)
{
	CustomPolicy = InValue.ToString();

	// Update Config
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	ConfigurationCluster->Sync.RenderSyncPolicy.Type = CustomPolicy;

	// Check if the custom config same as any of the ProjectionPoliсies configs 
	bIsCustomPolicy = true;
	for (const FString& ProjectionPolicy : UDisplayClusterConfiguratorEditorData::ProjectionPoliсies)
	{
		if (CustomPolicy.Equals(ProjectionPolicy))
		{
			bIsCustomPolicy = false;

			RenderSyncPolicyComboBox->SetSelectedItem(MakeShared<FString>(CustomPolicy));

			break;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Input Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorInputSyncPolicyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	// Get the Editing object
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num())
	{
		ConfigurationClusterPtr = Cast<UDisplayClusterConfigurationCluster>(OuterObjects[0]);
	}
	check(ConfigurationClusterPtr != nullptr);
}

void FDisplayClusterConfiguratorInputSyncPolicyCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	// Hide properties 
	TypeHandle->MarkHiddenByCustomization();
	ParametersHandle->MarkHiddenByCustomization();

	// Add custom rows
	ResetInputSyncPolicyOptions();
	AddInputSyncPolicyRow();
}

void FDisplayClusterConfiguratorInputSyncPolicyCustomization::ResetInputSyncPolicyOptions()
{
	InputSyncPolicyOptions.Reset();

	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	for (const FString& InputSyncPolicy : UDisplayClusterConfiguratorEditorData::InputSyncPolicies)
	{
		if (!ConfigurationCluster->Sync.InputSyncPolicy.Type.Equals(InputSyncPolicy))
		{
			InputSyncPolicyOptions.Add(MakeShared<FString>(InputSyncPolicy));
		}
	}
}

void FDisplayClusterConfiguratorInputSyncPolicyCustomization::AddInputSyncPolicyRow()
{
	if (InputSyncPolicyComboBox.IsValid())
	{
		return;
	}
	
	ChildBuilder->AddCustomRow(TypeHandle->GetPropertyDisplayName())
	.NameContent()
	[
		TypeHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SAssignNew(InputSyncPolicyComboBox, SDisplayClusterConfigurationSearchableComboBox)
		.OptionsSource(&InputSyncPolicyOptions)
		.OnGenerateWidget(this, &FDisplayClusterConfiguratorInputSyncPolicyCustomization::MakeInputSyncPolicyOptionComboWidget)
		.OnSelectionChanged(this, &FDisplayClusterConfiguratorInputSyncPolicyCustomization::OnInputSyncPolicySelected)
		.ContentPadding(2)
		.MaxListHeight(200.0f)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FDisplayClusterConfiguratorInputSyncPolicyCustomization::GetSelectedInputSyncPolicyText)
		]
	];
}

TSharedRef<SWidget> FDisplayClusterConfiguratorInputSyncPolicyCustomization::MakeInputSyncPolicyOptionComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem)); 
}

void FDisplayClusterConfiguratorInputSyncPolicyCustomization::OnInputSyncPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo)
{
	if (InPolicy.IsValid())
	{
		UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
		check(ConfigurationCluster != nullptr);
		ConfigurationCluster->Sync.InputSyncPolicy.Type = *InPolicy.Get();

		// Reset available options
		ResetInputSyncPolicyOptions();
		InputSyncPolicyComboBox->ResetOptionsSource(&InputSyncPolicyOptions);

		InputSyncPolicyComboBox->SetIsOpen(false);
	}
}

FText FDisplayClusterConfiguratorInputSyncPolicyCustomization::GetSelectedInputSyncPolicyText() const
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	return FText::FromString(ConfigurationCluster->Sync.InputSyncPolicy.Type);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Projection Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorProjectionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);
	CustomOption = MakeShared<FString>("Custom");

	// Get the Editing object
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num())
	{
		ConfigurationViewportPtr = Cast<UDisplayClusterConfigurationViewport>(OuterObjects[0]);
	}
	check(ConfigurationViewportPtr != nullptr);

	bIsCustomPolicy = IsCustomTypeInConfig();
	if (bIsCustomPolicy)
	{
		// Load default config
		UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
		CustomPolicy = ConfigurationViewport->ProjectionPolicy.Type;
	}
}

void FDisplayClusterConfiguratorProjectionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	// Hide properties 
	TypeHandle->MarkHiddenByCustomization();
	ParametersHandle->MarkHiddenByCustomization();

	// Add custom rows
	ResetProjectionPolicyOptions();
	AddProjectionPolicyRow();
	AddCustomPolicyRow();

	// Add Parameters property with Visibility handler
	InChildBuilder
		.AddProperty(ParametersHandle.ToSharedRef())
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorProjectionCustomization::GetCustomRowsVisibility)))
		.ShouldAutoExpand(true);
}

EVisibility FDisplayClusterConfiguratorProjectionCustomization::GetCustomRowsVisibility() const
{
	if (bIsCustomPolicy)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

void FDisplayClusterConfiguratorProjectionCustomization::ResetProjectionPolicyOptions()
{
	ProjectionPolicyOptions.Reset();

	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	for (const FString& ProjectionPolicy : UDisplayClusterConfiguratorEditorData::ProjectionPoliсies)
	{
		if (!ConfigurationViewport->ProjectionPolicy.Type.Equals(ProjectionPolicy))
		{
			ProjectionPolicyOptions.Add(MakeShared<FString>(ProjectionPolicy));
		}
	}

	// Add Custom option
	if (!bIsCustomPolicy)
	{
		ProjectionPolicyOptions.Add(CustomOption);
	}
}

void FDisplayClusterConfiguratorProjectionCustomization::AddProjectionPolicyRow()
{
	if (ProjectionPolicyComboBox.IsValid())
	{
		return;
	}
	
	ChildBuilder->AddCustomRow(TypeHandle->GetPropertyDisplayName())
	.NameContent()
	[
		TypeHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SAssignNew(ProjectionPolicyComboBox, SDisplayClusterConfigurationSearchableComboBox)
		.OptionsSource(&ProjectionPolicyOptions)
		.OnGenerateWidget(this, &FDisplayClusterConfiguratorProjectionCustomization::MakeProjectionPolicyOptionComboWidget)
		.OnSelectionChanged(this, &FDisplayClusterConfiguratorProjectionCustomization::OnProjectionPolicySelected)
		.ContentPadding(2)
		.MaxListHeight(200.0f)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FDisplayClusterConfiguratorProjectionCustomization::GetSelectedProjectionPolicyText)
		]
	];
}

void FDisplayClusterConfiguratorProjectionCustomization::AddCustomPolicyRow()
{
	if (CustomPolicyRow.IsValid())
	{
		return;
	}

	FText SyncProjectionName = LOCTEXT("SyncProjectionName", "Name");

	ChildBuilder->AddCustomRow(SyncProjectionName)
	.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorProjectionCustomization::GetCustomRowsVisibility)))
	.NameContent()
	[
		SNew(STextBlock).Text(SyncProjectionName)
	]
	.ValueContent()
	[
		SAssignNew(CustomPolicyRow, SEditableTextBox)
			.Text(this, &FDisplayClusterConfiguratorProjectionCustomization::GetCustomPolicyText)
			.OnTextCommitted(this, &FDisplayClusterConfiguratorProjectionCustomization::OnTextCommittedInCustomPolicyText)
			.Font(IDetailLayoutBuilder::GetDetailFont())
	];
}

TSharedRef<SWidget> FDisplayClusterConfiguratorProjectionCustomization::MakeProjectionPolicyOptionComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

void FDisplayClusterConfiguratorProjectionCustomization::OnProjectionPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo)
{
	if (InPolicy.IsValid())
	{
		FString SelectedPolicy = *InPolicy.Get();

		UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
		check(ConfigurationViewport != nullptr);

		if (SelectedPolicy.Equals(*CustomOption.Get()))
		{
			bIsCustomPolicy = true;
			ConfigurationViewport->ProjectionPolicy.Type = CustomPolicy;
		}
		else
		{
			bIsCustomPolicy = false;
			ConfigurationViewport->ProjectionPolicy.Type = SelectedPolicy;
		}

		// Reset available options
		ResetProjectionPolicyOptions();
		ProjectionPolicyComboBox->ResetOptionsSource(&ProjectionPolicyOptions);

		ProjectionPolicyComboBox->SetIsOpen(false);
	}
}

FText FDisplayClusterConfiguratorProjectionCustomization::GetSelectedProjectionPolicyText() const
{
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	if (bIsCustomPolicy)
	{
		return FText::FromString(*CustomOption.Get());
	}

	return FText::FromString(ConfigurationViewport->ProjectionPolicy.Type);
}

FText FDisplayClusterConfiguratorProjectionCustomization::GetCustomPolicyText() const
{
	return FText::FromString(CustomPolicy);
}

bool FDisplayClusterConfiguratorProjectionCustomization::IsCustomTypeInConfig() const
{
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	for (const FString& ProjectionPolicy : UDisplayClusterConfiguratorEditorData::ProjectionPoliсies)
	{
		if (ConfigurationViewport->ProjectionPolicy.Type.Equals(ProjectionPolicy))
		{
			return false;
		}
	}

	return true;
}

void FDisplayClusterConfiguratorProjectionCustomization::OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType)
{
	CustomPolicy = InValue.ToString();

	// Update Config
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	ConfigurationViewport->ProjectionPolicy.Type = CustomPolicy;

	// Check if the custom config same as any of the ProjectionPoliсies configs 
	bIsCustomPolicy = true;
	for (const FString& ProjectionPolicy : UDisplayClusterConfiguratorEditorData::ProjectionPoliсies)
	{
		if (CustomPolicy.Equals(ProjectionPolicy))
		{
			bIsCustomPolicy = false;

			ProjectionPolicyComboBox->SetSelectedItem(MakeShared<FString>(CustomPolicy));

			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
