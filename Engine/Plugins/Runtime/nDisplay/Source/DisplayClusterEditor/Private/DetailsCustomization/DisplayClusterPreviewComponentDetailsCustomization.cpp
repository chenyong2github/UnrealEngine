// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomization/DisplayClusterPreviewComponentDetailsCustomization.h"
#include "Components/DisplayClusterPreviewComponent.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "Async/Async.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"

#include "SSearchableComboBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "FDisplayClusterPreviewComponentDetailsCustomization"


FDisplayClusterPreviewComponentDetailsCustomization::FDisplayClusterPreviewComponentDetailsCustomization()
	: ProjPolicyOptionNone(MakeShared<FString>("None"))
{
}

TSharedRef<IDetailCustomization> FDisplayClusterPreviewComponentDetailsCustomization::MakeInstance()
{
	return MakeShared<FDisplayClusterPreviewComponentDetailsCustomization>();
}

void FDisplayClusterPreviewComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	LayoutBuilder = &InLayoutBuilder;

	// Hide the following categories, we don't really need them
	InLayoutBuilder.HideCategory(TEXT("Tags"));
	InLayoutBuilder.HideCategory(TEXT("Activation"));
	InLayoutBuilder.HideCategory(TEXT("Cooking"));
	InLayoutBuilder.HideCategory(TEXT("AssetUserData"));
	InLayoutBuilder.HideCategory(TEXT("Collision"));
	InLayoutBuilder.HideCategory(TEXT("ComponentReplication"));
	InLayoutBuilder.HideCategory(TEXT("Variable"));

	// Only single selection is allowed
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = InLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	// Store the object we're working with
	EditedObject = Cast<UDisplayClusterPreviewComponent>(SelectedObjects[0].Get());
	if (!EditedObject.IsValid())
	{
		return;
	}

	// Store policy category
	CategoryPolicy = &InLayoutBuilder.EditCategory("Policy");
	check(CategoryPolicy);

	// Store preview category
	CategoryPreview = &InLayoutBuilder.EditCategory("Preview");
	check(CategoryPreview);

	// Store parameters category
	CategoryParameters = &InLayoutBuilder.EditCategory("Parameters");
	check(CategoryParameters);

	RefreshDelegate = FSimpleDelegate::CreateLambda([this]()
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			LayoutBuilder->ForceRefreshDetails();
		});
	});

	// Finally, do the customization
	BuildLayout();
}

void FDisplayClusterPreviewComponentDetailsCustomization::BuildLayout()
{
	// Perform internal initialization
	Initialize();
	// General preview section
	BuildPreview();
	// Selected policy parameters
	BuildParams();
}

void FDisplayClusterPreviewComponentDetailsCustomization::Initialize()
{
	// Hide ProjectionPolicies property, it will be replaced with a combobox
	PropertyProjectionPolicy = LayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterPreviewComponent, ProjectionPolicy), UDisplayClusterPreviewComponent::StaticClass());
	check(PropertyProjectionPolicy->IsValidHandle());
	LayoutBuilder->HideProperty(PropertyProjectionPolicy);

	const bool bProjPolicyInstanceValid = EditedObject->ProjectionPolicyInstance.IsValid();
	const bool bWarpBlendSupported      = bProjPolicyInstanceValid && EditedObject->ProjectionPolicyInstance->IsWarpBlendSupported();

	if (!bProjPolicyInstanceValid)
	{
		LayoutBuilder->HideCategory(FName(*CategoryPreview->GetDisplayName().ToString()));
		LayoutBuilder->HideCategory(FName(*CategoryParameters->GetDisplayName().ToString()));
	}
	else
	{
		// Show bApplyWarpBlend property if selected projection supports this feature
		PropertyApplyWarpBlend = LayoutBuilder->GetProperty(GET_MEMBER_NAME_CHECKED(UDisplayClusterPreviewComponent, bApplyWarpBlend), UDisplayClusterPreviewComponent::StaticClass());
		check(PropertyApplyWarpBlend->IsValidHandle());
		if (!bWarpBlendSupported)
		{
			LayoutBuilder->HideProperty(PropertyApplyWarpBlend);
		}
	}
}

void FDisplayClusterPreviewComponentDetailsCustomization::BuildPreview()
{
	// Build preview category layout
	AddProjectionPolicyRow();
}

void FDisplayClusterPreviewComponentDetailsCustomization::BuildParams()
{
	if (EditedObject->ProjectionPolicyParameters)
	{
		CategoryParameters->AddExternalObjects(TArray<UObject*> { EditedObject->ProjectionPolicyParameters });
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Preview node ID
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterPreviewComponentDetailsCustomization::AddProjectionPolicyRow()
{
	// We need to refresh the 'custom' part of the layout each time the policy selection is changed
	PropertyProjectionPolicy->SetOnPropertyValueChanged(RefreshDelegate);

	// Build proj policy option list
	ProjPolicyOptions.Add(ProjPolicyOptionNone);
	for (const FString& Policy : EditedObject.Get()->ProjectionPolicies)
	{
		ProjPolicyOptions.Add(MakeShared<FString>(Policy));
	}

	// Create GUI representation
	{
		CategoryPolicy->AddCustomRow(PropertyProjectionPolicy->GetPropertyDisplayName())
			.NameContent()
			[
				PropertyProjectionPolicy->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SAssignNew(ProjPolicyComboBox, SSearchableComboBox)
				.OptionsSource(&ProjPolicyOptions)
				.OnGenerateWidget(this, &FDisplayClusterPreviewComponentDetailsCustomization::CreateComboWidget)
				.OnSelectionChanged(this, &FDisplayClusterPreviewComponentDetailsCustomization::OnProjectionPolicyChanged)
				.ContentPadding(2)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &FDisplayClusterPreviewComponentDetailsCustomization::GetSelectedProjPolicyText)
				]
			];
	}

	TSharedPtr<FString>* Found = ProjPolicyOptions.FindByPredicate([this](const TSharedPtr<FString>& Item)
	{
		return Item->Equals(EditedObject->ProjectionPolicy, ESearchCase::IgnoreCase);
	});

	// Set combobox selected item (options list is not empty here)
	ProjPolicyComboBox->SetSelectedItem(Found ? *Found : ProjPolicyOptions[0]);
}

void FDisplayClusterPreviewComponentDetailsCustomization::OnProjectionPolicyChanged(TSharedPtr<FString> ProjPolicy, ESelectInfo::Type SelectInfo)
{
	const FString NewValue = (ProjPolicy.IsValid() ? *ProjPolicy : *ProjPolicyOptionNone);
	PropertyProjectionPolicy->SetValue(NewValue);
}

FText FDisplayClusterPreviewComponentDetailsCustomization::GetSelectedProjPolicyText() const
{
	TSharedPtr<FString> CurSelection = ProjPolicyComboBox->GetSelectedItem();
	return FText::FromString(CurSelection.IsValid() ? *CurSelection : *ProjPolicyOptionNone);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Internals
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedRef<SWidget> FDisplayClusterPreviewComponentDetailsCustomization::CreateComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock).Text(FText::FromString(*InItem));
}

#undef LOCTEXT_NAMESPACE
