// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Details/Policies/DisplayClusterConfiguratorPolicyParameterCustomization.h"

#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorUtils.h"
#include "DisplayClusterConfiguratorPropertyUtils.h"
#include "Views/Details/Widgets/SDisplayClusterConfigurationSearchableComboBox.h"

#include "DisplayClusterRootActor.h"
#include "Blueprints/DisplayClusterBlueprint.h"
#include "Misc/DisplayClusterHelpers.h"
#include "DisplayClusterProjectionStrings.h"
#include "Misc/DisplayClusterTypesConverter.h"

#include "EditorDirectories.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorPolicyParameterCustomization"

//////////////////////////////////////////////////////////////////////////////////////////////
// Policy Parameter Configuration
//////////////////////////////////////////////////////////////////////////////////////////////

FPolicyParameterInfo::FPolicyParameterInfo(
	const FString& InDisplayName,
	const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint,
	UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle,
	const FString* InInitialValue)
{
	ParamDisplayName = InDisplayName;
	ParamKey = InKey;
	BlueprintOwnerPtr = InBlueprint;
	ConfigurationViewportPtr = InConfigurationViewport;
	ParametersHandle = InParametersHandle;
	BlueprintEditorPtrCached = FDisplayClusterConfiguratorUtils::GetBlueprintEditorForObject(InBlueprint);

	if (InInitialValue)
	{
		InitialValue = MakeShared<FString>(*InInitialValue);
	}
}

void FPolicyParameterInfo::SetParameterVisibilityDelegate(FParameterVisible InDelegate)
{
	OnParameterVisibilityCheck = InDelegate;
}

FText FPolicyParameterInfo::GetOrAddCustomParameterValueText() const
{
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	FString* ParameterValue = ConfigurationViewport->ProjectionPolicy.Parameters.Find(GetParameterKey());
	if (ParameterValue == nullptr)
	{
		UpdateCustomParameterValueText(InitialValue.IsValid() ? *InitialValue : TEXT(""), false);
		ParameterValue = ConfigurationViewport->ProjectionPolicy.Parameters.Find(GetParameterKey());
	}
	check(ParameterValue)
	
	return FText::FromString(*ParameterValue);
}

bool FPolicyParameterInfo::IsParameterAlreadyAdded() const
{
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	return ConfigurationViewport->ProjectionPolicy.Parameters.Contains(GetParameterKey());
}

void FPolicyParameterInfo::UpdateCustomParameterValueText(const FString& NewValue, bool bNotify) const
{
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);
	ConfigurationViewport->Modify();

	FStructProperty* StructProperty = FindFProperty<FStructProperty>(ConfigurationViewport->GetClass(), GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationViewport, ProjectionPolicy));
	check(StructProperty);

	uint8* MapContainer = StructProperty->ContainerPtrToValuePtr<uint8>(ConfigurationViewport);
	DisplayClusterConfiguratorPropertyUtils::AddKeyValueToMap(MapContainer, ParametersHandle, GetParameterKey(), NewValue);

	if (bNotify)
	{
		if (FDisplayClusterConfiguratorBlueprintEditor* BlueprintEditor = FDisplayClusterConfiguratorUtils::GetBlueprintEditorForObject(BlueprintOwnerPtr.Get()))
		{
			BlueprintEditor->ClusterChanged(true);
			BlueprintEditor->RefreshDisplayClusterPreviewActor();
		}
	}
}

EVisibility FPolicyParameterInfo::IsParameterVisible() const
{
	return OnParameterVisibilityCheck.IsBound() ? OnParameterVisibilityCheck.Execute(GetOrAddCustomParameterValueText()) : EVisibility::Visible;
}

FPolicyParameterInfoCombo::FPolicyParameterInfoCombo(const FString& InDisplayName, const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint, UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle,
	const TArray<FString>& InValues, const FString* InInitialItem, bool bSort) : FPolicyParameterInfo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle, InInitialItem)
{
	for (const FString& Value : InValues)
	{
		CustomParameterOptions.Add(MakeShared<FString>(Value));
	}

	if (bSort)
	{
		CustomParameterOptions.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
		{
			// Default sort isn't compatible with TSharedPtr<FString>.
			return *A < *B;
		});
	}
}

void FPolicyParameterInfoCombo::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{
	InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(this, &FPolicyParameterInfo::GetParameterDisplayName)
	]
	.ValueContent()
	[
		SAssignNew(GetCustomParameterValueComboBox(), SDisplayClusterConfigurationSearchableComboBox)
		.OptionsSource(&GetCustomParameterOptions())
		.InitiallySelectedItem(InitialValue)
		.OnGenerateWidget(this, &FPolicyParameterInfoCombo::MakeCustomParameterValueComboWidget)
		.OnSelectionChanged(this, &FPolicyParameterInfoCombo::OnCustomParameterValueSelected)
		.Visibility(this, &FPolicyParameterInfoCombo::IsParameterVisible)
		.ContentPadding(2)
		.MaxListHeight(200.0f)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &FPolicyParameterInfoCombo::GetOrAddCustomParameterValueText)
		]
	];
}

void FPolicyParameterInfoCombo::SetOnSelectedDelegate(FOnItemSelected InDelegate)
{
	OnItemSelected = InDelegate;
}

TSharedRef<SWidget> FPolicyParameterInfoCombo::MakeCustomParameterValueComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

void FPolicyParameterInfoCombo::OnCustomParameterValueSelected(TSharedPtr<FString> InValue, ESelectInfo::Type SelectInfo)
{
	UpdateCustomParameterValueText(InValue.IsValid() ? *InValue.Get() : "");
	OnItemSelected.ExecuteIfBound(*InValue);
}


FPolicyParameterInfoComponentCombo::FPolicyParameterInfoComponentCombo(
	const FString& InDisplayName,
	const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint,
	UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle,
	const TArray<TSubclassOf<UActorComponent>>& InComponentClasses) : FPolicyParameterInfoCombo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle, {}, nullptr)
{
	ComponentTypes = InComponentClasses;
	if (BlueprintEditorPtrCached)
	{
		// BlueprintEditorPtrCached can be null when remote control is interfacing with the world instance details panel. At this stage
		// there isn't a blueprint editor available. Normally this customization is hidden here but remote control seems to trigger it.
		
		ADisplayClusterRootActor* RootActor = CastChecked<ADisplayClusterRootActor>(BlueprintEditorPtrCached->GetPreviewActor());
		CreateParameterValues(RootActor);
	}
}

void FPolicyParameterInfoComponentCombo::CreateParameterValues(ADisplayClusterRootActor* RootActor)
{
	for (const TSubclassOf<UActorComponent>& ComponentType : ComponentTypes)
	{
		TArray<UActorComponent*> ActorComponents;
		RootActor->GetComponents(ComponentType, ActorComponents);
		for (UActorComponent* ActorComponent : ActorComponents)
		{
			if (ActorComponent->GetName().EndsWith(FDisplayClusterConfiguratorUtils::GetImplSuffix()))
			{
				// Ignore the default impl subobjects.
				continue;
			}
			const FString ComponentName = ActorComponent->GetName();
			CustomParameterOptions.Add(MakeShared<FString>(ComponentName));
		}
	}
}


FPolicyParameterInfoText::FPolicyParameterInfoText(const FString& InDisplayName, const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint,
	UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle) :
	FPolicyParameterInfo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle)
{
}

void FPolicyParameterInfoText::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{
	InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(GetParameterDisplayName())
		]
		.ValueContent()
		[
			SNew(SEditableTextBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(this, &FPolicyParameterInfoText::GetOrAddCustomParameterValueText)
			.OnTextCommitted_Lambda([this](const FText& InValue, ETextCommit::Type InCommitType)
			{
				UpdateCustomParameterValueText(InValue.ToString());
			})
		];
}

FPolicyParameterInfoBool::FPolicyParameterInfoBool(const FString& InDisplayName, const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint, UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle) :
	FPolicyParameterInfo(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle)
{
}

void FPolicyParameterInfoBool::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{
	InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(GetParameterDisplayName())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FPolicyParameterInfoBool::IsChecked)
			.OnCheckStateChanged_Lambda([this](ECheckBoxState InValue)
			{
				UpdateCustomParameterValueText(DisplayClusterTypesConverter::template ToString(InValue == ECheckBoxState::Checked ? true : false));
			})
		];
}

ECheckBoxState FPolicyParameterInfoBool::IsChecked() const
{
	const FString StrValue = GetOrAddCustomParameterValueText().ToString().ToLower();
	const bool bValue = DisplayClusterTypesConverter::template FromString<bool>(StrValue);

	return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}


void FPolicyParameterInfoFile::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{
	InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(GetParameterDisplayName())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.IsReadOnly(true)
				.Text(this, &FPolicyParameterInfoFile::GetOrAddCustomParameterValueText)
			]
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &FPolicyParameterInfoFile::OnChangePathClicked)
				.ToolTipText(LOCTEXT("ChangeSourcePath_Tooltip", "Browse for file"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("...", "..."))
				]
			]
		];
}

FString FPolicyParameterInfoFile::OpenSelectFileDialogue()
{
	FString FileTypes;
	FString AllExtensions;

	for (const FString& Format : FileExtensions)
	{
		TArray<FString> FormatComponents;
		Format.ParseIntoArray(FormatComponents, TEXT(";"), false);

		for (int32 ComponentIndex = 0; ComponentIndex < FormatComponents.Num(); ComponentIndex += 2)
		{
			const FString& Extension = FormatComponents[ComponentIndex];

			if (!AllExtensions.IsEmpty())
			{
				AllExtensions.AppendChar(TEXT(';'));
			}
			AllExtensions.Append(TEXT("*."));
			AllExtensions.Append(Extension);

			if (!FileTypes.IsEmpty())
			{
				FileTypes.AppendChar(TEXT('|'));
			}

			FileTypes.Append(FString::Printf(TEXT("(*.%s)|*.%s"), *Extension, *Extension));
		}
	}
	
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		const FString SupportedExtensions(FString::Printf(TEXT("All Files (%s)|%s|%s"), *AllExtensions, *AllExtensions, *FileTypes));
		const FString DefaultLocation(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT));

		TArray<FString> OpenedFiles;
		
		const bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("FileDialogTitle", "Open file").ToString(),
			DefaultLocation,
			TEXT(""),
			SupportedExtensions,
			EFileDialogFlags::None,
			OpenedFiles
		);

		if (bOpened && OpenedFiles.Num() > 0)
		{
			const FString& OpenedFile = OpenedFiles[0];
			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(OpenedFile));

			return FPaths::ConvertRelativePathToFull(OpenedFile);
		}
	}

	return FString();
}

FReply FPolicyParameterInfoFile::OnChangePathClicked()
{
	UpdateCustomParameterValueText(OpenSelectFileDialogue());
	return FReply::Handled();
}


TSharedRef<SWidget> FPolicyParameterInfoFloatReference::MakeFloatInputWidget(TSharedRef<float>& ProxyValue, const FText& Label,
                                                                     bool bRotationInDegrees,
                                                                     const FLinearColor& LabelColor,
                                                                     const FLinearColor& LabelBackgroundColor)
{
	return
		SNew(SNumericEntryBox<float>)
			.Value(this, &FPolicyParameterInfoFloatReference::OnGetValue, ProxyValue)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.UndeterminedString(NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values"))
			.OnValueCommitted(this, &FPolicyParameterInfoFloatReference::OnValueCommitted, ProxyValue)
			.LabelVAlign(VAlign_Fill)
			.LabelPadding(0)
			.AllowSpin(bRotationInDegrees)
			.MaxSliderValue(bRotationInDegrees ? 360.0f : TOptional<float>())
			.MinSliderValue(bRotationInDegrees ? 0.0f : TOptional<float>())
			.Label()
		[
			SNumericEntryBox<float>::BuildLabel(Label, LabelColor, LabelBackgroundColor)
		];
}

void FPolicyParameterInfoFloatReference::OnValueCommitted(float NewValue, ETextCommit::Type CommitType, TSharedRef<float> Value)
{
	*Value = NewValue;
	FormatTextAndUpdateParameter();
}

FPolicyParameterInfoMatrix::FPolicyParameterInfoMatrix(const FString& InDisplayName, const FString& InKey,
                                                       UDisplayClusterBlueprint* InBlueprint,
                                                       UDisplayClusterConfigurationViewport* InConfigurationViewport,
														const TSharedPtr<IPropertyHandle>& InParametersHandle):
	FPolicyParameterInfoFloatReference(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle),
	CachedTranslationX(MakeShared<float>()),
	CachedTranslationY(MakeShared<float>()),
	CachedTranslationZ(MakeShared<float>()),
	CachedRotationYaw(MakeShared<float>()),
	CachedRotationPitch(MakeShared<float>()),
	CachedRotationRoll(MakeShared<float>()),
	CachedScaleX(MakeShared<float>()),
	CachedScaleY(MakeShared<float>()),
	CachedScaleZ(MakeShared<float>())
{
	const FText TextValue = GetOrAddCustomParameterValueText();
	const FMatrix Matrix = DisplayClusterTypesConverter::template FromString<FMatrix>(TextValue.ToString());

	const FVector Translation = Matrix.GetOrigin();
	const FRotator Rotation = Matrix.Rotator();
	const FVector Scale = Matrix.GetScaleVector();

	*CachedTranslationX = Translation.X;
	*CachedTranslationY = Translation.Y;
	*CachedTranslationZ = Translation.Z;

	*CachedRotationYaw = Rotation.Yaw;
	*CachedRotationPitch = Rotation.Pitch;
	*CachedRotationRoll = Rotation.Roll;

	*CachedScaleX = Scale.X;
	*CachedScaleY = Scale.Y;
	*CachedScaleZ = Scale.Z;
}


void FPolicyParameterInfoMatrix::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{	IDetailGroup& Group = InDetailWidgetRow.AddGroup(*GetParameterKey(), GetParameterDisplayName());
	Group.HeaderRow()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(GetParameterDisplayName())
		.Visibility(this, &FPolicyParameterInfoMatrix::IsParameterVisible)
	]
	.Visibility(MakeAttributeRaw(this, &FPolicyParameterInfoMatrix::IsParameterVisible));
	
	CustomizeLocation(Group.AddWidgetRow());
	CustomizeRotation(Group.AddWidgetRow());
	CustomizeScale(Group.AddWidgetRow());
	
	Group.ToggleExpansion(true);
}

void FPolicyParameterInfoMatrix::CustomizeLocation(FDetailWidgetRow& InDetailWidgetRow)
{
	InDetailWidgetRow
	.Visibility(MakeAttributeRaw(this, &FPolicyParameterInfoMatrix::IsParameterVisible))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("LocationLabel", "Location"))
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedTranslationX, LOCTEXT("TranslationX", "X"), false, FLinearColor::White, SNumericEntryBox<float>::RedLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedTranslationY, LOCTEXT("TranslationY", "Y"), false, FLinearColor::White, SNumericEntryBox<float>::GreenLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedTranslationZ, LOCTEXT("TranslationZ", "Z"), false, FLinearColor::White, SNumericEntryBox<float>::BlueLabelBackgroundColor)
		]
	];
}

void FPolicyParameterInfoMatrix::CustomizeRotation(FDetailWidgetRow& InDetailWidgetRow)
{
	InDetailWidgetRow
	.Visibility(MakeAttributeRaw(this, &FPolicyParameterInfoMatrix::IsParameterVisible))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("RotationLabel", "Rotation"))
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedRotationRoll, LOCTEXT("RotationX", "X"), true, FLinearColor::White, SNumericEntryBox<float>::RedLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedRotationPitch, LOCTEXT("RotationY", "Y"), true, FLinearColor::White, SNumericEntryBox<float>::GreenLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedRotationYaw, LOCTEXT("RotationZ", "Z"), true, FLinearColor::White, SNumericEntryBox<float>::BlueLabelBackgroundColor)
		]
	];
}

void FPolicyParameterInfoMatrix::CustomizeScale(FDetailWidgetRow& InDetailWidgetRow)
{
	InDetailWidgetRow
	.Visibility(MakeAttributeRaw(this, &FPolicyParameterInfoMatrix::IsParameterVisible))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("ScaleLabel", "Scale"))
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedScaleX, LOCTEXT("ScaleX", "X"), false, FLinearColor::White, SNumericEntryBox<float>::RedLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedScaleY, LOCTEXT("ScaleY", "Y"), false, FLinearColor::White, SNumericEntryBox<float>::GreenLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedScaleZ, LOCTEXT("ScaleZ", "Z"), false, FLinearColor::White, SNumericEntryBox<float>::BlueLabelBackgroundColor)
		]
	];
}

void FPolicyParameterInfoMatrix::FormatTextAndUpdateParameter()
{
	const FVector Translation(*CachedTranslationX, *CachedTranslationY, *CachedTranslationZ);
	const FRotator Rotation(*CachedRotationPitch, *CachedRotationYaw, *CachedRotationRoll);
	const FVector Scale(*CachedScaleX, *CachedScaleY, *CachedScaleZ);
	
	const FMatrix Matrix = FScaleRotationTranslationMatrix(Scale, Rotation, Translation);
	
	const FString MatrixString = DisplayClusterTypesConverter::template ToString(Matrix);
	UpdateCustomParameterValueText(MatrixString);
}


FPolicyParameterInfoRotator::FPolicyParameterInfoRotator(const FString& InDisplayName, const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint,
	UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle) :
	FPolicyParameterInfoFloatReference(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle),
	CachedRotationYaw(MakeShared<float>()),
	CachedRotationPitch(MakeShared<float>()),
	CachedRotationRoll(MakeShared<float>())
{
	const FText TextValue = GetOrAddCustomParameterValueText();
	const FRotator Rotation = DisplayClusterTypesConverter::template FromString<FRotator>(TextValue.ToString());

	*CachedRotationYaw = Rotation.Yaw;
	*CachedRotationPitch = Rotation.Pitch;
	*CachedRotationRoll = Rotation.Roll;
}

void FPolicyParameterInfoRotator::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{
	InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
	.Visibility(MakeAttributeRaw(this, &FPolicyParameterInfoRotator::IsParameterVisible))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("RotationLabel", "Rotation"))
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedRotationRoll, LOCTEXT("RotationX", "X"), true, FLinearColor::White, SNumericEntryBox<float>::RedLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedRotationPitch, LOCTEXT("RotationY", "Y"), true, FLinearColor::White, SNumericEntryBox<float>::GreenLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedRotationYaw, LOCTEXT("RotationZ", "Z"), true, FLinearColor::White, SNumericEntryBox<float>::BlueLabelBackgroundColor)
		]
	];
}

void FPolicyParameterInfoRotator::FormatTextAndUpdateParameter()
{
	const FRotator Rotation(*CachedRotationPitch, *CachedRotationYaw, *CachedRotationRoll);

	const FString RotationString = DisplayClusterTypesConverter::template ToString(Rotation);
	UpdateCustomParameterValueText(RotationString);
}

FPolicyParameterInfoFrustumAngle::FPolicyParameterInfoFrustumAngle(const FString& InDisplayName, const FString& InKey,
	UDisplayClusterBlueprint* InBlueprint, UDisplayClusterConfigurationViewport* InConfigurationViewport,
	const TSharedPtr<IPropertyHandle>& InParametersHandle) :
	FPolicyParameterInfoFloatReference(InDisplayName, InKey, InBlueprint, InConfigurationViewport, InParametersHandle),
	CachedAngleL(MakeShared<float>()),
	CachedAngleR(MakeShared<float>()),
	CachedAngleT(MakeShared<float>()),
	CachedAngleB(MakeShared<float>())
{
	const FString TextValue = GetOrAddCustomParameterValueText().ToString();

	float Left;
	if (DisplayClusterHelpers::str::ExtractValue(TextValue, FString(DisplayClusterProjectionStrings::cfg::manual::AngleL), Left))
	{
		*CachedAngleL = Left;
	}

	float Right;
	if (DisplayClusterHelpers::str::ExtractValue(TextValue, FString(DisplayClusterProjectionStrings::cfg::manual::AngleR), Right))
	{
		*CachedAngleR = Right;
	}

	float Top;
	if (DisplayClusterHelpers::str::ExtractValue(TextValue, FString(DisplayClusterProjectionStrings::cfg::manual::AngleT), Top))
	{
		*CachedAngleT = Top;
	}

	float Bottom;
	if (DisplayClusterHelpers::str::ExtractValue(TextValue, FString(DisplayClusterProjectionStrings::cfg::manual::AngleB), Bottom))
	{
		*CachedAngleB = Bottom;
	}
}

void FPolicyParameterInfoFrustumAngle::CreateCustomRowWidget(IDetailChildrenBuilder& InDetailWidgetRow)
{	InDetailWidgetRow.AddCustomRow(GetParameterDisplayName())
	.Visibility(MakeAttributeRaw(this, &FPolicyParameterInfoFrustumAngle::IsParameterVisible))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(GetParameterDisplayName())
	]
	.ValueContent()
	.MinDesiredWidth(375.0f)
	.MaxDesiredWidth(375.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedAngleL, LOCTEXT("AngleL", "L"), true, FLinearColor::White, SNumericEntryBox<float>::RedLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedAngleR, LOCTEXT("AngleR", "R"), true, FLinearColor::White, SNumericEntryBox<float>::GreenLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedAngleT, LOCTEXT("AngleT", "T"), true, FLinearColor::White, SNumericEntryBox<float>::BlueLabelBackgroundColor)
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(0.0f, 2.0f, 3.0f, 2.0f))
		[
			MakeFloatInputWidget(CachedAngleB, LOCTEXT("AngleB", "B"), true, FLinearColor::White, FLinearColor(0.8f, 0.3f, 0.0f) /* Orange */)
		]
	];
}

void FPolicyParameterInfoFrustumAngle::FormatTextAndUpdateParameter()
{
	const FString AngleLStr = FString::SanitizeFloat(*CachedAngleL);
	const FString AngleRStr = FString::SanitizeFloat(*CachedAngleR);
	const FString AngleTStr = FString::SanitizeFloat(*CachedAngleT);
	const FString AngleBStr = FString::SanitizeFloat(*CachedAngleB);
	
	const FString AngleString = FString::Printf(TEXT("l=%s, r=%s, t=%s, b=%s"), *AngleLStr, *AngleRStr, *AngleTStr, *AngleBStr);

	UpdateCustomParameterValueText(AngleString);
}

#undef LOCTEXT_NAMESPACE
