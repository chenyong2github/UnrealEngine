// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/SIKRetargetAssetBrowser.h"

#include "SPositiveActionButton.h"
#include "AnimPreviewInstance.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/SRetargetAnimAssetsWindow.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"

#define LOCTEXT_NAMESPACE "IKRetargeterAssetBrowser"

void SIKRetargetAssetBrowser::Construct(
	const FArguments& InArgs,
	TSharedRef<FIKRetargetEditorController> InEditorController)
{
	EditorController = InEditorController;
	
	ChildSlot
    [
        SNew(SVerticalBox)
        
        + SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5)
		[
			SNew(SPositiveActionButton)
			.IsEnabled(this, &SIKRetargetAssetBrowser::IsExportButtonEnabled)
			.Icon(FAppStyle::Get().GetBrush("Icons.Save"))
			.Text(LOCTEXT("ExportButtonLabel", "Export Selected Animations"))
			.ToolTipText(LOCTEXT("ExportButtonToolTip", "Generate new retargeted sequence assets on target skeletal mesh (uses current retargeting configuration)."))
			.OnClicked(this, &SIKRetargetAssetBrowser::OnExportButtonClicked)
		]

		+SVerticalBox::Slot()
		[
			SAssignNew(AssetBrowserBox, SBox)
		]
    ];

	AddAssetBrowser();
}

void SIKRetargetAssetBrowser::AddAssetBrowser()
{
	FAssetPickerConfig AssetPickerConfig;

	// setup filtering
	AssetPickerConfig.Filter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
	AssetPickerConfig.Filter.ClassNames.Add(UAnimMontage::StaticClass()->GetFName());
	AssetPickerConfig.Filter.ClassNames.Add(UPoseAsset::StaticClass()->GetFName());
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
	AssetPickerConfig.bAddFilterUI = true;
	AssetPickerConfig.bShowPathInColumnView = true;
	AssetPickerConfig.bShowTypeInColumnView = true;
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SIKRetargetAssetBrowser::OnShouldFilterAsset);
	AssetPickerConfig.DefaultFilterMenuExpansion = EAssetTypeCategories::Animation;
	
	AssetPickerConfig.OnAssetDoubleClicked = FOnAssetSelected::CreateSP(this, &SIKRetargetAssetBrowser::OnAssetDoubleClicked);
	AssetPickerConfig.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	AssetPickerConfig.bAllowNullSelection = false;

	// hide all asset registry columns by default (we only really want the name and path)
	TArray<UObject::FAssetRegistryTag> AssetRegistryTags;
	UAnimSequence::StaticClass()->GetDefaultObject()->GetAssetRegistryTags(AssetRegistryTags);
	for(UObject::FAssetRegistryTag& AssetRegistryTag : AssetRegistryTags)
	{
		AssetPickerConfig.HiddenColumnNames.Add(AssetRegistryTag.Name.ToString());
	}

	// Also hide the type column by default (but allow users to enable it, so don't use bShowTypeInColumnView)
	AssetPickerConfig.HiddenColumnNames.Add(TEXT("Class"));

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	AssetBrowserBox->SetContent(ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig));
}

void SIKRetargetAssetBrowser::OnPathChange(const FString& NewPath)
{
	BatchOutputPath = NewPath;
}

FReply SIKRetargetAssetBrowser::OnExportButtonClicked() const
{
	FIKRetargetEditorController* Controller = EditorController.Pin().Get();
	if (!Controller)
	{
		return FReply::Handled();
	}

	// assemble the data for the assets we want to batch duplicate/retarget
	FIKRetargetBatchOperationContext BatchContext;

	TSharedRef<SSelectExportPathDialog> Dialog = SNew(SSelectExportPathDialog).DefaultAssetPath(FText::FromString(BatchContext.FolderPath));
	if(Dialog->ShowModal() != EAppReturnType::Cancel)
	{
		BatchContext.NameRule.FolderPath = Dialog->GetAssetPath();
	}

	// set the path to export to
	if (!BatchOutputPath.IsEmpty())
	{
		BatchContext.FolderPath = BatchOutputPath;
	}

	// add selected assets to dup/retarget
	TArray<FAssetData> SelectedAssets = GetCurrentSelectionDelegate.Execute();
	for (const FAssetData& Asset : SelectedAssets)
	{
		UE_LOG(LogTemp, Display, TEXT("Duplicating and Retargeting: %s"), *Asset.GetFullName());

		BatchContext.AssetsToRetarget.Add(Asset.GetAsset());
	}

	BatchContext.SourceMesh = Controller->GetSourceSkeletalMesh();
	BatchContext.TargetMesh = Controller->GetTargetSkeletalMesh();
	BatchContext.IKRetargetAsset = Controller->AssetController->GetAsset();
	BatchContext.bRemapReferencedAssets = false;
	BatchContext.NameRule.Suffix = "_Retargeted";

	// actually run the retarget
	FIKRetargetBatchOperation BatchOperation;
	BatchOperation.RunRetarget(BatchContext);

	return FReply::Handled();
	
}

bool SIKRetargetAssetBrowser::IsExportButtonEnabled() const
{
	if (!EditorController.Pin().IsValid())
	{
		return false; // editor in bad state
	}

	const UIKRetargetProcessor* Processor = EditorController.Pin()->GetRetargetProcessor();
	if (!Processor)
	{
		return false; // no retargeter running
	}

	if (!Processor->IsInitialized())
	{
		return false; // retargeter not loaded and valid
	}

	TArray<FAssetData> SelectedAssets = GetCurrentSelectionDelegate.Execute();
	if (SelectedAssets.IsEmpty())
	{
		return false; // nothing selected
	}

	return true;
}

void SIKRetargetAssetBrowser::OnAssetDoubleClicked(const FAssetData& AssetData)
{
	if (!AssetData.GetAsset())
	{
		return;
	}
	
	UAnimationAsset* NewAnimationAsset = Cast<UAnimationAsset>(AssetData.GetAsset());
	if (NewAnimationAsset && EditorController.Pin().IsValid())
	{
		EditorController.Pin()->PlayAnimationAsset(NewAnimationAsset);
	}
}

bool SIKRetargetAssetBrowser::OnShouldFilterAsset(const struct FAssetData& AssetData)
{
	// is this an animation asset?
	if (!AssetData.GetClass()->IsChildOf(UAnimationAsset::StaticClass()))
	{
		return true;
	}
	
	// controller setup
	FIKRetargetEditorController* Controller = EditorController.Pin().Get();
	if (!Controller)
	{
		return true;
	}

	// get source mesh
	USkeletalMesh* SourceMesh = Controller->GetSourceSkeletalMesh();
	if (!SourceMesh)
	{
		return true;
	}

	// get source skeleton
	USkeleton* DesiredSkeleton = SourceMesh->GetSkeleton();
	if (!DesiredSkeleton)
	{
		return true;
	}

	return !DesiredSkeleton->IsCompatibleSkeletonByAssetData(AssetData);
}

#undef LOCTEXT_NAMESPACE
