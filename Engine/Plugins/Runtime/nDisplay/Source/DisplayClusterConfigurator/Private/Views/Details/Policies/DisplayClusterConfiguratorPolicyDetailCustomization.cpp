// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Details/Policies/DisplayClusterConfiguratorPolicyDetailCustomization.h"

#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorUtils.h"
#include "DisplayClusterConfiguratorPolicyParameterCustomization.h"
#include "Views/Details/Widgets/SDisplayClusterConfigurationSearchableComboBox.h"

#include "DisplayClusterRootActor.h"
#include "Blueprints/DisplayClusterBlueprint.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterICVFX_CineCameraComponent.h"

#include "Camera/CameraComponent.h"

#include "DisplayClusterProjectionStrings.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DisplayClusterTypesConverter.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorPolicyDetailCustomization"

//////////////////////////////////////////////////////////////////////////////////////////////
// Projection Type Customization
//////////////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterConfiguratorProjectionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);
	CustomOption = MakeShared<FString>("Custom");

	// Get the Editing object
	ConfigurationViewportPtr = CastChecked<UDisplayClusterConfigurationViewport>(EditingObject);

	// Store what's currently selected.
	CurrentSelectedPolicy = ConfigurationViewportPtr->ProjectionPolicy.Type;
	
	bIsCustomPolicy = IsCustomTypeInConfig();
	if (bIsCustomPolicy)
	{
		// Load default config
		CustomPolicy = ConfigurationViewportPtr->ProjectionPolicy.Type;
	}
}

void FDisplayClusterConfiguratorProjectionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FDisplayClusterConfiguratorPolymorphicEntityCustomization::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);

	// Hide properties 
	TypeHandle->MarkHiddenByCustomization();
	ParametersHandle->MarkHiddenByCustomization();

	// Add custom rows
	ResetProjectionPolicyOptions();
	AddProjectionPolicyRow();
	AddCustomPolicyRow();

	BuildParametersForPolicy(GetCurrentPolicy());
	
	// Add Parameters property with Visibility handler
	InChildBuilder
		.AddProperty(ParametersHandle.ToSharedRef())
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDisplayClusterConfiguratorProjectionCustomization::GetCustomRowsVisibility)))
		.ShouldAutoExpand(true);
}

TSharedRef<SWidget> FDisplayClusterConfiguratorProjectionCustomization::MakeProjectionPolicyOptionComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

EVisibility FDisplayClusterConfiguratorProjectionCustomization::GetCustomRowsVisibility() const
{
	return bIsCustomPolicy ? EVisibility::Visible :  EVisibility::Collapsed;
}

void FDisplayClusterConfiguratorProjectionCustomization::ResetProjectionPolicyOptions()
{
	ProjectionPolicyOptions.Reset();

	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	for (const FString& ProjectionPolicy : UDisplayClusterConfigurationData::ProjectionPolicies)
	{
		ProjectionPolicyOptions.Add(MakeShared<FString>(ProjectionPolicy));
	}

	// Add Custom option
	if (!bIsCustomPolicy)
	{
		ProjectionPolicyOptions.Add(CustomOption);
	}

	if (ProjectionPolicyComboBox.IsValid())
	{
		// Refreshes the available options now that the shared array has been updated.
		
		ProjectionPolicyComboBox->ResetOptionsSource();
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

void FDisplayClusterConfiguratorProjectionCustomization::OnProjectionPolicySelected(TSharedPtr<FString> InPolicy, ESelectInfo::Type SelectInfo)
{
	if (InPolicy.IsValid())
	{
		FString SelectedPolicy = *InPolicy.Get();

		UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
		check(ConfigurationViewport != nullptr);

		ConfigurationViewport->Modify();
		ModifyBlueprint();
		
		if (SelectedPolicy.Equals(*CustomOption.Get()))
		{
			bIsCustomPolicy = true;
			CustomPolicy = ConfigurationViewport->ProjectionPolicy.Type;
			IsCustomHandle->SetValue(true);
		}
		else
		{
			bIsCustomPolicy = false;
			IsCustomHandle->SetValue(false);
			
			TypeHandle->SetValue(SelectedPolicy);

			if (CurrentSelectedPolicy.ToLower() != SelectedPolicy.ToLower())
			{
				// Reset when going from custom to another policy.
				ensure(ParametersHandle->AsMap()->Empty() == FPropertyAccess::Result::Success);
			}
		}

		CurrentSelectedPolicy = SelectedPolicy;

		RefreshBlueprint();
		PropertyUtilities.Pin()->ForceRefresh();
	}
	else
	{
		CurrentSelectedPolicy.Reset();
	}
}

FText FDisplayClusterConfiguratorProjectionCustomization::GetSelectedProjectionPolicyText() const
{
	return FText::FromString(GetCurrentPolicy());
}

FText FDisplayClusterConfiguratorProjectionCustomization::GetCustomPolicyText() const
{
	return FText::FromString(CustomPolicy);
}

const FString& FDisplayClusterConfiguratorProjectionCustomization::GetCurrentPolicy() const
{
	if (bIsCustomPolicy)
	{
		return *CustomOption.Get();
	}

	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	return ConfigurationViewport->ProjectionPolicy.Type;
}

bool FDisplayClusterConfiguratorProjectionCustomization::IsCustomTypeInConfig() const
{
	UDisplayClusterConfigurationViewport* ConfigurationViewport = ConfigurationViewportPtr.Get();
	check(ConfigurationViewport != nullptr);

	if (ConfigurationViewport->ProjectionPolicy.bIsCustom)
	{
		return true;
	}
	
	for (const FString& ProjectionPolicy : UDisplayClusterConfigurationData::ProjectionPolicies)
	{
		if (ConfigurationViewport->ProjectionPolicy.Type.ToLower().Equals(ProjectionPolicy.ToLower()))
		{
			return false;
		}
	}

	return true;
}

void FDisplayClusterConfiguratorProjectionCustomization::OnTextCommittedInCustomPolicyText(const FText& InValue, ETextCommit::Type CommitType)
{
	if (!bIsCustomPolicy)
	{
		// Can be hit if this textbox was selected but the user switched the policy out of CustomPolicy.
		return;
	}
	
	CustomPolicy = InValue.ToString();
	TypeHandle->SetValue(CustomPolicy);

	// Check if the custom config same as any of the ProjectionPolicies configs
	// Turning this off for now in case users want to customize individual parameters..
	// Uncomment this to auto select a default policy if the user types one in the custom name field.
	/*
	for (const FString& ProjectionPolicy : UDisplayClusterConfigurationData::ProjectionPoliсies)
	{
		if (CustomPolicy.Equals(ProjectionPolicy))
		{
			bIsCustomPolicy = false;

			ProjectionPolicyComboBox->SetSelectedItem(MakeShared<FString>(CustomPolicy));

			break;
		}
	}
	*/
}

void FDisplayClusterConfiguratorProjectionCustomization::BuildParametersForPolicy(const FString& Policy)
{
	CustomPolicyParameters.Reset();

	UDisplayClusterBlueprint* Blueprint = FDisplayClusterConfiguratorUtils::FindBlueprintFromObject(EditingObject);
	check(Blueprint);

	const FString PolicyLower = Policy.ToLower();
	/*
	 * Simple
	 */
	if (PolicyLower == DisplayClusterProjectionStrings::projection::Simple)
	{
		CreateSimplePolicy(Blueprint);
	}
	/*
	 * Camera
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::Camera)
	{
		CreateCameraPolicy(Blueprint);
	}
	/*
	 * Mesh
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::Mesh)
	{
		CreateMeshPolicy(Blueprint);
	}
	/*
	 * Domeprojection
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::Domeprojection)
	{
		CreateDomePolicy(Blueprint);
	}
	/*
	 * VIOSO
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::VIOSO)
	{
		CreateVIOSOPolicy(Blueprint);
	}
	/*
	 * EasyBlend
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::EasyBlend)
	{
		CreateEasyBlendPolicy(Blueprint);
	}
	/*
	 * Manual
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::Manual)
	{
		CreateManualPolicy(Blueprint);
	}
	/*
	 * MPCDI
	 */
	else if (PolicyLower == DisplayClusterProjectionStrings::projection::MPCDI)
	{
		CreateMPCDIPolicy(Blueprint);
	}
	
	// Create the row widgets.
	for (TSharedPtr<FPolicyParameterInfo>& Param : CustomPolicyParameters)
	{
		Param->CreateCustomRowWidget(*ChildBuilder);
	}
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateSimplePolicy(UDisplayClusterBlueprint* Blueprint)
{
	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoComponentCombo>(
		"Screen",
		DisplayClusterProjectionStrings::cfg::simple::Screen,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ UDisplayClusterScreenComponent::StaticClass() }));
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateCameraPolicy(UDisplayClusterBlueprint* Blueprint)
{
	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoComponentCombo>(
		"Camera",
		DisplayClusterProjectionStrings::cfg::camera::Component,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ UCameraComponent::StaticClass(),
			UDisplayClusterICVFX_CineCameraComponent::StaticClass() }));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoBool>(
		"Native",
		DisplayClusterProjectionStrings::cfg::camera::Native,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle));
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateMeshPolicy(UDisplayClusterBlueprint* Blueprint)
{
	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoComponentCombo>(
		"Mesh",
		DisplayClusterProjectionStrings::cfg::mesh::Component,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ UStaticMeshComponent::StaticClass() }));
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateDomePolicy(UDisplayClusterBlueprint* Blueprint)
{
	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
		"File",
		DisplayClusterProjectionStrings::cfg::domeprojection::File,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<FString>{"xml"}));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoComponentCombo>(
		"Origin",
		DisplayClusterProjectionStrings::cfg::domeprojection::Origin,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ USceneComponent::StaticClass() }));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoNumber<int32>>(
		"Channel",
		DisplayClusterProjectionStrings::cfg::domeprojection::Channel,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle));
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateVIOSOPolicy(UDisplayClusterBlueprint* Blueprint)
{
	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
		"File",
		DisplayClusterProjectionStrings::cfg::VIOSO::File,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<FString>{"vwf"}));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoComponentCombo>(
		"Origin",
		DisplayClusterProjectionStrings::cfg::VIOSO::Origin,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ USceneComponent::StaticClass() }));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoMatrix>(
		"Matrix",
		DisplayClusterProjectionStrings::cfg::VIOSO::BaseMatrix,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle));
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateEasyBlendPolicy(UDisplayClusterBlueprint* Blueprint)
{
	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
		"File",
		DisplayClusterProjectionStrings::cfg::easyblend::File,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<FString>{"pol", "ol"}));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoComponentCombo>(
		"Origin",
		DisplayClusterProjectionStrings::cfg::easyblend::Origin,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ USceneComponent::StaticClass() }));

	CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoNumber<float>>(
		"Scale",
		DisplayClusterProjectionStrings::cfg::easyblend::Scale,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle));
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateManualPolicy(UDisplayClusterBlueprint* Blueprint)
{
	check(Blueprint);
	
	const FString RenderingKey = "ManualRendering";
	const FString RenderingMono = "Mono";
	const FString RenderingStereo = "Stereo";
	const FString RenderingMonoStereo = "Mono & Stereo";

	const FString FrustumKey = "ManualFrustum";
	const FString FrustumMatrix = "Matrix";
	const FString FrustumAngles = "Angles";

	auto RefreshPolicy = [this](const FString& SelectedItem)
	{
		PropertyUtilities.Pin()->ForceRefresh();
	};

	const bool bSort = false;
	
	const TSharedPtr<FPolicyParameterInfoCombo> RenderingCombo = MakeShared<FPolicyParameterInfoCombo>(
		"Rendering",
		RenderingKey,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<FString>{RenderingMono, RenderingStereo, RenderingMonoStereo},
		& RenderingMono,
		bSort);
	RenderingCombo->SetOnSelectedDelegate(FPolicyParameterInfoCombo::FOnItemSelected::CreateLambda(RefreshPolicy));
	CustomPolicyParameters.Add(RenderingCombo);

	const TSharedPtr<FPolicyParameterInfoCombo> FrustumCombo = MakeShared<FPolicyParameterInfoCombo>(
		"Frustum",
		FrustumKey,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<FString>{FrustumMatrix, FrustumAngles},
		& FrustumMatrix,
		bSort);
	FrustumCombo->SetOnSelectedDelegate(FPolicyParameterInfoCombo::FOnItemSelected::CreateLambda(RefreshPolicy));
	CustomPolicyParameters.Add(FrustumCombo);

	/*
	 * Rotation
	 */
	{
		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoRotator>(
			"Rotation",
			DisplayClusterProjectionStrings::cfg::manual::Rotation,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle));
	}
	
	/*
	 * Matrices
	 */
	{
		auto IsMatrixVisible = [RenderingCombo, RenderingMono, RenderingMonoStereo, FrustumCombo, FrustumMatrix]() -> bool
		{
			const FString RenderSetting = RenderingCombo->GetOrAddCustomParameterValueText().ToString();
			const FString FrustumSetting = FrustumCombo->GetOrAddCustomParameterValueText().ToString();

			const bool bVisible = (*RenderSetting == RenderingMono && *FrustumSetting == FrustumMatrix)
				|| (*RenderSetting == RenderingMonoStereo && *FrustumSetting == FrustumMatrix);

			return bVisible;
		};

		if (IsMatrixVisible())
		{
			const TSharedPtr<FPolicyParameterInfoMatrix> MatrixPolicy = MakeShared<FPolicyParameterInfoMatrix>(
				"Matrix",
				DisplayClusterProjectionStrings::cfg::manual::Matrix,
				Blueprint,
				ConfigurationViewportPtr.Get(),
				ParametersHandle);
			CustomPolicyParameters.Add(MatrixPolicy);
		}
	}

	{
		auto IsMatrixLeftRightVisible = [RenderingCombo, RenderingStereo, RenderingMonoStereo, FrustumCombo, FrustumMatrix]() -> bool
		{
			const FString RenderSetting = RenderingCombo->GetOrAddCustomParameterValueText().ToString();
			const FString FrustumSetting = FrustumCombo->GetOrAddCustomParameterValueText().ToString();

			const bool bVisible = (*RenderSetting == RenderingStereo && *FrustumSetting == FrustumMatrix)
				|| (*RenderSetting == RenderingMonoStereo && *FrustumSetting == FrustumMatrix);

			return bVisible;
		};

		if (IsMatrixLeftRightVisible())
		{
			const TSharedPtr<FPolicyParameterInfoMatrix> MatrixLeftPolicy = MakeShared<FPolicyParameterInfoMatrix>(
				"MatrixLeft",
				DisplayClusterProjectionStrings::cfg::manual::MatrixLeft,
				Blueprint,
				ConfigurationViewportPtr.Get(),
				ParametersHandle);
			CustomPolicyParameters.Add(MatrixLeftPolicy);

			const TSharedPtr<FPolicyParameterInfoMatrix> MatrixRightPolicy =
				MakeShared<FPolicyParameterInfoMatrix>(
					"MatrixRight",
					DisplayClusterProjectionStrings::cfg::manual::MatrixRight,
					Blueprint,
					ConfigurationViewportPtr.Get(),
					ParametersHandle);
			CustomPolicyParameters.Add(MatrixRightPolicy);
		}
	}

	/*
	 * Frustums
	 */
	{
		auto IsFrustumVisible = [RenderingCombo, RenderingMono, RenderingMonoStereo, FrustumCombo, FrustumAngles]() -> bool
		{
			const FString RenderSetting = RenderingCombo->GetOrAddCustomParameterValueText().ToString();
			const FString FrustumSetting = FrustumCombo->GetOrAddCustomParameterValueText().ToString();

			const bool bVisible = ((*RenderSetting == RenderingMono || *RenderSetting == RenderingMonoStereo) && *FrustumSetting == FrustumAngles);
			return bVisible;
		};

		if (IsFrustumVisible())
		{
			CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFrustumAngle>(
				"Frustum",
				DisplayClusterProjectionStrings::cfg::manual::Frustum,
				Blueprint,
				ConfigurationViewportPtr.Get(),
				ParametersHandle));
		}

		auto IsLeftRightFrustumVisible = [RenderingCombo, RenderingStereo, RenderingMonoStereo, FrustumCombo, FrustumAngles]() -> bool
		{
			const FString RenderSetting = RenderingCombo->GetOrAddCustomParameterValueText().ToString();
			const FString FrustumSetting = FrustumCombo->GetOrAddCustomParameterValueText().ToString();

			const bool bVisible = ((*RenderSetting == RenderingStereo || *RenderSetting == RenderingMonoStereo) && *FrustumSetting == FrustumAngles);
			return bVisible;
		};

		if (IsLeftRightFrustumVisible())
		{
			CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFrustumAngle>(
				"FrustumLeft",
				DisplayClusterProjectionStrings::cfg::manual::FrustumLeft,
				Blueprint,
				ConfigurationViewportPtr.Get(),
				ParametersHandle));

			CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFrustumAngle>(
				"FrustumRight",
				DisplayClusterProjectionStrings::cfg::manual::FrustumRight,
				Blueprint,
				ConfigurationViewportPtr.Get(),
				ParametersHandle));
		}
	}
}

void FDisplayClusterConfiguratorProjectionCustomization::CreateMPCDIPolicy(UDisplayClusterBlueprint* Blueprint)
{
	check(Blueprint);

	const FString MPCDITypeKey = "MPCDIType";
	const FString TypeMPCDI = "MPCDI";
	const FString TypePFM = "Explicit PFM";
	
	auto RefreshPolicy = [this](const FString& SelectedItem)
	{
		PropertyUtilities.Pin()->ForceRefresh();
	};

	const bool bSort = false;

	const TSharedPtr<FPolicyParameterInfoCombo> MPCDICombo = MakeShared<FPolicyParameterInfoCombo>(
		"MPCDI Type",
		MPCDITypeKey,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<FString>{TypeMPCDI, TypePFM},
		&TypeMPCDI,
		bSort);
	MPCDICombo->SetOnSelectedDelegate(FPolicyParameterInfoCombo::FOnItemSelected::CreateLambda(RefreshPolicy));
	CustomPolicyParameters.Add(MPCDICombo);
	
	const FString Setting = MPCDICombo->GetOrAddCustomParameterValueText().ToString();
	if (Setting == TypeMPCDI)
	{
		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
			"File",
			DisplayClusterProjectionStrings::cfg::mpcdi::File,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle,
			TArray<FString>{"mpcdi"}));

		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoText>(
			"Buffer",
			DisplayClusterProjectionStrings::cfg::mpcdi::Buffer,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle));

		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoText>(
			"Region",
			DisplayClusterProjectionStrings::cfg::mpcdi::Region,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle));
	}
	else if (Setting == TypePFM)
	{
		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
			"File",
			DisplayClusterProjectionStrings::cfg::mpcdi::FilePFM,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle,
			TArray<FString>{"pfm"}));

		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
			"Alpha Mask",
			DisplayClusterProjectionStrings::cfg::mpcdi::FileAlpha,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle,
			TArray<FString>{"png"}));
		
		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoNumber<float>>(
			"Alpha Gamma",
			DisplayClusterProjectionStrings::cfg::mpcdi::AlphaGamma,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle,
			1.f));

		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoFile>(
			"Beta Mask",
			DisplayClusterProjectionStrings::cfg::mpcdi::FileBeta,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle,
			TArray<FString>{"png"}));

		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoNumber<float>>(
			"Scale",
			DisplayClusterProjectionStrings::cfg::mpcdi::WorldScale,
			Blueprint,
			ConfigurationViewportPtr.Get(), 
			ParametersHandle,
			1.f));
		
		CustomPolicyParameters.Add(MakeShared<FPolicyParameterInfoBool>(
			"Use Unreal Axis",
			DisplayClusterProjectionStrings::cfg::mpcdi::UseUnrealAxis,
			Blueprint,
			ConfigurationViewportPtr.Get(),
			ParametersHandle));
	}
	
	TSharedPtr<FPolicyParameterInfoCombo> Origin = MakeShared<FPolicyParameterInfoComponentCombo>(
		"Origin",
		DisplayClusterProjectionStrings::cfg::mpcdi::Origin,
		Blueprint,
		ConfigurationViewportPtr.Get(),
		ParametersHandle,
		TArray<TSubclassOf<UActorComponent>>{ USceneComponent::StaticClass() });
}

#undef LOCTEXT_NAMESPACE
