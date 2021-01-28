// Copyright Epic Games, Inc. All Rights Reservekd.

#include "IKRigDefinitionDetails.h"
#include "Widgets/Input/SButton.h"
#include "AssetData.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#include "IKRigDefinition.h"
#include "IKRigController.h"

#include "ScopedTransaction.h"
#include "PropertyCustomizationHelpers.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#define LOCTEXT_NAMESPACE	"IKRigDefinitionDetails"

TSharedRef<IDetailCustomization> FIKRigDefinitionDetails::MakeInstance()
{
	return MakeShareable(new FIKRigDefinitionDetails);
}

FIKRigDefinitionDetails::~FIKRigDefinitionDetails()
{
}
void FIKRigDefinitionDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	DetailBuilderWeakPtr = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FIKRigDefinitionDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// get selected IKRigDefinition asset
	const TArray< TWeakObjectPtr<UObject> >& SelectedObjectsList = DetailBuilder.GetSelectedObjects();
	TArray< TWeakObjectPtr<UIKRigDefinition> > SelectedIKRigDefinitions;
	for (auto SelectionIt = SelectedObjectsList.CreateConstIterator(); SelectionIt; ++SelectionIt)
	{
		if (UIKRigDefinition* TestIKRigDefinition = Cast<UIKRigDefinition>(SelectionIt->Get()))
		{
			SelectedIKRigDefinitions.Add(TestIKRigDefinition);
		}
	}
	if (SelectedIKRigDefinitions.Num() > 1)
	{
		return; // we only support 1 asset for now
	}

	// resolve pointer to selected IKRigDefinition
	IKRigDefinition = SelectedIKRigDefinitions[0];
	if (!IKRigDefinition.IsValid())
	{
		return;
	}

	// store controller class (MVC pattern) for UI callbacks to interact with
	IKRigController = UIKRigController::GetControllerByRigDefinition(IKRigDefinition.Get());
	
	//
	// EDIT SKELETON
	//

	IDetailCategoryBuilder& SkeletonCategory = DetailBuilder.EditCategory("Skeleton");
	SkeletonCategory.AddCustomRow(FText::FromString("UpdateSkeleton"))
	.NameContent()
	[
		SNullWidget::NullWidget
	]
	.ValueContent()
		.MaxDesiredWidth(0.0f)
		.MinDesiredWidth(150.0f)
	[
		SNew(SButton)
		.ContentPadding(3)
		.IsEnabled(this, &FIKRigDefinitionDetails::CanImport)
		.OnClicked(this, &FIKRigDefinitionDetails::OnImportHierarchy)
		.ToolTipText(LOCTEXT("OnImportHierarchyTooltip", "Set skeleton to selected asset. This replaces existing skeleton."))
		.Text(LOCTEXT("UpdateHierarchyTitle", "Update Skeleton"))
		.HAlign(HAlign_Center)
	];

	
	SkeletonCategory.AddCustomRow(FText::FromString("Hierarchy"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("SelectSourceSkeleton", "Source Skeleton"))
	]
	.ValueContent()
		.MaxDesiredWidth(0.0f)
		.MinDesiredWidth(200.0f)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor::Gray) // Darken the outer border
		[
			SNew(SBox)
			[
				SNew(SObjectPropertyEntryBox)
				.ObjectPath(this, &FIKRigDefinitionDetails::GetCurrentSourceAsset)
				.OnShouldFilterAsset(this, &FIKRigDefinitionDetails::ShouldFilterAsset)
				.OnObjectChanged(this, &FIKRigDefinitionDetails::OnAssetSelected)
				.AllowClear(false)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
			]
		]
	];
}

//------------------------------------------------------------------
//
// HIERARCHY
//

bool FIKRigDefinitionDetails::CanImport() const
{
	return (SelectedAsset.IsValid());
}

FString FIKRigDefinitionDetails::GetCurrentSourceAsset() const
{
	return GetPathNameSafe(SelectedAsset.IsValid()? SelectedAsset.Get() : nullptr);
}

bool FIKRigDefinitionDetails::ShouldFilterAsset(const FAssetData& AssetData)
{
	return (AssetData.AssetClass != USkeletalMesh::StaticClass()->GetFName() && AssetData.AssetClass != USkeleton::StaticClass()->GetFName());
}

void FIKRigDefinitionDetails::OnAssetSelected(const FAssetData& AssetData)
{
	SelectedAsset = AssetData.GetAsset();
}

FReply FIKRigDefinitionDetails::OnImportHierarchy()
{
	if (SelectedAsset.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("UpdateSkeleton", "Update Skeleton"));
		IKRigDefinition->Modify();

		const FReferenceSkeleton* RefSkeleton = nullptr;
		if (SelectedAsset->IsA(USkeleton::StaticClass()))
		{
			IKRigDefinition->SourceAsset = SelectedAsset.Get();
			RefSkeleton = &(CastChecked<USkeleton>(SelectedAsset)->GetReferenceSkeleton());
		}
		else if (SelectedAsset->IsA(USkeletalMesh::StaticClass()))
		{
			IKRigDefinition->SourceAsset = SelectedAsset.Get();
			RefSkeleton = &(CastChecked<USkeletalMesh>(SelectedAsset)->GetRefSkeleton());
		}

		if (RefSkeleton)
		{
			IKRigController->SetSkeleton(*RefSkeleton);
		}

		// Raw because we don't want to keep alive the details builder when calling the force refresh details
		IDetailLayoutBuilder* DetailLayoutBuilder = DetailBuilderWeakPtr.Pin().Get();
		if (DetailLayoutBuilder)
		{
			DetailLayoutBuilder->ForceRefreshDetails();
		}
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
