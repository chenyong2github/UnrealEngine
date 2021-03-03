// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprintActions.h"
#include "ControlRigBlueprintFactory.h"
#include "ControlRigBlueprint.h"
#include "ControlRigEditorStyle.h"
#include "IControlRigEditorModule.h"

#include "Styling/SlateIconFinder.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "AssetTools/Private/AssetTools.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "ScopedTransaction.h"
#include "LevelSequenceActor.h"
#include "EngineUtils.h"
#include "ISequencer.h"
#include "ILevelSequenceEditorToolkit.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "ControlRigObjectBinding.h"
#include "EditMode/ControlRigEditMode.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "MovieSceneToolsProjectSettings.h"

#define LOCTEXT_NAMESPACE "ControlRigBlueprintActions"

FDelegateHandle FControlRigBlueprintActions::OnSpawnedSkeletalMeshActorChangedHandle;

UFactory* FControlRigBlueprintActions::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UControlRigBlueprintFactory* ControlRigBlueprintFactory = NewObject<UControlRigBlueprintFactory>();
	UControlRigBlueprint* ControlRigBlueprint = CastChecked<UControlRigBlueprint>(InBlueprint);
	ControlRigBlueprintFactory->ParentClass = TSubclassOf<UControlRig>(*InBlueprint->GeneratedClass);
	return ControlRigBlueprintFactory;
}

void FControlRigBlueprintActions::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UControlRigBlueprint* ControlRigBlueprint = Cast<UControlRigBlueprint>(*ObjIt))
		{
			const bool bBringToFrontIfOpen = true;
			if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(ControlRigBlueprint, bBringToFrontIfOpen))
			{
				EditorInstance->FocusWindow(ControlRigBlueprint);
			}
			else
			{
				IControlRigEditorModule& ControlRigEditorModule = FModuleManager::LoadModuleChecked<IControlRigEditorModule>("ControlRigEditor");
				ControlRigEditorModule.CreateControlRigEditor(Mode, EditWithinLevelEditor, ControlRigBlueprint);
			}
		}
	}
}

TSharedPtr<SWidget> FControlRigBlueprintActions::GetThumbnailOverlay(const FAssetData& AssetData) const
{
	const FSlateBrush* Icon = FSlateIconFinder::FindIconBrushForClass(UControlRigBlueprint::StaticClass());

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetNoBrush())
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		[
			SNew(SImage)
			.Image(Icon)
		];
}

void FControlRigBlueprintActions::ExtendSketalMeshToolMenu()
{
	TArray<UToolMenu*> MenusToExtend;
	MenusToExtend.Add(UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SkeletalMesh"));
	MenusToExtend.Add(UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.Skeleton"));

	for(UToolMenu* Menu : MenusToExtend)
	{
		if (Menu == nullptr)
		{
			continue;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		Section.AddDynamicEntry("GetActions", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (Context)
			{
				TArray<UObject*> SelectedObjects = Context->GetSelectedObjects();
				if (SelectedObjects.Num() > 0)
				{
					InSection.AddMenuEntry(
						"CreateControlRig",
						LOCTEXT("CreateControlRig", "Create Control Rig"),
						LOCTEXT("CreateControlRig_ToolTip", "Creates a control rig and preconfigures it for this asset"),
						FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig", "ControlRig.RigUnit"),
						FExecuteAction::CreateLambda([SelectedObjects]()
						{
							for (UObject* SelectedObject : SelectedObjects)
							{
								FControlRigBlueprintActions::CreateControlRigFromSkeletalMeshOrSkeleton(SelectedObject);
							}
						})
					);
				}
			}
		}));
	}
}

UControlRigBlueprint* FControlRigBlueprintActions::CreateNewControlRigAsset(const FString& InDesiredPackagePath)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UControlRigBlueprintFactory* Factory = NewObject<UControlRigBlueprintFactory>();
	Factory->ParentClass = UControlRig::StaticClass();

	FString UniquePackageName;
	FString UniqueAssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(InDesiredPackagePath, TEXT(""), UniquePackageName, UniqueAssetName);

	if (UniquePackageName.EndsWith(UniqueAssetName))
	{
		UniquePackageName = UniquePackageName.LeftChop(UniqueAssetName.Len() + 1);
	}

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(*UniqueAssetName, *UniquePackageName, nullptr, Factory);
	return Cast<UControlRigBlueprint>(NewAsset);
}

UControlRigBlueprint* FControlRigBlueprintActions::CreateControlRigFromSkeletalMeshOrSkeleton(UObject* InSelectedObject)
{
	FScopedTransaction Transaction(LOCTEXT("CreateControlRigFromSkeletalMesh", "Create Control Rig"));

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(InSelectedObject);
	USkeleton* Skeleton = Cast<USkeleton>(InSelectedObject);
	const FReferenceSkeleton* RefSkeleton = nullptr;

	if(SkeletalMesh)
	{
		Skeleton = SkeletalMesh->GetSkeleton();
		RefSkeleton = &SkeletalMesh->GetRefSkeleton();
	}
	else if (Skeleton)
	{
		RefSkeleton = &Skeleton->GetReferenceSkeleton();
	}
	else
	{
		UE_LOG(LogControlRigEditor, Error, TEXT("CreateControlRigFromSkeletalMeshOrSkeleton: Provided object has to be a SkeletalMesh or Skeleton."));
		return nullptr;
	}

	check(RefSkeleton);

	FString PackagePath = InSelectedObject->GetPathName();
	FString ControlRigName = FString::Printf(TEXT("%s_CtrlRig"), *InSelectedObject->GetName());

	int32 LastSlashPos = INDEX_NONE;
	if (PackagePath.FindLastChar('/', LastSlashPos))
	{
		PackagePath = PackagePath.Left(LastSlashPos);
	}

	UControlRigBlueprint* NewControlRigBlueprint = CreateNewControlRigAsset(PackagePath / ControlRigName);
	if (NewControlRigBlueprint == nullptr)
	{
		return nullptr;
	}

	NewControlRigBlueprint->HierarchyContainer.BoneHierarchy.ImportSkeleton(*RefSkeleton, NAME_None, false, false, false, false);
	NewControlRigBlueprint->HierarchyContainer.CurveContainer.ImportCurvesFromSkeleton(Skeleton, NAME_None, false, false, false);
	NewControlRigBlueprint->SourceHierarchyImport = Skeleton;
	NewControlRigBlueprint->SourceCurveImport = Skeleton;
	NewControlRigBlueprint->PropagateHierarchyFromBPToInstances(true);

	if(SkeletalMesh)
	{
		NewControlRigBlueprint->SetPreviewMesh(SkeletalMesh);
	}

	NewControlRigBlueprint->RecompileVM();

	return NewControlRigBlueprint;
}

USkeletalMesh* FControlRigBlueprintActions::GetSkeletalMeshFromControlRigBlueprint(UObject* InAsset)
{
	if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(InAsset))
	{
		return Blueprint->GetPreviewMesh();
	}
	return nullptr;
}

void FControlRigBlueprintActions::PostSpawningSkeletalMeshActor(AActor* InSpawnedActor, UObject* InAsset)
{
	if (InSpawnedActor->HasAnyFlags(RF_Transient))
	{
		return;
	}
	
	OnSpawnedSkeletalMeshActorChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddStatic(&FControlRigBlueprintActions::OnSpawnedSkeletalMeshActorChanged, InAsset);
}

void FControlRigBlueprintActions::OnSpawnedSkeletalMeshActorChanged(UObject* InObject, FPropertyChangedEvent& InEvent, UObject* InAsset)
{
	if (!OnSpawnedSkeletalMeshActorChangedHandle.IsValid())
	{
		return;
	}

	// we are waiting for the top level property change event
	// after the spawn.
	if (InEvent.Property != nullptr)
	{
		return;
	}

	ASkeletalMeshActor* MeshActor = Cast<ASkeletalMeshActor>(InObject);
	check(MeshActor);

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnSpawnedSkeletalMeshActorChangedHandle);

	UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(InAsset);
	if (RigBlueprint == nullptr)
	{
		return;
	}
	UClass* ControlRigClass = RigBlueprint->GeneratedClass;

	// find a level sequence in the world
	ALevelSequenceActor* LevelSequenceActor = nullptr;
	for (ALevelSequenceActor* ExistingLevelSequenceActor : TActorRange<ALevelSequenceActor>(MeshActor->GetWorld()))
	{
		LevelSequenceActor = ExistingLevelSequenceActor;
		break;
	}
    //The creation of the FSequencer that happens below is not transactional so we need to make sure we doen't transaction there.
	GEditor->CancelTransaction(0);

	// todo: show a dialog for picking an existing one or creating a new one
	if (LevelSequenceActor == nullptr)
	{
		FString SequenceName = FString::Printf(TEXT("%s_Take1"), *InAsset->GetName());
		FString PackagePath = TEXT("/Game");

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FString UniquePackageName;
		FString UniqueAssetName;
		AssetToolsModule.Get().CreateUniqueAssetName(PackagePath / SequenceName, TEXT(""), UniquePackageName, UniqueAssetName);

		UPackage* Package = CreatePackage(*UniquePackageName);
		ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
		Sequence->Initialize(); //creates movie scene
		Sequence->MarkPackageDirty();

		// Set up some sensible defaults
		const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
		FFrameRate TickResolution = Sequence->GetMovieScene()->GetTickResolution();
		Sequence->GetMovieScene()->SetPlaybackRange((ProjectSettings->DefaultStartTime*TickResolution).FloorToFrame(), (ProjectSettings->DefaultDuration*TickResolution).FloorToFrame().Value);

		UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(ALevelSequenceActor::StaticClass());
		if (!ensure(ActorFactory))
		{
			return;
		}

		AActor* NewActor = GEditor->UseActorFactory(ActorFactory, FAssetData(Sequence), &FTransform::Identity);
		if (NewActor == nullptr)
		{
			return;
		}

		LevelSequenceActor = CastChecked<ALevelSequenceActor>(NewActor);
		LevelSequenceActor->SetSequence(Sequence);
	}

	ULevelSequence* Sequence = LevelSequenceActor->GetSequence();
	if (Sequence == nullptr)
	{
		Sequence = LevelSequenceActor->LoadSequence();
	}
	if (Sequence == nullptr)
	{
		return;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Sequence);

	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Sequence, false);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
	TWeakPtr<ISequencer> WeakSequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;

	if (WeakSequencer.IsValid())
	{
		TArray<TWeakObjectPtr<AActor> > ActorsToAdd;
		ActorsToAdd.Add(MeshActor);
		TArray<FGuid> ActorTracks = WeakSequencer.Pin()->AddActors(ActorsToAdd, false);
		for (FGuid ActorTrackGuid : ActorTracks)
		{
			//Delete binding from default animating rig
			FGuid CompGuid = WeakSequencer.Pin()->FindObjectId(*(MeshActor->GetSkeletalMeshComponent()), WeakSequencer.Pin()->GetFocusedTemplateID());
			if (CompGuid.IsValid())
			{
				if (!MovieScene->RemovePossessable(CompGuid))
				{
					MovieScene->RemoveSpawnable(CompGuid);
				}
			}
			UMovieSceneControlRigParameterTrack* Track = MovieScene->AddTrack<UMovieSceneControlRigParameterTrack>(ActorTrackGuid);
			if (Track)
			{
				USkeletalMesh* SkeletalMesh = MeshActor->GetSkeletalMeshComponent()->SkeletalMesh;
				USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
				
				FString ObjectName = (ControlRigClass->GetName());
				ObjectName.RemoveFromEnd(TEXT("_C"));

				UControlRig* ControlRig = NewObject<UControlRig>(Track, ControlRigClass, FName(*ObjectName), RF_Transactional);
				ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
				ControlRig->GetObjectBinding()->BindToObject(MeshActor->GetSkeletalMeshComponent());
				ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
				ControlRig->Initialize();
				ControlRig->Evaluate_AnyThread();
				ControlRig->CreateRigControlsForCurveContainer();

				WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

				Track->Modify();
				UMovieSceneSection* NewSection = Track->CreateControlRigSection(0, ControlRig, true);
				//mz todo need to have multiple rigs with same class
				Track->SetTrackName(FName(*ObjectName));
				Track->SetDisplayName(FText::FromString(ObjectName));

				WeakSequencer.Pin()->EmptySelection();
				WeakSequencer.Pin()->SelectSection(NewSection);
				WeakSequencer.Pin()->ThrobSectionSelection();
				WeakSequencer.Pin()->ObjectImplicitlyAdded(ControlRig);
				FText Name = LOCTEXT("SequenceTrackFilter_ControlRigControls", "Control Rig Controls");
				WeakSequencer.Pin()->SetFilterOn(Name, true);
				WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
				FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
				if (!ControlRigEditMode)
				{
					GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
					ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

				}
				if (ControlRigEditMode)
				{
					ControlRigEditMode->SetObjects(ControlRig, nullptr, WeakSequencer.Pin());
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

