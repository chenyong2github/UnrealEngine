// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigParameterTrackEditor.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Framework/Commands/Commands.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "AssetData.h"
#include "Modules/ModuleManager.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "SequencerSectionPainter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "CommonMovieSceneTools.h"
#include "AssetRegistryModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "MatineeImportTools.h"
#include "Matinee/InterpTrackAnimControl.h"
#include "SequencerUtilities.h"
#include "ISectionLayoutBuilder.h"
#include "EditorStyleSet.h"
#include "MovieSceneTimeHelpers.h"
#include "Fonts/FontMeasure.h"
#include "AnimationEditorUtils.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Toolkits/AssetEditorManager.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"
#include "ControlRig.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "ControlRigObjectBinding.h"
#include "LevelEditorViewport.h"
#include "IKeyArea.h"
#include "ISequencer.h"
#include "CurveModel.h"
#include "ControlRigEditorModule.h"
#include "SequencerSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "Channels/FloatChannelCurveModel.h"
#include "TransformNoScale.h"
#include "ControlRigComponent.h"
#include "ISequencerObjectChangeListener.h"
#include "MovieSceneToolHelpers.h"
#include "Rigs/FKControlRig.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "ControlRig/Private/Units/Execution/RigUnit_InverseExecution.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Exporters/AnimSeqExportOption.h"
#include "SBakeToControlRigDialog.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "TimerManager.h"
#include "BakeToControlRigSettings.h"


#define LOCTEXT_NAMESPACE "FControlRigParameterTrackEditor"

static USkeletalMeshComponent* AcquireSkeletalMeshFromObject(UObject* BoundObject, TSharedPtr<ISequencer> SequencerPtr)
{
	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
		Actor->GetComponents(SkeletalMeshComponents);
		
		if (SkeletalMeshComponents.Num() == 1)
		{
			return SkeletalMeshComponents[0];
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (SkeletalMeshComponent->SkeletalMesh)
		{
			return SkeletalMeshComponent;
		}
	}

	return nullptr;
}


static USkeleton* GetSkeletonFromComponent(UActorComponent* InComponent)
{
	USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(InComponent);
	if (SkeletalMeshComp && SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->GetSkeleton())
	{
		// @todo Multiple actors, multiple components
		return SkeletalMeshComp->SkeletalMesh->GetSkeleton();
	}

	return nullptr;
}

static USkeleton* AcquireSkeletonFromObjectGuid(const FGuid& Guid, UObject** Object, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;
	*Object = BoundObject;
	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshComponents;
		Actor->GetComponents(SkeletalMeshComponents);
		if (SkeletalMeshComponents.Num() == 1)
		{
			return GetSkeletonFromComponent(SkeletalMeshComponents[0]);
		}
		SkeletalMeshComponents.Empty();

		AActor* ActorCDO = Cast<AActor>(Actor->GetClass()->GetDefaultObject());
		if (ActorCDO)
		{
			ActorCDO->GetComponents(SkeletalMeshComponents);
			if (SkeletalMeshComponents.Num() == 1)
			{
				return GetSkeletonFromComponent(SkeletalMeshComponents[0]);
			}
			SkeletalMeshComponents.Empty();
		}

		UBlueprintGeneratedClass* ActorBlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		if (ActorBlueprintGeneratedClass)
		{
			const TArray<USCS_Node*>& ActorBlueprintNodes = ActorBlueprintGeneratedClass->SimpleConstructionScript->GetAllNodes();

			for (USCS_Node* Node : ActorBlueprintNodes)
			{
				if (Node->ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
				{
					if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Node->GetActualComponentTemplate(ActorBlueprintGeneratedClass)))
					{
						SkeletalMeshComponents.Add(SkeletalMeshComponent);
					}
				}
			}

			if (SkeletalMeshComponents.Num() == 1)
			{
				return GetSkeletonFromComponent(SkeletalMeshComponents[0]);
			}
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (USkeleton* Skeleton = GetSkeletonFromComponent(SkeletalMeshComponent))
		{
			return Skeleton;
		}
	}

	return nullptr;
}

FControlRigParameterTrackEditor::FControlRigParameterTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FKeyframeTrackEditor<UMovieSceneControlRigParameterTrack>(InSequencer), bCurveDisplayTickIsPending(false), bIsDoingSelection(false), bFilterAssetBySkeleton(true),bFilterAssetByAnimatableControls(true)

{
	FMovieSceneToolsModule::Get().RegisterAnimationBakeHelper(this);

	UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

	SelectionChangedHandle = InSequencer->GetSelectionChangedTracks().AddRaw(this, &FControlRigParameterTrackEditor::OnSelectionChanged);
	SequencerChangedHandle = InSequencer->OnMovieSceneDataChanged().AddRaw(this, &FControlRigParameterTrackEditor::OnSequencerDataChanged);
	OnActivateSequenceChangedHandle = InSequencer->OnActivateSequence().AddRaw(this, &FControlRigParameterTrackEditor::OnActivateSequenceChanged);
	CurveChangedHandle = InSequencer->GetCurveDisplayChanged().AddRaw(this, &FControlRigParameterTrackEditor::OnCurveDisplayChanged);
	OnChannelChangedHandle = InSequencer->OnChannelChanged().AddRaw(this, &FControlRigParameterTrackEditor::OnChannelChanged);
	OnMovieSceneChannelChangedHandle = MovieScene->OnChannelChanged().AddRaw(this, &FControlRigParameterTrackEditor::OnChannelChanged);
	OnActorAddedToSequencerHandle = InSequencer->OnActorAddedToSequencer().AddRaw(this, &FControlRigParameterTrackEditor::HandleActorAdded);

	//REMOVE ME IN UE5
	//InSequencer->GetObjectChangeListener().GetOnPropagateObjectChanges().AddRaw(this, &FControlRigParameterTrackEditor::OnPropagateObjectChanges);
	{
		//we check for two things, one if the control rig has been replaced if so we need to switch.
		//the other is if bound object on the edit mode is null we request a re-evaluate which will reset it up.
		FDelegateHandle OnObjectsReplacedHandle = GEditor->OnObjectsReplaced().AddLambda([&](const TMap<UObject*, UObject*>& ReplacementMap)
		{
			if (GetSequencer().IsValid())
			{
				TMap<UControlRig*, UControlRig*> OldToNewControlRigs;
				FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
				if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true) && ControlRigEditMode->GetControlRig(true)->GetObjectBinding())
				{
					if (ControlRigEditMode->GetControlRig(true)->GetObjectBinding()->GetBoundObject() == nullptr)
					{
						GetSequencer()->RequestEvaluate();
					}
				}
				//Reset Bindings for replaced objects.
				for (TPair<UObject*, UObject*> ReplacedObject : ReplacementMap)
				{
					if (UControlRigComponent* OldControlRigComponent = Cast<UControlRigComponent>(ReplacedObject.Key))
					{
						UControlRigComponent* NewControlRigComponent = Cast<UControlRigComponent>(ReplacedObject.Value);
						if (OldControlRigComponent->GetControlRig())
						{
							OldToNewControlRigs.Emplace(OldControlRigComponent->GetControlRig(), NewControlRigComponent->GetControlRig());
						}
					}
					else if (UControlRig* OldControlRig = Cast<UControlRig>(ReplacedObject.Key))
					{
						UControlRig* NewControlRig = Cast<UControlRig>(ReplacedObject.Value);
						OldToNewControlRigs.Emplace(OldControlRig, NewControlRig);
					}
				}
				UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
				const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
				for (const FMovieSceneBinding& Binding : Bindings)
				{
					UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None));
					if (Track && Track->GetControlRig())
					{
						UControlRig* OldControlRig = Track->GetControlRig();
						UControlRig** NewControlRig = OldToNewControlRigs.Find(OldControlRig);
						if (NewControlRig)
						{  
							OldControlRig->ClearControlSelection();
							UnbindControlRig(OldControlRig);
							if (*NewControlRig)
							{
								Track->ReplaceControlRig(*NewControlRig, OldControlRig->GetClass() != (*NewControlRig)->GetClass());
								BindControlRig(*NewControlRig);

								GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
							}
							else
							{
								Track->ReplaceControlRig(nullptr, true);
							}
							if (ControlRigEditMode)
							{
								if (ControlRigEditMode->GetControlRig(false) == OldControlRig)
								{
									ControlRigEditMode->SetObjects(*NewControlRig, nullptr, GetSequencer());
								}
								if (*NewControlRig)
								{
									(*NewControlRig)->ClearControlSelection();
								}
								//Force refresh now, not later
								GetSequencer()->EmptySelection();
								//Also need to clear these guys out may cause unsure if component is selected
								if (USelection* SelectedComponents = GEditor->GetSelectedComponents())
								{
									SelectedComponents->DeselectAll();
								}
								if (USelection* SelectedActors = GEditor->GetSelectedActors())
								{
									SelectedActors->DeselectAll();
								}
							}
						}
					}
				}
			}

		});
		AcquiredResources.Add([=] { GEditor->OnObjectsReplaced().Remove(OnObjectsReplacedHandle); });
	}
	//register all modified/selections for control rigs
	const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None));
		if (Track && Track->GetControlRig())
		{
			BindControlRig(Track->GetControlRig());
		}
	}
}

FControlRigParameterTrackEditor::~FControlRigParameterTrackEditor()
{
	UnbindAllControlRigs();
	if (GetSequencer().IsValid())
	{
		//REMOVE ME IN UE5
		GetSequencer()->GetObjectChangeListener().GetOnPropagateObjectChanges().RemoveAll(this);
	}
	FMovieSceneToolsModule::Get().UnregisterAnimationBakeHelper(this);
}

void FControlRigParameterTrackEditor::BindControlRig(UControlRig* ControlRig)
{
	if (ControlRig && BoundControlRigs.Contains(ControlRig) == false)
	{
		ControlRig->ControlModified().AddRaw(this, &FControlRigParameterTrackEditor::HandleControlModified);
		ControlRig->OnInitialized_AnyThread().AddRaw(this, &FControlRigParameterTrackEditor::HandleOnInitialized);
		ControlRig->ControlSelected().AddRaw(this, &FControlRigParameterTrackEditor::HandleControlSelected);
		BoundControlRigs.Add(ControlRig);
	}
}
void FControlRigParameterTrackEditor::UnbindControlRig(UControlRig* ControlRig)
{
	if (ControlRig && BoundControlRigs.Contains(ControlRig) == true)
	{
		ControlRig->ControlModified().RemoveAll(this);
		ControlRig->OnInitialized_AnyThread().RemoveAll(this);
		ControlRig->ControlSelected().RemoveAll(this);
		BoundControlRigs.Remove(ControlRig);
	}
}
void FControlRigParameterTrackEditor::UnbindAllControlRigs()
{
	for(TWeakObjectPtr<UControlRig>& ObjectPtr: BoundControlRigs)
	{
		if (ObjectPtr.IsValid())
		{
			UControlRig* ControlRig = ObjectPtr.Get();
			ControlRig->ControlModified().RemoveAll(this);
			ControlRig->OnInitialized_AnyThread().RemoveAll(this);
			ControlRig->ControlSelected().RemoveAll(this);
		}
	}
	BoundControlRigs.SetNum(0);
}


void FControlRigParameterTrackEditor::ObjectImplicitlyAdded(UObject* InObject)
{
	UControlRig* ControlRig = Cast<UControlRig>(InObject);
	if (ControlRig)
	{
		BindControlRig(ControlRig);
	}
}


void FControlRigParameterTrackEditor::OnRelease()
{
	UnbindAllControlRigs();
	if (GetSequencer().IsValid())
	{
		if (SelectionChangedHandle.IsValid())
		{
			GetSequencer()->GetSelectionChangedTracks().Remove(SelectionChangedHandle);
		}
		if (SequencerChangedHandle.IsValid())
		{
			GetSequencer()->OnMovieSceneDataChanged().Remove(SequencerChangedHandle);
		}
		if (OnActivateSequenceChangedHandle.IsValid())
		{
			GetSequencer()->OnActivateSequence().Remove(OnActivateSequenceChangedHandle);
		}
		if (CurveChangedHandle.IsValid())
		{
			GetSequencer()->GetCurveDisplayChanged().Remove(CurveChangedHandle);
		}
		if (OnActorAddedToSequencerHandle.IsValid())
		{
			GetSequencer()->OnActorAddedToSequencer().Remove(OnActorAddedToSequencerHandle);
		}
		if (OnChannelChangedHandle.IsValid())
		{
			GetSequencer()->OnChannelChanged().Remove(OnChannelChangedHandle);
		}
		
		if (GetSequencer()->GetFocusedMovieSceneSequence() && GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene())
		{
			UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
			if (OnMovieSceneChannelChangedHandle.IsValid())
			{
				MovieScene->OnChannelChanged().Remove(OnMovieSceneChannelChangedHandle);
			}
		}
	}
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		if (GLevelEditorModeTools().HasToolkitHost())
		{
			GLevelEditorModeTools().DeactivateMode(FControlRigEditMode::ModeName);
		}

		ControlRigEditMode->SetObjects(nullptr, nullptr, GetSequencer());
	}

	AcquiredResources.Release();

}

TSharedRef<ISequencerTrackEditor> FControlRigParameterTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FControlRigParameterTrackEditor(InSequencer));
}


bool FControlRigParameterTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneControlRigParameterTrack::StaticClass();
}


TSharedRef<ISequencerSection> FControlRigParameterTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	check(SupportsType(SectionObject.GetOuter()->GetClass()));

	return MakeShareable(new FControlRigParameterSection(SectionObject, GetSequencer()));
}

void FControlRigParameterTrackEditor::BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()) || ObjectClass->IsChildOf(UChildActorComponent::StaticClass()))
	{
		const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();
		UObject* BoundObject = nullptr;
		USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBindings[0], &BoundObject, ParentSequencer);
		USkeletalMeshComponent*  SkelMeshComp = AcquireSkeletalMeshFromObject(BoundObject, ParentSequencer);

		if (Skeleton && SkelMeshComp)
		{
			MenuBuilder.BeginSection("Control Rig", LOCTEXT("ControlRig", "Control Rig"));
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("EditWithFKControlRig", "Edit With FK Control Rig"),
					LOCTEXT("ConvertToFKControlRigTooltip", "Convert to FK Control Rig and add a track for it"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateRaw(this, &FControlRigParameterTrackEditor::ConvertToFKControlRig, ObjectBindings[0], BoundObject, SkelMeshComp, Skeleton)),
					NAME_None,
					EUserInterfaceActionType::Button);

				MenuBuilder.AddMenuEntry(
					NSLOCTEXT("Sequencer", "FilterAssetBySkeleton", "Filter Asset By Skeleton"),
					NSLOCTEXT("Sequencer", "FilterAssetBySkeletonTooltip", "Filters Control Rig assets to match current skeleton"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigParameterTrackEditor::ToggleFilterAssetBySkeleton),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP(this, &FControlRigParameterTrackEditor::IsToggleFilterAssetBySkeleton)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton);

				MenuBuilder.AddSubMenu(
					LOCTEXT("BakeToControlRig", "Bake To Control Rig"),
					LOCTEXT("BakeToControlRigTooltip", "Bake to an invertible Control Rig that matches this skeleton"),
					FNewMenuDelegate::CreateRaw(this, &FControlRigParameterTrackEditor::BakeToControlRigSubMenu, ObjectBindings[0], BoundObject, SkelMeshComp, Skeleton)
				);
			}
			MenuBuilder.EndSection();
		}
	}
}

class FControlRigClassFilter : public IClassViewerFilter
{
public:
	FControlRigClassFilter(bool bInCheckSkeleton, bool bInCheckAnimatable, bool bInCheckInversion, USkeleton* InSkeleton) : 
		bFilterAssetBySkeleton(bInCheckSkeleton),
		bFilterExposesAnimatableControls(bInCheckAnimatable), 
		bFilterInversion(bInCheckInversion),
		AssetRegistry(FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get())
	{
		if (InSkeleton)
		{
			SkeletonName = FAssetData(InSkeleton).GetExportTextName();
		}
	}
	bool bFilterAssetBySkeleton;
	bool bFilterExposesAnimatableControls;
	bool bFilterInversion;

	FString SkeletonName;
	const IAssetRegistry& AssetRegistry;

	bool MatchesFilter(const FAssetData& AssetData)
	{
		bool bExposesAnimatableControls = AssetData.GetTagValueRef<bool>(TEXT("bExposesAnimatableControls"));
		if (bFilterExposesAnimatableControls == true && bExposesAnimatableControls == false)
		{
			return false;
		}
		if (bFilterInversion)
		{
			bool bHasInversion = false;
			FAssetDataTagMapSharedView::FFindTagResult Tag = AssetData.TagsAndValues.FindTag(TEXT("SupportedEventNames"));
			if (Tag.IsSet())
			{
				FString EventString = FRigUnit_InverseExecution::EventName.ToString();
				TArray<FString> SupportedEventNames;
				Tag.GetValue().ParseIntoArray(SupportedEventNames, TEXT(","), true);
	
				for (const FString& Name : SupportedEventNames)
				{
					if (Name.Contains(EventString))
					{
						bHasInversion = true;
						break;
					}
				}
				if (bHasInversion == false)
				{
					return false;
				}
			}
		}
		if (bFilterAssetBySkeleton)
		{
			FString PreviewSkeletalMesh = AssetData.GetTagValueRef<FString>(TEXT("PreviewSkeletalMesh"));
			if (PreviewSkeletalMesh.Len() > 0)
			{
				FAssetData SkelMeshData = AssetRegistry.GetAssetByObjectPath(FName(*PreviewSkeletalMesh));
				FString PreviewSkeleton = SkelMeshData.GetTagValueRef<FString>(TEXT("Skeleton"));
				if (PreviewSkeleton == SkeletonName)
				{
					return true;
				}
			}
			FString PreviewSkeleton = AssetData.GetTagValueRef<FString>(TEXT("PreviewSkeleton"));
			if (PreviewSkeleton == SkeletonName)
			{
				return true;
			}
			FString SourceHierarchyImport = AssetData.GetTagValueRef<FString>(TEXT("SourceHierarchyImport"));
			if (SourceHierarchyImport == SkeletonName)
			{
				return true;
			}
			FString SourceCurveImport = AssetData.GetTagValueRef<FString>(TEXT("SourceCurveImport"));
			if (SourceCurveImport == SkeletonName)
			{
				return true;
			}
			return false;
		}
		return true;

	}
	bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		const bool bChildOfObjectClass = InClass->IsChildOf(UControlRig::StaticClass());
		const bool bMatchesFlags = !InClass->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		const bool bNotNative = !InClass->IsNative();

		if (bChildOfObjectClass && bMatchesFlags && bNotNative)
		{
			FAssetData AssetData(InClass);
			return MatchesFilter(AssetData);

		}
		return false;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		const bool bChildOfObjectClass = InUnloadedClassData->IsChildOf(UControlRig::StaticClass());
		const bool bMatchesFlags = !InUnloadedClassData->HasAnyClassFlags(CLASS_Hidden | CLASS_HideDropDown | CLASS_Deprecated | CLASS_Abstract);
		if (bChildOfObjectClass && bMatchesFlags)
		{
			FString GeneratedClassPathString = InUnloadedClassData->GetClassPath().ToString();
			FName BlueprintPath = FName(*GeneratedClassPathString.LeftChop(2)); // Chop off _C
			FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(BlueprintPath);
			return MatchesFilter(AssetData);

		}
		return false;
	}

};

void FControlRigParameterTrackEditor::ConvertToFKControlRig(FGuid ObjectBinding, UObject* BoundObject, USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton)
{
	BakeToControlRig(UFKControlRig::StaticClass(), ObjectBinding, BoundObject, SkelMeshComp, Skeleton);
}

void FControlRigParameterTrackEditor::BakeToControlRigSubMenu(FMenuBuilder& MenuBuilder, FGuid ObjectBinding, UObject* BoundObject, USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton)
{
	const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();

	if (Skeleton)
	{
		FClassViewerInitializationOptions Options;
		Options.bShowUnloadedBlueprints = true;
		Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;
		TSharedPtr<FControlRigClassFilter> ClassFilter = MakeShareable(new FControlRigClassFilter(bFilterAssetBySkeleton,true, true, Skeleton));
		Options.ClassFilter = ClassFilter;
		Options.bShowNoneOption = false;

		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

		TSharedRef<SWidget> ClassViewer = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateRaw(this, &FControlRigParameterTrackEditor::BakeToControlRig, ObjectBinding, BoundObject, SkelMeshComp, Skeleton));
		MenuBuilder.AddWidget(ClassViewer, FText::GetEmpty(), true);
	}
}


class SBakeToAnimAndControlRigOptionsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBakeToAnimAndControlRigOptionsWindow)
		: _ExportOptions(nullptr), _BakeSettings(nullptr)
		, _WidgetWindow()
	{}

	SLATE_ARGUMENT(UAnimSeqExportOption*, ExportOptions)
	SLATE_ARGUMENT(UBakeToControlRigSettings*, BakeSettings)
	SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }

	FReply OnExport()
	{
		bShouldExport = true;
		if (WidgetWindow.IsValid())
		{
			WidgetWindow.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}


	FReply OnCancel()
	{
		bShouldExport = false;
		if (WidgetWindow.IsValid())
		{
			WidgetWindow.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			return OnCancel();
		}

		return FReply::Unhandled();
	}

	bool ShouldExport() const
	{
		return bShouldExport;
	}


	SBakeToAnimAndControlRigOptionsWindow()
		: ExportOptions(nullptr)
		, BakeSettings(nullptr)
		, bShouldExport(false)
	{}

private:

	FReply OnResetToDefaultClick() const;

private:
	UAnimSeqExportOption* ExportOptions;
	UBakeToControlRigSettings* BakeSettings;
	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<class IDetailsView> DetailsView2;
	TWeakPtr< SWindow > WidgetWindow;
	bool			bShouldExport;
};


void SBakeToAnimAndControlRigOptionsWindow::Construct(const FArguments& InArgs)
{
	ExportOptions = InArgs._ExportOptions;
	BakeSettings = InArgs._BakeSettings;
	WidgetWindow = InArgs._WidgetWindow;

	check(ExportOptions);

	FText CancelText = LOCTEXT("AnimSequenceOptions_Cancel", "Cancel");
	FText CancelTooltipText = LOCTEXT("AnimSequenceOptions_Cancel_ToolTip", "Cancel control rig creation");

	TSharedPtr<SBox> HeaderToolBox;
	TSharedPtr<SHorizontalBox> AnimHeaderButtons;
	TSharedPtr<SBox> InspectorBox;
	TSharedPtr<SBox> InspectorBox2;
	this->ChildSlot
		[
			SNew(SBox)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SAssignNew(HeaderToolBox, SBox)
		]
	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("CurveEd.LabelFont"))
		.Text(LOCTEXT("Export_CurrentFileTitle", "Current File: "))
		]
		]
		]
	+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2)
		[
			SAssignNew(InspectorBox, SBox)
		]
	+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(2)
		[
			SAssignNew(InspectorBox2, SBox)
		]
	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(2)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(2)
		+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
		.Text(LOCTEXT("Create", "Create"))
		.OnClicked(this, &SBakeToAnimAndControlRigOptionsWindow::OnExport)
		]
	+ SUniformGridPanel::Slot(2, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
		.Text(CancelText)
		.ToolTipText(CancelTooltipText)
		.OnClicked(this, &SBakeToAnimAndControlRigOptionsWindow::OnCancel)
		]
		]
			]
		];

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView2 = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	InspectorBox->SetContent(DetailsView->AsShared());
	InspectorBox2->SetContent(DetailsView2->AsShared());
	HeaderToolBox->SetContent(
		SNew(SBorder)
		.Padding(FMargin(3))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		[
			SNew(SBox)
			.HAlign(HAlign_Right)
		[
			SAssignNew(AnimHeaderButtons, SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(2.0f, 0.0f))
		[
			SNew(SButton)
			.Text(LOCTEXT("AnimSequenceOptions_ResetOptions", "Reset to Default"))
		.OnClicked(this, &SBakeToAnimAndControlRigOptionsWindow::OnResetToDefaultClick)
		]
		]
		]
		]
	);

	DetailsView->SetObject(ExportOptions);
	DetailsView2->SetObject(BakeSettings);
}

FReply SBakeToAnimAndControlRigOptionsWindow::OnResetToDefaultClick() const
{
	ExportOptions->ResetToDefault();
	//Refresh the view to make sure the custom UI are updating correctly
	DetailsView->SetObject(ExportOptions, true);
	return FReply::Handled();
}

void FControlRigParameterTrackEditor::BakeToControlRig(UClass* InClass, FGuid ObjectBinding, UObject* BoundActor, USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton)
{
	FSlateApplication::Get().DismissAllMenus();
	const TSharedPtr<ISequencer> SequencerParent = GetSequencer();

	if (InClass && InClass->IsChildOf(UControlRig::StaticClass()) && SequencerParent.IsValid())
	{

		UMovieSceneSequence* OwnerSequence = GetSequencer()->GetFocusedMovieSceneSequence();
		UMovieScene* OwnerMovieScene = OwnerSequence->GetMovieScene();
		{

			UAnimSequence* TempAnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None);
			TempAnimSequence->SetSkeleton(Skeleton);
			const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();
			FMovieSceneSequenceIDRef Template = ParentSequencer->GetFocusedTemplateID();
			FMovieSceneSequenceTransform RootToLocalTransform;
			UAnimSeqExportOption* AnimSeqExportOption = NewObject<UAnimSeqExportOption>(GetTransientPackage(), NAME_None);
			UBakeToControlRigSettings* BakeSettings = GetMutableDefault<UBakeToControlRigSettings>();

			TSharedPtr<SWindow> ParentWindow;
			if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
			{
				IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
				ParentWindow = MainFrame.GetParentWindow();
			}

			TSharedRef<SWindow> Window = SNew(SWindow)
				.Title(LOCTEXT("AnimSeqTitle", "Options For Baking"))
				.SizingRule(ESizingRule::UserSized)
				.AutoCenter(EAutoCenter::PrimaryWorkArea)
				.ClientSize(FVector2D(500, 445));

			TSharedPtr<SBakeToAnimAndControlRigOptionsWindow> OptionWindow;
			Window->SetContent
			(
				SAssignNew(OptionWindow, SBakeToAnimAndControlRigOptionsWindow)
				.ExportOptions(AnimSeqExportOption)
				.BakeSettings(BakeSettings)
				.WidgetWindow(Window)
			);

			FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

			if (OptionWindow.Get()->ShouldExport())
			{

				bool bResult = MovieSceneToolHelpers::ExportToAnimSequence(TempAnimSequence, AnimSeqExportOption, OwnerMovieScene, ParentSequencer.Get(), SkelMeshComp, Template, RootToLocalTransform);
				if (bResult == false)
				{
					TempAnimSequence->MarkPendingKill();
					AnimSeqExportOption->MarkPendingKill();
					return;
				}

				const FScopedTransaction Transaction(LOCTEXT("BakeToControlRig_Transaction", "Bake To Control Rig"));

				OwnerMovieScene->Modify();
				UMovieSceneControlRigParameterTrack* Track = OwnerMovieScene->FindTrack<UMovieSceneControlRigParameterTrack>(ObjectBinding);
				if (Track)
				{
					Track->Modify();
					for (UMovieSceneSection* Section : Track->GetAllSections())
					{
						Section->SetIsActive(false);
					}
				}
				else
				{
					Track = Cast<UMovieSceneControlRigParameterTrack>(AddTrack(OwnerMovieScene, ObjectBinding, UMovieSceneControlRigParameterTrack::StaticClass(), NAME_None));
					if (Track)
					{
						Track->Modify();
					}
				}


				if (Track)
				{

					FString ObjectName = InClass->GetName();
					ObjectName.RemoveFromEnd(TEXT("_C"));
					UControlRig* ControlRig = NewObject<UControlRig>(Track, InClass, FName(*ObjectName), RF_Transactional);
					if (InClass != UFKControlRig::StaticClass() && !ControlRig->SupportsEvent(FRigUnit_InverseExecution::EventName))
					{
						TempAnimSequence->MarkPendingKill();
						AnimSeqExportOption->MarkPendingKill();
						OwnerMovieScene->RemoveTrack(*Track);
						return;
					}

					FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
					if (!ControlRigEditMode)
					{
						GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
						ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

					}
					else
					{
						UControlRig* OldControlRig = ControlRigEditMode->GetControlRig(false);
						if (OldControlRig)
						{
							UnbindControlRig(OldControlRig);
						}
					}


					bool bSequencerOwnsControlRig = true;

					ControlRig->Modify();
					ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
					ControlRig->GetObjectBinding()->BindToObject(BoundActor);
					ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
					ControlRig->Initialize();
					ControlRig->Evaluate_AnyThread();

					UMovieSceneSection* NewSection = Track->CreateControlRigSection(0, ControlRig, bSequencerOwnsControlRig);
					UMovieSceneControlRigParameterSection* ParamSection = Cast<UMovieSceneControlRigParameterSection>(NewSection);

					//mz todo need to have multiple rigs with same class
					Track->SetTrackName(FName(*ObjectName));
					Track->SetDisplayName(FText::FromString(ObjectName));

					GetSequencer()->EmptySelection();
					GetSequencer()->SelectSection(NewSection);
					GetSequencer()->ThrobSectionSelection();
					GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
					ParamSection->LoadAnimSequenceIntoThisSection(TempAnimSequence, OwnerMovieScene, Skeleton,
						BakeSettings->bReduceKeys, BakeSettings->Tolerance);

					//Turn Off Any Skeletal Animation Tracks
					const FMovieSceneBinding* Binding = OwnerMovieScene->FindBinding(ObjectBinding);
					if (Binding)
					{
						for (UMovieSceneTrack* MovieSceneTrack : Binding->GetTracks())
						{
							if (UMovieSceneSkeletalAnimationTrack* SkelTrack = Cast<UMovieSceneSkeletalAnimationTrack>(MovieSceneTrack))
							{
								SkelTrack->Modify();
								//can't just turn off the track so need to mute the sections
								const TArray<UMovieSceneSection*>& Sections = SkelTrack->GetAllSections();
								for (UMovieSceneSection* Section : Sections)
								{
									if (Section)
									{
										Section->TryModify();
										Section->SetIsActive(false);
									}
								}
							}
						}
					}
					//Finish Setup
					if (ControlRigEditMode)
					{
						ControlRigEditMode->SetObjects(ControlRig, nullptr, GetSequencer());
					}
					BindControlRig(ControlRig);

					TempAnimSequence->MarkPendingKill();
					AnimSeqExportOption->MarkPendingKill();
					GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
					

				}
			}
		}
	}
}

void FControlRigParameterTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()) || ObjectClass->IsChildOf(UChildActorComponent::StaticClass()))
	{
		const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();
		UObject *BoundObject = nullptr;
		USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBindings[0], &BoundObject, ParentSequencer);

		if (AActor* BoundActor = Cast<AActor>(BoundObject))
		{
			if (UControlRigComponent* ControlRigComponent = BoundActor->FindComponentByClass<UControlRigComponent>())
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("AddControlRig", "Animation ControlRig"),
					NSLOCTEXT("Sequencer", "AddControlRigTooltip", "Adds an animation Control Rig track"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FControlRigParameterTrackEditor::AddControlRigFromComponent, ObjectBindings[0]),
						FCanExecuteAction()
					)
				);
				return;
			}
		}

		if (Skeleton)
		{
			//if there are any other control rigs we don't allow it for now..
			//mz todo will allow later
			UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
			UMovieSceneControlRigParameterTrack* ExistingTrack = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), ObjectBindings[0], NAME_None));
			if (!ExistingTrack)
			{
				UMovieSceneTrack* Track = nullptr;

				MenuBuilder.AddSubMenu(LOCTEXT("ControlRigText", "Control Rig"), FText(), FNewMenuDelegate::CreateSP(this, &FControlRigParameterTrackEditor::HandleAddTrackSubMenu, ObjectBindings, Track));
			}
		}
	}
}


void FControlRigParameterTrackEditor::HandleAddTrackSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddFKControlRig", "FK Control Rig"),
		NSLOCTEXT("Sequencer", "AddFKControlRigTooltip", "Adds an FK Control Rig track"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FControlRigParameterTrackEditor::AddFKControlRig, ObjectBindings),
			FCanExecuteAction()
		)
	);

	MenuBuilder.AddMenuEntry(
		NSLOCTEXT("Sequencer", "FilterAssetBySkeleton", "Filter Asset By Skeleton"),
		NSLOCTEXT("Sequencer", "FilterAssetBySkeletonTooltip", "Filters Control Rig assets to match current skeleton"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FControlRigParameterTrackEditor::ToggleFilterAssetBySkeleton),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FControlRigParameterTrackEditor::IsToggleFilterAssetBySkeleton)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddMenuEntry(
		NSLOCTEXT("Sequencer", "FilterAssetByAnimatableControls", "Filter Asset By Animatable Controls"),
		NSLOCTEXT("Sequencer", "FilterAssetByAnimatableControlsTooltip", "Filters Control Rig assets to only show those with Animatable Controls"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FControlRigParameterTrackEditor::ToggleFilterAssetByAnimatableControls),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &FControlRigParameterTrackEditor::IsToggleFilterAssetByAnimatableControls)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);

	MenuBuilder.AddSubMenu(
		LOCTEXT("AddAssetControlRig", "Asset-Based Control Rig"),
		NSLOCTEXT("Sequencer", "AddAsetControlRigTooltip", "Adds an asset based Control Rig track"),
		FNewMenuDelegate::CreateRaw(this, &FControlRigParameterTrackEditor::HandleAddControlRigSubMenu, ObjectBindings, Track)
	);
}

void FControlRigParameterTrackEditor::ToggleFilterAssetBySkeleton()
{
	bFilterAssetBySkeleton = bFilterAssetBySkeleton ? false : true;
}

bool FControlRigParameterTrackEditor::IsToggleFilterAssetBySkeleton()
{
	return bFilterAssetBySkeleton;
}

void FControlRigParameterTrackEditor::ToggleFilterAssetByAnimatableControls()
{
	bFilterAssetByAnimatableControls = bFilterAssetByAnimatableControls ? false : true;

}

bool FControlRigParameterTrackEditor::IsToggleFilterAssetByAnimatableControls()
{
	return bFilterAssetByAnimatableControls;
}

void FControlRigParameterTrackEditor::HandleAddControlRigSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track)
{
	/*
	MenuBuilder.BeginSection(TEXT("ChooseSequence"), LOCTEXT("ChooseSequence", "Choose Sequence"));
	{
	FAssetPickerConfig AssetPickerConfig;
	{
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FControlRigTrackEditor::OnSequencerAssetSelected, ObjectBindings, Track);
	AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this, &FControlRigTrackEditor::OnSequencerAssetEnterPressed, ObjectBindings, Track);
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.Filter.ClassNames.Add(UControlRigSequence::StaticClass()->GetFName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
	.WidthOverride(300.0f)
	.HeightOverride(300.0f)
	[
	ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
	];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
	}
	MenuBuilder.EndSection();
	*/


	const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();
	UObject *BoundObject = nullptr;
	//todo support multiple bindings?
	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBindings[0], &BoundObject, GetSequencer());

	if (Skeleton)
	{
		//MenuBuilder.AddSubMenu(
		//	LOCTEXT("AddControlRigTrack", "ControlRigTrack"), NSLOCTEXT("ControlRig", "AddControlRigTrack", "Adds a Control Rigtrack."),
		//	FNewMenuDelegate::CreateRaw(this, &FControlRigParameterTrackEditor::AddAnimationSubMenu, BoundObject, ObjectBindings[0], Skeleton)
		//);

		FClassViewerInitializationOptions Options;
		Options.bShowUnloadedBlueprints = true;
		Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;

		TSharedPtr<FControlRigClassFilter> ClassFilter = MakeShareable(new FControlRigClassFilter(bFilterAssetBySkeleton, bFilterAssetByAnimatableControls, false, Skeleton));
		Options.ClassFilter = ClassFilter;
		Options.bShowNoneOption = false;

		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
		//	FNewMenuDelegate::CreateRaw(this, &FControlRigParameterTrackEditor::AddAnimationSubMenu, BoundObject, ObjectBindings[0], Skeleton)



		TSharedRef<SWidget> ClassViewer = ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateRaw(this, &FControlRigParameterTrackEditor::AddControlRig, BoundObject, ObjectBindings[0]));
		MenuBuilder.AddWidget(ClassViewer, FText::GetEmpty(), true);

		/*
		FAssetPickerConfig AssetPickerConfig;
		{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FControlRigParameterTrackEditor::OnControlRigAssetSelected, ObjectBindings, Skeleton);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this, &FControlRigParameterTrackEditor::OnControlRigAssetEnterPressed, ObjectBindings, Skeleton);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateRaw(this, &FControlRigParameterTrackEditor::ShouldFilterAsset);
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.Filter.ClassNames.Add((UControlRig::StaticClass())->GetFName());
		AssetPickerConfig.Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(Skeleton).GetExportTextName());
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

		TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
		ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

		MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
		*/
	}

}

/*
bool FControlRigParameterTrackEditor::ShouldFilterAsset(const FAssetData& AssetData)
{
	if (AssetData.AssetClass == UControlRig::StaticClass()->GetFName())
	{
		return true;
	}
	return false;
}

void FControlRigParameterTrackEditor::OnControlRigAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings, USkeleton* Skeleton)
{
	FSlateApplication::Get().DismissAllMenus();

	UObject* SelectedObject = AssetData.GetAsset();todo
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	if (SelectedObject && SelectedObject->IsA(UControlRig::StaticClass()) && SequencerPtr.IsValid())
	{
		UControlRig* ControlRig = CastChecked<UControlRig>(AssetData.GetAsset());

		const FScopedTransaction Transaction(LOCTEXT("AddAnimation_Transaction", "Add Animation"));

		for (FGuid ObjectBinding : ObjectBindings)
		{
			UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
			int32 RowIndex = INDEX_NONE;
			//AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FControlRigParameterTrackEditor::AddControlRig, BoundObject, ObjectBindings[0], Skeleton)));
		}
	}
*/

void FControlRigParameterTrackEditor::AddControlRig(UClass* InClass, UObject* BoundActor, FGuid ObjectBinding, UControlRig* InExistingControlRig)
{
	FSlateApplication::Get().DismissAllMenus();
	const TSharedPtr<ISequencer> SequencerParent = GetSequencer();

	if (InClass && InClass->IsChildOf(UControlRig::StaticClass()) && SequencerParent.IsValid())
	{
		UMovieSceneSequence* OwnerSequence = GetSequencer()->GetFocusedMovieSceneSequence();
		UMovieScene* OwnerMovieScene = OwnerSequence->GetMovieScene();
		FScopedTransaction AddControlRigTrackTransaction(LOCTEXT("AddControlRigTrack_Transaction", "Add Control Rig Track"));

		OwnerSequence->Modify();
		OwnerMovieScene->Modify();
		UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(AddTrack(OwnerMovieScene, ObjectBinding, UMovieSceneControlRigParameterTrack::StaticClass(), NAME_None));
		if (Track)
		{
			FString ObjectName = InClass->GetName(); //GetDisplayNameText().ToString();
			ObjectName.RemoveFromEnd(TEXT("_C"));

			bool bSequencerOwnsControlRig = false;
			UControlRig* ControlRig = InExistingControlRig;
			if (ControlRig == nullptr)
			{
				ControlRig = NewObject<UControlRig>(Track, InClass, FName(*ObjectName), RF_Transactional);
				bSequencerOwnsControlRig = true;
			}

			ControlRig->Modify();
			ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
			ControlRig->GetObjectBinding()->BindToObject(BoundActor);
			ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
			ControlRig->Initialize();
			ControlRig->Evaluate_AnyThread();

			SequencerParent->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

			Track->Modify();
			UMovieSceneSection* NewSection = Track->CreateControlRigSection(0, ControlRig, bSequencerOwnsControlRig);
			NewSection->Modify();

			//mz todo need to have multiple rigs with same class
			Track->SetTrackName(FName(*ObjectName));
			Track->SetDisplayName(FText::FromString(ObjectName));

			GetSequencer()->EmptySelection();
			GetSequencer()->SelectSection(NewSection);
			GetSequencer()->ThrobSectionSelection();
			GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);

			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
			if (!ControlRigEditMode)
			{
				GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
				ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

			}
			if (ControlRigEditMode)
			{
				ControlRigEditMode->SetObjects(ControlRig, nullptr, GetSequencer());
			}
			BindControlRig(ControlRig);
			
		}
	}
}

void FControlRigParameterTrackEditor::AddControlRig(UClass* InClass, UObject* BoundActor, FGuid ObjectBinding)
{
	AddControlRig(InClass, BoundActor, ObjectBinding, nullptr);
}

//This now adds all of the control rig components, not just the first one
void FControlRigParameterTrackEditor::AddControlRigFromComponent(FGuid InGuid)
{
	const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();
	UObject* BoundObject = ParentSequencer.IsValid() ? ParentSequencer->FindSpawnedObjectOrTemplate(InGuid) : nullptr;

	if (AActor* BoundActor = Cast<AActor>(BoundObject))
	{
		TArray<UControlRigComponent*> ControlRigComponents;
		BoundActor->GetComponents<UControlRigComponent>(ControlRigComponents);
		for (UControlRigComponent* ControlRigComponent : ControlRigComponents)
		{
			if (UControlRig* CR = ControlRigComponent->GetControlRig())
			{
				AddControlRig(CR->GetClass(), BoundActor, InGuid, CR);
			}
			
		}
	}
}

void FControlRigParameterTrackEditor::AddFKControlRig(TArray<FGuid> ObjectBindings)
{
	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		UObject *BoundObject = nullptr;
		AcquireSkeletonFromObjectGuid(ObjectBinding, &BoundObject, GetSequencer());
		if (BoundObject)
		{
			AddControlRig(UFKControlRig::StaticClass(), BoundObject, ObjectBinding);
		}
	}
}

bool FControlRigParameterTrackEditor::HasTransformKeyOverridePriority() const
{
	return CanAddTransformKeysForSelectedObjects();

}
bool FControlRigParameterTrackEditor::CanAddTransformKeysForSelectedObjects() const
{
	// WASD hotkeys to fly the viewport can conflict with hotkeys for setting keyframes (ie. s). 
	// If the viewport is moving, disregard setting keyframes.
	for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
	{
		if (LevelVC && LevelVC->IsMovingCamera())
		{
			return false;
		}
	}

	if (!GetSequencer()->IsAllowedToChange())
	{
		return false;
	}

	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode && ControlRigEditMode->GetControlRig(false))
	{
		UControlRig* ControlRig = ControlRigEditMode->GetControlRig(false);
		FString OurName = ControlRig->GetName();
		FName Name(*OurName);
		if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
		{
			TArray<FName> ControlNames = ControlRig->CurrentControlSelection();
			return (ControlNames.Num() > 0);
		}
	}
	return false;
}

void FControlRigParameterTrackEditor::OnAddTransformKeysForSelectedObjects(EMovieSceneTransformChannel Channel)
{
	if (!GetSequencer()->IsAllowedToChange())
	{
		return;
	}

	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode && ControlRigEditMode->GetControlRig(false))
	{
		UControlRig* ControlRig = ControlRigEditMode->GetControlRig(false);
		FString OurName = ControlRig->GetName();
		FName Name(*OurName);
		if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
		{
			TArray<FName> ControlNames = ControlRig->CurrentControlSelection();
			for (const FName& ControlName : ControlNames)
			{
				USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
				if (Component)
				{
					AddControlKeys(Component, ControlRig, Name, ControlName, Channel, ESequencerKeyMode::ManualKeyForced, FLT_MAX);
				}
			}
		}
	}
}

void FControlRigParameterTrackEditor::OnChannelChanged(const FMovieSceneChannelMetaData* MetaData, UMovieSceneSection* InSection)
{
	UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(InSection);
	if (Section && Section->GetControlRig() && MetaData)
	{
		Section->ControlsToSet.Empty();
		TArray<FString> StringArray;
		FString String = MetaData->Name.ToString();
		String.ParseIntoArray(StringArray, TEXT("."));
		if (StringArray.Num() > 0)
		{
			FName ControlName(*StringArray[0]);
			Section->ControlsToSet.Add(ControlName);
			FControlRigInteractionScope InteractionScope(Section->GetControlRig());
			GetSequencer()->ForceEvaluate(); //now run sequencer...
			Section->GetControlRig()->Evaluate_AnyThread();
			Section->ControlsToSet.Empty();
		}
	}
}

void FControlRigParameterTrackEditor::AddTrackForComponent(USceneComponent* InComponent)
{
	if (USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(InComponent))
	{
		if (SkelMeshComp->SkeletalMesh && !SkelMeshComp->SkeletalMesh->GetDefaultAnimatingRig().IsNull())
		{
			UObject* Object = SkelMeshComp->SkeletalMesh->GetDefaultAnimatingRig().LoadSynchronous();
			if (Object != nullptr && (Object->IsA<UControlRigBlueprint>() || Object->IsA<UControlRigComponent>()))
			{
				FGuid Binding = GetSequencer()->GetHandleToObject(InComponent, true /*bCreateHandle*/);
				if (Binding.IsValid())
				{
					UMovieSceneSequence* OwnerSequence = GetSequencer()->GetFocusedMovieSceneSequence();
					UMovieScene* OwnerMovieScene = OwnerSequence->GetMovieScene();
					UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(OwnerMovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding, NAME_None));
					if (Track == nullptr)
					{
						if (UControlRigBlueprint* BPControlRig = Cast<UControlRigBlueprint>(Object))
						{
							if (UControlRigBlueprintGeneratedClass* RigClass = BPControlRig->GetControlRigBlueprintGeneratedClass())
							{
								if (UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(true /* create if needed */)))
								{
									AddControlRig(CDO->GetClass(), InComponent, Binding);
								}
							}
						}
					}
				}
			}
		}
	}
	TArray<USceneComponent*> ChildComponents;
	InComponent->GetChildrenComponents(false, ChildComponents);
	for (USceneComponent* ChildComponent : ChildComponents)
	{
		AddTrackForComponent(ChildComponent);
	}
}

void FControlRigParameterTrackEditor::HandleActorAdded(AActor* Actor, FGuid TargetObjectGuid)
{
	if (Actor)
	{
		if (UControlRigComponent* ControlRigComponent = Actor->FindComponentByClass<UControlRigComponent>())
		{
			AddControlRigFromComponent(TargetObjectGuid);
			return;
		}
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
			{
				AddTrackForComponent(SceneComp);
			}
		}
	}
}

void FControlRigParameterTrackEditor::OnActivateSequenceChanged(FMovieSceneSequenceIDRef ID)
{
	UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
	//register all modified/selections for control rigs
	const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None));
		if (Track && Track->GetControlRig())
		{
			BindControlRig(Track->GetControlRig());
		}
	}
}


void FControlRigParameterTrackEditor::OnSequencerDataChanged(EMovieSceneDataChangeType DataChangeType)
{
	UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

	//if we have a valid control rig edit mode need to check and see the control rig in that mode is still in a track
	//if not we get rid of it.
	if (ControlRigEditMode && ControlRigEditMode->GetControlRig(false) != nullptr && MovieScene && (DataChangeType == EMovieSceneDataChangeType::MovieSceneStructureItemRemoved ||
		DataChangeType == EMovieSceneDataChangeType::Unknown))
	{
		float FPS = 1.f / (float)GetSequencer()->GetFocusedDisplayRate().AsInterval();
		ControlRigEditMode->GetControlRig(false)->SetFramesPerSecond(FPS);

		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None));
			if (Track && Track->GetControlRig() == ControlRigEditMode->GetControlRig(false))
			{
				return; //just exit out we still have a good track
			}
		}
		//okay no good track so deactive it and delete it's Control Rig and bingings.
		if (GLevelEditorModeTools().HasToolkitHost())
		{
			GLevelEditorModeTools().DeactivateMode(FControlRigEditMode::ModeName);
		}
		ControlRigEditMode->SetObjects(nullptr, nullptr, GetSequencer());
	}
}

void FControlRigParameterTrackEditor::OnCurveDisplayChanged(FCurveModel* CurveModel, bool bDisplayed)
{
	if (bIsDoingSelection)
	{
		return;
	}
	TGuardValue<bool> Guard(bIsDoingSelection, true);
	FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), !GIsTransacting);

	TArray<FString> StringArray;
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	UControlRig* ControlRig = nullptr;

	if (CurveModel)
	{
		UMovieSceneControlRigParameterSection* MovieSection = Cast<UMovieSceneControlRigParameterSection>(CurveModel->GetOwningObject());
		if (MovieSection)
		{
			ControlRig = MovieSection->GetControlRig();
			//Only create the edit mode if we have a  curve selected and it's not set and we have some boundobjects.
			if (!ControlRigEditMode)
			{
				GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
				ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
				if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
				{
					if (ControlRigEditMode)
					{
						ControlRigEditMode->SetObjects(ControlRig, nullptr, GetSequencer());
					}
				}
			}
			else
			{
				if (ControlRigEditMode->GetControlRig(false) != ControlRig)
				{
					ControlRigEditMode->SetObjects(ControlRig, nullptr, GetSequencer());
				}
			}
			//Not 100% safe but for now it is since that's all we show in the curve editor
			//We need the Float Curve Model so we can get the ChannelHandle so we can also select the keyarea in the sequencer window if needed.
			FFloatChannelCurveModel* FCurveModel = static_cast<FFloatChannelCurveModel*>(CurveModel);
			FString String = CurveModel->GetLongDisplayName().ToString();
			StringArray.SetNum(0);
			String.ParseIntoArray(StringArray, TEXT("."));
			if (StringArray.Num() > 2)
			{
				//Not great but it should always be the third name
				FName ControlName(*StringArray[2]);
				ControlRig->SelectControl(ControlName, bDisplayed);
				if (bDisplayed)
				{
					DisplayedControls.Add(ControlName);
				}
				else
				{
					UnDisplayedControls.Add(ControlName);
				}
			}
			else
			{
				UE_LOG(LogControlRigEditor, Display, TEXT("Could not find Rig Control From FCurveModel::LongName"));
			}

			if (bCurveDisplayTickIsPending == false)
			{
				bCurveDisplayTickIsPending = true;
				GEditor->GetTimerManager()->SetTimerForNextTick([MovieSection, bDisplayed, this]()
				{

					if (DisplayedControls.Num() > 0 || UnDisplayedControls.Num() > 0)
					{
						TGuardValue<bool> Guard(bIsDoingSelection, true);
						UMovieSceneControlRigParameterSection* ParamSection = Cast<UMovieSceneControlRigParameterSection>(MovieSection);
						bool bSync = GetSequencer()->GetSequencerSettings()->ShouldSyncCurveEditorSelection();
						GetSequencer()->SuspendSelectionBroadcast();
						GetSequencer()->GetSequencerSettings()->SyncCurveEditorSelection(false);
						if (UnDisplayedControls.Num() > 0)
						{
							for (const FName& ControlName : UnDisplayedControls)
							{
								SelectSequencerNodeInSection(ParamSection, ControlName, false);
							}
							UnDisplayedControls.Empty();
						}
						if (DisplayedControls.Num() > 0)
						{
							for (const FName& ControlName : DisplayedControls)
							{
								SelectSequencerNodeInSection(ParamSection, ControlName, true);
							}
							DisplayedControls.Empty();
						}
						GetSequencer()->ResumeSelectionBroadcast(); //need to resume first so when we refreh the tree we do the Selection.Tick, which since syncing is off won't 
																	//mess up the curve editor.
						GetSequencer()->RefreshTree();
						GetSequencer()->GetSequencerSettings()->SyncCurveEditorSelection(bSync);
					};
					bCurveDisplayTickIsPending = false;
	
				});

			}
		}
		
	}
}

void FControlRigParameterTrackEditor::PostEvaluation(UMovieScene* MovieScene, FFrameNumber Frame)
{
	if (MovieScene)
	{
		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			if (UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None)))
			{
				if (UControlRig* ControlRig = Track->GetControlRig())
				{
					if (ControlRig->GetObjectBinding())
					{
						if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
						{
							ControlRigComponent->Update(.1); //delta time doesn't matter.
						}
					}
				}
			}
		}
	}
}

void FControlRigParameterTrackEditor::OnSelectionChanged(TArray<UMovieSceneTrack*> InTracks)
{
	if (bIsDoingSelection || GetSequencer().IsValid() == false)
	{
		return;
	}
	
	TGuardValue<bool> Guard(bIsDoingSelection, true);

	TArray<FString> StringArray;
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	UControlRig* ControlRig = nullptr;

	TArray<const IKeyArea*> KeyAreas;
	GetSequencer()->GetSelectedKeyAreas(KeyAreas);
	FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"), !GIsTransacting);

	if (KeyAreas.Num() <= 0)
	{
		if (ControlRigEditMode)
		{
			ControlRig = ControlRigEditMode->GetControlRig(false);
			if (ControlRig)
			{
				ControlRig->ClearControlSelection();
			}
		}
		for (UMovieSceneTrack* Track : InTracks)
		{
			UMovieSceneControlRigParameterTrack* CRTrack = Cast<UMovieSceneControlRigParameterTrack>(Track);
			if (CRTrack)
			{
				UControlRig* TrackControlRig = CRTrack->GetControlRig();
				if (TrackControlRig)
				{
					if (ControlRigEditMode)
					{
						ControlRig = ControlRigEditMode->GetControlRig(false);
						if (ControlRig != TrackControlRig)
						{
							ControlRigEditMode->SetObjects(TrackControlRig, nullptr, GetSequencer());
						}
						break;
					}
					else
					{
						GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
						ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
						if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = TrackControlRig->GetObjectBinding())
						{
							if (ControlRigEditMode)
							{
								ControlRigEditMode->SetObjects(TrackControlRig, nullptr, GetSequencer());
							}
						}
					}
				}
			}
		}
		return;
	}

	TMap<UControlRig *, TSet<FName>> RigsAndControls;
	for (const IKeyArea* KeyArea : KeyAreas)
	{
		UMovieSceneControlRigParameterSection* MovieSection = Cast<UMovieSceneControlRigParameterSection>(KeyArea->GetOwningSection());
		if (MovieSection)
		{
			ControlRig = MovieSection->GetControlRig();
			//Only create the edit mode if we have a KeyAra selected and it's not set and we have some boundobjects.
			if (!ControlRigEditMode)
			{
				GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
				ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
				if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
				{
					if (ControlRigEditMode)
					{
						ControlRigEditMode->SetObjects(ControlRig, nullptr, GetSequencer());
					}
				}
			}
			else
			{
				if (ControlRigEditMode->GetControlRig(false) != ControlRig)
				{
					if (ControlRigEditMode->GetControlRig(false))
					{
						ControlRigEditMode->GetControlRig(false)->ClearControlSelection();
					}
					ControlRigEditMode->SetObjects(ControlRig, nullptr, GetSequencer());
					//force an evaluation, this will get the control rig setup so edit mode looks good.
					if (GetSequencer().IsValid())
					{
						GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
					}
				}
			}

			const FMovieSceneChannelMetaData* MetaData = KeyArea->GetChannel().GetMetaData();
			if (MetaData)
			{
				StringArray.SetNum(0);
				FString String = MetaData->Name.ToString();
				String.ParseIntoArray(StringArray, TEXT("."));
				if (StringArray.Num() > 0)
				{
					FName ControlName(*StringArray[0]);
					RigsAndControls.FindOrAdd(ControlRig).Add(ControlName);
				}
			}
		}
	}

	ControlRig = nullptr;
	//Always clear the control rig(s) in the edit mode.
	if (ControlRigEditMode)
	{
		ControlRig = ControlRigEditMode->GetControlRig(false);
		if (ControlRig)
		{
			ControlRig->ClearControlSelection();
		}
	}
	for (TPair<UControlRig *, TSet<FName>> Pair : RigsAndControls)
	{
		if (Pair.Key != ControlRig)
		{
			Pair.Key->ClearControlSelection();
		}
		for (const FName& Name : Pair.Value)
		{
			Pair.Key->SelectControl(Name, true);
		}
	}
}


FMovieSceneTrackEditor::FFindOrCreateHandleResult FControlRigParameterTrackEditor::FindOrCreateHandleToSceneCompOrOwner(USceneComponent* InComp)
{
	const bool bCreateHandleIfMissing = false;
	FName CreatedFolderName = NAME_None;

	FFindOrCreateHandleResult Result;
	bool bHandleWasValid = GetSequencer()->GetHandleToObject(InComp, bCreateHandleIfMissing).IsValid();

	Result.Handle = GetSequencer()->GetHandleToObject(InComp, bCreateHandleIfMissing, CreatedFolderName);
	Result.bWasCreated = bHandleWasValid == false && Result.Handle.IsValid();

	UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();

	// Prioritize a control rig parameter track on this component
	if (Result.Handle.IsValid())
	{
		if (MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Result.Handle, NAME_None))
		{
			return Result;
		}
	}

	// If the owner has a control rig parameter track, let's use it
	UObject* OwnerObject = InComp->GetOwner();
	FGuid OwnerHandle = GetSequencer()->GetHandleToObject(OwnerObject, bCreateHandleIfMissing);
	bHandleWasValid = OwnerHandle.IsValid();
	if (OwnerHandle.IsValid())
	{
		if (MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), OwnerHandle, NAME_None))
		{
			Result.Handle = OwnerHandle;
			Result.bWasCreated = bHandleWasValid == false && Result.Handle.IsValid();
			return Result;
		}
	}

	// If the component handle doesn't exist, let's use the owner handle
	if (Result.Handle.IsValid() == false)
	{
		Result.Handle = OwnerHandle;
		Result.bWasCreated = bHandleWasValid == false && Result.Handle.IsValid();

	}
	return Result;
}

void FControlRigParameterTrackEditor::SelectSequencerNodeInSection(UMovieSceneControlRigParameterSection* ParamSection, const FName& ControlName, bool bSelected)
{
	if (ParamSection)
	{
		FChannelMapInfo* pChannelIndex = ParamSection->ControlChannelMap.Find(ControlName);
		if (pChannelIndex != nullptr)
		{
			if (pChannelIndex->ParentControlIndex == INDEX_NONE)
			{
				GetSequencer()->SelectByNthCategoryNode(ParamSection, pChannelIndex->ControlIndex, bSelected);
			}
			else
			{
				const FName FloatChannelTypeName = FMovieSceneFloatChannel::StaticStruct()->GetFName();

				FMovieSceneChannelProxy& ChannelProxy = ParamSection->GetChannelProxy();
				for (const FMovieSceneChannelEntry& Entry : ParamSection->GetChannelProxy().GetAllEntries())
				{
					const FName ChannelTypeName = Entry.GetChannelTypeName();
					if (pChannelIndex->ChannelTypeName == ChannelTypeName || (ChannelTypeName == FloatChannelTypeName && pChannelIndex->ChannelTypeName == NAME_None))
					{
						FMovieSceneChannelHandle Channel = ChannelProxy.MakeHandle(ChannelTypeName, pChannelIndex->ChannelIndex);
						TArray<FMovieSceneChannelHandle> Channels;
						Channels.Add(Channel);
						GetSequencer()->SelectByChannels(ParamSection, Channels, false, bSelected);
						break;
					}
				}
			}
		}
	}
}


void FControlRigParameterTrackEditor::HandleControlSelected(UControlRig* Subject, const FRigControl& Control, bool bSelected)
{
	//if parent selected we select child here if it's a bool,integer or single float
	TArray<FRigControl> Controls;
	FRigControlHierarchy& ControlHierarchy = Subject->GetControlHierarchy();
	for (const FRigControl& OtherControl : ControlHierarchy.GetControls())
	{
		if (OtherControl.ParentIndex == Control.Index &&
			(OtherControl.ControlType == ERigControlType::Bool ||
				OtherControl.ControlType == ERigControlType::Float ||
				OtherControl.ControlType == ERigControlType::Integer))
		{
			Subject->SelectControl(OtherControl.Name, bSelected);
		}
	}

	if (bIsDoingSelection)
	{
		return;
	}
	TGuardValue<bool> Guard(bIsDoingSelection, true);

	FName ControlRigName(*Subject->GetName());
	if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = Subject->GetObjectBinding())
	{
		UObject *ActorObject = nullptr;
		USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
		if (!Component)
		{
			return;
		}
		ActorObject = Component->GetOwner();
		bool bCreateTrack = false;
		FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToSceneCompOrOwner(Component);
		FGuid ObjectHandle = HandleResult.Handle;
		if (!ObjectHandle.IsValid())
		{
			return;
		}

		FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, UMovieSceneControlRigParameterTrack::StaticClass(), ControlRigName, bCreateTrack);
		UMovieSceneControlRigParameterTrack* Track = CastChecked<UMovieSceneControlRigParameterTrack>(TrackResult.Track, ECastCheckedType::NullAllowed);
		if (Track)
		{
			GetSequencer()->SuspendSelectionBroadcast();
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				UMovieSceneControlRigParameterSection* ParamSection = Cast<UMovieSceneControlRigParameterSection>(Section);
				SelectSequencerNodeInSection(ParamSection,Control.Name, bSelected);
			}
			GetSequencer()->ResumeSelectionBroadcast();

			//Force refresh now, not later
			GetSequencer()->RefreshTree();

		}
	}
}

//REMOVE ME IN UE5
void FControlRigParameterTrackEditor::OnPropagateObjectChanges(UObject* InChangedObject)
{
	//not needed
	/*
	if (AActor* Actor = Cast<AActor>(InChangedObject))
	{
		if (UMovieScene* MovieScene = GetFocusedMovieScene())
		{
			const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
			for (const FMovieSceneBinding& Binding : Bindings)
			{
				if (UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None)))
				{
					if (UControlRig* ControlRig = Track->GetControlRig())
					{
						if (ControlRig->GetObjectBinding())
						{
							if (USceneComponent* SceneComponent = Cast<USceneComponent>(ControlRig->GetObjectBinding()->GetBoundObject()))
							{
								if (SceneComponent->GetOwner() == Actor)
								{
									if (GetSequencer().IsValid())
									{
										GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::Unknown);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	*/
}

void FControlRigParameterTrackEditor::HandleOnInitialized(UControlRig* ControlRig, const EControlRigState InState, const FName& InEventName)
{
	if (GetSequencer().IsValid())
	{
		//If FK control rig on next tick we refresh the tree
		if (ControlRig->IsA<UFKControlRig>())
		{
			GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		}
	}
}


void FControlRigParameterTrackEditor::HandleControlModified(UControlRig* ControlRig, const FRigControl& Control, const FRigControlModifiedContext& Context)
{
	if (GetSequencer().IsValid() && !GetSequencer()->IsAllowedToChange())
	{
		return;
	}
	UMovieScene* MovieScene = GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene();
	const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None));
		if (Track && Track->GetControlRig() == ControlRig)
		{
			FName Name(*ControlRig->GetName());
			if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
			{
				USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
				if (Component)
				{
					ESequencerKeyMode KeyMode = ESequencerKeyMode::AutoKey;
					if (Context.SetKey == EControlRigSetKey::Always)
					{
						KeyMode = ESequencerKeyMode::ManualKeyForced;
					}
					else if (Context.SetKey == EControlRigSetKey::Never)
					{
						KeyMode = ESequencerKeyMode::ManualKey; //best we have here
					}
					AddControlKeys(Component, ControlRig, Name, Control.Name, EMovieSceneTransformChannel::All, KeyMode, Context.LocalTime);
				}
			}
		}
	}
}

void FControlRigParameterTrackEditor::GetControlRigKeys(UControlRig* InControlRig, FName ParameterName, EMovieSceneTransformChannel ChannelsToKey, UMovieSceneControlRigParameterSection* SectionToKey, FGeneratedTrackKeys& OutGeneratedKeys)
{
	const TArray<bool>& ControlsMask = SectionToKey->GetControlsMask();
	EMovieSceneTransformChannel TransformMask = SectionToKey->GetTransformMask().GetChannels();

	TArray<FRigControl> Controls;
	InControlRig->GetControlsInOrder(Controls);
	// If key all is enabled, for a key on all the channels
	if (GetSequencer()->GetKeyGroupMode() == EKeyGroupMode::KeyAll)
	{
		ChannelsToKey = EMovieSceneTransformChannel::All;
	}

	//Need seperate index fo bools,ints and enums and floats since there are seperate entries for each later when they are accessed by the set key stuff.
	int32 ChannelIndex = 0;
	int32 BoolChannelIndex = 0;
	int32 EnumChannelIndex = 0;
	int32 IntChannelIndex = 0;
	for (int32 ControlIndex = 0; ControlIndex < Controls.Num(); ++ControlIndex)
	{
		const FRigControl& RigControl = Controls[ControlIndex];

		if (!RigControl.bAnimatable)
		{
			continue;
		}

		bool bMaskKeyOut = (ControlIndex >= ControlsMask.Num() || ControlsMask[ControlIndex] == false);

		bool bSetKey = RigControl.Name == ParameterName && !bMaskKeyOut;

		switch (RigControl.ControlType)
		{
		case ERigControlType::Bool:
		{
			bool Val = RigControl.Value.Get<bool>();
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneBoolChannel>(BoolChannelIndex++, Val, bSetKey));
			break;
		}
		case ERigControlType::Integer:
		{
			if (RigControl.ControlEnum)
			{
				uint8 Val = (uint8)RigControl.Value.Get<uint8>();
				OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneByteChannel>(EnumChannelIndex++, Val, bSetKey));
			}
			else
			{
				int32 Val = RigControl.Value.Get<int32>();
				OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneIntegerChannel>(IntChannelIndex++, Val, bSetKey));
			}
			break;
		}
		case ERigControlType::Float:
		{
			float Val = RigControl.Value.Get<float>();
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, Val, bSetKey));
			break;
		}
		case ERigControlType::Vector2D:
		{
			FVector2D Val = RigControl.Value.Get<FVector2D>();
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, Val.X, bSetKey));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, Val.Y, bSetKey));
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		{
			FVector Val = RigControl.Value.Get<FVector>();
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, Val.X, bSetKey));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, Val.Y, bSetKey));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, Val.Z, bSetKey));
			break;
		}

		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			FVector Translation, Scale(1.0f, 1.0f, 1.0f);
			FRotator Rotation;

			if (RigControl.ControlType == ERigControlType::TransformNoScale)
			{
				FTransformNoScale NoScale = RigControl.Value.Get<FTransformNoScale>();
				Translation = NoScale.Location;
				Rotation = NoScale.Rotation.Rotator();
			}
			else if (RigControl.ControlType == ERigControlType::EulerTransform)
			{
				FEulerTransform Euler = RigControl.Value.Get < FEulerTransform >();
				Translation = Euler.Location;
				Rotation = Euler.Rotation;
				Scale = Euler.Scale;
			}
			else
			{
				FTransform Val = RigControl.Value.Get<FTransform>();
				Translation = Val.GetTranslation();
				Rotation = Val.GetRotation().Rotator();
				Scale = Val.GetScale3D();
			}
			FVector CurrentVector = Translation;
			bool bKeyX = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::TranslationX);
			bool bKeyY = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::TranslationY);
			bool bKeyZ = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::TranslationZ);
			if (GetSequencer()->GetKeyGroupMode() == EKeyGroupMode::KeyGroup && (bKeyX || bKeyY || bKeyZ))
			{
				bKeyX = bKeyY = bKeyZ = true;
			}
			if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::TranslationX))
			{
				bKeyX = false;
			}
			if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::TranslationY))
			{
				bKeyY = false;
			}
			if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::TranslationZ))
			{
				bKeyZ = false;
			}
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentVector.X, bKeyX));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentVector.Y, bKeyY));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentVector.Z, bKeyZ));

			FRotator CurrentRotator = Rotation;
			bKeyX = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::RotationX);
			bKeyY = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::RotationY);
			bKeyZ = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::RotationZ);
			if (GetSequencer()->GetKeyGroupMode() == EKeyGroupMode::KeyGroup && (bKeyX || bKeyY || bKeyZ))
			{
				bKeyX = bKeyY = bKeyZ = true;
			}
			if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::RotationX))
			{
				bKeyX = false;
			}
			if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::RotationY))
			{
				bKeyY = false;
			}
			if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::RotationZ))
			{
				bKeyZ = false;
			}

			/* @Mike.Zyracki this is my gut feeling - we should run SetClosestToMe on the rotator SOMEWHERE....
			FMovieSceneInterrogationData InterrogationData;
			GetSequencer()->GetEvaluationTemplate().CopyActuators(InterrogationData.GetAccumulator());
			for (const FTransformInterrogationData& PreviousVal : InterrogationData.Iterate<FTransformInterrogationData>(UMovieSceneControlRigParameterSection::GetTransformInterrogationKey()))
			{
			if ((PreviousVal.ParameterName == RigControl.Name))
			{
			FRotator PreviousRot = PreviousVal.Val.GetRotation().Rotator();
			PreviousRot.SetClosestToMe(CurrentRotator);
			}
			}
			*/

			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentRotator.Roll, bKeyX));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentRotator.Pitch, bKeyY));
			OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentRotator.Yaw, bKeyZ));

			if (RigControl.ControlType == ERigControlType::Transform || RigControl.ControlType == ERigControlType::EulerTransform)
			{
				CurrentVector = Scale;
				bKeyX = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::ScaleX);
				bKeyY = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::ScaleY);
				bKeyZ = bSetKey && EnumHasAnyFlags(ChannelsToKey, EMovieSceneTransformChannel::ScaleZ);
				if (GetSequencer()->GetKeyGroupMode() == EKeyGroupMode::KeyGroup && (bKeyX || bKeyY || bKeyZ))
				{
					bKeyX = bKeyY = bKeyZ = true;
				}
				if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::ScaleX))
				{
					bKeyX = false;
				}
				if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::ScaleY))
				{
					bKeyY = false;
				}
				if (!EnumHasAnyFlags(TransformMask, EMovieSceneTransformChannel::ScaleZ))
				{
					bKeyZ = false;
				}
				OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentVector.X, bKeyX));
				OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentVector.Y, bKeyY));
				OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneFloatChannel>(ChannelIndex++, CurrentVector.Z, bKeyZ));
			}
			break;
		}
		}
	}
}

FKeyPropertyResult FControlRigParameterTrackEditor::AddKeysToControlRigHandle(USceneComponent *InSceneComp, UControlRig* InControlRig,
	FGuid ObjectHandle, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedKeys,
	ESequencerKeyMode KeyMode, TSubclassOf<UMovieSceneTrack> TrackClass, FName ControlRigName, FName RigControlName)
{
	EAutoChangeMode AutoChangeMode = GetSequencer()->GetAutoChangeMode();
	EAllowEditsMode AllowEditsMode = GetSequencer()->GetAllowEditsMode();

	bool bCreateTrack =
		(KeyMode == ESequencerKeyMode::AutoKey && (AutoChangeMode == EAutoChangeMode::AutoTrack || AutoChangeMode == EAutoChangeMode::All)) ||
		KeyMode == ESequencerKeyMode::ManualKey ||
		KeyMode == ESequencerKeyMode::ManualKeyForced ||
		AllowEditsMode == EAllowEditsMode::AllowSequencerEditsOnly;

	bool bCreateSection = false;
	// we don't do this, maybe revisit if a bug occurs, but currently extends sections on autokey.
	//bCreateTrack || (KeyMode == ESequencerKeyMode::AutoKey && (AutoChangeMode != EAutoChangeMode::None));

	// Try to find an existing Track, and if one doesn't exist check the key params and create one if requested.

	FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, TrackClass, ControlRigName, bCreateTrack);
	UMovieSceneControlRigParameterTrack* Track = CastChecked<UMovieSceneControlRigParameterTrack>(TrackResult.Track, ECastCheckedType::NullAllowed);

	bool bTrackCreated = TrackResult.bWasCreated;

	bool bSectionCreated = false;
	FKeyPropertyResult KeyPropertyResult;

	if (Track)
	{
		float Weight = 1.0f;

		UMovieSceneSection* SectionToKey = bCreateSection ? Track->FindOrExtendSection(KeyTime, Weight) : Track->FindSection(KeyTime);

		// If there's no overlapping section to key, create one only if a track was newly created. Otherwise, skip keying altogether
		// so that the user is forced to create a section to key on.
		if (bTrackCreated && !SectionToKey)
		{
			Track->Modify();
			SectionToKey = Track->FindOrAddSection(KeyTime, bSectionCreated);
			if (bSectionCreated && GetSequencer()->GetInfiniteKeyAreas())
			{
				SectionToKey->SetRange(TRange<FFrameNumber>::All());
			}
		}

		if (SectionToKey && SectionToKey->GetRange().Contains(KeyTime))
		{
			if (!bTrackCreated)
			{
				ModifyOurGeneratedKeysByCurrentAndWeight(InSceneComp, InControlRig, RigControlName, Track, SectionToKey, KeyTime, GeneratedKeys, Weight);
			}
			UMovieSceneControlRigParameterSection* ParamSection = Cast<UMovieSceneControlRigParameterSection>(SectionToKey);
			if (!ParamSection->GetDoNotKey())
			{
				KeyPropertyResult |= AddKeysToSection(SectionToKey, KeyTime, GeneratedKeys, KeyMode);
			}
		}
	}

	KeyPropertyResult.bTrackCreated |= bTrackCreated || bSectionCreated;
	return KeyPropertyResult;
}

FKeyPropertyResult FControlRigParameterTrackEditor::AddKeysToControlRig(
	USceneComponent *InSceneComp, UControlRig* InControlRig, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedKeys,
	ESequencerKeyMode KeyMode, TSubclassOf<UMovieSceneTrack> TrackClass, FName ControlRigName, FName RigControlName)
{
	FKeyPropertyResult KeyPropertyResult;
	EAutoChangeMode AutoChangeMode = GetSequencer()->GetAutoChangeMode();
	EAllowEditsMode AllowEditsMode = GetSequencer()->GetAllowEditsMode();
	bool bCreateHandle =
		(KeyMode == ESequencerKeyMode::AutoKey && (AutoChangeMode == EAutoChangeMode::All)) ||
		KeyMode == ESequencerKeyMode::ManualKey ||
		KeyMode == ESequencerKeyMode::ManualKeyForced ||
		AllowEditsMode == EAllowEditsMode::AllowSequencerEditsOnly;

	FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToSceneCompOrOwner(InSceneComp);
	FGuid ObjectHandle = HandleResult.Handle;
	KeyPropertyResult.bHandleCreated = HandleResult.bWasCreated;
	if (ObjectHandle.IsValid())
	{
		KeyPropertyResult |= AddKeysToControlRigHandle(InSceneComp, InControlRig, ObjectHandle, KeyTime, GeneratedKeys, KeyMode, TrackClass, ControlRigName, RigControlName);
	}

	return KeyPropertyResult;
}

void FControlRigParameterTrackEditor::AddControlKeys(USceneComponent *InSceneComp, UControlRig* InControlRig, FName ControlRigName, FName RigControlName, EMovieSceneTransformChannel ChannelsToKey, ESequencerKeyMode KeyMode, float InLocalTime)
{
	if (KeyMode == ESequencerKeyMode::ManualKey || (GetSequencer().IsValid() && !GetSequencer()->IsAllowedToChange()))
	{
		return;
	}
	bool bCreateTrack = false;
	bool bCreateHandle = false;
	FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToSceneCompOrOwner(InSceneComp);
	FGuid ObjectHandle = HandleResult.Handle;
	if (!ObjectHandle.IsValid())
	{
		return;
	}
	FFindOrCreateTrackResult TrackResult = FindOrCreateTrackForObject(ObjectHandle, UMovieSceneControlRigParameterTrack::StaticClass(), ControlRigName, bCreateTrack);
	UMovieSceneControlRigParameterTrack* Track = CastChecked<UMovieSceneControlRigParameterTrack>(TrackResult.Track, ECastCheckedType::NullAllowed);
	UMovieSceneControlRigParameterSection* ParamSection = nullptr;
	if (Track)
	{
		FFrameNumber  FrameTime = GetTimeForKey();
		UMovieSceneSection* Section = Track->FindSection(FrameTime);
		ParamSection = Cast<UMovieSceneControlRigParameterSection>(Section);

		if (ParamSection && ParamSection->GetDoNotKey())
		{
			return;
		}
	}

	if (!ParamSection)
	{
		return;
	}

	TSharedRef<FGeneratedTrackKeys> GeneratedKeys = MakeShared<FGeneratedTrackKeys>();

	GetControlRigKeys(InControlRig, RigControlName, ChannelsToKey, ParamSection, *GeneratedKeys);
	TGuardValue<bool> Guard(bIsDoingSelection, true);

	auto OnKeyProperty = [=](FFrameNumber Time) -> FKeyPropertyResult
	{
		FFrameNumber LocalTime = Time;
		if (InLocalTime != FLT_MAX)
		{
			//convert from frame time since conversion may give us one frame less, e.g 1.53333330 * 24000.0/1.0 = 36799.999199999998
			FFrameTime LocalFrameTime = GetSequencer()->GetFocusedTickResolution().AsFrameTime((double)InLocalTime);
			LocalTime = LocalFrameTime.RoundToFrame();
		}
		return this->AddKeysToControlRig(InSceneComp, InControlRig, LocalTime, *GeneratedKeys, KeyMode, UMovieSceneControlRigParameterTrack::StaticClass(), ControlRigName, RigControlName);
	};

	AnimatablePropertyChanged(FOnKeyProperty::CreateLambda(OnKeyProperty));

}


bool FControlRigParameterTrackEditor::ModifyOurGeneratedKeysByCurrentAndWeight(UObject *Object, UControlRig* InControlRig, FName RigControlName, UMovieSceneTrack *Track, UMovieSceneSection* SectionToKey, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedTotalKeys, float Weight) const
{
	FFrameRate TickResolution = GetSequencer()->GetFocusedTickResolution();
	FMovieSceneEvaluationTrack EvalTrack = CastChecked<UMovieSceneControlRigParameterTrack>(Track)->GenerateTrackTemplate(Track);

	FMovieSceneInterrogationData InterrogationData;
	GetSequencer()->GetEvaluationTemplate().CopyActuators(InterrogationData.GetAccumulator());

	FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, GetSequencer()->GetFocusedTickResolution()));
	EvalTrack.Interrogate(Context, InterrogationData, Object);
	const TArray<FRigControl>& Controls = InControlRig->AvailableControls();
	UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(SectionToKey);
	FMovieSceneChannelProxy& Proxy = SectionToKey->GetChannelProxy();
	int32 ChannelIndex = 0;
	FChannelMapInfo* pChannelIndex = nullptr;
	for (const FRigControl& RigControl : Controls)
	{
		if (!RigControl.bAnimatable)
		{
			continue;
		}
		switch (RigControl.ControlType)
		{
		case ERigControlType::Float:
		{
			for (const FFloatInterrogationData& Val : InterrogationData.Iterate<FFloatInterrogationData>(UMovieSceneControlRigParameterSection::GetFloatInterrogationKey()))
			{
				if ((Val.ParameterName == RigControl.Name))
				{
					pChannelIndex = Section->ControlChannelMap.Find(RigControl.Name);
					if (pChannelIndex)
					{
						ChannelIndex = pChannelIndex->TotalChannelIndex;
						GeneratedTotalKeys[ChannelIndex]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&Val.Val, Weight);
					}
					break;
				}
			}
			break;
		}
		//no blending of bools,ints/enums
		case ERigControlType::Bool:
		case ERigControlType::Integer:
		{

			break;
		}
		case ERigControlType::Vector2D:
		{
			for (const FVector2DInterrogationData& Val : InterrogationData.Iterate<FVector2DInterrogationData>(UMovieSceneControlRigParameterSection::GetVector2DInterrogationKey()))
			{
				if ((Val.ParameterName == RigControl.Name))
				{
					pChannelIndex = Section->ControlChannelMap.Find(RigControl.Name);
					if (pChannelIndex)
					{
						ChannelIndex = pChannelIndex->TotalChannelIndex;
						GeneratedTotalKeys[ChannelIndex]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&Val.Val.X, Weight);
						GeneratedTotalKeys[ChannelIndex + 1]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&Val.Val.Y, Weight);
					}
					break;
				}
			}
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		{
			for (const FVectorInterrogationData& Val : InterrogationData.Iterate<FVectorInterrogationData>(UMovieSceneControlRigParameterSection::GetVectorInterrogationKey()))
			{
				if ((Val.ParameterName == RigControl.Name))
				{
					pChannelIndex = Section->ControlChannelMap.Find(RigControl.Name);
					if (pChannelIndex)
					{
						ChannelIndex = pChannelIndex->TotalChannelIndex;

						/* @Mike.Zyracki why is this causing the value to continuously grow?
						*/
						if (RigControl.ControlType != ERigControlType::Rotator)
						{
							GeneratedTotalKeys[ChannelIndex]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void*)&Val.Val.X, Weight);
							GeneratedTotalKeys[ChannelIndex + 1]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void*)&Val.Val.Y, Weight);
							GeneratedTotalKeys[ChannelIndex + 2]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void*)&Val.Val.Z, Weight);
						}
					}
					break;
				}
			}
			break;
		}

		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			for (const FTransformInterrogationData& Val : InterrogationData.Iterate<FTransformInterrogationData>(UMovieSceneControlRigParameterSection::GetTransformInterrogationKey()))
			{

				if ((Val.ParameterName == RigControl.Name))
				{
					pChannelIndex = Section->ControlChannelMap.Find(RigControl.Name);
					if (pChannelIndex)
					{
						ChannelIndex = pChannelIndex->TotalChannelIndex;
						FVector CurrentPos = Val.Val.GetTranslation();
						FRotator CurrentRot = Val.Val.GetRotation().Rotator();
						GeneratedTotalKeys[ChannelIndex]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentPos.X, Weight);
						GeneratedTotalKeys[ChannelIndex + 1]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentPos.Y, Weight);
						GeneratedTotalKeys[ChannelIndex + 2]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentPos.Z, Weight);

						GeneratedTotalKeys[ChannelIndex + 3]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentRot.Roll, Weight);
						GeneratedTotalKeys[ChannelIndex + 4]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentRot.Pitch, Weight);
						GeneratedTotalKeys[ChannelIndex + 5]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentRot.Yaw, Weight);

						if (RigControl.ControlType == ERigControlType::Transform || RigControl.ControlType == ERigControlType::EulerTransform)
						{
							FVector CurrentScale = Val.Val.GetScale3D();
							GeneratedTotalKeys[ChannelIndex + 6]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentScale.X, Weight);
							GeneratedTotalKeys[ChannelIndex + 7]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentScale.Y, Weight);
							GeneratedTotalKeys[ChannelIndex + 8]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurrentScale.Z, Weight);
						}
					}
					break;
				}
			}
			break;
		}
		}
	}
	return true;
}

void FControlRigParameterTrackEditor::BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* InTrack)
{
	bool bSectionAdded;
	UMovieSceneControlRigParameterTrack* Track = Cast< UMovieSceneControlRigParameterTrack>(InTrack);
	if (!Track || Track->GetControlRig() == nullptr)
	{
		return;
	}

	UMovieSceneControlRigParameterSection* SectionToKey = Cast<UMovieSceneControlRigParameterSection>(Track->FindOrAddSection(0, bSectionAdded));
	if (!SectionToKey)
	{
		return;
	}

	TArray<FFBXNodeAndChannels>* NodeAndChannels = Track->GetNodeAndChannelMappings();

	MenuBuilder.BeginSection("Import To Control Rig", NSLOCTEXT("Sequencer", "ImportToControlRig", "Import To Control Rig"));
	{
		MenuBuilder.AddMenuEntry(
			NSLOCTEXT("Sequencer", "ImportControlRigFBX", "Import Control Rig FBX"),
			NSLOCTEXT("Sequencer", "ImportControlRigFBXTooltip", "Import Control Rig FBX"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FControlRigParameterTrackEditor::ImportFBX, Track, SectionToKey, NodeAndChannels)));
	}
	MenuBuilder.EndSection();

	MenuBuilder.AddMenuSeparator();

	if (UFKControlRig* AutoRig = Cast<UFKControlRig>(Track->GetControlRig()))
	{
		MenuBuilder.BeginSection("FK Control Rig", NSLOCTEXT("Sequencer", "FKControlRig", "FK Control Rig"));
		{

			MenuBuilder.AddMenuEntry(
				NSLOCTEXT("Sequencer", "SelectBonesToAnimate", "Select Bones Or Curves To Animate"),
				NSLOCTEXT("Sequencer", "SelectBonesToAnimateToolTip", "Select which bones or curves you want to directly animate"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FControlRigParameterTrackEditor::SelectFKBonesToAnimate, AutoRig,Track)));

			MenuBuilder.AddMenuEntry(
				NSLOCTEXT("Sequencer", "FKRigApplyMode", "Additive"),
				NSLOCTEXT("Sequencer", "FKRigApplyModeToolTip", "Toggles the apply mode between Replace and Additive"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FControlRigParameterTrackEditor::ToggleFKControlRig, Track, AutoRig),
					FCanExecuteAction::CreateUObject(AutoRig, &UFKControlRig::CanToggleApplyMode),
					FIsActionChecked::CreateUObject(AutoRig, &UFKControlRig::IsApplyModeAdditive)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);
		}
		MenuBuilder.EndSection();

		MenuBuilder.AddMenuSeparator();
	}

}

void FControlRigParameterTrackEditor::ToggleFKControlRig(UMovieSceneControlRigParameterTrack* Track, UFKControlRig* FKControlRig)
{
	FScopedTransaction Transaction(LOCTEXT("ToggleFKControlRig", "Toggle FK Control Rig"));
	FKControlRig->Modify();
	Track->Modify();
	FKControlRig->ToggleApplyMode();
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		if (Section)
		{
			UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(Section);
			if (CRSection)
			{
				Section->Modify();
				CRSection->ClearAllParameters();
				CRSection->RecreateWithThisControlRig(CRSection->GetControlRig(), true);
			}
		}
	}
	GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

}


void FControlRigParameterTrackEditor::ImportFBX(UMovieSceneControlRigParameterTrack* InTrack, UMovieSceneControlRigParameterSection* InSection,
	TArray<FFBXNodeAndChannels>* NodeAndChannels)
{
	if (NodeAndChannels)
	{
		//NodeAndChannels will be deleted later
		MovieSceneToolHelpers::ImportFBXIntoChannelsWithDialog(GetSequencer().ToSharedRef(), NodeAndChannels);
	}
}



class SFKControlRigBoneSelect : public SCompoundWidget, public FGCObject
{
public:

	SLATE_BEGIN_ARGS(SFKControlRigBoneSelect) {}
	SLATE_ATTRIBUTE(UFKControlRig*, AutoRig)
	SLATE_ATTRIBUTE(UMovieSceneControlRigParameterTrack*,Track)
	SLATE_ATTRIBUTE(ISequencer*, Sequencer)
	SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
	{
		AutoRig = InArgs._AutoRig.Get();
		Track = InArgs._Track.Get();
		Sequencer = InArgs._Sequencer.Get();

		this->ChildSlot[
			SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SFKControlRigBoneSelectDescription", "Select Bones You Want To Be Active On The FK Control Rig"))
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(SSeparator)
				]
			+ SVerticalBox::Slot()
				.Padding(8.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(SBorder)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
				[
					//Save this widget so we can populate it later with check boxes
					SAssignNew(CheckBoxContainer, SVerticalBox)
				]
					]
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.Padding(8.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
				.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
				.OnClicked(this, &SFKControlRigBoneSelect::ChangeAllOptions, true)
				.Text(LOCTEXT("FKRigSelectAll", "Select All"))
				]
			+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
				.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
				.OnClicked(this, &SFKControlRigBoneSelect::ChangeAllOptions, false)
				.Text(LOCTEXT("FKRigDeselectAll", "Deselect All"))
				]

				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(SSeparator)
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(8.0f, 4.0f, 8.0f, 4.0f)
				[
					SNew(SUniformGridPanel)
					.SlotPadding(FEditorStyle::GetMargin("StandardDialog.SlotPadding"))
				.MinDesiredSlotWidth(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
				.MinDesiredSlotHeight(FEditorStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
				.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
				.OnClicked(this, &SFKControlRigBoneSelect::OnButtonClick, true)
				.Text(LOCTEXT("FKRigeOk", "OK"))
				]
			+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
				.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
				.OnClicked(this, &SFKControlRigBoneSelect::OnButtonClick, false)
				.Text(LOCTEXT("FKRigCancel", "Cancel"))
				]
				]
		];
	}


	/**
	* Creates a Slate check box
	*
	* @param	Label		Text label for the check box
	* @param	ButtonId	The ID for the check box
	* @return				The created check box widget
	*/
	TSharedRef<SWidget> CreateCheckBox(const FString& Label, int32 ButtonId)
	{
		return
			SNew(SCheckBox)
			.IsChecked(this, &SFKControlRigBoneSelect::IsCheckboxChecked, ButtonId)
			.OnCheckStateChanged(this, &SFKControlRigBoneSelect::OnCheckboxChanged, ButtonId)
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			];
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(AutoRig);
	}

	/**
	* Returns the state of the check box
	*
	* @param	ButtonId	The ID for the check box
	* @return				The status of the check box
	*/
	ECheckBoxState IsCheckboxChecked(int32 ButtonId) const
	{
		return CheckBoxInfoMap.FindChecked(ButtonId).bActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	/**
	* Handler for all check box clicks
	*
	* @param	NewCheckboxState	The new state of the check box
	* @param	CheckboxThatChanged	The ID of the radio button that has changed.
	*/
	void OnCheckboxChanged(ECheckBoxState NewCheckboxState, int32 CheckboxThatChanged)
	{
		FFKBoneCheckInfo& Info = CheckBoxInfoMap.FindChecked(CheckboxThatChanged);
		Info.bActive = !Info.bActive;
	}

	/**
	* Handler for the Select All and Deselect All buttons
	*
	* @param	bNewCheckedState	The new state of the check boxes
	*/
	FReply ChangeAllOptions(bool bNewCheckedState)
	{
		for (TPair<int32, FFKBoneCheckInfo>& Pair : CheckBoxInfoMap)
		{
			FFKBoneCheckInfo& Info = Pair.Value;
			Info.bActive = bNewCheckedState;
		}
		return FReply::Handled();
	}

	/**
	* Populated the dialog with multiple check boxes, each corresponding to a bone
	*
	* @param	BoneInfos	The list of Bones to populate the dialog with
	*/
	void PopulateOptions(TArray<FFKBoneCheckInfo>& BoneInfos)
	{
		for (FFKBoneCheckInfo& Info : BoneInfos)
		{
			CheckBoxInfoMap.Add(Info.BoneID, Info);

			CheckBoxContainer->AddSlot()
				.AutoHeight()
				[
					CreateCheckBox(Info.BoneName.GetPlainNameString(), Info.BoneID)
				];
		}
	}


private:

	/**
	* Handles when a button is pressed, should be bound with appropriate EResult Key
	*
	* @param ButtonID - The return type of the button which has been pressed.
	*/
	FReply OnButtonClick(bool bValid)
	{
		TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared());

		if (Window.IsValid())
		{
			Window->RequestDestroyWindow();
		}
		//if ok selected bValid == true
		if (bValid && AutoRig)
		{
			TArray<FFKBoneCheckInfo> BoneCheckArray;
			BoneCheckArray.SetNumUninitialized(CheckBoxInfoMap.Num());
			int32 Index = 0;
			for (TPair<int32, FFKBoneCheckInfo>& Pair : CheckBoxInfoMap)
			{
				FFKBoneCheckInfo& Info = Pair.Value;
				BoneCheckArray[Index++] = Info;
			
			}
			if (Track  && Sequencer)
			{
				TArray<bool> Mask;
				Mask.SetNum(BoneCheckArray.Num());
				for (const FFKBoneCheckInfo& Info : BoneCheckArray)
				{
					Mask[Info.BoneID] = Info.bActive;
				}

				TArray<UMovieSceneSection*> Sections = Track->GetAllSections();
				for (UMovieSceneSection* IterSection : Sections)
				{
					UMovieSceneControlRigParameterSection* Section = Cast< UMovieSceneControlRigParameterSection>(IterSection);
					if (Section)
					{
						Section->SetControlsMask(Mask);
					}
				}
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
			}
			AutoRig->SetControlActive(BoneCheckArray);
		}
		return bValid ? FReply::Handled() : FReply::Unhandled();
	}

	/** The slate container that the bone check boxes get added to */
	TSharedPtr<SVerticalBox>	 CheckBoxContainer;
	/** Store the check box state for each bone */
	TMap<int32, FFKBoneCheckInfo> CheckBoxInfoMap;

	UFKControlRig* AutoRig;
	UMovieSceneControlRigParameterTrack* Track;
	ISequencer* Sequencer;
};

void FControlRigParameterTrackEditor::SelectFKBonesToAnimate(UFKControlRig* AutoRig, UMovieSceneControlRigParameterTrack* Track)
{
	if (AutoRig)
	{
		const FText TitleText = NSLOCTEXT("Sequencer", "SelectBonesOrCurvesToAnimate", "Select Bones Or Curves To Animate");

		// Create the window to choose our options
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(TitleText)
			.HasCloseButton(true)
			.SizingRule(ESizingRule::UserSized)
			.ClientSize(FVector2D(400.0f, 200.0f))
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			.SupportsMinimize(false);

		TSharedRef<SFKControlRigBoneSelect> DialogWidget = SNew(SFKControlRigBoneSelect)
			.AutoRig(AutoRig)
			.Track(Track)
			.Sequencer(GetSequencer().Get());

		TArray<FName> ControlRigNames = AutoRig->GetControlNames();
		TArray<FFKBoneCheckInfo> BoneInfos;
		for (int32 Index = 0; Index < ControlRigNames.Num(); ++Index)
		{
			FFKBoneCheckInfo Info;
			Info.BoneID = Index;
			Info.BoneName = ControlRigNames[Index];
			Info.bActive = AutoRig->GetControlActive(Index);
			BoneInfos.Add(Info);
		}

		DialogWidget->PopulateOptions(BoneInfos);

		Window->SetContent(DialogWidget);
		FSlateApplication::Get().AddWindow(Window);
	}

	//reconstruct all channel proxies TODO or not to do that is the question
}


void FControlRigParameterSection::BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& InObjectBinding)
{
	UMovieSceneControlRigParameterSection* ParameterSection = CastChecked<UMovieSceneControlRigParameterSection>(WeakSection.Get());
	TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();

	UControlRig* ControlRig = ParameterSection->GetControlRig();

	if (ControlRig)
	{

		UFKControlRig* AutoRig = Cast<UFKControlRig>(ControlRig);
		if (AutoRig || ControlRig->SupportsEvent(FRigUnit_InverseExecution::EventName))
		{
			UObject* BoundObject = nullptr;
			USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(InObjectBinding, &BoundObject, WeakSequencer.Pin());

			if (Skeleton)
			{
				// Load the asset registry module
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

				// Collect a full list of assets with the specified class
				TArray<FAssetData> AssetDataList;
				AssetRegistryModule.Get().GetAssetsByClass(UAnimSequenceBase::StaticClass()->GetFName(), AssetDataList, true);

				if (AssetDataList.Num())
				{
					MenuBuilder.AddSubMenu(
						LOCTEXT("ImportAnimSequenceIntoThisSection", "Import Anim Sequence Into This Section"), NSLOCTEXT("Sequencer", "ImportAnimSequenceIntoThisSectionTP", "Import Anim Sequence Into This Section"),
						FNewMenuDelegate::CreateRaw(this, &FControlRigParameterSection::AddAnimationSubMenuForFK, InObjectBinding, Skeleton, ParameterSection)
					);
				}
			}
		}
		TArray<FRigControl> Controls;
		ControlRig->GetControlsInOrder(Controls);

		auto MakeUIAction = [=](EMovieSceneTransformChannel ChannelsToToggle)
		{
			return FUIAction(
				FExecuteAction::CreateLambda([=]
			{
				FScopedTransaction Transaction(LOCTEXT("SetActiveChannelsTransaction", "Set Active Channels"));
				ParameterSection->Modify();
				EMovieSceneTransformChannel Channels = ParameterSection->GetTransformMask().GetChannels();

				if (EnumHasAllFlags(Channels, ChannelsToToggle) || (Channels & ChannelsToToggle) == EMovieSceneTransformChannel::None)
				{
					ParameterSection->SetTransformMask(ParameterSection->GetTransformMask().GetChannels() ^ ChannelsToToggle);
				}
				else
				{
					ParameterSection->SetTransformMask(ParameterSection->GetTransformMask().GetChannels() | ChannelsToToggle);
				}

				// Restore pre-animated state for the bound objects so that inactive channels will return to their default values.
				for (TWeakObjectPtr<> WeakObject : SequencerPtr->FindBoundObjects(InObjectBinding, SequencerPtr->GetFocusedTemplateID()))
				{
					if (UObject* Object = WeakObject.Get())
					{
						SequencerPtr->RestorePreAnimatedState();
					}
				}

				SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
			}
				),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([=]
			{
				EMovieSceneTransformChannel Channels = ParameterSection->GetTransformMask().GetChannels();
				if (EnumHasAllFlags(Channels, ChannelsToToggle))
				{
					return ECheckBoxState::Checked;
				}
				else if (EnumHasAnyFlags(Channels, ChannelsToToggle))
				{
					return ECheckBoxState::Undetermined;
				}
				return ECheckBoxState::Unchecked;
			})
				);
		};
		auto ToggleControls = [=](int32 Index)
		{
			return FUIAction(
				FExecuteAction::CreateLambda([=]
			{
				FScopedTransaction Transaction(LOCTEXT("ToggleRigControlFiltersTransaction", "Toggle Rig Control Filters"));
				ParameterSection->Modify();
				if (Index >= 0)
				{
					ParameterSection->SetControlsMask(Index, !ParameterSection->GetControlsMask(Index));
				}
				else
				{
					ParameterSection->FillControlsMask(!ParameterSection->GetControlsMask(0));
				}
				SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

			}
				),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([=]
			{
				TArray<bool> ControlBool = ParameterSection->GetControlsMask();
				if (Index >= 0)
				{
					if (ControlBool[Index])
					{
						return ECheckBoxState::Checked;

					}
					else
					{
						return ECheckBoxState::Unchecked;
					}
				}
				else
				{
					TOptional<bool> FirstVal;
					for (bool Val : ControlBool)
					{
						if (FirstVal.IsSet())
						{
							if (Val != FirstVal)
							{
								return ECheckBoxState::Undetermined;
							}
						}
						else
						{
							FirstVal = Val;
						}

					}
					return (FirstVal.IsSet() && FirstVal.GetValue()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}
			})
				);
		};
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("RigSectionActiveChannels", "Active Channels"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("ToggleRigControlsText", "Rig Controls"), LOCTEXT("ToggleRigControlsText_Tooltip", "Causes this section to affect all rig controls"),
				FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder) {
				int32 Index = 0;
				for (const FRigControl& RigControl : Controls)
				{
					const FName RigName = RigControl.Name;
					FText Name = FText::FromName(RigName);
					FText Text = FText::Format(LOCTEXT("RigControlToggle", "{0}"), Name);
					FText TooltipText = FText::Format(LOCTEXT("RigControlToggleTooltip", "Causes this section to affect rig control {0}"), Name);
					SubMenuBuilder.AddMenuEntry(
						Text, TooltipText,
						FSlateIcon(), ToggleControls(Index++), NAME_None, EUserInterfaceActionType::ToggleButton);
				}
			}),
				ToggleControls(-1),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);

			MenuBuilder.AddSubMenu(
				LOCTEXT("AllTranslation", "Translation"), LOCTEXT("AllTranslation_ToolTip", "Causes this section to affect the translation of rig control transforms"),
				FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder) {
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("TranslationX", "X"), LOCTEXT("TranslationX_ToolTip", "Causes this section to affect the X channel of the transform's translation"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::TranslationX), NAME_None, EUserInterfaceActionType::ToggleButton);
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("TranslationY", "Y"), LOCTEXT("TranslationY_ToolTip", "Causes this section to affect the Y channel of the transform's translation"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::TranslationY), NAME_None, EUserInterfaceActionType::ToggleButton);
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("TranslationZ", "Z"), LOCTEXT("TranslationZ_ToolTip", "Causes this section to affect the Z channel of the transform's translation"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::TranslationZ), NAME_None, EUserInterfaceActionType::ToggleButton);
			}),
				MakeUIAction(EMovieSceneTransformChannel::Translation),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);

			MenuBuilder.AddSubMenu(
				LOCTEXT("AllRotation", "Rotation"), LOCTEXT("AllRotation_ToolTip", "Causes this section to affect the rotation of the rig control transform"),
				FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder) {
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("RotationX", "Roll (X)"), LOCTEXT("RotationX_ToolTip", "Causes this section to affect the roll (X) channel the transform's rotation"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::RotationX), NAME_None, EUserInterfaceActionType::ToggleButton);
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("RotationY", "Pitch (Y)"), LOCTEXT("RotationY_ToolTip", "Causes this section to affect the pitch (Y) channel the transform's rotation"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::RotationY), NAME_None, EUserInterfaceActionType::ToggleButton);
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("RotationZ", "Yaw (Z)"), LOCTEXT("RotationZ_ToolTip", "Causes this section to affect the yaw (Z) channel the transform's rotation"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::RotationZ), NAME_None, EUserInterfaceActionType::ToggleButton);
			}),
				MakeUIAction(EMovieSceneTransformChannel::Rotation),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);

			MenuBuilder.AddSubMenu(
				LOCTEXT("AllScale", "Scale"), LOCTEXT("AllScale_ToolTip", "Causes this section to affect the scale of the rig control transform"),
				FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder) {
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("ScaleX", "X"), LOCTEXT("ScaleX_ToolTip", "Causes this section to affect the X channel of the transform's scale"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::ScaleX), NAME_None, EUserInterfaceActionType::ToggleButton);
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("ScaleY", "Y"), LOCTEXT("ScaleY_ToolTip", "Causes this section to affect the Y channel of the transform's scale"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::ScaleY), NAME_None, EUserInterfaceActionType::ToggleButton);
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("ScaleZ", "Z"), LOCTEXT("ScaleZ_ToolTip", "Causes this section to affect the Z channel of the transform's scale"),
					FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::ScaleZ), NAME_None, EUserInterfaceActionType::ToggleButton);
			}),
				MakeUIAction(EMovieSceneTransformChannel::Scale),
				NAME_None,
				EUserInterfaceActionType::ToggleButton);

			//mz todo h
			MenuBuilder.AddMenuEntry(
				LOCTEXT("Weight", "Weight"), LOCTEXT("Weight_ToolTip", "Causes this section to be applied with a user-specified weight curve"),
				FSlateIcon(), MakeUIAction(EMovieSceneTransformChannel::Weight), NAME_None, EUserInterfaceActionType::ToggleButton);
		}
		MenuBuilder.EndSection();
	}
}

//mz todo
bool FControlRigParameterSection::RequestDeleteCategory(const TArray<FName>& CategoryNamePaths)
{
	UMovieSceneControlRigParameterSection* ParameterSection = CastChecked<UMovieSceneControlRigParameterSection>(WeakSection.Get());
	TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();
	/*
	const FScopedTransaction Transaction(LOCTEXT("DeleteTransformCategory", "Delete transform category"));

	if (ParameterSection->TryModify())
	{
	FName CategoryName = CategoryNamePaths[CategoryNamePaths.Num() - 1];

	EMovieSceneTransformChannel Channel = ParameterSection->GetTransformMask().GetChannels();
	EMovieSceneTransformChannel ChannelToRemove = ParameterSection->GetTransformMaskByName(CategoryName).GetChannels();

	Channel = Channel ^ ChannelToRemove;

	ParameterSection->SetTransformMask(Channel);

	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	return true;
	}
	*/
	return false;
}

bool FControlRigParameterSection::RequestDeleteKeyArea(const TArray<FName>& KeyAreaNamePaths)
{
	UMovieSceneControlRigParameterSection* ParameterSection = CastChecked<UMovieSceneControlRigParameterSection>(WeakSection.Get());
	TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();

	/*
	const FScopedTransaction Transaction(LOCTEXT("DeleteTransformChannel", "Delete transform channel"));

	if (ParameterSection->TryModify())
	{
	// Only delete the last key area path which is the channel. ie. TranslationX as opposed to Translation
	FName KeyAreaName = KeyAreaNamePaths[KeyAreaNamePaths.Num() - 1];

	EMovieSceneTransformChannel Channel = ParameterSection->GetTransformMask().GetChannels();
	EMovieSceneTransformChannel ChannelToRemove = ParameterSection->GetTransformMaskByName(KeyAreaName).GetChannels();

	Channel = Channel ^ ChannelToRemove;

	ParameterSection->SetTransformMask(Channel);

	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	return true;
	}
	*/
	return true;
}


void FControlRigParameterSection::AddAnimationSubMenuForFK(FMenuBuilder& MenuBuilder, FGuid ObjectBinding, USkeleton* Skeleton, UMovieSceneControlRigParameterSection* Section)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FControlRigParameterSection::OnAnimationAssetSelectedForFK, ObjectBinding, Section);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this, &FControlRigParameterSection::OnAnimationAssetEnterPressedForFK, ObjectBinding, Section);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateRaw(this, &FControlRigParameterSection::ShouldFilterAssetForFK);
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.Filter.ClassNames.Add(UAnimSequenceBase::StaticClass()->GetFName());
		AssetPickerConfig.Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(Skeleton).GetExportTextName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}


void FControlRigParameterSection::OnAnimationAssetSelectedForFK(const FAssetData& AssetData, FGuid ObjectBinding, UMovieSceneControlRigParameterSection* Section)
{
	FSlateApplication::Get().DismissAllMenus();

	UObject* SelectedObject = AssetData.GetAsset();
	TSharedPtr<ISequencer> SequencerPtr = WeakSequencer.Pin();

	if (SelectedObject && SelectedObject->IsA(UAnimSequence::StaticClass()) && SequencerPtr.IsValid())
	{
		UAnimSequence* AnimSequence = Cast<UAnimSequence>(AssetData.GetAsset());
		UObject* BoundObject = nullptr;
		USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBinding, &BoundObject, SequencerPtr);

		if (AnimSequence && Skeleton && AnimSequence->GetRawAnimationData().Num() > 0)
		{

			FScopedTransaction Transaction(LOCTEXT("BakeAnimation_Transaction", "Bake Animation To FK Control Rig"));
			Section->Modify();
			UMovieScene* MovieScene = SequencerPtr->GetFocusedMovieSceneSequence()->GetMovieScene();
			FFrameNumber StartFrame = SequencerPtr->GetLocalTime().Time.GetFrame();
			if (!Section->LoadAnimSequenceIntoThisSection(AnimSequence, MovieScene, Skeleton, false, 0.1f, StartFrame))
			{
				Transaction.Cancel();
			}
			SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		}
	}
}

bool FControlRigParameterSection::ShouldFilterAssetForFK(const FAssetData& AssetData)
{
	// we don't want 

	if (AssetData.AssetClass == UAnimMontage::StaticClass()->GetFName())
	{
		return true;
	}

	const FString EnumString = AssetData.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType));
	if (EnumString.IsEmpty())
	{
		return false;
	}

	UEnum* AdditiveTypeEnum = StaticEnum<EAdditiveAnimationType>();
	return ((EAdditiveAnimationType)AdditiveTypeEnum->GetValueByName(*EnumString) != AAT_None);

}

void FControlRigParameterSection::OnAnimationAssetEnterPressedForFK(const TArray<FAssetData>& AssetData, FGuid  ObjectBinding, UMovieSceneControlRigParameterSection* Section)
{
	if (AssetData.Num() > 0)
	{
		OnAnimationAssetSelectedForFK(AssetData[0].GetAsset(), ObjectBinding, Section);
	}
}

#undef LOCTEXT_NAMESPACE
