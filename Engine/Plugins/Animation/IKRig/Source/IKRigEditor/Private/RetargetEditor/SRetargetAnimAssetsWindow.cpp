// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/SRetargetAnimAssetsWindow.h"

#include "AnimationBlueprintLibrary.h"
#include "AnimPose.h"
#include "AnimPreviewInstance.h"
#include "ContentBrowserModule.h"
#include "EditorReimportHandler.h"
#include "IContentBrowserSingleton.h"
#include "ObjectEditorUtils.h"
#include "SSkeletonWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SSeparator.h"
#include "PropertyCustomizationHelpers.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EditorFramework/AssetImportData.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopedSlowTask.h"
#include "Retargeter/IKRetargeter.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "RetargetAnimAssetWindow"

int32 FIKRetargetAnimAssetsContext::GenerateAssetLists()
{
	// re-generate lists of selected and referenced assets
	AnimationAssetsToRetarget.Reset();
	AnimBlueprintsToRetarget.Reset();

	for (TWeakObjectPtr<UObject> AssetPtr : AssetsToRetarget)
	{
		UObject* Asset = AssetPtr.Get();
		if (UAnimationAsset* AnimAsset = Cast<UAnimationAsset>(Asset))
		{
			AnimationAssetsToRetarget.AddUnique(AnimAsset);
		}
		else if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset))
		{
			// Add parents blueprint. 
			UAnimBlueprint* ParentBP = Cast<UAnimBlueprint>(AnimBlueprint->ParentClass->ClassGeneratedBy);
			while (ParentBP)
			{
				AnimBlueprintsToRetarget.AddUnique(ParentBP);
				ParentBP = Cast<UAnimBlueprint>(ParentBP->ParentClass->ClassGeneratedBy);
			}
				
			AnimBlueprintsToRetarget.AddUnique(AnimBlueprint);				
		}
	}

	if (bRemapReferencedAssets)
	{
		// Grab assets from the blueprint.
		// Do this first as it can add complex assets to the retarget array which will need to be processed next.
		for (UAnimBlueprint* AnimBlueprint : AnimBlueprintsToRetarget)
		{
			GetAllAnimationSequencesReferredInBlueprint(AnimBlueprint, AnimationAssetsToRetarget);
		}

		int32 AssetIndex = 0;
		while (AssetIndex < AnimationAssetsToRetarget.Num())
		{
			UAnimationAsset* AnimAsset = AnimationAssetsToRetarget[AssetIndex++];
			AnimAsset->HandleAnimReferenceCollection(AnimationAssetsToRetarget, true);
		}
	}

	return AnimationAssetsToRetarget.Num();
}

void FIKRetargetAnimAssetsContext::DuplicateRetargetAssets()
{
	UPackage* DestinationPackage = TargetMesh->GetOutermost();

	TArray<UAnimationAsset*> AnimationAssetsToDuplicate = AnimationAssetsToRetarget;
	TArray<UAnimBlueprint*> AnimBlueprintsToDuplicate = AnimBlueprintsToRetarget;

	// We only want to duplicate unmapped assets, so we remove mapped assets from the list we're duplicating
	for (TPair<UAnimationAsset*, UAnimationAsset*>& Pair : RemappedAnimAssets)
	{
		AnimationAssetsToDuplicate.Remove(Pair.Key);
	}

	DuplicatedAnimAssets = DuplicateAssets<UAnimationAsset>(AnimationAssetsToDuplicate, DestinationPackage, &NameRule);
	DuplicatedBlueprints = DuplicateAssets<UAnimBlueprint>(AnimBlueprintsToDuplicate, DestinationPackage, &NameRule);

	// If we are moving the new asset to a different directory we need to fixup the reimport path.
	// This should only effect source FBX paths within the project.
	if (!NameRule.FolderPath.IsEmpty())
	{
		for (TPair<UAnimationAsset*, UAnimationAsset*>& Pair : DuplicatedAnimAssets)
		{
			UAnimSequence* SourceSequence = Cast<UAnimSequence>(Pair.Key);
			UAnimSequence* DestinationSequence = Cast<UAnimSequence>(Pair.Value);
			if (!(SourceSequence && DestinationSequence))
			{
				continue;
			}
			
			for (int index = 0; index < SourceSequence->AssetImportData->SourceData.SourceFiles.Num(); index++)
			{
				const FString& RelativeFilename = SourceSequence->AssetImportData->SourceData.SourceFiles[index].RelativeFilename;
				const FString OldPackagePath = FPackageName::GetLongPackagePath(SourceSequence->GetPathName()) / TEXT("");
				const FString NewPackagePath = FPackageName::GetLongPackagePath(DestinationSequence->GetPathName()) / TEXT("");
				const FString AbsoluteSrcPath = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(OldPackagePath));
				const FString SrcFile = AbsoluteSrcPath / RelativeFilename;
				const bool bSrcFileExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*SrcFile);
				if (!bSrcFileExists || (NewPackagePath == OldPackagePath))
				{
					continue;
				}

				FString BasePath = FPackageName::LongPackageNameToFilename(OldPackagePath);
				FString OldSourceFilePath = FPaths::ConvertRelativePathToFull(BasePath, RelativeFilename);
				TArray<FString> Paths;
				Paths.Add(OldSourceFilePath);
				
				// update the FBX reimport file path
				FReimportManager::Instance()->UpdateReimportPaths(DestinationSequence, Paths);
			}
		}
	}

	// Remapped assets needs the duplicated ones added
	RemappedAnimAssets.Append(DuplicatedAnimAssets);

	DuplicatedAnimAssets.GenerateValueArray(AnimationAssetsToRetarget);
	DuplicatedBlueprints.GenerateValueArray(AnimBlueprintsToRetarget);
}

void FIKRetargetAnimAssetsContext::RetargetAssets()
{
	USkeleton* OldSkeleton = SourceMesh->GetSkeleton();
	USkeleton* NewSkeleton = TargetMesh->GetSkeleton();

	for (UAnimationAsset* AssetToRetarget : AnimationAssetsToRetarget)
	{
		// synchronize curves between old/new asset
		UAnimSequence* AnimSequenceToRetarget = Cast<UAnimSequence>(AssetToRetarget);
		if (AnimSequenceToRetarget)
		{
			// copy curve data from source asset, preserving data in the target if present.
			UAnimationBlueprintLibrary::CopyAnimationCurveNamesToSkeleton(OldSkeleton, NewSkeleton, AnimSequenceToRetarget, ERawCurveTrackTypes::RCT_Float);	
			// clear transform curves since those curves won't work in new skeleton
			IAnimationDataController& Controller = AnimSequenceToRetarget->GetController();
			Controller.RemoveAllCurvesOfType(ERawCurveTrackTypes::RCT_Transform);
		}

		// replace references to other animation
		AssetToRetarget->ReplaceReferredAnimations(RemappedAnimAssets);
		AssetToRetarget->SetSkeleton(NewSkeleton);
		AssetToRetarget->MarkPackageDirty();
	}

	// convert the animation using the IK retargeter
	ConvertAnimation();

	// convert all Animation Blueprints and compile 
	for (UAnimBlueprint* AnimBlueprint : AnimBlueprintsToRetarget)
	{
		// replace skeleton
		AnimBlueprint->TargetSkeleton = NewSkeleton;

		// if they have parent blueprint, make sure to re-link to the new one also
		UAnimBlueprint* CurrentParentBP = Cast<UAnimBlueprint>(AnimBlueprint->ParentClass->ClassGeneratedBy);
		if (CurrentParentBP)
		{
			UAnimBlueprint* const * ParentBP = DuplicatedBlueprints.Find(CurrentParentBP);
			if (ParentBP)
			{
				AnimBlueprint->ParentClass = (*ParentBP)->GeneratedClass;
			}
		}

		if(RemappedAnimAssets.Num() > 0)
		{
			ReplaceReferredAnimationsInBlueprint(AnimBlueprint, RemappedAnimAssets);
		}

		FBlueprintEditorUtils::RefreshAllNodes(AnimBlueprint);
		FKismetEditorUtilities::CompileBlueprint(AnimBlueprint, EBlueprintCompileOptions::SkipGarbageCollection);
		AnimBlueprint->PostEditChange();
		AnimBlueprint->MarkPackageDirty();
	}
}

void FIKRetargetAnimAssetsContext::ConvertAnimation()
{
	// initialize the retargeter
	UObject* TransientOuter = Cast<UObject>(GetTransientPackage());
	UIKRetargeter* Retargeter = DuplicateObject(IKRetargetAsset, TransientOuter);	
	Retargeter->Initialize(SourceMesh, TargetMesh, TransientOuter);
	if (!Retargeter->bIsLoadedAndValid)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unable to initialize the IK Retargeter. Newly created animations were not retargeted!"));
		return;
	}

	// for each pair of source / target animation sequences
	for (TPair<UAnimationAsset*, UAnimationAsset*>& Pair : DuplicatedAnimAssets)
	{
		UAnimSequence* SourceSequence = Cast<UAnimSequence>(Pair.Key);
		UAnimSequence* DestinationSequence = Cast<UAnimSequence>(Pair.Value);
		if (!(SourceSequence && DestinationSequence))
		{
			continue;
		}

		// remove all keys from the destination animation sequence
		IAnimationDataController& TargetSeqController = DestinationSequence->GetController();
		TargetSeqController.RemoveAllBoneTracks();

		// number of frames in this animation
		const int32 NumFrames = SourceSequence->GetNumberOfSampledKeys();

		// make space for the target keyframe data
		const int32 NumTargetBones = Retargeter->TargetSkeleton.BoneNames.Num();
		TArray<FRawAnimSequenceTrack> BoneTracks;
		BoneTracks.SetNumZeroed(NumTargetBones);

		// retarget each frame's pose from source to target
		for (int32 FrameIndex=0; FrameIndex<NumFrames; ++FrameIndex)
		{
			// get the source global pose
			FAnimPose SourcePoseAtFrame;
			UAnimPoseExtensions::GetAnimPoseAtFrame(SourceSequence, FrameIndex, FAnimPoseEvaluationOptions(), SourcePoseAtFrame);
			TArray<FName> BoneNames;
			UAnimPoseExtensions::GetBoneNames(SourcePoseAtFrame,BoneNames);
			TArray<FTransform> SourceComponentPose;
			for (const FName& BoneName : BoneNames)
			{
				FTransform BonePose = UAnimPoseExtensions::GetBonePose(SourcePoseAtFrame, BoneName, EAnimPoseSpaces::World);
				SourceComponentPose.Add(BonePose);
			}

			// run the retarget
			TArray<FTransform>& TargetComponentPose = Retargeter->RunRetargeter(SourceComponentPose);

			// convert to a local-space pose
			TArray<FTransform> TargetLocalPose = TargetComponentPose;
			Retargeter->TargetSkeleton.UpdateLocalTransformsBelowBone(0,TargetLocalPose, TargetComponentPose);

			// store key data for each bone
			for (int32 TargetBoneIndex=0; TargetBoneIndex<TargetLocalPose.Num(); ++TargetBoneIndex)
			{
				BoneTracks[TargetBoneIndex].PosKeys.Add(TargetLocalPose[TargetBoneIndex].GetLocation());
				BoneTracks[TargetBoneIndex].RotKeys.Add(TargetLocalPose[TargetBoneIndex].GetRotation());
				BoneTracks[TargetBoneIndex].ScaleKeys.Add(TargetLocalPose[TargetBoneIndex].GetScale3D());
			}
		}

		// add keys to bone tracks
		const bool bShouldTransact = false;
		for (int32 TargetBoneIndex=0; TargetBoneIndex<NumTargetBones; ++TargetBoneIndex)
		{
			FName TargetBoneName = Retargeter->TargetSkeleton.BoneNames[TargetBoneIndex];
			FRawAnimSequenceTrack& RawTrack = BoneTracks[TargetBoneIndex];
			TargetSeqController.AddBoneTrack(TargetBoneName, bShouldTransact);
			TargetSeqController.SetBoneTrackKeys(TargetBoneName, RawTrack.PosKeys, RawTrack.RotKeys, RawTrack.ScaleKeys);
		}
	}
}

void FIKRetargetAnimAssetsContext::NotifyUserOfResults() const
{
	// gather newly created objects
	TArray<UObject*> NewAssets;
	GetNewAssets(NewAssets);

	// log details of what assets were created
	for (UObject* NewAsset : NewAssets)
	{
		UE_LOG(LogTemp, Display, TEXT("Duplicate and Retarget - New Asset Created: %s"), *NewAsset->GetName());
	}
	
	// notify user
	FNotificationInfo Notification(FText::GetEmpty());
	Notification.ExpireDuration = 5.f;
	Notification.Text = FText::Format(
		LOCTEXT("MultiNonDuplicatedAsset", "{0} assets were retargeted to new skeleton {1}. See Output for details."),
		FText::AsNumber(NewAssets.Num()),
		FText::FromString(TargetMesh->GetName()));
	FSlateNotificationManager::Get().AddNotification(Notification);
	
	// select all new assets
	TArray<FAssetData> CurrentSelection;
	for(UObject* NewObject : NewAssets)
	{
		CurrentSelection.Add(FAssetData(NewObject));
	}

	// show assets in browser
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().SyncBrowserToAssets(CurrentSelection);
}

void FIKRetargetAnimAssetsContext::GetNewAssets(TArray<UObject*>& NewAssets) const
{
	TArray<UAnimationAsset*> NewAnims;
	DuplicatedAnimAssets.GenerateValueArray(NewAnims);
	for (UAnimationAsset* NewAnim : NewAnims)
	{
		NewAssets.Add(Cast<UObject>(NewAnim));
	}

	TArray<UAnimBlueprint*> NewBlueprints;
	DuplicatedBlueprints.GenerateValueArray(NewBlueprints);
	for (UAnimBlueprint* NewBlueprint : NewBlueprints)
	{
		NewAssets.Add(Cast<UObject>(NewBlueprint));
	}
}

void FIKRetargetAnimAssetsContext::Reset()
{
	SourceMesh = nullptr;
	TargetMesh = nullptr;
	IKRetargetAsset = nullptr;
	bRemapReferencedAssets = true;
	NameRule.Prefix = "";
	NameRule.Suffix = "";
	NameRule.ReplaceFrom = "";
	NameRule.ReplaceTo = "";
}

bool FIKRetargetAnimAssetsContext::IsValid() const
{
	// todo validate compatibility
	return SourceMesh && TargetMesh && IKRetargetAsset && (SourceMesh != TargetMesh);
}

void FIKRetargetAnimAssetsContext::RunRetarget()
{
	// todo progess bar not showing up, why?
	//FScopedSlowTask Progress(2.f, LOCTEXT("GatheringBatchRetarget", "Gathering animation assets..."));
	//Progress.MakeDialog();
	
	const int32 NumAssets = GenerateAssetLists();
	
	//Progress.EnterProgressFrame(1.f, FText::Format(LOCTEXT("DuplicatingBatchRetarget", "Duplicating {0} animation assets..."), NumAssets));

	DuplicateRetargetAssets();
	
	//Progress.EnterProgressFrame(1.f, FText::Format(LOCTEXT("RunningBatchRetarget", "Retargeting {0} animation assets..."), NumAssets));

	RetargetAssets();
	
	NotifyUserOfResults();
}

/**
* Duplicates the supplied AssetsToDuplicate and returns a map of original asset to duplicate. Templated wrapper that calls DuplicateAssetInternal.
*
* @param	AssetsToDuplicate	The animations to duplicate
* @param	DestinationPackage	The package that the duplicates should be placed in
*
* @return	TMap of original animation to duplicate
*/
template<class AssetType>
TMap<AssetType*, AssetType*> FIKRetargetAnimAssetsContext::DuplicateAssets(
	const TArray<AssetType*>& AssetsToDuplicate,
	UPackage* DestinationPackage,
	const FNameDuplicationRule* NameRule)
{
	TArray<UObject*> Assets;
	for (AssetType* Asset : AssetsToDuplicate)
	{
		Assets.Add(Asset);
	}

	// duplicate assets
	TMap<UObject*, UObject*> DuplicateAssetsMap = DuplicateAssetsInternal(Assets, DestinationPackage, NameRule);

	// cast to AssetType
	TMap<AssetType*, AssetType*> ReturnMap;
	for (const TTuple<UObject*, UObject*>& DuplicateAsset : DuplicateAssetsMap)
	{
		ReturnMap.Add(Cast<AssetType>(DuplicateAsset.Key), Cast<AssetType>(DuplicateAsset.Value));
	}
	
	return ReturnMap;
}

void SRetargetPoseViewport::Construct(const FArguments& InArgs)
{
	SEditorViewport::Construct(SEditorViewport::FArguments());

	PreviewComponent = NewObject<UDebugSkelMeshComponent>();
	PreviewComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	PreviewScene.AddComponent(PreviewComponent, FTransform::Identity);

	SetSkeletalMesh(InArgs._SkeletalMesh);
}

void SRetargetPoseViewport::SetSkeletalMesh(USkeletalMesh* InSkeltalMesh)
{
	if(InSkeltalMesh == Mesh)
	{
		return;
	}
	
	Mesh = InSkeltalMesh;

	if(Mesh)
	{
		PreviewComponent->SetSkeletalMesh(Mesh);
		PreviewComponent->EnablePreview(true, nullptr);
		// todo add IK retargeter and set it to output the retarget pose
		PreviewComponent->PreviewInstance->SetForceRetargetBasePose(true);
		PreviewComponent->RefreshBoneTransforms(nullptr);

		//Place the camera at a good viewer position
		FBoxSphereBounds Bounds = Mesh->GetBounds();
		Client->FocusViewportOnBox(Bounds.GetBox(), true);
	}
	else
	{
		PreviewComponent->SetSkeletalMesh(nullptr);
	}

	Client->Invalidate();
}

SRetargetPoseViewport::SRetargetPoseViewport()
: PreviewScene(FPreviewScene::ConstructionValues())
{
}

bool SRetargetPoseViewport::IsVisible() const
{
	return true;
}

TSharedRef<FEditorViewportClient> SRetargetPoseViewport::MakeEditorViewportClient()
{
	TSharedPtr<FEditorViewportClient> EditorViewportClient = MakeShareable(new FRetargetPoseViewportClient(PreviewScene, SharedThis(this)));

	EditorViewportClient->ViewportType = LVT_Perspective;
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	EditorViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);

	EditorViewportClient->SetRealtime(false);
	EditorViewportClient->VisibilityDelegate.BindSP(this, &SRetargetPoseViewport::IsVisible);
	EditorViewportClient->SetViewMode(VMI_Lit);

	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SRetargetPoseViewport::MakeViewportToolbar()
{
	return nullptr;
}


TSharedPtr<SWindow> SRetargetAnimAssetsWindow::DialogWindow;

void SRetargetAnimAssetsWindow::Construct(const FArguments& InArgs)
{
	AssetThumbnailPool = MakeShareable( new FAssetThumbnailPool(1024) );
	
	this->ChildSlot
	[
		SNew (SHorizontalBox)
		
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.AutoWidth()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Top)
			.Padding(0, 5)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DuplicateAndRetarget_SourceTitle", "Source Skeletal Mesh"))
						.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.BoldFont"))
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(5, 5)
					[
						SAssignNew(SourceViewport, SRetargetPoseViewport)
						.SkeletalMesh(RetargetContext.SourceMesh)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(5, 5)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(USkeletalMesh::StaticClass())
						.AllowClear(true)
						.DisplayUseSelected(true)
						.DisplayBrowse(true)
						.DisplayThumbnail(true)
						.ThumbnailPool(AssetThumbnailPool)
						.IsEnabled_Lambda([this]()
						{
							if (!RetargetContext.IKRetargetAsset)
							{
								return false;
							}
							
							return RetargetContext.IKRetargetAsset->SourceIKRigAsset != nullptr;
						})
						.ObjectPath(this, &SRetargetAnimAssetsWindow::GetCurrentSourceMeshPath)
						.OnObjectChanged(this, &SRetargetAnimAssetsWindow::SourceMeshAssigned)
						.OnShouldFilterAsset_Lambda([this](const FAssetData& AssetData)
						{
							if (!RetargetContext.IKRetargetAsset)
							{
								return true;
							}
							
							USkeletalMesh* Mesh = Cast<USkeletalMesh>(AssetData.GetAsset());
							if (!Mesh)
							{
								return true;
							}
							
							USkeletalMesh* PreviewMesh = RetargetContext.IKRetargetAsset->SourceIKRigAsset->GetPreviewMesh();
							if (!PreviewMesh)
							{
								return true;
							}
							
							return Mesh->GetSkeleton() != PreviewMesh->GetSkeleton();
						})
					]
				]

				+SHorizontalBox::Slot()
				.Padding(5)
				.AutoWidth()
				[
					SNew(SSeparator)
					.Orientation(Orient_Vertical)
				]

				+SHorizontalBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DuplicateAndRetarget_TargetTitle", "Target Skeletal Mesh"))
						.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.BoldFont"))
						.AutoWrapText(true)
					]
				
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(5, 5)
					[
						SAssignNew(TargetViewport, SRetargetPoseViewport)
						.SkeletalMesh(nullptr)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(5, 5)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(USkeletalMesh::StaticClass())
						.AllowClear(true)
						.DisplayUseSelected(true)
						.DisplayBrowse(true)
						.DisplayThumbnail(true)
						.ThumbnailPool(AssetThumbnailPool)
						.IsEnabled_Lambda([this]()
						{
							if (!RetargetContext.IKRetargetAsset)
							{
								return false;
							}
							
							return RetargetContext.IKRetargetAsset->TargetIKRigAsset != nullptr;
						})
						.ObjectPath(this, &SRetargetAnimAssetsWindow::GetCurrentTargetMeshPath)
						.OnObjectChanged(this, &SRetargetAnimAssetsWindow::TargetMeshAssigned)
						.OnShouldFilterAsset_Lambda([this](const FAssetData& AssetData)
						{
							/*
							if (!RetargetContext.IKRetargetAsset)
							{
								return true;
							}
							
							USkeletalMesh* Mesh = Cast<USkeletalMesh>(AssetData.GetAsset());
							if (!Mesh)
							{
								return true;
							}

							
							USkeletalMesh* PreviewMesh = RetargetContext.IKRetargetAsset->TargetIKRigAsset->GetPreviewMesh();
							if (!PreviewMesh)
							{
								return true;
							}
							
							return Mesh->GetSkeleton() != PreviewMesh->GetSkeleton();*/
							return false;
						})
					]
				]
			]
		]

		+SHorizontalBox::Slot()
		.Padding(5)
		.AutoWidth()
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
		]
			
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0, 5)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DuplicateAndRetarget_RetargetAsset", "IK Retargeter"))
				.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.BoldFont"))
				.AutoWrapText(true)
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(2)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UIKRetargeter::StaticClass())
				.AllowClear(true)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.DisplayThumbnail(true)
				.ThumbnailPool(AssetThumbnailPool)
				.ObjectPath(this, &SRetargetAnimAssetsWindow::GetCurrentRetargeterPath)
				.OnObjectChanged(this, &SRetargetAnimAssetsWindow::RetargeterAssigned)
			]

			+SVerticalBox::Slot()
			.Padding(5)
			.AutoHeight()
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]
			
			+SVerticalBox::Slot()
			[	
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(2, 3)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.SmallBoldFont"))
					.Text(LOCTEXT("DuplicateAndRetarget_RenameLabel", "Rename New Assets"))
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 1)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(STextBlock).Text(LOCTEXT("DuplicateAndRetarget_Prefix", "Prefix"))
					]

					+SHorizontalBox::Slot()
					[
						SNew(SEditableTextBox)
							.Text(this, &SRetargetAnimAssetsWindow::GetPrefixName)
							.MinDesiredWidth(100)
							.OnTextChanged(this, &SRetargetAnimAssetsWindow::SetPrefixName)
							.IsReadOnly(false)
							.RevertTextOnEscape(true)
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 1)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(STextBlock).Text(LOCTEXT("DuplicateAndRetarget_Suffix", "Suffix"))
					]

					+SHorizontalBox::Slot()
					[
						SNew(SEditableTextBox)
							.Text(this, &SRetargetAnimAssetsWindow::GetSuffixName)
							.MinDesiredWidth(100)
							.OnTextChanged(this, &SRetargetAnimAssetsWindow::SetSuffixName)
							.IsReadOnly(false)
							.RevertTextOnEscape(true)
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 1)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(STextBlock).Text(LOCTEXT("DuplicateAndRetarget_Search", "Search "))
					]

					+SHorizontalBox::Slot()
					[
						SNew(SEditableTextBox)
							.Text(this, &SRetargetAnimAssetsWindow::GetReplaceFrom)
							.MinDesiredWidth(100)
							.OnTextChanged(this, &SRetargetAnimAssetsWindow::SetReplaceFrom)
							.IsReadOnly(false)
							.RevertTextOnEscape(true)
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 1)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					[
						SNew(STextBlock).Text(LOCTEXT("DuplicateAndRetarget_Replace", "Replace "))
					]

					+SHorizontalBox::Slot()
					[
						SNew(SEditableTextBox)
							.Text(this, &SRetargetAnimAssetsWindow::GetReplaceTo)
							.MinDesiredWidth(100)
							.OnTextChanged(this, &SRetargetAnimAssetsWindow::SetReplaceTo)
							.IsReadOnly(false)
							.RevertTextOnEscape(true)
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 3)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.Padding(5, 5)
					[
						SNew(STextBlock)
						.Text(this,  &SRetargetAnimAssetsWindow::GetExampleText)
						.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.ItalicFont"))
					]
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2, 3)
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DuplicateAndRetarget_Folder", "Folder "))
						.Font(FEditorStyle::GetFontStyle("Persona.RetargetManager.SmallBoldFont"))
					]

					+SHorizontalBox::Slot()
					.FillWidth(1)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SRetargetAnimAssetsWindow::GetFolderPath)
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("DuplicateAndRetarget_ChangeFolder", "Change..."))
						//.OnClicked(this, &SAnimationRemapSkeleton::ShowFolderOption)
					]
				]

				+SVerticalBox::Slot()
				.Padding(5)
				.AutoHeight()
				[
					SNew(SSeparator)
					.Orientation(Orient_Horizontal)
				]

				+SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Fill)
				.Padding(2)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SRetargetAnimAssetsWindow::IsRemappingReferencedAssets)
					.OnCheckStateChanged(this, &SRetargetAnimAssetsWindow::OnRemappingReferencedAssetsChanged)
					[
						SNew(STextBlock).Text(LOCTEXT("DuplicateAndRetarget_AllowRemap", "Remap Referenced Assets"))
					]
				]
			]

			+SVerticalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(2)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton).HAlign(HAlign_Center)
					.Text(LOCTEXT("RetargetOptions_Cancel", "Cancel"))
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked(this, &SRetargetAnimAssetsWindow::OnCancel)
				]
				+SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton).HAlign(HAlign_Center)
					.Text(LOCTEXT("RetargetOptions_Apply", "Retarget"))
					.IsEnabled(this, &SRetargetAnimAssetsWindow::CanApply)
					.OnClicked(this, &SRetargetAnimAssetsWindow::OnApply)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
				]
				
			]
		]
	];

	UpdateExampleText();
}

bool SRetargetAnimAssetsWindow::CanApply() const
{
	return RetargetContext.IsValid();
}

FReply SRetargetAnimAssetsWindow::OnApply()
{
	CloseWindow();
	RetargetContext.RunRetarget();
	return FReply::Handled();
}

FReply SRetargetAnimAssetsWindow::OnCancel()
{
	CloseWindow();
	return FReply::Handled();
}

void SRetargetAnimAssetsWindow::CloseWindow()
{
	if ( DialogWindow.IsValid() )
	{
		DialogWindow->RequestDestroyWindow();
	}
}

void SRetargetAnimAssetsWindow::ShowWindow(TArray<UObject*> InSelectedAssets)
{	
	if(DialogWindow.IsValid())
	{
		FSlateApplication::Get().DestroyWindowImmediately(DialogWindow.ToSharedRef());
	}
	
	DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("RetargetAssets", "Duplicate and Retarget Animation Assets"))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.HasCloseButton(true)
		.MaxWidth(1024.0f)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::Autosized);
	
	TSharedPtr<class SRetargetAnimAssetsWindow> DialogWidget;
	TSharedPtr<SBorder> DialogWrapper =
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4.0f)
		[
			SAssignNew(DialogWidget, SRetargetAnimAssetsWindow)
		];

	DialogWidget->RetargetContext.AssetsToRetarget = FObjectEditorUtils::GetTypedWeakObjectPtrs<UObject>(InSelectedAssets);
	DialogWindow->SetOnWindowClosed(FRequestDestroyWindowOverride::CreateSP(DialogWidget.Get(), &SRetargetAnimAssetsWindow::OnDialogClosed));
	DialogWindow->SetContent(DialogWrapper.ToSharedRef());
	
	FSlateApplication::Get().AddWindow(DialogWindow.ToSharedRef());
}

void SRetargetAnimAssetsWindow::OnDialogClosed(const TSharedRef<SWindow>& Window)
{
	DialogWindow = nullptr;
}

void SRetargetAnimAssetsWindow::SourceMeshAssigned(const FAssetData& InAssetData)
{
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	RetargetContext.SourceMesh = Mesh;
	SourceViewport->SetSkeletalMesh(RetargetContext.SourceMesh);
}

void SRetargetAnimAssetsWindow::TargetMeshAssigned(const FAssetData& InAssetData)
{
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset());
	RetargetContext.TargetMesh = Mesh;
	TargetViewport->SetSkeletalMesh(RetargetContext.TargetMesh);
}

FString SRetargetAnimAssetsWindow::GetCurrentSourceMeshPath() const
{
	return RetargetContext.SourceMesh ? RetargetContext.SourceMesh->GetPathName() : FString("");
}

FString SRetargetAnimAssetsWindow::GetCurrentTargetMeshPath() const
{
	return RetargetContext.TargetMesh ? RetargetContext.TargetMesh->GetPathName() : FString("");
}

FString SRetargetAnimAssetsWindow::GetCurrentRetargeterPath() const
{
	return RetargetContext.IKRetargetAsset ? RetargetContext.IKRetargetAsset->GetPathName() : FString("");
}

void SRetargetAnimAssetsWindow::RetargeterAssigned(const FAssetData& InAssetData)
{
	UIKRetargeter* InRetargeter = Cast<UIKRetargeter>(InAssetData.GetAsset());
	RetargetContext.IKRetargetAsset = InRetargeter;
	const UIKRigDefinition* SourceIKRig = InRetargeter ? InRetargeter->SourceIKRigAsset : nullptr;
	const UIKRigDefinition* TargetIKRig = InRetargeter ? InRetargeter->TargetIKRigAsset : nullptr;
	USkeletalMesh* SourceMesh =  SourceIKRig ? SourceIKRig->GetPreviewMesh() : nullptr;
	USkeletalMesh* TargetMesh =  TargetIKRig ? TargetIKRig->GetPreviewMesh() : nullptr;
	SourceMeshAssigned(FAssetData(SourceMesh));
	TargetMeshAssigned(FAssetData(TargetMesh));
}

ECheckBoxState SRetargetAnimAssetsWindow::IsRemappingReferencedAssets() const
{
	return RetargetContext.bRemapReferencedAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SRetargetAnimAssetsWindow::OnRemappingReferencedAssetsChanged(ECheckBoxState InNewRadioState)
{
	RetargetContext.bRemapReferencedAssets = (InNewRadioState == ECheckBoxState::Checked);
}

FText SRetargetAnimAssetsWindow::GetPrefixName() const
{
	return FText::FromString(RetargetContext.NameRule.Prefix);
}

void SRetargetAnimAssetsWindow::SetPrefixName(const FText &InText)
{
	RetargetContext.NameRule.Prefix = InText.ToString();
	UpdateExampleText();
}

FText SRetargetAnimAssetsWindow::GetSuffixName() const
{
	return FText::FromString(RetargetContext.NameRule.Suffix);
}

void SRetargetAnimAssetsWindow::SetSuffixName(const FText &InText)
{
	RetargetContext.NameRule.Suffix = InText.ToString();
	UpdateExampleText();
}

FText SRetargetAnimAssetsWindow::GetReplaceFrom() const
{
	return FText::FromString(RetargetContext.NameRule.ReplaceFrom);
}

void SRetargetAnimAssetsWindow::SetReplaceFrom(const FText &InText)
{
	RetargetContext.NameRule.ReplaceFrom = InText.ToString();
	UpdateExampleText();
}

FText SRetargetAnimAssetsWindow::GetReplaceTo() const
{
	return FText::FromString(RetargetContext.NameRule.ReplaceTo);
}

void SRetargetAnimAssetsWindow::SetReplaceTo(const FText &InText)
{
	RetargetContext.NameRule.ReplaceTo = InText.ToString();
	UpdateExampleText();
}

FText SRetargetAnimAssetsWindow::GetExampleText() const
{
	return ExampleText;
}

void SRetargetAnimAssetsWindow::UpdateExampleText()
{
	const FString ReplaceFrom = FString::Printf(TEXT("Old Name : ###%s###"), *RetargetContext.NameRule.ReplaceFrom);
	const FString ReplaceTo = FString::Printf(TEXT("New Name : %s###%s###%s"), *RetargetContext.NameRule.Prefix, *RetargetContext.NameRule.ReplaceTo, *RetargetContext.NameRule.Suffix);

	ExampleText = FText::FromString(FString::Printf(TEXT("%s\n%s"), *ReplaceFrom, *ReplaceTo));
}

FText SRetargetAnimAssetsWindow::GetFolderPath() const
{
	return FText::FromString(RetargetContext.NameRule.FolderPath);
}

#undef LOCTEXT_NAMESPACE
