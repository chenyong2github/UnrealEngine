// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerEdMode.h"
#include "EditorViewportClient.h"
#include "Curves/KeyHandle.h"
#include "ISequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "Sequencer.h"
#include "Framework/Application/SlateApplication.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "SequencerCommonHelpers.h"
#include "MovieSceneHitProxy.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "SubtitleManager.h"
#include "SequencerMeshTrail.h"
#include "SequencerKeyActor.h"
#include "EditorWorldExtension.h"
#include "ViewportWorldInteraction.h"
#include "SSequencer.h"
#include "MovieSceneTracksComponentTypes.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "SequencerSectionPainter.h"
#include "MovieSceneToolHelpers.h"

const FEditorModeID FSequencerEdMode::EM_SequencerMode(TEXT("EM_SequencerMode"));

static TAutoConsoleVariable<bool> CVarDrawMeshTrails(
	TEXT("Sequencer.DrawMeshTrails"),
	true,
	TEXT("Toggle to show or hide Level Sequencer VR Editor trails"));

namespace UE
{
namespace SequencerEdMode
{

static const float DrawTrackTimeRes = 0.1f;

struct FTrackTransforms
{
	TArray<FFrameTime> Times;
	TArray<FTransform> Transforms;

	void Initialize(UObject* BoundObject, TArrayView<const FTrajectoryKey> TrajectoryKeys, ISequencer* Sequencer)
	{
		using namespace UE::MovieScene;

		// Hack: static system interrogator for now to avoid re-allocating UObjects all the time
		static FSystemInterrogator Interrogator;
		Interrogator.Reset();

		USceneComponent* SceneComponent = Cast<USceneComponent>(BoundObject);
		if (!SceneComponent)
		{
			AActor* Actor = Cast<AActor>(BoundObject);
			SceneComponent = Actor ? Actor->GetRootComponent() : nullptr;
		}

		if (!SceneComponent)
		{
			return;
		}

		FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();

		TRange<FFrameNumber> ViewRange(TickResolution.AsFrameNumber(Sequencer->GetViewRange().GetLowerBoundValue()), TickResolution.AsFrameNumber(Sequencer->GetViewRange().GetUpperBoundValue()));

		Times.Reserve(TrajectoryKeys.Num());

		FInterrogationChannel Channel = Interrogator.ImportTransformHierarchy(SceneComponent, Sequencer, Sequencer->GetFocusedTemplateID());

		if (TrajectoryKeys.Num() > 0)
		{
			Times.Add(TrajectoryKeys[0].Time);
			Interrogator.AddInterrogation(TrajectoryKeys[0].Time);
		}

		const int32 NumTrajectoryKeys = TrajectoryKeys.Num();
		for (int32 Index = 0; Index < TrajectoryKeys.Num(); ++Index)
		{
			const FTrajectoryKey& ThisKey = TrajectoryKeys[Index];

			Times.Add(ThisKey.Time);
			Interrogator.AddInterrogation(ThisKey.Time);

			const bool bIsConstantKey = ThisKey.Is(ERichCurveInterpMode::RCIM_Constant);
			if (!bIsConstantKey && Index != NumTrajectoryKeys-1)
			{
				const FTrajectoryKey& NextKey = TrajectoryKeys[Index+1];

				FFrameTime Diff = NextKey.Time - ThisKey.Time;
				int32 NumSteps = FMath::CeilToInt(TickResolution.AsSeconds(Diff) / DrawTrackTimeRes);
				// Limit the number of steps to prevent a rendering performance hit
				NumSteps = FMath::Min( 100, NumSteps );

				// Ensure that sub steps evaluate at equal points between the key times such that a NumSteps=2 results in:
				// PrevKey          step1          step2         ThisKey
				// |                  '              '              |
				NumSteps += 1;
				for (int32 Substep = 1; Substep < NumSteps; ++Substep)
				{
					FFrameTime Time = ThisKey.Time + (Diff * float(Substep)/NumSteps);

					Times.Add(Time);
					Interrogator.AddInterrogation(Time);
				}
			}
		}

		Interrogator.Update();
		Interrogator.QueryWorldSpaceTransforms(Channel, Transforms);

		check(Transforms.Num() == Times.Num());
		Interrogator.Reset();
	}
};

} // namespace SequencerEdMode
} // namespace UE

FSequencerEdMode::FSequencerEdMode()
{
	FSequencerEdModeTool* SequencerEdModeTool = new FSequencerEdModeTool(this);

	Tools.Add( SequencerEdModeTool );
	SetCurrentTool( SequencerEdModeTool );

	bDrawMeshTrails = CVarDrawMeshTrails->GetBool();
	CVarDrawMeshTrails.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda([this](IConsoleVariable* Var) 
	{
		bDrawMeshTrails = Var->GetBool();
	}));

	AudioTexture = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent"));
	check(AudioTexture);
}

FSequencerEdMode::~FSequencerEdMode()
{
	CVarDrawMeshTrails.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());
}

void FSequencerEdMode::Enter()
{
	FEdMode::Enter();
}

void FSequencerEdMode::Exit()
{
	CleanUpMeshTrails();

	Sequencers.Reset();

	FEdMode::Exit();
}

bool FSequencerEdMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	// Compatible with all modes so that we can take over with the sequencer hotkeys
	return true;
}

bool FSequencerEdMode::InputKey( FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event )
{
	TSharedPtr<ISequencer> ActiveSequencer;

	for (TWeakPtr<ISequencer> WeakSequencer : Sequencers)
	{
		ActiveSequencer = WeakSequencer.Pin();
		if (ActiveSequencer.IsValid())
		{
			break;
		}
	}

	if (ActiveSequencer.IsValid() && Event != IE_Released)
	{
		FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();

		if (ActiveSequencer->GetCommandBindings(ESequencerCommandBindings::Shared).Get()->ProcessCommandBindings(Key, KeyState, (Event == IE_Repeat) ))
		{
			return true;
		}
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

void FSequencerEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

#if WITH_EDITORONLY_DATA
	if (PDI)
	{
		DrawAudioTracks(PDI);
	}

	// Draw spline trails using the PDI
	if (View->Family->EngineShowFlags.Splines)
	{
		DrawTracks3D(PDI);
	}
	// Draw mesh trails (doesn't use the PDI)
	else if (bDrawMeshTrails)
	{
		PDI = nullptr;
		DrawTracks3D(PDI);
	}
#endif
}

void FSequencerEdMode::DrawHUD(FEditorViewportClient* ViewportClient,FViewport* Viewport,const FSceneView* View,FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient,Viewport,View,Canvas);

	if( ViewportClient->AllowsCinematicControl() )
	{
		// Get the size of the viewport
		const int32 SizeX = Viewport->GetSizeXY().X;
		const int32 SizeY = Viewport->GetSizeXY().Y;

		// Draw subtitles (toggle is handled internally)
		FVector2D MinPos(0.f, 0.f);
		FVector2D MaxPos(1.f, .9f);
		FIntRect SubtitleRegion(FMath::TruncToInt(SizeX * MinPos.X), FMath::TruncToInt(SizeY * MinPos.Y), FMath::TruncToInt(SizeX * MaxPos.X), FMath::TruncToInt(SizeY * MaxPos.Y));
		FSubtitleManager::GetSubtitleManager()->DisplaySubtitles( Canvas, SubtitleRegion, ViewportClient->GetWorld()->GetAudioTimeSeconds() );
	}
}

void FSequencerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FMeshTrailData& MeshTrail : MeshTrails)
	{
		Collector.AddReferencedObject(MeshTrail.Track);
		Collector.AddReferencedObject(MeshTrail.Trail);
	}
}

void FSequencerEdMode::OnKeySelected(FViewport* Viewport, HMovieSceneKeyProxy* KeyProxy)
{
	if (!KeyProxy)
	{
		return;
	}

	const bool bToggleSelection = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	const bool bAddToSelection = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);

	for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
	{
		bool bChangedSelection = false;

		TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
		if (Sequencer.IsValid())
		{
			Sequencer->SetLocalTimeDirectly(KeyProxy->Key.Time);

			FSequencerSelection& Selection = Sequencer->GetSelection();
			if (!bAddToSelection && !bToggleSelection)
			{
				if (!bChangedSelection)
				{
					Sequencer->GetSelection().SuspendBroadcast();
					bChangedSelection = true;
				}

				Sequencer->GetSelection().EmptySelectedKeys();
			}

			for (const FTrajectoryKey::FData& KeyData : KeyProxy->Key.KeyData)
			{
				UMovieSceneSection* Section = KeyData.Section.Get();
				TOptional<FSectionHandle> SectionHandle = Sequencer->GetNodeTree()->GetSectionHandle(Section);
				if (SectionHandle && KeyData.KeyHandle.IsSet())
				{
					TArray<TSharedRef<FSequencerSectionKeyAreaNode>> KeyAreaNodes;
					SectionHandle->GetTrackNode()->GetChildKeyAreaNodesRecursively(KeyAreaNodes);

					for (TSharedRef<FSequencerSectionKeyAreaNode> KeyAreaNode : KeyAreaNodes)
					{
						TSharedPtr<IKeyArea> KeyArea = KeyAreaNode->GetKeyArea(Section);
						if (KeyArea.IsValid() && KeyArea->GetName() == KeyData.ChannelName)
						{
							if (!bChangedSelection)
							{
								Sequencer->GetSelection().SuspendBroadcast();
								bChangedSelection = true;
							}

							Sequencer->SelectKey(Section, KeyArea, KeyData.KeyHandle.GetValue(), bToggleSelection);
							break;
						}
					}
				}
			}
			if (bChangedSelection)
			{
				Sequencer->GetSelection().ResumeBroadcast();
				Sequencer->GetSelection().GetOnKeySelectionChanged().Broadcast();
				Sequencer->GetSelection().GetOnOutlinerNodeSelectionChangedObjectGuids().Broadcast();
			}
		}
	}
}

void FSequencerEdMode::DrawMeshTransformTrailFromKey(const class ASequencerKeyActor* KeyActor)
{
	ASequencerMeshTrail* Trail = Cast<ASequencerMeshTrail>(KeyActor->GetOwner());
	if(Trail != nullptr)
	{
		FMeshTrailData* TrailPtr = MeshTrails.FindByPredicate([Trail](const FMeshTrailData InTrail)
		{
			return Trail == InTrail.Trail;
		});
		if(TrailPtr != nullptr)
		{
			// From the key, get the mesh trail, and then the track associated with that mesh trail
			UMovieScene3DTransformTrack* Track = TrailPtr->Track;
			// Draw a mesh trail for the key's associated actor
			TArray<TWeakObjectPtr<UObject>> KeyObjects;
			AActor* TrailActor = KeyActor->GetAssociatedActor();
			KeyObjects.Add(TrailActor);
			FPrimitiveDrawInterface* PDI = nullptr;

			for (TWeakPtr<ISequencer> WeakSequencer : Sequencers)
			{
				TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
				if (Sequencer.IsValid())
				{
					DrawTransformTrack(Sequencer, PDI, Track, KeyObjects, true);
				}
			}
		}
	}
}

void FSequencerEdMode::CleanUpMeshTrails()
{
	// Clean up any existing trails
	for (FMeshTrailData& MeshTrail : MeshTrails)
	{
		if (MeshTrail.Trail)
		{
			MeshTrail.Trail->Cleanup();
		}
	}
	MeshTrails.Empty();
}

void FSequencerEdMode::DrawTransformTrack(const TSharedPtr<ISequencer>& Sequencer, FPrimitiveDrawInterface* PDI,
											UMovieScene3DTransformTrack* TransformTrack, TArrayView<const TWeakObjectPtr<>> BoundObjects, const bool bIsSelected)
{
	using namespace UE::MovieScene;

	const bool bHitTesting = PDI && PDI->IsHitTesting();

	ASequencerMeshTrail* TrailActor = nullptr;
	// Get the Trail Actor associated with this track if we are drawing mesh trails
	if (bDrawMeshTrails)
	{
		FMeshTrailData* TrailPtr = MeshTrails.FindByPredicate([TransformTrack](const FMeshTrailData InTrail)
		{
			return InTrail.Track == TransformTrack;
		});
		if (TrailPtr != nullptr)
		{
			TrailActor = TrailPtr->Trail;
		}
	}

	bool bShowTrajectory = TransformTrack->GetAllSections().ContainsByPredicate(
		[bIsSelected](UMovieSceneSection* Section)
		{
			UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
			if (TransformSection)
			{
				switch (TransformSection->GetShow3DTrajectory())
				{
				case EShow3DTrajectory::EST_Always:				return true;
				case EShow3DTrajectory::EST_Never:				return false;
				case EShow3DTrajectory::EST_OnlyWhenSelected:	return bIsSelected;
				}
			}
			return false;
		}
	);
	
	FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();

	if (!bShowTrajectory || !TransformTrack->GetAllSections().ContainsByPredicate([](UMovieSceneSection* In){ return In->IsActive(); }))
	{
		return;
	}

	TArray<UMovieScene3DTransformSection*> AllSectionsScratch;

	FLinearColor TrackColor = FSequencerSectionPainter::BlendColor(TransformTrack->GetColorTint());
	FColor       KeyColor   = TrackColor.ToFColor(true);

	// Draw one line per-track (should only really ever be one)
	TRange<FFrameNumber> ViewRange(TickResolution.AsFrameNumber(Sequencer->GetViewRange().GetLowerBoundValue()), TickResolution.AsFrameNumber(Sequencer->GetViewRange().GetUpperBoundValue()));

	TArray<FTrajectoryKey> TrajectoryKeys = TransformTrack->GetTrajectoryData(Sequencer->GetLocalTime().Time.FrameNumber, Sequencer->GetSequencerSettings()->GetTrajectoryPathCap(), ViewRange);
	for (TWeakObjectPtr<> WeakBinding : BoundObjects)
	{
		UObject* BoundObject = WeakBinding.Get();
		if (!BoundObject)
		{
			continue;
		}

		UE::SequencerEdMode::FTrackTransforms TrackTransforms;
		TrackTransforms.Initialize(BoundObject, TrajectoryKeys, Sequencer.Get());

		int32 TransformIndex = 0;

		for (int32 TrajectoryIndex = 0; TrajectoryIndex < TrajectoryKeys.Num(); ++TrajectoryIndex)
		{
			const FTrajectoryKey& ThisKey = TrajectoryKeys[TrajectoryIndex];

			if (TransformIndex >= TrackTransforms.Transforms.Num())
			{
				continue;
			}

			FTransform ThisTransform = TrackTransforms.Transforms[TransformIndex];

			if (TrajectoryIndex < TrajectoryKeys.Num()-1)
			{
				FFrameTime NextKeyTime = TrajectoryKeys[TrajectoryIndex+1].Time;

				// Draw all the interpolated times between this and the next key
				FVector StartPosition = TrackTransforms.Transforms[TransformIndex].GetTranslation();
				++TransformIndex;

				const bool bIsConstantKey = ThisKey.Is(ERichCurveInterpMode::RCIM_Constant);
				if (bIsConstantKey)
				{
					if (PDI)
					{
						FVector EndPosition = TrackTransforms.Transforms[TransformIndex].GetTranslation();
						DrawDashedLine(PDI, StartPosition, EndPosition, TrackColor, 20, SDPG_Foreground);
					}
				}
				else
				{
					// Draw intermediate segments
					for ( ; TransformIndex < TrackTransforms.Times.Num() && TrackTransforms.Times[TransformIndex] < NextKeyTime; ++TransformIndex )
					{
						FTransform EndTransform = TrackTransforms.Transforms[TransformIndex];

						if (PDI)
						{
							PDI->DrawLine(StartPosition, EndTransform.GetTranslation(), TrackColor, SDPG_Foreground);
						}
						else if (TrailActor)
						{
							FTransform FrameTransform = EndTransform;
							FrameTransform.SetScale3D(FVector(3.0f));

							FFrameTime FrameTime = TrackTransforms.Times[TransformIndex];
							TrailActor->AddFrameMeshComponent(FrameTime / TickResolution, FrameTransform);
						}

						StartPosition = EndTransform.GetTranslation();
					}

					// Draw the final segment
					if (PDI && TrackTransforms.Times[TransformIndex] == NextKeyTime)
					{
						FTransform EndTransform = TrackTransforms.Transforms[TransformIndex];
						PDI->DrawLine(StartPosition, EndTransform.GetTranslation(), TrackColor, SDPG_Foreground);
					}
				}
			}

			// If this trajectory key does not have any key handles associated with it, we've nothing left to do
			if (ThisKey.KeyData.Num() == 0)
			{
				continue;
			}

			if (bHitTesting && PDI)
			{
				PDI->SetHitProxy(new HMovieSceneKeyProxy(TransformTrack, ThisKey));
			}

			// Drawing keys
			if (PDI != nullptr)
			{
				if (bHitTesting)
				{
					PDI->SetHitProxy(new HMovieSceneKeyProxy(TransformTrack, ThisKey));
				}

				PDI->DrawPoint(ThisTransform.GetTranslation(), KeyColor, 6.f, SDPG_Foreground);

				if (bHitTesting)
				{
					PDI->SetHitProxy(nullptr);
				}
			}
			else if (TrailActor != nullptr)
			{
				AllSectionsScratch.Reset();
				for (const FTrajectoryKey::FData& Value : ThisKey.KeyData)
				{
					UMovieScene3DTransformSection* Section = Value.Section.Get();
					if (Section && !AllSectionsScratch.Contains(Section))
					{
						FTransform MeshTransform = ThisTransform;
						MeshTransform.SetScale3D(FVector(3.0f));

						TrailActor->AddKeyMeshActor(ThisKey.Time / TickResolution, MeshTransform, Section);
						AllSectionsScratch.Add(Section);
					}
				}
			}
		}
	}
}


void FSequencerEdMode::DrawTracks3D(FPrimitiveDrawInterface* PDI)
{
	for (TWeakPtr<FSequencer> WeakSequencer : Sequencers)
	{
		TSharedPtr<FSequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			continue;
		}

		UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
		if (!Sequence)
		{
			continue;
		}

		// Gather a map of object bindings to their implict selection state
		TMap<const FMovieSceneBinding*, bool> ObjectBindingNodesSelectionMap;

		const FSequencerSelection& Selection = Sequencer->GetSelection();
		const TSharedRef<FSequencerNodeTree>& NodeTree  = Sequencer->GetNodeTree();
		for (const FMovieSceneBinding& Binding : Sequence->GetMovieScene()->GetBindings())
		{
			TSharedPtr<FSequencerObjectBindingNode> ObjectBindingNode = NodeTree->FindObjectBindingNode(Binding.GetObjectGuid());
			if (!ObjectBindingNode.IsValid())
			{
				continue;
			}

			bool bSelected = false;
			auto Traverse_IsSelected = [&Selection, &bSelected](FSequencerDisplayNode& InNode)
			{
				TSharedRef<FSequencerDisplayNode> Shared = InNode.AsShared();
				if (Selection.IsSelected(Shared) || Selection.NodeHasSelectedKeysOrSections(Shared))
				{
					bSelected = true;
					// Stop traversing
					return false;
				}

				return true;
			};

			ObjectBindingNode->Traverse_ParentFirst(Traverse_IsSelected, true);

			// If one of our parent is selected, we're considered selected
			TSharedPtr<FSequencerDisplayNode> ParentNode = ObjectBindingNode->GetParent();
			while (!bSelected && ParentNode.IsValid())
			{
				if (Selection.IsSelected(ParentNode.ToSharedRef()) || Selection.NodeHasSelectedKeysOrSections(ParentNode.ToSharedRef()))
				{
					bSelected = true;
				}

				ParentNode = ParentNode->GetParent();
			}

			ObjectBindingNodesSelectionMap.Add(&Binding, bSelected);
		}

		// Gather up the transform track nodes from the object binding nodes
		for (TTuple<const FMovieSceneBinding*, bool>& Pair : ObjectBindingNodesSelectionMap)
		{
			for (UMovieSceneTrack* Track : Pair.Key->GetTracks())
			{
				UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
				if (!TransformTrack)
				{
					continue;
				}

				// Ensure that we've got a mesh trail for this track
				if (bDrawMeshTrails)
				{
					const bool bHasMeshTrail = Algo::FindBy(MeshTrails, TransformTrack, &FMeshTrailData::Track) != nullptr;
					if (!bHasMeshTrail)
					{
						UViewportWorldInteraction* WorldInteraction = Cast<UViewportWorldInteraction>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( GetWorld() )->FindExtension( UViewportWorldInteraction::StaticClass() ) );
						if( WorldInteraction != nullptr )
						{
							ASequencerMeshTrail* TrailActor = WorldInteraction->SpawnTransientSceneActor<ASequencerMeshTrail>(TEXT("SequencerMeshTrail"), true);
							FMeshTrailData MeshTrail = FMeshTrailData(TransformTrack, TrailActor);
							MeshTrails.Add(MeshTrail);
						}
					}
				}

				const bool bIsSelected = Pair.Value;
				DrawTransformTrack(Sequencer, PDI, TransformTrack, Sequencer->FindObjectsInCurrentSequence(Pair.Key->GetObjectGuid()), bIsSelected);
			}
		}
	}
}

void FSequencerEdMode::DrawAudioTracks(FPrimitiveDrawInterface* PDI)
{
	for (TWeakPtr<ISequencer> WeakSequencer : Sequencers)
	{
		TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
		if (!Sequencer.IsValid())
		{
			continue;
		}

		UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
		if (!Sequence)
		{
			continue;
		}

		FQualifiedFrameTime CurrentTime = Sequencer->GetLocalTime();

		const FSequencerSelection& Selection = Sequencer->GetSelection();
		for (UMovieSceneTrack* Track : Selection.GetSelectedTracks())
		{
			UMovieSceneAudioTrack* AudioTrack = Cast<UMovieSceneAudioTrack>(Track);

			if (!AudioTrack || !AudioTrack->IsAMasterTrack())
			{
				continue;
			}

			for (UMovieSceneSection* Section : AudioTrack->GetAudioSections())
			{
				UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section);
				const FMovieSceneActorReferenceData& AttachActorData = AudioSection->GetAttachActorData();

				TMovieSceneChannelData<const FMovieSceneActorReferenceKey> ChannelData = AttachActorData.GetData();

				TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
				TArrayView<const FMovieSceneActorReferenceKey> Values = ChannelData.GetValues();
		
				FMovieSceneActorReferenceKey CurrentValue;
				AttachActorData.Evaluate(CurrentTime.Time, CurrentValue);

				for (int32 Index = 0; Index < Times.Num(); ++Index)
				{
					FMovieSceneObjectBindingID AttachBindingID = Values[Index].Object;
					FName AttachSocketName = Values[Index].SocketName;

					for (TWeakObjectPtr<> WeakObject : AttachBindingID.ResolveBoundObjects(Sequencer->GetFocusedTemplateID(), *Sequencer))
					{
						AActor* AttachActor = Cast<AActor>(WeakObject.Get());
						if (AttachActor)
						{
							USceneComponent* AttachComponent = AudioSection->GetAttachComponent(AttachActor, Values[Index]);
							if (AttachComponent)
							{
								FVector Location = AttachComponent->GetSocketLocation(AttachSocketName);
								bool bIsActive = CurrentValue == Values[Index];
								FColor Color = bIsActive ? FColor::Green : FColor::White;

								float Scale = PDI->View->WorldToScreen(Location).W * (4.0f / PDI->View->UnscaledViewRect.Width() / PDI->View->ViewMatrices.GetProjectionMatrix().M[0][0]);
								Scale *= bIsActive ? 15.f : 10.f;

								PDI->DrawSprite(Location, Scale, Scale, AudioTexture->Resource, Color, SDPG_Foreground, 0.0, 0.0, 0.0, 0.0, SE_BLEND_Masked);
								break;
							}
						}
					}
				}
			}
		}
	}
}

FSequencerEdModeTool::FSequencerEdModeTool(FSequencerEdMode* InSequencerEdMode) :
	SequencerEdMode(InSequencerEdMode)
{
}

FSequencerEdModeTool::~FSequencerEdModeTool()
{
}

bool FSequencerEdModeTool::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if( Key == EKeys::LeftMouseButton )
	{
		if( Event == IE_Pressed)
		{
			int32 HitX = ViewportClient->Viewport->GetMouseX();
			int32 HitY = ViewportClient->Viewport->GetMouseY();
			HHitProxy*HitResult = ViewportClient->Viewport->GetHitProxy(HitX, HitY);

			if(HitResult)
			{
				if( HitResult->IsA(HMovieSceneKeyProxy::StaticGetType()) )
				{
					HMovieSceneKeyProxy* KeyProxy = (HMovieSceneKeyProxy*)HitResult;
					SequencerEdMode->OnKeySelected(ViewportClient->Viewport, KeyProxy);
				}
			}
		}
	}

	return FModeTool::InputKey(ViewportClient, Viewport, Key, Event);
}
