// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Details/DisplayClusterConfiguratorDetailCustomization.h"

#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Views/Details/Widgets/SDisplayClusterConfigurationSearchableComboBox.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorExternalImagePicker.h"
#include "DisplayClusterConfiguratorUtils.h"

#include "DisplayClusterRootActor.h"
#include "Blueprints/DisplayClusterBlueprint.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterCameraComponent.h"

#include "DisplayClusterProjectionStrings.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DisplayClusterConfiguratorPropertyUtils.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorDetailCustomization"

//////////////////////////////////////////////////////////////////////////////////////////////
// Base UCLASS Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////

FDisplayClusterConfiguratorDetailCustomization::FDisplayClusterConfiguratorDetailCustomization()
	: ToolkitPtr(nullptr)
	, LayoutBuilder(nullptr)
	, NDisplayCategory(nullptr)
{}

void FDisplayClusterConfiguratorDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	UObject* ObjectBeingEdited = nullptr;
	
	{
		TArray<TWeakObjectPtr<UObject>> ObjectsBeingEdited = InLayoutBuilder.GetSelectedObjects();
		check(ObjectsBeingEdited.Num() > 0);
		ObjectBeingEdited = ObjectsBeingEdited[0].Get();
		
		for (UObject* Owner = ObjectBeingEdited; Owner; Owner = Owner->GetOuter())
		{
			if (ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(Owner))
			{
				RootActorPtr = RootActor;
				break;
			}
		}
	}

	if (!RootActorPtr.IsValid() || (ObjectBeingEdited && ObjectBeingEdited->IsTemplate(RF_ClassDefaultObject)))
	{
		if (FDisplayClusterConfiguratorBlueprintEditor* BPEditor = FDisplayClusterConfiguratorUtils::GetBlueprintEditorForObject(ObjectBeingEdited))
		{
			ToolkitPtr = StaticCastSharedRef<FDisplayClusterConfiguratorBlueprintEditor>(BPEditor->AsShared());
		}
	}

	check(RootActorPtr.IsValid() || ToolkitPtr.IsValid());
	
	LayoutBuilder = &InLayoutBuilder;
	NDisplayCategory = &LayoutBuilder->EditCategory("NDisplay", FText::GetEmpty());
	NDisplayCategory->InitiallyCollapsed(false);

	// Hide properties that should only be visible on the instance. VisibleInstanceOnly doesn't work with these properties.
	TArray<TSharedRef<IPropertyHandle>> CategoryProperties;
	NDisplayCategory->GetDefaultProperties(CategoryProperties);

	for (TSharedRef<IPropertyHandle>& PropertyHandle : CategoryProperties)
	{
		FProperty* Property = PropertyHandle->GetProperty();

		const bool bShouldHide = PropertyHandle->HasMetaData("nDisplayHidden") || 
			(IsRunningForBlueprintEditor() ? 
				PropertyHandle->HasMetaData("nDisplayInstanceOnly") || Property && Property->HasAnyPropertyFlags(CPF_DisableEditOnTemplate) :
				false);

		if (bShouldHide)
		{
			PropertyHandle->MarkHiddenByCustomization();
		}
	}
}

ADisplayClusterRootActor* FDisplayClusterConfiguratorDetailCustomization::GetRootActor() const
{
	ADisplayClusterRootActor* RootActor = nullptr;
	
	if (ToolkitPtr.IsValid())
	{
		RootActor = Cast<ADisplayClusterRootActor>(ToolkitPtr.Pin()->GetPreviewActor());
	}
	else
	{
		RootActor = RootActorPtr.Get();
	}

	check(RootActor);
	return RootActor;
}

UDisplayClusterConfigurationData* FDisplayClusterConfiguratorDetailCustomization::GetConfigData() const
{
	UDisplayClusterConfigurationData* ConfigData = nullptr;
	
	if (ToolkitPtr.IsValid())
	{
		ConfigData = ToolkitPtr.Pin()->GetConfig();
	}
	else if (RootActorPtr.IsValid())
	{
		ConfigData = RootActorPtr->GetConfigData();
	}
	
	check(ConfigData);
	return ConfigData;
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

	TSharedPtr<IPropertyHandle> CustomParamsHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationData, CustomParameters));
	check(CustomParamsHandle->IsValidHandle());
	NDisplayCategory->AddProperty(CustomParamsHandle).ShouldAutoExpand(false);
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

	ClusterNodesHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Nodes));
	check(ClusterNodesHandle->IsValidHandle());

	if (!IsRunningForBlueprintEditor())
	{
		// Add a reset cluster nodes default button. Normal reset to defaults on the map won't display because of EditFixedSize flag.
		// This is also handy since UE will stop propagating CDO container structure changes when an instanced value is modified.
		
		NDisplayCategory->AddCustomRow(ClusterNodesHandle->GetDefaultCategoryText())
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Fill)
			[
				SNew(SButton)
				.Text(LOCTEXT("ResetClusterNodesButton_Label", "Reset Cluster Nodes"))
				.ToolTipText(LOCTEXT("ResetClusterNodesButton_Tooltip", "Reset all cluster nodes to class defaults."))
				.OnClicked(FOnClicked::CreateLambda([this]()
				{
					ClusterNodesHandle->ResetToDefault();
					return FReply::Handled();
				}))
			]
		];
	}
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
	UDisplayClusterConfigurationData* ConfigurationData = GetConfigData();
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
	
	AActor* RootActor = GetRootActor();
	
	TArray<UActorComponent*> ActorComponents;
	RootActor->GetComponents(UDisplayClusterCameraComponent::StaticClass(), ActorComponents);
	for (UActorComponent* ActorComponent : ActorComponents)
	{
		const FString ComponentName = ActorComponent->GetName();
		CameraOptions.Add(MakeShared<FString>(ComponentName));
	}

	// Component order not guaranteed, sort for consistency.
	CameraOptions.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
	{
		// Default sort isn't compatible with TSharedPtr<FString>.
		return *A < *B;
	});
	
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
			CameraHandle->SetValue(TEXT(""));

		}
		else
		{
			CameraHandle->SetValue(*InCamera.Get());
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
// Base Scene Component Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorSceneComponentDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	Super::CustomizeDetails(InLayoutBuilder);
	SceneComponenPtr = nullptr;

	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		SceneComponenPtr = Cast<UDisplayClusterSceneComponent>(SelectedObjects[0]);
	}
	check(SceneComponenPtr != nullptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Screen Component Detail Customization
//////////////////////////////////////////////////////////////////////////////////////////////

const TArray<FDisplayClusterConfiguratorAspectRatioPresetSize> FDisplayClusterConfiguratorAspectRatioPresetSize::CommonPresets =
{
	FDisplayClusterConfiguratorAspectRatioPresetSize(LOCTEXT("3x2", "3:2"), FVector2D(100.f, 66.67f)),
	FDisplayClusterConfiguratorAspectRatioPresetSize(LOCTEXT("4x3", "4:3"), FVector2D(100.f, 75.f)),
	FDisplayClusterConfiguratorAspectRatioPresetSize(LOCTEXT("16x9", "16:9"), FVector2D(100.f, 56.25f)),
	FDisplayClusterConfiguratorAspectRatioPresetSize(LOCTEXT("16x10", "16:10"), FVector2D(100.f, 62.5f)),
	FDisplayClusterConfiguratorAspectRatioPresetSize(LOCTEXT("1.90", "1.90"), FVector2D(100.f, 52.73f))
};

const int32 FDisplayClusterConfiguratorAspectRatioPresetSize::DefaultPreset = 2;

void FDisplayClusterConfiguratorScreenDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	// Get the Editing object
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num())
	{
		ScreenComponentPtr = Cast<UDisplayClusterScreenComponent>(SelectedObjects[0]);
	}
	check(ScreenComponentPtr != nullptr);

	for (const FDisplayClusterConfiguratorAspectRatioPresetSize& PresetItem : FDisplayClusterConfiguratorAspectRatioPresetSize::CommonPresets)
	{
		TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize> PresetItemPtr = MakeShared<FDisplayClusterConfiguratorAspectRatioPresetSize>(PresetItem);
		PresetItems.Add(PresetItemPtr);
	}
	
	check(FDisplayClusterConfiguratorAspectRatioPresetSize::DefaultPreset >= 0 && FDisplayClusterConfiguratorAspectRatioPresetSize::DefaultPreset < PresetItems.Num());
	const TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize> InitiallySelectedPresetItem = PresetItems[FDisplayClusterConfiguratorAspectRatioPresetSize::DefaultPreset];
	// Make sure default value is set for current preset.
	GetAspectRatioAndSetDefaultValueForPreset(*InitiallySelectedPresetItem.Get());
	
	const FText RowName = LOCTEXT("DisplayClusterConfiguratorResolution", "Aspect Ratio Preset");
	
	SizeHandlePtr = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterScreenComponent, SizeCm));
	SizeHandlePtr->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateRaw(this, &FDisplayClusterConfiguratorScreenDetailCustomization::OnSizePropertyChanged));
	SizeHandlePtr->SetOnPropertyResetToDefault(FSimpleDelegate::CreateRaw(this, &FDisplayClusterConfiguratorScreenDetailCustomization::OnSizePropertyChanged));

	// This will detect custom ratios.
	OnSizePropertyChanged();
	
	InLayoutBuilder.EditCategory("DisplayCluster")
	.AddCustomRow(RowName)
	.NameWidget
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(RowName)
	]
	.ValueWidget
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		[
			SAssignNew(PresetsComboBox, SComboBox<TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>>)
			.OptionsSource(&PresetItems)
			.InitiallySelectedItem(InitiallySelectedPresetItem)
			.OnSelectionChanged(this, &FDisplayClusterConfiguratorScreenDetailCustomization::OnSelectedPresetChanged)
			.OnGenerateWidget_Lambda([=](TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize> Item)
			{
				return SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(GetPresetDisplayText(Item));
			})
			.Content()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(this, &FDisplayClusterConfiguratorScreenDetailCustomization::GetPresetsComboBoxSelectedText)
			]
		]
	];
}

FText FDisplayClusterConfiguratorScreenDetailCustomization::GetPresetsComboBoxSelectedText() const
{
	return GetSelectedPresetDisplayText(PresetsComboBox->GetSelectedItem());
}

FText FDisplayClusterConfiguratorScreenDetailCustomization::GetPresetDisplayText(
	const TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>& Preset) const
{
	FText DisplayText = FText::GetEmpty();

	if (Preset.IsValid())
	{
		DisplayText = FText::Format(LOCTEXT("PresetDisplayText", "{0}"), Preset->DisplayName);
	}

	return DisplayText;
}

FText FDisplayClusterConfiguratorScreenDetailCustomization::GetSelectedPresetDisplayText(
	const TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize>& Preset) const
{
	FText DisplayText = FText::GetEmpty();

	if (bIsCustomAspectRatio)
	{
		DisplayText = LOCTEXT("PresetDisplayCustomText", "Custom");
	}
	else if (Preset.IsValid())
	{
		DisplayText = FText::Format(LOCTEXT("PresetDisplayText", "{0}"), Preset->DisplayName);
	}

	return DisplayText;
}

void FDisplayClusterConfiguratorScreenDetailCustomization::OnSelectedPresetChanged(
	TSharedPtr<FDisplayClusterConfiguratorAspectRatioPresetSize> SelectedPreset, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Type::Direct && SelectedPreset.IsValid() && SizeHandlePtr.IsValid())
	{
		FVector2D NewValue;
		GetAspectRatioAndSetDefaultValueForPreset(*SelectedPreset.Get(), &NewValue);

		// Compute size based on new aspect ratio and old value.
		{
			FVector2D OldSize;
			SizeHandlePtr->GetValue(OldSize);
			NewValue.X = OldSize.X;
			NewValue.Y = (double)NewValue.X / SelectedPreset->GetAspectRatio();
		}
		
		SizeHandlePtr->SetValue(NewValue);
	}
}

void FDisplayClusterConfiguratorScreenDetailCustomization::GetAspectRatioAndSetDefaultValueForPreset(
	const FDisplayClusterConfiguratorAspectRatioPresetSize& Preset, FVector2D* OutAspectRatio)
{
	const FVector2D NewValueCm = Preset.Size;
	const FVector2D NewValue(NewValueCm / 100.f);
	
	if (UDisplayClusterScreenComponent* Archetype = Cast<UDisplayClusterScreenComponent>(ScreenComponentPtr->GetArchetype()))
	{
		// Set the DEFAULT value here, that way user can always reset to default for the current preset.
		Archetype->Modify();
		Archetype->SetScreenSize(NewValue);
	}

	if (OutAspectRatio)
	{
		*OutAspectRatio = NewValueCm;
	}
}

void FDisplayClusterConfiguratorScreenDetailCustomization::OnSizePropertyChanged()
{
	if (SizeHandlePtr.IsValid())
	{
		FVector2D SizeValue;
		SizeHandlePtr->GetValue(SizeValue);

		const double AspectRatio = (double)SizeValue.X / (double)SizeValue.Y;

		const FDisplayClusterConfiguratorAspectRatioPresetSize* FoundPreset = nullptr;
		for (const FDisplayClusterConfiguratorAspectRatioPresetSize& Preset : FDisplayClusterConfiguratorAspectRatioPresetSize::CommonPresets)
		{
			const double PresetAspectRatio = Preset.GetAspectRatio();
			if (FMath::IsNearlyEqual(AspectRatio, PresetAspectRatio, 0.001))
			{
				FoundPreset = &Preset;
				break;
			}
		}

		bIsCustomAspectRatio = FoundPreset == nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Base Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle,
	FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	
	TArray<UObject*> OuterObjects;
	InPropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num())
	{
		EditingObject = OuterObjects[0];
	}
}

void FDisplayClusterConfiguratorTypeCustomization::RefreshBlueprint()
{
	if (FDisplayClusterConfiguratorBlueprintEditor* BlueprintEditor = FDisplayClusterConfiguratorUtils::GetBlueprintEditorForObject(EditingObject))
	{
		BlueprintEditor->RefreshDisplayClusterPreviewActor();
	}
}

void FDisplayClusterConfiguratorTypeCustomization::ModifyBlueprint()
{
	if (UDisplayClusterBlueprint* Blueprint = FDisplayClusterConfiguratorUtils::FindBlueprintFromObject(EditingObject))
	{
		FDisplayClusterConfiguratorUtils::MarkDisplayClusterBlueprintAsModified(Blueprint, false);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorClusterSyncTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorTypeCustomization::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	RenderSyncPolicyHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationClusterSync, RenderSyncPolicy));
	InputSyncPolicyHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationClusterSync, InputSyncPolicy));
}

void FDisplayClusterConfiguratorClusterSyncTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorTypeCustomization::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

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
	FDisplayClusterConfiguratorTypeCustomization::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	TypeHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationPolymorphicEntity, Type));
	check(TypeHandle->IsValidHandle());
	ParametersHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationPolymorphicEntity, Parameters));
	check(ParametersHandle->IsValidHandle());
	IsCustomHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationPolymorphicEntity, bIsCustom));
	check(IsCustomHandle->IsValidHandle());

	IsCustomHandle->MarkHiddenByCustomization();
	
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
	FDisplayClusterConfiguratorTypeCustomization::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	ChildBuilder = &InChildBuilder;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Render Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

const FString FDisplayClusterConfiguratorRenderSyncPolicyCustomization::SwapGroupName = TEXT("SwapGroup");
const FString FDisplayClusterConfiguratorRenderSyncPolicyCustomization::SwapBarrierName = TEXT("SwapBarrier");

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

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
	FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);
	
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
	if (UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get())
	{
		if (ConfigurationCluster->Sync.RenderSyncPolicy.Type.Equals(*NvidiaOption.Get()))
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::ResetRenderSyncPolicyOptions()
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	RenderSyncPolicyOptions.Reset();
	for (const FString& RenderSyncPolicy : UDisplayClusterConfigurationData::RenderSyncPolicies)
	{
		RenderSyncPolicyOptions.Add(MakeShared<FString>(RenderSyncPolicy));
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
			SwapGroupValue = InValue;
			AddToParameterMap(SwapGroupName, FString::FromInt(SwapGroupValue));
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
			SwapBarrierValue = InValue;
			AddToParameterMap(SwapBarrierName, FString::FromInt(SwapBarrierValue));
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

		const FString SelectedPolicy = *InPolicy.Get();

		ConfigurationCluster->Modify();
		ModifyBlueprint();
		
		if (SelectedPolicy.Equals(*CustomOption.Get()))
		{
			bIsCustomPolicy = true;
			TypeHandle->SetValue(CustomPolicy);
			IsCustomHandle->SetValue(true);
		}
		else
		{
			bIsCustomPolicy = false;
			TypeHandle->SetValue(SelectedPolicy);
			IsCustomHandle->SetValue(false);
		}

		if (ConfigurationCluster->Sync.RenderSyncPolicy.Type.Equals(*NvidiaOption.Get()))
		{
			AddToParameterMap(SwapGroupName, FString::FromInt(SwapGroupValue));
			AddToParameterMap(SwapBarrierName, FString::FromInt(SwapBarrierValue));
		}
		else
		{
			RemoveFromParameterMap(SwapGroupName);
			RemoveFromParameterMap(SwapBarrierName);
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
	if (ConfigurationCluster == nullptr)
	{
		return FText::GetEmpty();
	}

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

	if (ConfigurationCluster->Sync.RenderSyncPolicy.bIsCustom)
	{
		return true;
	}
	
	for (const FString& Policy : UDisplayClusterConfigurationData::RenderSyncPolicies)
	{
		if (ConfigurationCluster->Sync.RenderSyncPolicy.Type.ToLower().Equals(Policy.ToLower()))
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
	
	TypeHandle->SetValue(CustomPolicy);

	// Check if the custom config same as any of the ProjectionPoliсies configs 
	bIsCustomPolicy = true;
	for (const FString& ProjectionPolicy : UDisplayClusterConfigurationData::ProjectionPolicies)
	{
		if (CustomPolicy.Equals(ProjectionPolicy))
		{
			bIsCustomPolicy = false;

			RenderSyncPolicyComboBox->SetSelectedItem(MakeShared<FString>(CustomPolicy));

			break;
		}
	}
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::AddToParameterMap(const FString& Key,
                                                                                 const FString& Value)
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);
	
	FStructProperty* SyncStructProperty = FindFProperty<FStructProperty>(ConfigurationCluster->GetClass(), GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Sync));
	check(SyncStructProperty);

	FStructProperty* RenderStructProperty = FindFProperty<FStructProperty>(SyncStructProperty->Struct, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationClusterSync, RenderSyncPolicy));
	check(RenderStructProperty);

	uint8* MapContainer = RenderStructProperty->ContainerPtrToValuePtr<uint8>(&ConfigurationCluster->Sync);
	DisplayClusterConfiguratorPropertyUtils::AddKeyValueToMap(MapContainer, ParametersHandle, Key, Value);
}

void FDisplayClusterConfiguratorRenderSyncPolicyCustomization::RemoveFromParameterMap(const FString& Key)
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	check(ConfigurationCluster != nullptr);

	FStructProperty* SyncStructProperty = FindFProperty<FStructProperty>(ConfigurationCluster->GetClass(), GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationCluster, Sync));
	check(SyncStructProperty);

	FStructProperty* RenderStructProperty = FindFProperty<FStructProperty>(SyncStructProperty->Struct, GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationClusterSync, RenderSyncPolicy));
	check(RenderStructProperty);

	uint8* MapContainer = RenderStructProperty->ContainerPtrToValuePtr<uint8>(&ConfigurationCluster->Sync);
	DisplayClusterConfiguratorPropertyUtils::RemoveKeyFromMap(MapContainer, ParametersHandle, Key);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Input Sync Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorInputSyncPolicyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

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
	FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

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

	for (const FString& InputSyncPolicy : UDisplayClusterConfigurationData::InputSyncPolicies)
	{
		InputSyncPolicyOptions.Add(MakeShared<FString>(InputSyncPolicy));
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

		ConfigurationCluster->Modify();
		ModifyBlueprint();
		
		TypeHandle->SetValue(*InPolicy.Get());
		
		// Reset available options
		ResetInputSyncPolicyOptions();
		InputSyncPolicyComboBox->ResetOptionsSource(&InputSyncPolicyOptions);

		InputSyncPolicyComboBox->SetIsOpen(false);
	}
}

FText FDisplayClusterConfiguratorInputSyncPolicyCustomization::GetSelectedInputSyncPolicyText() const
{
	UDisplayClusterConfigurationCluster* ConfigurationCluster = ConfigurationClusterPtr.Get();
	if (ConfigurationCluster == nullptr)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(ConfigurationCluster->Sync.InputSyncPolicy.Type);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// External Image Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterConfiguratorExternalImageTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorTypeCustomization::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);

	ImagePathHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDisplayClusterConfigurationExternalImage, ImagePath));
	check(ImagePathHandle->IsValidHandle());

	FString ImagePath;
	ImagePathHandle->GetValue(ImagePath);

	TArray<FString> ImageExtensions = {
		"png",
		"jpeg",
		"jpg",
		"bmp",
		"ico",
		"icns",
		"exr"
	};

	// Create header row
	InHeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SDisplayClusterConfiguratorExternalImagePicker)
		.ImagePath(ImagePath)
		.Extensions(ImageExtensions)
		.OnImagePathPicked_Lambda([=](const FString& NewImagePath) { ImagePathHandle->SetValue(NewImagePath); })
	];
}

void FDisplayClusterConfiguratorExternalImageTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorTypeCustomization::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);
}

#undef LOCTEXT_NAMESPACE
