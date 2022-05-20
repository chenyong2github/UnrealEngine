// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetEditorController.h"

#include "AnimPose.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "EditorModeManager.h"
#include "IContentBrowserSingleton.h"
#include "Widgets/Input/SButton.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "RetargetEditor/IKRetargetAnimInstance.h"
#include "RetargetEditor/IKRetargetDefaultMode.h"
#include "RetargetEditor/IKRetargetEditPoseMode.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/SIKRetargetChainMapList.h"
#include "Retargeter/IKRetargeter.h"
#include "RigEditor/SIKRigOutputLog.h"
#include "RigEditor/IKRigController.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "IKRetargetEditorController"

void FIKRetargetEditorController::Initialize(TSharedPtr<FIKRetargetEditor> InEditor, UIKRetargeter* InAsset)
{
	Editor = InEditor;
	AssetController = UIKRetargeterController::GetController(InAsset);
	AssetController->SetEditorController(this);

	// bind callbacks when SOURCE or TARGET IK Rigs are modified
	BindToIKRigAsset(AssetController->GetAsset()->GetSourceIKRigWriteable());
	BindToIKRigAsset(AssetController->GetAsset()->GetTargetIKRigWriteable());

	// bind callback when retargeter needs reinitialized
	AssetController->OnRetargeterNeedsInitialized().AddSP(this, &FIKRetargetEditorController::OnRetargeterNeedsInitialized);
}

void FIKRetargetEditorController::BindToIKRigAsset(UIKRigDefinition* InIKRig) const
{
	if (!InIKRig)
	{
		return;
	}

	UIKRigController* Controller = UIKRigController::GetIKRigController(InIKRig);
	if (!Controller->OnIKRigNeedsInitialized().IsBoundToObject(this))
	{
		Controller->OnIKRigNeedsInitialized().AddSP(this, &FIKRetargetEditorController::OnIKRigNeedsInitialized);
		Controller->OnRetargetChainRenamed().AddSP(this, &FIKRetargetEditorController::OnRetargetChainRenamed);
		Controller->OnRetargetChainRemoved().AddSP(this, &FIKRetargetEditorController::OnRetargetChainRemoved);
	}
}

void FIKRetargetEditorController::OnIKRigNeedsInitialized(UIKRigDefinition* ModifiedIKRig) const
{
	const UIKRetargeter* Retargeter = AssetController->GetAsset();
	
	check(ModifiedIKRig && Retargeter)
	 
	const bool bIsSource = ModifiedIKRig == Retargeter->GetSourceIKRig();
	const bool bIsTarget = ModifiedIKRig == Retargeter->GetTargetIKRig();
	if (!(bIsSource || bIsTarget))
	{
		return;
	}

	// the target anim instance has the IK RetargetPoseFromMesh node which needs reinitialized with new asset version
	ClearOutputLog();
	TargetAnimInstance->SetProcessorNeedsInitialized();
	RefreshAllViews();
}

void FIKRetargetEditorController::OnRetargetChainRenamed(UIKRigDefinition* ModifiedIKRig, FName OldName, FName NewName) const
{
	check(ModifiedIKRig)
	
	AssetController->OnRetargetChainRenamed(ModifiedIKRig, OldName, NewName);
}

void FIKRetargetEditorController::OnRetargetChainRemoved(UIKRigDefinition* ModifiedIKRig, const FName& InChainRemoved) const
{
	check(ModifiedIKRig)
	AssetController->OnRetargetChainRemoved(ModifiedIKRig, InChainRemoved);
	RefreshAllViews();
}

void FIKRetargetEditorController::OnRetargeterNeedsInitialized(const UIKRetargeter* Retargeter) const
{
	// clear the output log
	ClearOutputLog();
	// force reinit the runtime retarget processor
	TargetAnimInstance->SetProcessorNeedsInitialized();
	// refresh all the UI views
	RefreshAllViews();
}

void FIKRetargetEditorController::AddOffsetAndUpdatePreviewMeshPosition(
	const FVector& Offset,
	USceneComponent* Component) const
{
	UIKRetargeter* Asset = AssetController->GetAsset();
	FVector Position;
	float Scale;
	if (Component == TargetSkelMeshComponent)
	{
		Asset->TargetMeshOffset += Offset;
		Position = Asset->TargetMeshOffset;
		Scale = Asset->TargetMeshScale;
	}
	else
	{
		Asset->SourceMeshOffset += Offset;
		Position = Asset->SourceMeshOffset;
		Scale = 1.0f;
	}

	constexpr bool bSweep = false;
	constexpr FHitResult* OutSweepHitResult = nullptr;
	constexpr ETeleportType Teleport = ETeleportType::ResetPhysics;
	Component->SetWorldLocation(Position, bSweep, OutSweepHitResult, Teleport);
	Component->SetWorldScale3D(FVector(Scale,Scale,Scale));
}

USkeletalMesh* FIKRetargetEditorController::GetSourceSkeletalMesh() const
{
	return AssetController ? AssetController->GetSourcePreviewMesh() : nullptr;
}

USkeletalMesh* FIKRetargetEditorController::GetTargetSkeletalMesh() const
{
	return AssetController ? AssetController->GetTargetPreviewMesh() : nullptr;
}

const USkeleton* FIKRetargetEditorController::GetSourceSkeleton() const
{
	if (const USkeletalMesh* Mesh = GetSourceSkeletalMesh())
	{
		return Mesh->GetSkeleton();
	}
	
	return nullptr;
}

const USkeleton* FIKRetargetEditorController::GetTargetSkeleton() const
{
	if (const USkeletalMesh* Mesh = GetTargetSkeletalMesh())
	{
		return Mesh->GetSkeleton();
	}
	
	return nullptr;
}

const TArray<FName>& FIKRetargetEditorController::GetSelectedBones() const
{
	if (const FIKRetargetEditPoseMode* EditPoseMode = Editor.Pin()->GetEditorModeManager().GetActiveModeTyped<FIKRetargetEditPoseMode>(FIKRetargetEditPoseMode::ModeName))
	{
		return EditPoseMode->GetSelectedBones();
	}
	
	static TArray<FName> Empty;
	return Empty;
}

FTransform FIKRetargetEditorController::GetTargetBoneGlobalTransform(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex) const
{
	check(RetargetProcessor && RetargetProcessor->IsInitialized())

	// get transform of bone
	FTransform BoneTransform = RetargetProcessor->GetTargetBoneRetargetPoseGlobalTransform(TargetBoneIndex);

	// scale and offset
	BoneTransform.ScaleTranslation(AssetController->GetAsset()->TargetMeshScale);
	BoneTransform.AddToTranslation(AssetController->GetAsset()->TargetMeshOffset);
	BoneTransform.NormalizeRotation();

	return BoneTransform;
}

FTransform FIKRetargetEditorController::GetTargetBoneLocalTransform(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex) const
{
	check(RetargetProcessor && RetargetProcessor->IsInitialized())

	return RetargetProcessor->GetTargetBoneRetargetPoseLocalTransform(TargetBoneIndex);
}

bool FIKRetargetEditorController::GetTargetBoneLineSegments(
	const UIKRetargetProcessor* RetargetProcessor,
	const int32& TargetBoneIndex,
	FVector& OutStart,
	TArray<FVector>& OutChildren) const
{
	// get the runtime processor
	check(RetargetProcessor && RetargetProcessor->IsInitialized())
	
	// get the target skeleton we want to draw
	const FTargetSkeleton& TargetSkeleton = RetargetProcessor->GetTargetSkeleton();
	check(TargetSkeleton.BoneNames.IsValidIndex(TargetBoneIndex))

	// get the origin of the bone chain
	OutStart = TargetSkeleton.RetargetGlobalPose[TargetBoneIndex].GetTranslation();

	// get children
	TArray<int32> ChildIndices;
	TargetSkeleton.GetChildrenIndices(TargetBoneIndex, ChildIndices);
	for (const int32& ChildIndex : ChildIndices)
	{
		OutChildren.Emplace(TargetSkeleton.RetargetGlobalPose[ChildIndex].GetTranslation());
	}

	// add the target translation offset and scale
	const UIKRetargeter* Asset = AssetController->GetAsset();
	OutStart *= Asset->TargetMeshScale;
	OutStart += Asset->TargetMeshOffset;
	for (FVector& ChildPoint : OutChildren)
	{
		ChildPoint *= Asset->TargetMeshScale;
		ChildPoint += Asset->TargetMeshOffset;
	}
	
	return true;
}

UIKRetargetProcessor* FIKRetargetEditorController::GetRetargetProcessor() const
{	
	if(UIKRetargetAnimInstance* AnimInstance = TargetAnimInstance.Get())
	{
		return AnimInstance->GetRetargetProcessor();
	}

	return nullptr;	
}

void FIKRetargetEditorController::ResetIKPlantingState() const
{
	if (UIKRetargetProcessor* Processor = GetRetargetProcessor())
	{
		Processor->ResetPlanting();
	}
}

void FIKRetargetEditorController::ClearOutputLog() const
{
	if (OutputLogView.IsValid())
	{
		OutputLogView.Get()->ClearLog();
	}
}

void FIKRetargetEditorController::RefreshAllViews() const
{
	Editor.Pin()->RegenerateMenusAndToolbars();
	DetailsView->ForceRefresh();

	// cannot assume chains view is always available
	if (ChainsView.IsValid())
	{
		ChainsView.Get()->RefreshView();
	}

	// refresh the asset browser to ensure it shows compatible sequences
	if (AssetBrowserView.IsValid())
	{
		AssetBrowserView.Get()->RefreshView();
	}
}

void FIKRetargetEditorController::PlayAnimationAsset(UAnimationAsset* AssetToPlay)
{
	if (AssetToPlay && SourceAnimInstance.IsValid())
	{
		SourceAnimInstance->SetAnimationAsset(AssetToPlay);
		PreviousAsset = AssetToPlay;
		// tell asset to output the retargeted pose
		AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::RunRetarget);
	}
}

void FIKRetargetEditorController::PlayPreviousAnimationAsset() const
{
	if (PreviousAsset)
	{
		SourceAnimInstance->SetAnimationAsset(PreviousAsset);
		// tell asset to output the retarget
		AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::RunRetarget);
	}
}

void FIKRetargetEditorController::HandleGoToRetargetPose() const
{
	Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);
	Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetDefaultMode::ModeName);

	// put source back in ref pose
	SourceSkelMeshComponent->ShowReferencePose(true);
	// have to move component back to offset position because ShowReferencePose() sets it back to origin
	AddOffsetAndUpdatePreviewMeshPosition(FVector::ZeroVector, SourceSkelMeshComponent);
	// tell asset to output the retarget pose
	AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::ShowRetargetPose);
}

void FIKRetargetEditorController::HandleEditPose() const
{
	if (IsEditingPose())
	{
		// stop pose editing
		Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetEditPoseMode::ModeName);
		Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetDefaultMode::ModeName);
		AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::RunRetarget);
		
		// must reinitialize after editing the retarget pose
		AssetController->BroadcastNeedsReinitialized();
		// continue playing whatever animation asset was last used
		PlayPreviousAnimationAsset();
	}
	else
	{
		// start pose editing
		Editor.Pin()->GetEditorModeManager().DeactivateMode(FIKRetargetDefaultMode::ModeName);
		Editor.Pin()->GetEditorModeManager().ActivateMode(FIKRetargetEditPoseMode::ModeName);
		AssetController->GetAsset()->SetOutputMode(ERetargeterOutputMode::EditRetargetPose);
	}
}

bool FIKRetargetEditorController::CanEditPose() const
{
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	if (!Processor)
	{
		return false;
	}

	return Processor->IsInitialized();
}

bool FIKRetargetEditorController::IsEditingPose() const
{
	return AssetController->GetAsset()->GetOutputMode() == ERetargeterOutputMode::EditRetargetPose;
}

void FIKRetargetEditorController::HandleNewPose()
{
	// get a unique pose name to use as suggestion
	const FString DefaultNewPoseName = LOCTEXT("NewRetargetPoseName", "CustomRetargetPose").ToString();
	const FName UniqueNewPoseName = AssetController->MakePoseNameUnique(DefaultNewPoseName);
	
	SAssignNew(NewPoseWindow, SWindow)
	.Title(LOCTEXT("NewRetargetPoseOptions", "Create New Retarget Pose"))
	.ClientSize(FVector2D(300, 80))
	.HasCloseButton(true)
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SAssignNew(NewPoseEditableText, SEditableTextBox)
				.MinDesiredWidth(275)
				.Text(FText::FromName(UniqueNewPoseName))
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &FIKRetargetEditorController::CreateNewPose)
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [Window=NewPoseWindow]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	];

	GEditor->EditorAddModalWindow(NewPoseWindow.ToSharedRef());
	NewPoseWindow.Reset();
}

bool FIKRetargetEditorController::CanCreatePose() const
{
	return !IsEditingPose();
}

FReply FIKRetargetEditorController::CreateNewPose() const
{
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	AssetController->AddRetargetPose(NewPoseName);
	NewPoseWindow->RequestDestroyWindow();
	DetailsView->ForceRefresh();
	return FReply::Handled();
}

void FIKRetargetEditorController::HandleDuplicatePose()
{
	// get a unique pose name to use as suggestion for duplicate
	const FString DuplicateSuffix = LOCTEXT("DuplicateSuffix", "_Copy").ToString();
	FString CurrentPoseName = GetCurrentPoseName().ToString();
	const FString DefaultDuplicatePoseName = CurrentPoseName.Append(*DuplicateSuffix);
	const FName UniqueNewPoseName = AssetController->MakePoseNameUnique(DefaultDuplicatePoseName);
	
	SAssignNew(NewPoseWindow, SWindow)
	.Title(LOCTEXT("DuplicateRetargetPoseOptions", "Duplicate Retarget Pose"))
	.ClientSize(FVector2D(300, 80))
	.HasCloseButton(true)
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SAssignNew(NewPoseEditableText, SEditableTextBox)
				.MinDesiredWidth(275)
				.Text(FText::FromName(UniqueNewPoseName))
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.OnClicked(this, &FIKRetargetEditorController::CreateDuplicatePose)
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [Window=NewPoseWindow]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(NewPoseWindow.ToSharedRef());
	NewPoseWindow.Reset();
}

FReply FIKRetargetEditorController::CreateDuplicatePose() const
{
	const FIKRetargetPose& PoseToDuplicate = AssetController->GetCurrentRetargetPose();
	const FName NewPoseName = FName(NewPoseEditableText.Get()->GetText().ToString());
	AssetController->AddRetargetPose(NewPoseName, &PoseToDuplicate);
	NewPoseWindow->RequestDestroyWindow();
	DetailsView->ForceRefresh();
	return FReply::Handled();
}

void FIKRetargetEditorController::HandleImportPose()
{
	RetargetPoseToImport = nullptr;

	// load the content browser module to display an asset picker
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	// the asset picker will only show animation sequences
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassNames.Add(URetargetPose::StaticClass()->GetFName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &FIKRetargetEditorController::OnRetargetPoseSelected);
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::Tile;

	ImportPoseWindow = SNew(SWindow)
	.Title(LOCTEXT("ImportRetargetPose", "Import Retarget Pose"))
	.ClientSize(FVector2D(500, 600))
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.Padding(4)
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			]
			
			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("ImportButtonLabel", "Import New Retarget Pose"))
					.OnClicked(this, &FIKRetargetEditorController::ImportRetargetPose)
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [Window=ImportPoseWindow]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(ImportPoseWindow.ToSharedRef());
	ImportPoseWindow.Reset();
}

FReply FIKRetargetEditorController::ImportRetargetPose() const
{
	ImportPoseWindow->RequestDestroyWindow();
	
	if (RetargetPoseToImport.IsNull())
	{
		return FReply::Handled();
	}

	const TObjectPtr<URetargetPose> RetargetPose = Cast<URetargetPose>(RetargetPoseToImport.TryLoad());
	if (!RetargetPose)
	{
		return FReply::Handled();
	}

	// create a new pose with the data from the selected retarget pose asset
	FIKRetargetPose Pose;
	RetargetPose->GetAsRetargetPose(Pose);
	AssetController->AddRetargetPose(FName(RetargetPose->GetName()), &Pose);
	
	RefreshAllViews();
	
	return FReply::Unhandled();
}

void FIKRetargetEditorController::OnRetargetPoseSelected(const FAssetData& SelectedAsset)
{
	RetargetPoseToImport = SelectedAsset.ToSoftObjectPath();
}

void FIKRetargetEditorController::HandleImportPoseFromSequence()
{
	SequenceToImportAsPose = nullptr;

	// get a unique pose name to use as suggestion
	const FString DefaultImportedPoseName = LOCTEXT("ImportedRetargetPoseName", "ImportedRetargetPose").ToString();
	const FName UniqueNewPoseName = AssetController->MakePoseNameUnique(DefaultImportedPoseName);
	ImportedPoseName = FText::FromName(UniqueNewPoseName);

	// load the content browser module to display an asset picker
	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	// the asset picker will only show animation sequences compatible with the preview mesh
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
	AssetPickerConfig.Filter.ClassNames.Add(UAnimMontage::StaticClass()->GetFName());
	AssetPickerConfig.Filter.ClassNames.Add(UPoseAsset::StaticClass()->GetFName());
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
	AssetPickerConfig.bAddFilterUI = true;
	AssetPickerConfig.bShowPathInColumnView = true;
	AssetPickerConfig.bShowTypeInColumnView = true;
	AssetPickerConfig.DefaultFilterMenuExpansion = EAssetTypeCategories::Animation;
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &FIKRetargetEditorController::OnShouldFilterSequenceToImport);
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &FIKRetargetEditorController::OnSequenceSelectedForPose);
	AssetPickerConfig.bAllowNullSelection = false;

	// hide all asset registry columns by default (we only really want the name and path)
	TArray<UObject::FAssetRegistryTag> AssetRegistryTags;
	UAnimSequence::StaticClass()->GetDefaultObject()->GetAssetRegistryTags(AssetRegistryTags);
	FName ColumnToKeep = FName("Number of Frames");
	for(UObject::FAssetRegistryTag& AssetRegistryTag : AssetRegistryTags)
	{
		if (AssetRegistryTag.Name != ColumnToKeep)
		{
			AssetPickerConfig.HiddenColumnNames.Add(AssetRegistryTag.Name.ToString());
		}
	}

	// Also hide the type column by default (but allow users to enable it, so don't use bShowTypeInColumnView)
	AssetPickerConfig.HiddenColumnNames.Add(TEXT("Class"));
	AssetPickerConfig.HiddenColumnNames.Add(TEXT("HasVirtualizedData"));
	AssetPickerConfig.HiddenColumnNames.Add(TEXT("DiskSize"));

	// create pop-up window for user to select animation sequence asset to import as a retarget pose]
	ImportPoseFromSequenceWindow = SNew(SWindow)
	.Title(LOCTEXT("ImportRetargetPose", "Import Retarget Pose from Sequence Asset"))
	.ClientSize(FVector2D(500, 600))
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.Padding(4)
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ImportFrame_Label", "Sequence Frame: "))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.Padding(2.0f, 0.0f)
				[
					SNew(SNumericEntryBox<int32>)
					.ToolTipText(LOCTEXT("ArrayIndex", "Frame of sequence to import pose from."))
					.AllowSpin(true)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.MinValue(0)
					.Value_Lambda([this]
					{
						return FrameOfSequenceToImport;
					})
					.OnValueChanged(SNumericEntryBox<int32>::FOnValueChanged::CreateLambda([this](int32 Value)
					{
						FrameOfSequenceToImport = Value;
					}))
				]
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ImportName_Label", "Pose Name: "))
				]
				
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SEditableTextBox)
					.Text(FText::FromName(UniqueNewPoseName))
					.OnTextChanged(FOnTextChanged::CreateLambda([this](const FText InText)
					{
						ImportedPoseName = InText;
					}))
				]
			]
			
			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("ImportButtonLabel", "Import As Retarget Pose"))
					.OnClicked(this, &FIKRetargetEditorController::OnImportPoseFromSequence)
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [Window=ImportPoseFromSequenceWindow]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(ImportPoseFromSequenceWindow.ToSharedRef());
	ImportPoseFromSequenceWindow.Reset();
}

bool FIKRetargetEditorController::OnShouldFilterSequenceToImport(const struct FAssetData& AssetData) const
{
	// is this an animation asset?
	if (!AssetData.IsInstanceOf(UAnimationAsset::StaticClass()))
	{
		return true;
	}

	// get target skeleton
	constexpr bool bEditingTarget = true; // TODO in future allow editing source or target
	const USkeleton* DesiredSkeleton = bEditingTarget ? GetTargetSkeleton() : GetSourceSkeleton();
	if (!DesiredSkeleton)
	{
		return true;
	}

	return !DesiredSkeleton->IsCompatibleSkeletonByAssetData(AssetData);
}

FReply FIKRetargetEditorController::OnImportPoseFromSequence()
{
	ImportPoseFromSequenceWindow->RequestDestroyWindow();
	
	if (SequenceToImportAsPose.IsNull())
	{
		return FReply::Handled();
	}

	const TObjectPtr<UAnimSequence> AnimSequence = Cast<UAnimSequence>(SequenceToImportAsPose.TryLoad());
	if (!AnimSequence)
	{
		return FReply::Handled();
	}
	
	USkeletalMesh* Mesh = GetTargetSkeletalMesh();
	if (!Mesh)
	{
		return FReply::Handled();
	}

	UIKRetargetProcessor* Retargeter = GetRetargetProcessor();
	if (!Retargeter)
	{
		return FReply::Handled();
	}
	
	// ensure we evaluate the source animation using the skeletal mesh proportions that were evaluated in the viewport
	FAnimPoseEvaluationOptions EvaluationOptions = FAnimPoseEvaluationOptions();
	EvaluationOptions.OptionalSkeletalMesh = Mesh;

	FrameOfSequenceToImport = FMath::Clamp(FrameOfSequenceToImport, 0, AnimSequence->GetNumberOfSampledKeys());
	
	// evaluate the sequence at the desired frame
	FAnimPose ImportedPose;
	UAnimPoseExtensions::GetAnimPoseAtFrame(AnimSequence, FrameOfSequenceToImport, EvaluationOptions, ImportedPose);

	// record delta pose for all bones being retargeted
	FIKRetargetPose ImportedRetargetPose;
	
	// get all imported bone transforms and record them in the retarget pose
	FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	const FTargetSkeleton& TargetSkeleton = Retargeter->GetTargetSkeleton();
	int32 NumBones = RefSkeleton.GetNum();
	const int32 RootBoneIndex = Retargeter->GetTargetSkeletonRootBone();
	for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		const FName& BoneName = RefSkeleton.GetBoneName(BoneIndex);
		const int32 RetargetBoneIndex = TargetSkeleton.FindBoneIndexByName(BoneName);
		const bool bIsRetargetRoot = RetargetBoneIndex == RootBoneIndex;
		
		// if this is the retarget root, we want to record the translation delta as well
		if (bIsRetargetRoot)
		{
			const FTransform GlobalTransformImported = UAnimPoseExtensions::GetBonePose(ImportedPose, BoneName, EAnimPoseSpaces::World);
			const FTransform GlobalTransformReference = UAnimPoseExtensions::GetRefBonePose(ImportedPose, BoneName, EAnimPoseSpaces::World);
			const FVector TranslationDelta = GlobalTransformImported.GetLocation() - GlobalTransformReference.GetLocation();
			ImportedRetargetPose.RootTranslationOffset = TranslationDelta;

			// rotation offsets are interpreted as relative to the parent (local), but in the case of the retarget root bone,
			// when we generate the retarget pose, it's parents will be left at ref pose, so we need to generate a local
			// rotation offset relative to the ref pose parent, NOT the (potentially) posed parent transform from the animation.
			FTransform GlobalParentTransformInRefPose = FTransform::Identity;
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				const FName& ParentBoneName = RefSkeleton.GetBoneName(ParentIndex);
				GlobalParentTransformInRefPose = UAnimPoseExtensions::GetRefBonePose(ImportedPose, ParentBoneName, EAnimPoseSpaces::World);
			}

			// this is a bit crazy, but we have to generate a delta rotation in the local space of the retarget root
			// bone while treating the root bone as being in global space since the retarget pose does not consider any
			// bones above it.
			const FQuat GlobalDeltaRotation = GlobalTransformImported.GetRotation() * GlobalTransformReference.GetRotation().Inverse();
			const FQuat BoneGlobalOrig = GlobalTransformReference.GetRotation();
			const FQuat BoneGlobalPlusOffset = GlobalDeltaRotation * BoneGlobalOrig;
			const FQuat ParentInv = GlobalParentTransformInRefPose.GetRotation().Inverse();
			const FQuat BoneLocal = ParentInv * BoneGlobalOrig;
			const FQuat BoneLocalPlusOffset = ParentInv * BoneGlobalPlusOffset;
			const FQuat BoneLocalOffset = BoneLocal * BoneLocalPlusOffset.Inverse();
			
			ImportedRetargetPose.BoneRotationOffsets.Add(BoneName, BoneLocalOffset.Inverse());
		}
		else
		{
			// record the delta rotation
			const FTransform LocalTransformImported = UAnimPoseExtensions::GetBonePose(ImportedPose, BoneName, EAnimPoseSpaces::Local);
			const FTransform LocalTransformReference = RefPose[BoneIndex];
			const FQuat DeltaRotation = LocalTransformImported.GetRotation() * LocalTransformReference.GetRotation().Inverse();
			// only if it's difference than the ref pose
			if (DeltaRotation.GetAngle() > KINDA_SMALL_NUMBER)
			{
				ImportedRetargetPose.BoneRotationOffsets.Add(BoneName, DeltaRotation);
			}
		}
	}

	// store the newly imported retarget pose in the asset
	AssetController->AddRetargetPose(FName(ImportedPoseName.ToString()), &ImportedRetargetPose);
	
	return FReply::Unhandled();
}

void FIKRetargetEditorController::OnSequenceSelectedForPose(const FAssetData& SelectedAsset)
{
	SequenceToImportAsPose = SelectedAsset.ToSoftObjectPath();
}

void FIKRetargetEditorController::HandleExportPose()
{
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	SaveAssetDialogConfig.DefaultPath = AssetController->GetAsset()->GetPackage()->GetPathName();
	SaveAssetDialogConfig.DefaultAssetName = GetCurrentPoseName().ToString();
	SaveAssetDialogConfig.AssetClassNames.Add(URetargetPose::StaticClass()->GetFName());
	SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
	SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("ExportRetargetPoseDialogTitle", "Export Retarget Pose");

	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
	if (SaveObjectPath.IsEmpty())
	{
		return;
	}

	const FString PackagePath = FPackageName::ObjectPathToPackageName(SaveObjectPath);
	const FString AssetName = FPaths::GetBaseFilename(PackagePath, true);
	URetargetPoseFactory* NewFactory = NewObject<URetargetPoseFactory>();
	const FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	URetargetPose* NewPoseAsset =  Cast<URetargetPose>(AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, URetargetPose::StaticClass(), NewFactory));
	TArray<UObject*> ObjectsToSync;
	ObjectsToSync.Add(NewPoseAsset);
	GEditor->SyncBrowserToObjects(ObjectsToSync);
	
	// fill new pose asset with existing pose data
	const FIKRetargetPose& CurrentRetargetPose = AssetController->GetCurrentRetargetPose();
	NewPoseAsset->RootTranslationOffset = CurrentRetargetPose.RootTranslationOffset;
	NewPoseAsset->BoneRotationOffsets = CurrentRetargetPose.BoneRotationOffsets;
}

void FIKRetargetEditorController::HandleDeletePose() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	AssetController->RemoveRetargetPose(CurrentPose);
	DetailsView->ForceRefresh();
}

bool FIKRetargetEditorController::CanDeletePose() const
{	
	// cannot delete default pose
	const bool bNotUsingDefaultPose = AssetController->GetCurrentRetargetPoseName() != UIKRetargeter::GetDefaultPoseName();
	// cannot delete pose while editing
	return bNotUsingDefaultPose && !IsEditingPose();
}

void FIKRetargetEditorController::HandleResetAllBones() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	static TArray<FName> Empty; // empty list will reset all bones
	AssetController->ResetRetargetPose(CurrentPose, Empty);
}

void FIKRetargetEditorController::HandleResetSelectedBones() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	AssetController->ResetRetargetPose(CurrentPose, GetSelectedBones());
}

void FIKRetargetEditorController::HandleResetSelectedAndChildrenBones() const
{
	const FName CurrentPose = AssetController->GetCurrentRetargetPoseName();
	const TArray<FName>& SelectedBones = GetSelectedBones();
	
	// get list of all children of selected bones
	const UIKRetargetProcessor* Processor = GetRetargetProcessor();
	const FTargetSkeleton& Skeleton = Processor->GetTargetSkeleton();
	TArray<int32> AllChildrenIndices;
	for (const FName& SelectedBone : SelectedBones)
	{
		const int32 SelectedBoneIndex = Skeleton.FindBoneIndexByName(SelectedBone);
		Skeleton.GetChildrenIndicesRecursive(SelectedBoneIndex, AllChildrenIndices);
	}

	// merge total list of all selected bones and their children
	TArray<FName> BonesToReset = SelectedBones;
	for (const int32 ChildIndex : AllChildrenIndices)
	{
		BonesToReset.AddUnique(Skeleton.BoneNames[ChildIndex]);
	}
	
	// reset the bones
	AssetController->ResetRetargetPose(CurrentPose, BonesToReset);
}

bool FIKRetargetEditorController::CanResetSelected() const
{
	return CanResetPose() && !GetSelectedBones().IsEmpty();
}

bool FIKRetargetEditorController::CanResetPose() const
{
	// only allow reseting pose while editing to avoid confusion
	return IsEditingPose();
}

void FIKRetargetEditorController::HandleRenamePose()
{
	SAssignNew(RenamePoseWindow, SWindow)
	.Title(LOCTEXT("RenameRetargetPoseOptions", "Rename Retarget Pose"))
	.ClientSize(FVector2D(250, 80))
	.HasCloseButton(true)
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FAppStyle::GetBrush("Menu.Background") )
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SAssignNew(NewNameEditableText, SEditableTextBox)
				.Text(GetCurrentPoseName())
			]

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("OkButtonLabel", "Ok"))
					.IsEnabled_Lambda([this]()
					{
						return !GetCurrentPoseName().EqualTo(NewNameEditableText.Get()->GetText());
					})
					.OnClicked(this, &FIKRetargetEditorController::RenamePose)
				]
				
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "Button")
					.TextStyle( FAppStyle::Get(), "DialogButtonText" )
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
					.OnClicked_Lambda( [Window=RenamePoseWindow]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]	
		]
	];

	GEditor->EditorAddModalWindow(RenamePoseWindow.ToSharedRef());
	RenamePoseWindow.Reset();
}

FReply FIKRetargetEditorController::RenamePose() const
{
	const FName NewPoseName = FName(NewNameEditableText.Get()->GetText().ToString());
	RenamePoseWindow->RequestDestroyWindow();
	
	AssetController->RenameCurrentRetargetPose(NewPoseName);
	DetailsView->ForceRefresh();
	return FReply::Handled();
}

bool FIKRetargetEditorController::CanRenamePose() const
{
	// cannot rename default pose
	const bool bNotUsingDefaultPose = AssetController->GetCurrentRetargetPoseName() != UIKRetargeter::GetDefaultPoseName();
	// cannot rename pose while editing
	return bNotUsingDefaultPose && !IsEditingPose();
}

FText FIKRetargetEditorController::GetCurrentPoseName() const
{
	return FText::FromName(AssetController->GetCurrentRetargetPoseName());
}

void FIKRetargetEditorController::OnPoseSelected(TSharedPtr<FName> InPosePose, ESelectInfo::Type SelectInfo) const
{
	AssetController->SetCurrentRetargetPose(*InPosePose.Get());
}

#undef LOCTEXT_NAMESPACE
