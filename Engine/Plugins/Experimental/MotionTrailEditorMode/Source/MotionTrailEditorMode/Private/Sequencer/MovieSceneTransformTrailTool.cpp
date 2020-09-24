// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTransformTrailTool.h"
#include "TrailHierarchy.h"
#include "MovieSceneTransformTrail.h"
#include "MotionTrailEditorMode.h"

#include "ISequencer.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "Systems/MovieSceneComponentTransformSystem.h"
#include "MovieSceneSequence.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "ViewportWorldInteraction.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

namespace UE
{
namespace MotionTrailEditor
{

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
	const bool bIsVisible = WeakEditorMode->GetHierarchyForSequencer(OwningTrail->GetSequencer().Get())->GetVisibilityManager().IsTrailVisible(OwningTrail->GetCachedHierarchyGuid());

	if (!bIsVisible)
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
	const bool bIsVisible = WeakEditorMode->GetHierarchyForSequencer(OwningTrail->GetSequencer().Get())->GetVisibilityManager().IsTrailVisible(OwningTrail->GetCachedHierarchyGuid());

	if (!bIsVisible)
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
	const bool bIsVisible = WeakEditorMode->GetHierarchyForSequencer(OwningTrail->GetSequencer().Get())->GetVisibilityManager().IsTrailVisible(OwningTrail->GetCachedHierarchyGuid());
	UTrailToolManager* TrailToolManager = Cast<UTrailToolManager>(WeakEditorMode->GetToolManager()->GetActiveTool(EToolSide::Mouse));
	if (!bIsVisible || !TrailToolManager)
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

TArray<UObject*> FDefaultMovieSceneTransformTrailTool::GetKeySceneComponents()
{
	TArray<UObject*> SceneComponents;
	SceneComponents.Reserve(Keys.Num() * 2);
	for (const TPair<FFrameNumber, TUniquePtr<FKeyInfo>>& FrameKeyPair : Keys)
	{
		SceneComponents.Add(FrameKeyPair.Value->SceneComponent);
		SceneComponents.Add(FrameKeyPair.Value->ParentSceneComponent);
	}
	return SceneComponents;
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

	UMovieSceneSection* AbsoluteTransformSection = OwningTrail->GetSection();
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = AbsoluteTransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	FloatChannels = FloatChannels.Slice(OwningTrail->GetChannelOffset(), uint8(EMSTrailTransformChannel::MaxChannel) + 1);
	for (int32 ChannelIdx = 0; ChannelIdx <= uint8(EMSTrailTransformChannel::MaxChannel); ChannelIdx++)
	{
		FMovieSceneFloatChannel* FloatChannel = FloatChannels[ChannelIdx];
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
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = OwningTrail->GetSection()->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	FloatChannels = FloatChannels.Slice(OwningTrail->GetChannelOffset(), uint8(EMSTrailTransformChannel::MaxChannel) + 1);

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
				(Keys[TimeKeyPair.Key]->IdxMap.Contains(TransformChannel) && FloatChannels[uint8(TransformChannel)]->GetData().GetIndex(Keys[TimeKeyPair.Key]->IdxMap[TransformChannel]) == INDEX_NONE) ||
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
		if (FrameKeyPair.Value->bDirty && ViewRange.Contains(EvalTime + KINDA_SMALL_NUMBER))
		{
			FrameKeyPair.Value->UpdateKeyTransform(EKeyUpdateType::FromTrailCache, ParentTrajectoryCache);
		}
	}
}

FDefaultMovieSceneTransformTrailTool::FKeyInfo::FKeyInfo(const FFrameNumber InFrameNumber, UMovieSceneSection* InSection, FMovieSceneTransformTrail* InOwningTrail)
	: SceneComponent(NewObject<USceneComponent>())
	, ParentSceneComponent(NewObject<USceneComponent>())
	, IdxMap()
	, DragStartTransform()
	, FrameNumber(InFrameNumber)
	, bDirty(true)
	, Section(InSection)
	, OwningTrail(InOwningTrail)
{
	TArrayView<FMovieSceneFloatChannel*> Channels = InSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	Channels = Channels.Slice(OwningTrail->GetChannelOffset(), uint8(EMSTrailTransformChannel::MaxChannel) + 1);
	for (uint8 Idx = 0; Idx <= uint8(EMSTrailTransformChannel::MaxChannel); Idx++)
	{
		const int32 FoundIdx = Channels[Idx]->GetData().FindKey(InFrameNumber);
		if (FoundIdx != INDEX_NONE)
		{
			IdxMap.Add(EMSTrailTransformChannel(Idx), Channels[Idx]->GetData().GetHandle(FoundIdx));
		}
	}

	SceneComponent->AttachToComponent(ParentSceneComponent, FAttachmentTransformRules::KeepRelativeTransform);
}

void  FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnKeyTransformChanged(UTransformProxy*, FTransform NewTransform)
{
	if (DragStartCompTransform)
	{
		UpdateKeyTransform(EKeyUpdateType::FromComponentDelta);
	}
}

void FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragStart(UTransformProxy*)
{
	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	Channels = Channels.Slice(OwningTrail->GetChannelOffset(), uint8(EMSTrailTransformChannel::MaxChannel) + 1);
	for (const TPair<EMSTrailTransformChannel, FKeyHandle>& ChannelHandlePair : IdxMap)
	{
		const int32 KeyIdx = Channels[uint8(ChannelHandlePair.Key)]->GetData().GetIndex(ChannelHandlePair.Value);
		if (KeyIdx == INDEX_NONE) // This can happen when the channel is re-built on undo and all key handles are invalidated, hack for now but shouldn't happen anyways 
		{
			OwningTrail->ForceEvaluateNextTick();
			return;
		}

		DragStartTransform.Add(ChannelHandlePair.Key, Channels[uint8(ChannelHandlePair.Key)]->GetData().GetValues()[KeyIdx].Value);
	}
	DragStartCompTransform = UE::MovieScene::FIntermediate3DTransform(SceneComponent->GetRelativeLocation(), SceneComponent->GetRelativeRotation(), SceneComponent->GetRelativeScale3D());
}

void FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragEnd(UTransformProxy*)
{
	DragStartTransform.Reset();
	DragStartCompTransform = TOptional<UE::MovieScene::FIntermediate3DTransform>();
}

void FDefaultMovieSceneTransformTrailTool::FKeyInfo::UpdateKeyTransform(EKeyUpdateType UpdateType, FTrajectoryCache* ParentTrajectoryCache)
{
	bDirty = false;
	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	Channels = Channels.Slice(OwningTrail->GetChannelOffset(), uint8(EMSTrailTransformChannel::MaxChannel) + 1);
	if (UpdateType == EKeyUpdateType::FromComponentDelta)
	{
		const UE::MovieScene::FIntermediate3DTransform CurrentTransform = UE::MovieScene::FIntermediate3DTransform(
			SceneComponent->GetRelativeLocation(),
			SceneComponent->GetRelativeRotation(),
			SceneComponent->GetRelativeScale3D()
		);

		const UE::MovieScene::FIntermediate3DTransform RelativeTransform = OwningTrail->CalculateDeltaToApply(*DragStartCompTransform, CurrentTransform);

		OwningTrail->ForceEvaluateNextTick();
		Section->Modify();

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
	else if (UpdateType == EKeyUpdateType::FromTrailCache)
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

} // namespace MovieScene
} // namespace UE


void UMSTrailTransformProxy::AddKey(UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo* KeyInfo)
{
	FKeyDelegateHandles KeyDelegateHandles;
	KeyDelegateHandles.OnTransformChangedHandle = OnTransformChanged.AddRaw(KeyInfo, &UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnKeyTransformChanged);
	KeyDelegateHandles.OnBeginTransformEditSequenceHandle = OnBeginTransformEdit.AddRaw(KeyInfo, &UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragStart);
	KeyDelegateHandles.OnEndTransformEditSequenceHandle = OnEndTransformEdit.AddRaw(KeyInfo, &UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragEnd);
	KeysTracked.Add(KeyInfo, KeyDelegateHandles);
	AddComponent(KeyInfo->SceneComponent);
}

void UMSTrailTransformProxy::RemoveKey(UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo* KeyInfo)
{
	OnTransformChanged.Remove(KeysTracked[KeyInfo].OnTransformChangedHandle);
	OnBeginTransformEdit.Remove(KeysTracked[KeyInfo].OnBeginTransformEditSequenceHandle);
	OnEndTransformEdit.Remove(KeysTracked[KeyInfo].OnEndTransformEditSequenceHandle);
	KeysTracked.Remove(KeyInfo);
	RemoveComponent(KeyInfo->SceneComponent);
}

void UMSTrailTransformProxy::RemoveComponent(USceneComponent* Component)
{
	for (int32 Idx = 0; Idx < Objects.Num(); Idx++)
	{
		if (Objects[Idx].Component == Component)
		{
			Objects.RemoveAt(Idx);
			UpdateSharedTransform();
			OnTransformChanged.Broadcast(this, SharedTransform);
			return;
		}
	}
	return;
}
