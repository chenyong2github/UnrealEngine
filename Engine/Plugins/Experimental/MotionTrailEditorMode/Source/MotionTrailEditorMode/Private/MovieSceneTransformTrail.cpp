// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTransformTrail.h"
#include "TrailHierarchy.h"
#include "MotionTrailEditorMode.h"

#include "ISequencer.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Interrogation/SequencerInterrogationLinker.h"
#include "Systems/MovieSceneComponentTransformSystem.h"
#include "MovieSceneSequence.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "ViewportWorldInteraction.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

UMSTrailKeyProperties* FDefaultMovieSceneTransformTrailTool::KeyProps = nullptr;

void FDefaultMovieSceneTransformTrailTool::Setup()
{
	if (!KeyProps)
	{
		KeyProps = NewObject<UMSTrailKeyProperties>();
	}
	BuildKeys();
}

void FDefaultMovieSceneTransformTrailTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (!OwningTrail->GetDrawInfo()->IsVisible())
	{
		if (ActiveTransformGizmo.IsValid() && Cast<UMSTrailTransformProxy>(ActiveTransformGizmo->ActiveTarget))
		{
			ClearSelection();
		}
		return;
	}

	FEditorViewportClient* EditorViewportClient = StaticCast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
	if (!EditorViewportClient)
	{
		return;
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(EditorViewportClient->Viewport, EditorViewportClient->GetScene(), EditorViewportClient->EngineShowFlags));
	FSceneView* SceneView = EditorViewportClient->CalcSceneView(&ViewFamily);
	FTrailScreenSpaceTransform ScreenSpaceTransform = FTrailScreenSpaceTransform(SceneView, GEditor->GetActiveViewport(), EditorViewportClient->GetDPIScale());

	for (const TPair<FFrameNumber, TUniquePtr<FKeyInfo>>& FrameKeyPair : Keys)
	{
		if (OwningTrail->GetDrawInfo()->GetCachedViewRange().Contains(OwningTrail->WeakSequencer.Pin()->GetFocusedTickResolution().AsSeconds(FrameKeyPair.Value->FrameNumber)))
		{
			RenderAPI->GetPrimitiveDrawInterface()->DrawPoint(FrameKeyPair.Value->SceneComponent->GetComponentLocation(), FLinearColor::Gray, KeyProps->KeySize, SDPG_Foreground);
		}
	}
}

FInputRayHit FDefaultMovieSceneTransformTrailTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	if (!OwningTrail->GetDrawInfo()->IsVisible())
	{
		return FInputRayHit();
	}

	FEditorViewportClient* EditorViewportClient = StaticCast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
	if (!EditorViewportClient)
	{
		return FInputRayHit();
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(EditorViewportClient->Viewport, EditorViewportClient->GetScene(), EditorViewportClient->EngineShowFlags));
	FSceneView* SceneView = EditorViewportClient->CalcSceneView(&ViewFamily);
	FTrailScreenSpaceTransform ScreenSpaceTransform = FTrailScreenSpaceTransform(SceneView, GEditor->GetActiveViewport(), EditorViewportClient->GetDPIScale());

	const FVector2D RayProjectedPos = ScreenSpaceTransform.ProjectPoint(ClickPos.WorldRay.PointAt(1.0f)).GetValue();

	CachedSelected = nullptr;
	float MinHitDistance = TNumericLimits<float>::Max();
	for (const TPair<FFrameNumber, TUniquePtr<FKeyInfo>>& FrameKeyPair : Keys)
	{
		if (OwningTrail->GetDrawInfo()->GetCachedViewRange().Contains(OwningTrail->WeakSequencer.Pin()->GetFocusedTickResolution().AsSeconds(FrameKeyPair.Value->FrameNumber)))
		{
			TOptional<FVector2D> KeyProjectedPos = ScreenSpaceTransform.ProjectPoint(FrameKeyPair.Value->SceneComponent->GetComponentLocation());

			if (KeyProjectedPos && FVector2D::Distance(KeyProjectedPos.GetValue(), RayProjectedPos) < KeyProps->KeySize)
			{
				const float HitDistance = ClickPos.WorldRay.GetParameter(FrameKeyPair.Value->SceneComponent->GetComponentLocation());
				if (HitDistance < MinHitDistance)
				{
					MinHitDistance = HitDistance;
					CachedSelected = FrameKeyPair.Value.Get();
				}
			}
		}
	}

	return (MinHitDistance < TNumericLimits<float>::Max()) ? FInputRayHit(MinHitDistance) : FInputRayHit();
}

void FDefaultMovieSceneTransformTrailTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	UTrailToolManager* TrailToolManager = Cast<UTrailToolManager>(WeakEditorMode->GetToolManager()->GetActiveTool(EToolSide::Mouse));
	if (!OwningTrail->GetDrawInfo()->IsVisible() || !TrailToolManager)
	{
		return;
	}

	if (CachedSelected)
	{
		ActiveTransformGizmo = Cast<UTransformGizmo>(TrailToolManager->GetGizmoManager()->FindGizmoByInstanceIdentifier(UTrailToolManager::TrailKeyTransformGizmoInstanceIdentifier));

		UMSTrailTransformProxy* MSTrailTransformProxy;
		if (ActiveTransformGizmo.IsValid() && Cast<UMSTrailTransformProxy>(Cast<UTransformGizmo>(ActiveTransformGizmo)->ActiveTarget))
		{
			MSTrailTransformProxy = Cast<UMSTrailTransformProxy>(ActiveTransformGizmo->ActiveTarget);

			if (!FSlateApplication::Get().GetModifierKeys().IsShiftDown())
			{
				MSTrailTransformProxy = NewObject<UMSTrailTransformProxy>(TrailToolManager);
			}
		}
		else
		{
			MSTrailTransformProxy = NewObject<UMSTrailTransformProxy>(TrailToolManager);
			MSTrailTransformProxy->bRotatePerObject = true;

			ETransformGizmoSubElements GizmoElements = ETransformGizmoSubElements::TranslateRotateUniformScale;
			ActiveTransformGizmo = TrailToolManager->GetGizmoManager()->CreateCustomTransformGizmo(GizmoElements, TrailToolManager, UTrailToolManager::TrailKeyTransformGizmoInstanceIdentifier);
		}

		if (MSTrailTransformProxy->GetKeysTracked().Contains(CachedSelected))
		{
			MSTrailTransformProxy->RemoveKey(CachedSelected);
		}
		else
		{
			MSTrailTransformProxy->AddKey(CachedSelected);
		}

		if (MSTrailTransformProxy->IsEmpty())
		{
			TrailToolManager->GetGizmoManager()->DestroyGizmo(ActiveTransformGizmo.Get());
			return;
		}

		// re-create actor, TODO: re-initialize actor? ActiveTransformGizmo->Initialize()
		for (const TPair<FKeyInfo*, UMSTrailTransformProxy::FKeyDelegateHandles>& SelectedKeyInfo : MSTrailTransformProxy->GetKeysTracked())
		{
			UpdateGizmoActorComponents(SelectedKeyInfo.Key, ActiveTransformGizmo.Get());
		}
		
		ActiveTransformGizmo->SetActiveTarget(MSTrailTransformProxy);
	}
}

void FDefaultMovieSceneTransformTrailTool::OnSectionChanged()
{
	if (ShouldRebuildKeys())
	{
		ClearSelection();
		BuildKeys();
	}

	DirtyKeyTransforms();
}

void FDefaultMovieSceneTransformTrailTool::BuildKeys()
{
	check(WeakEditorMode.IsValid());
	UTrailToolManager* TrailToolManager = Cast<UTrailToolManager>(WeakEditorMode->GetToolManager()->GetActiveTool(EToolSide::Mouse));
	if (!TrailToolManager)
	{
		return;
	}

	Keys.Reset();

	UMovieScene3DTransformSection* AbsoluteTransformSection = OwningTrail->GetTransformSection();
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = AbsoluteTransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	for (FMovieSceneFloatChannel* FloatChannel : FloatChannels)
	{
		for (int32 Idx = 0; Idx < FloatChannel->GetNumKeys(); Idx++)
		{
			const FFrameNumber CurTime = FloatChannel->GetTimes()[Idx];

			if (!Keys.Contains(CurTime))
			{
				TUniquePtr<FKeyInfo> TempKeyInfo = MakeUnique<FKeyInfo>(CurTime, AbsoluteTransformSection, OwningTrail);
				Keys.Add(CurTime, MoveTemp(TempKeyInfo));
			}
		}
	}
}

bool FDefaultMovieSceneTransformTrailTool::ShouldRebuildKeys()
{
	TMap<FFrameNumber, TSet<EMSTrailTransformChannel>> KeyTimes;
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = OwningTrail->GetTransformSection()->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

	for (uint8 ChannelIdx = 0; ChannelIdx <= uint8(EMSTrailTransformChannel::MaxChannel); ChannelIdx++)
	{
		FMovieSceneFloatChannel* FloatChannel = FloatChannels[ChannelIdx];
		for (int32 Idx = 0; Idx < FloatChannel->GetNumKeys(); Idx++)
		{
			const FFrameNumber CurTime = FloatChannel->GetTimes()[Idx];
			KeyTimes.FindOrAdd(CurTime).Add(EMSTrailTransformChannel(ChannelIdx));
		}
	}

	if (KeyTimes.Num() != Keys.Num())
	{
		return true;
	}

	for (const TPair<FFrameNumber, TSet<EMSTrailTransformChannel>>& TimeKeyPair : KeyTimes)
	{
		if (!Keys.Contains(TimeKeyPair.Key))
		{
			return true;
		}
		for (uint8 ChannelIdx = 0; ChannelIdx <= uint8(EMSTrailTransformChannel::MaxChannel); ChannelIdx++)
		{
			const EMSTrailTransformChannel TransformChannel = EMSTrailTransformChannel(ChannelIdx);
			if ((!TimeKeyPair.Value.Contains(TransformChannel) && Keys[TimeKeyPair.Key]->IdxMap.Contains(TransformChannel)) ||
				(TimeKeyPair.Value.Contains(TransformChannel) && !Keys[TimeKeyPair.Key]->IdxMap.Contains(TransformChannel)))
			{
				return true;
			}
		}
	}

	return false;
}

void FDefaultMovieSceneTransformTrailTool::ClearSelection()
{
	check(WeakEditorMode.IsValid());
	UTrailToolManager* TrailToolManager = Cast<UTrailToolManager>(WeakEditorMode->GetToolManager()->GetActiveTool(EToolSide::Mouse));
	if (ActiveTransformGizmo.IsValid() && TrailToolManager)
	{
		UMSTrailTransformProxy* MSTrailTransformProxy = Cast<UMSTrailTransformProxy>(ActiveTransformGizmo->ActiveTarget);
		if (MSTrailTransformProxy)
		{
			for (const TPair<FFrameNumber, TUniquePtr<FKeyInfo>>& KeyInfoPair : Keys)
			{
				if (MSTrailTransformProxy->GetKeysTracked().Contains(KeyInfoPair.Value.Get()))
				{
					MSTrailTransformProxy->RemoveKey(KeyInfoPair.Value.Get());
				}
			}

			if (MSTrailTransformProxy->IsEmpty())
			{
				TrailToolManager->GetGizmoManager()->DestroyGizmo(ActiveTransformGizmo.Get());
			}
		}
	}

	ActiveTransformGizmo = nullptr;
}

void FDefaultMovieSceneTransformTrailTool::DirtyKeyTransforms()
{
	for (const TPair<FFrameNumber, TUniquePtr<FKeyInfo>>& FrameKeyPair : Keys)
	{
		FrameKeyPair.Value->bDirty = true;
	}
}

void FDefaultMovieSceneTransformTrailTool::UpdateKeysInRange(FTrajectoryCache* ParentTrajectoryCache, const TRange<double>& ViewRange)
{
	for (const TPair<FFrameNumber, TUniquePtr<FKeyInfo>>& FrameKeyPair : Keys)
	{
		const double EvalTime = OwningTrail->GetSequencer()->GetFocusedTickResolution().AsSeconds(FrameKeyPair.Value->FrameNumber);
		if (FrameKeyPair.Value->bDirty && ViewRange.Contains(EvalTime))
		{
			FrameKeyPair.Value->UpdateKeyTransform(EKeyUpdateType::FromTrailCache, ParentTrajectoryCache);
		}
	}
}

FDefaultMovieSceneTransformTrailTool::FKeyInfo::FKeyInfo(const FFrameNumber InFrameNumber, UMovieScene3DTransformSection* InTrackSection, FMovieSceneTransformTrail* InOwningTrail)
	: SceneComponent(NewObject<USceneComponent>())
	, ParentSceneComponent(NewObject<USceneComponent>())
	, IdxMap()
	, DragStartTransform()
	, FrameNumber(InFrameNumber)
	, bDirty(true)
	, TrackSection(InTrackSection)
	, OwningTrail(InOwningTrail)
{
	TArrayView<FMovieSceneFloatChannel*> Channels = InTrackSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	for (uint8 Idx = 0; Idx <= uint8(EMSTrailTransformChannel::MaxChannel); Idx++)
	{
		const int32 FoundIdx = Channels[Idx]->GetData().FindKey(InFrameNumber);
		if (FoundIdx != INDEX_NONE)
		{
			IdxMap.Add(EMSTrailTransformChannel(Idx), Channels[Idx]->GetData().GetHandle(FoundIdx));
		}
	}

	SceneComponent->AttachToComponent(ParentSceneComponent,FAttachmentTransformRules::KeepRelativeTransform);
}

void FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragStart(class UTransformProxy*)
{
	TArrayView<FMovieSceneFloatChannel*> Channels = TrackSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	for(const TPair<EMSTrailTransformChannel, FKeyHandle>& ChannelHandlePair : IdxMap)
	{
		const int32 KeyIdx = Channels[uint8(ChannelHandlePair.Key)]->GetData().GetIndex(ChannelHandlePair.Value);
		DragStartTransform.Add(ChannelHandlePair.Key, Channels[uint8(ChannelHandlePair.Key)]->GetData().GetValues()[KeyIdx].Value);
	}
	DragStartCompTransform = UE::MovieScene::FIntermediate3DTransform(SceneComponent->GetRelativeLocation(), SceneComponent->GetRelativeRotation(), SceneComponent->GetRelativeScale3D());
}

void FDefaultMovieSceneTransformTrailTool::FKeyInfo::UpdateKeyTransform(EKeyUpdateType UpdateType, FTrajectoryCache* ParentTrajectoryCache)
{
	bDirty = false;
	TArrayView<FMovieSceneFloatChannel*> Channels = TrackSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (UpdateType == EKeyUpdateType::FromComponentDelta)
	{
		UE::MovieScene::FIntermediate3DTransform RelativeTransform = UE::MovieScene::FIntermediate3DTransform(
			SceneComponent->GetRelativeLocation() - DragStartCompTransform->GetTranslation(),
			SceneComponent->GetRelativeRotation() - DragStartCompTransform->GetRotation(),
			SceneComponent->GetRelativeScale3D() / DragStartCompTransform->GetScale()
		);

		OwningTrail->ForceEvaluateNextTick();
		TrackSection->Modify();
		
		auto TryUpdateChannel = [this, &RelativeTransform, &Channels](const EMSTrailTransformChannel Channel) {
			if (IdxMap.Contains(Channel))
			{
				const int32 KeyIdx = Channels[uint8(Channel)]->GetData().GetIndex(IdxMap[Channel]);
				Channels[uint8(Channel)]->GetData().GetValues()[KeyIdx].Value =
					DragStartTransform[Channel] + RelativeTransform[uint8(Channel)];
			}
		};

		auto TryUpdateScaleChannel = [this, &RelativeTransform, &Channels](const EMSTrailTransformChannel Channel) {
			if (IdxMap.Contains(Channel))
			{
				const int32 KeyIdx = Channels[uint8(Channel)]->GetData().GetIndex(IdxMap[Channel]);
				Channels[uint8(Channel)]->GetData().GetValues()[KeyIdx].Value =
					DragStartTransform[Channel] * RelativeTransform[uint8(Channel)];
			}
		};

		TryUpdateChannel(EMSTrailTransformChannel::TranslateX);
		TryUpdateChannel(EMSTrailTransformChannel::TranslateY);
		TryUpdateChannel(EMSTrailTransformChannel::TranslateZ);
		TryUpdateChannel(EMSTrailTransformChannel::RotateX);
		TryUpdateChannel(EMSTrailTransformChannel::RotateY);
		TryUpdateChannel(EMSTrailTransformChannel::RotateZ);
		TryUpdateScaleChannel(EMSTrailTransformChannel::ScaleX);
		TryUpdateScaleChannel(EMSTrailTransformChannel::ScaleY);
		TryUpdateScaleChannel(EMSTrailTransformChannel::ScaleZ);
	}
	else if(UpdateType == EKeyUpdateType::FromTrailCache)
	{
		const double EvalTime = OwningTrail->GetSequencer()->GetFocusedTickResolution().AsSeconds(FrameNumber);

		if (ParentTrajectoryCache)
		{
			const FTransform ParentTransform = ParentTrajectoryCache->GetInterp(EvalTime);
			ParentSceneComponent->SetWorldTransform(ParentTransform);
		}

		const FTransform TempTransform = OwningTrail->GetTrajectoryTransforms()->GetInterp(EvalTime);
		SceneComponent->SetWorldTransform(TempTransform);
		SceneComponent->SetWorldRotation(FQuat::Identity);
		SceneComponent->SetWorldScale3D(FVector::OneVector);
	}
}

void FDefaultMovieSceneTransformTrailTool::UpdateGizmoActorComponents(FKeyInfo* KeyInfo, UTransformGizmo* TransformGizmo)
{
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::TranslateX))
	{
		TransformGizmo->GetGizmoActor()->TranslateX = nullptr;
		TransformGizmo->GetGizmoActor()->TranslateXY = nullptr;
		TransformGizmo->GetGizmoActor()->TranslateXZ = nullptr;
	}
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::TranslateY))
	{
		TransformGizmo->GetGizmoActor()->TranslateY = nullptr;
		TransformGizmo->GetGizmoActor()->TranslateXY = nullptr;
		TransformGizmo->GetGizmoActor()->TranslateYZ = nullptr;
	}
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::TranslateZ))
	{
		TransformGizmo->GetGizmoActor()->TranslateZ = nullptr;
		TransformGizmo->GetGizmoActor()->TranslateXZ = nullptr;
		TransformGizmo->GetGizmoActor()->TranslateYZ = nullptr;
	}

	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::RotateX))
	{
		TransformGizmo->GetGizmoActor()->RotateX = nullptr;
	}
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::RotateY))
	{
		TransformGizmo->GetGizmoActor()->RotateY = nullptr;
	}
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::RotateZ))
	{
		TransformGizmo->GetGizmoActor()->RotateZ = nullptr;
	}

	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::ScaleX))
	{
		TransformGizmo->GetGizmoActor()->AxisScaleX = nullptr;
	}
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::ScaleY))
	{
		TransformGizmo->GetGizmoActor()->AxisScaleY = nullptr;
	}
	if (!KeyInfo->IdxMap.Contains(EMSTrailTransformChannel::ScaleZ))
	{
		TransformGizmo->GetGizmoActor()->AxisScaleZ = nullptr;
	}
}

FMovieSceneTransformTrail::FMovieSceneTransformTrail(const FLinearColor& InColor, const bool bInIsVisible, TWeakObjectPtr<UMovieScene3DTransformTrack> InWeakTrack, TSharedPtr<ISequencer> InSequencer)
	: FTrail()
	, CachedEffectiveRange(TRange<double>::Empty())
	, DefaultTrailTool()
	, DrawInfo()
	, TrajectoryCache()
	, LastTransformTrackSig(InWeakTrack->GetSignature())
	, WeakTrack(InWeakTrack)
	, WeakSequencer(InSequencer)
	, InterrogationLinker(NewObject<USequencerInterrogationLinker>())
{
	DefaultTrailTool = MakeUnique<FDefaultMovieSceneTransformTrailTool>(this);
	TrajectoryCache = MakeUnique<FArrayTrajectoryCache>(0.01, GetEffectiveTrackRange());
	DrawInfo = MakeUnique<FCachedTrajectoryDrawInfo>(InColor, bInIsVisible, TrajectoryCache.Get());
	InterrogationLinker->ImportTrack(WeakTrack.Get());
}

ETrailCacheState FMovieSceneTransformTrail::UpdateTrail(const FSceneContext& InSceneContext)
{
	UMovieScene3DTransformTrack* Track = WeakTrack.Get();
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	FGuid SequencerBinding;
	if (Sequencer)
	{ // TODO: expensive, but for some reason Track stays alive even after it is deleted
		Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrackBinding(*Track, SequencerBinding);
	}

	checkf(InSceneContext.TrailHierarchy->GetHierarchy()[InSceneContext.YourNode].Parents.Num() == 1, TEXT("MovieSceneTransformTrails only support one parent"));
	const FGuid ParentGuid = InSceneContext.TrailHierarchy->GetHierarchy()[InSceneContext.YourNode].Parents[0];
	const TUniquePtr<FTrail>& Parent = InSceneContext.TrailHierarchy->GetAllTrails()[ParentGuid];

	ETrailCacheState ParentCacheState = InSceneContext.ParentCacheStates[ParentGuid];

	if (!Sequencer || !SequencerBinding.IsValid() || (ParentCacheState == ETrailCacheState::Dead))
	{
		return ETrailCacheState::Dead;
	}
	
	const bool bTrackUnchanged = Track->GetSignature() == LastTransformTrackSig;
	const bool bParentChanged = ParentCacheState != ETrailCacheState::UpToDate;

	ETrailCacheState CacheState;
	FTrailEvaluateTimes TempEvalTimes = InSceneContext.EvalTimes;
	TArrayView<FMovieSceneFloatChannel*> Channels = GetTransformSection()->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (!bTrackUnchanged || bParentChanged || bForceEvaluateNextTick)
	{
		if (DefaultTrailTool->IsActive())
		{
			DefaultTrailTool->OnSectionChanged();
		}

		const double Spacing = InSceneContext.EvalTimes.Spacing.Get(InSceneContext.TrailHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerSegment);
		CachedEffectiveRange = TRange<double>::Hull({ Parent->GetEffectiveRange(), GetEffectiveTrackRange() });
		*TrajectoryCache = FArrayTrajectoryCache(Spacing, CachedEffectiveRange, FTransform::Identity * Parent->GetTrajectoryTransforms()->GetDefault()); // TODO:: Get channel default values
		TrajectoryCache->UpdateCacheTimes(TempEvalTimes);

		CacheState = ETrailCacheState::Stale;
		bForceEvaluateNextTick = false;
		LastTransformTrackSig = Track->GetSignature();
	}
	else 
	{
		TrajectoryCache->UpdateCacheTimes(TempEvalTimes);
	
		CacheState = ETrailCacheState::UpToDate;
	}

	if (TempEvalTimes.EvalTimes.Num() > 0)
	{
		// TODO: re-populating the interrogator every frame is kind of inefficient
		InterrogationLinker->ImportTrack(WeakTrack.Get());

		for (const double Time : TempEvalTimes.EvalTimes)
		{
			const FFrameTime TickTime = Time * Sequencer->GetFocusedTickResolution();
			InterrogationLinker->AddInterrogation(TickTime);
		}

		InterrogationLinker->Update();

		TArray<UE::MovieScene::FIntermediate3DTransform> TempLocalTransforms;
		TempLocalTransforms.SetNum(TempEvalTimes.EvalTimes.Num());
		InterrogationLinker->FindSystem<UMovieSceneComponentTransformSystem>()->Interrogate(TempLocalTransforms);

		for (int32 Idx = 0; Idx < TempEvalTimes.EvalTimes.Num(); Idx++)
		{
			const FTransform TempLocalTransform = FTransform(TempLocalTransforms[Idx].GetRotation(), TempLocalTransforms[Idx].GetTranslation(), TempLocalTransforms[Idx].GetScale());
			FTransform TempWorldTransform = TempLocalTransform * Parent->GetTrajectoryTransforms()->Get(TempEvalTimes.EvalTimes[Idx]);
			TempWorldTransform.NormalizeRotation();
			TrajectoryCache->Set(TempEvalTimes.EvalTimes[Idx] + KINDA_SMALL_NUMBER, TempWorldTransform);
		}

		InterrogationLinker->Reset();
	}

	if (DefaultTrailTool->IsActive())
	{
		DefaultTrailTool->UpdateKeysInRange(Parent->GetTrajectoryTransforms(), InSceneContext.EvalTimes.Range);
	}

	return CacheState;
}

TMap<FString, FInteractiveTrailTool*> FMovieSceneTransformTrail::GetTools()
{
	TMap<FString, FInteractiveTrailTool*> TempToolMap;
	TempToolMap.Add(UMotionTrailEditorMode::DefaultToolName, DefaultTrailTool.Get());
	return TempToolMap;
}

void FMovieSceneTransformTrail::AddReferencedObjects(FReferenceCollector & Collector)
{
	Collector.AddReferencedObject(InterrogationLinker);
	TArray<UObject*> ToolKeys = DefaultTrailTool->GetKeySceneComponents();
	Collector.AddReferencedObjects(ToolKeys);
}

UMovieScene3DTransformSection* FMovieSceneTransformTrail::GetTransformSection() const
{
	UMovieScene3DTransformTrack* TransformTrack = WeakTrack.Get();
	check(TransformTrack);

	UMovieScene3DTransformSection* AbsoluteTransformSection = nullptr;
	for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
	{
		UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
		check(Section);

		if (!TransformSection->GetBlendType().IsValid() || TransformSection->GetBlendType().Get() == EMovieSceneBlendType::Absolute)
		{
			AbsoluteTransformSection = TransformSection;
			break;
		}
	}

	check(AbsoluteTransformSection);

	return AbsoluteTransformSection;
}

TRange<double> FMovieSceneTransformTrail::GetEffectiveTrackRange() const
{
	UMovieScene3DTransformTrack* TransformTrack = WeakTrack.Get();
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(TransformTrack && Sequencer);

	TRange<double> EffectiveTrackRange = TRange<double>::Empty();
	for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
	{
		TRange<FFrameNumber> EffectiveRange = Section->ComputeEffectiveRange();
		TRange<double> SectionRangeSeconds = TRange<double>(
			Sequencer->GetFocusedTickResolution().AsSeconds(EffectiveRange.GetLowerBoundValue()),
			Sequencer->GetFocusedTickResolution().AsSeconds(EffectiveRange.GetUpperBoundValue())
		);
		EffectiveTrackRange = TRange<double>::Hull(TArray<TRange<double>>{ EffectiveTrackRange, SectionRangeSeconds });
	}

	return EffectiveTrackRange;
}
