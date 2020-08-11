// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trail.h"
#include "MotionTrailEditorToolset.h"
#include "TrajectoryDrawInfo.h"

#include "BaseGizmos/TransformProxy.h"

#include "MovieSceneTracksComponentTypes.h"

#include "UObject/GCObject.h"

#include "MovieSceneTransformTrail.generated.h"

class FMovieSceneTransformTrail;

// TODO: split tool stuff into a different file, operate on IEditableMovieSceneTrail or something
enum class EMSTrailTransformChannel : uint8
{
	TranslateX = 0,
	TranslateY = 1,
	TranslateZ = 2,
	RotateX = 3,
	RotateY = 4,
	RotateZ = 5,
	ScaleX = 6,
	ScaleY = 7,
	ScaleZ = 8,
	MaxChannel = 8
};

UCLASS()
class UMSTrailKeyProperties : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = ToolShowOptions)
	float KeySize = 10.0f;
};

class FDefaultMovieSceneTransformTrailTool : public FInteractiveTrailTool
{
public:
	FDefaultMovieSceneTransformTrailTool(FMovieSceneTransformTrail* InOwningTrail)
		: OwningTrail(InOwningTrail)
	{}

	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	virtual TArray<UObject*> GetStaticToolProperties() const override { return TArray<UObject*>{KeyProps}; }

	TArray<UObject*> GetKeySceneComponents()
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

	void OnSectionChanged();
	void UpdateKeysInRange(FTrajectoryCache* ParentTrajectoryCache, const TRange<double>& ViewRange);
private:

	void BuildKeys();
	bool ShouldRebuildKeys();
	void ClearSelection();
	void DirtyKeyTransforms();

	enum class EKeyUpdateType
	{
		FromComponent,
		FromComponentDelta,
		FromTrailCache
	};

	// TODO: support world/local transform, must find way to get reference to parent node and call SceneComponent->AttachTo(DummyParentCommponent)
	struct FKeyInfo
	{
		FKeyInfo(const FFrameNumber InFrameNumber, class UMovieScene3DTransformSection* InTrackSection, FMovieSceneTransformTrail* InOwningTrail);

		void OnKeyTransformChanged(class UTransformProxy*, FTransform NewTransform)
		{
			if (DragStartCompTransform)
			{
				UpdateKeyTransform(EKeyUpdateType::FromComponentDelta);
			}
		}

		void OnDragStart(class UTransformProxy*);

		void OnDragEnd(class UTransformProxy*)
		{
			DragStartTransform.Reset();
			DragStartCompTransform = TOptional<UE::MovieScene::FIntermediate3DTransform>();
		}

		// Re-eval transform or use given one
		void UpdateKeyTransform(EKeyUpdateType UpdateType, FTrajectoryCache* ParentTrajectoryCache = nullptr);

		// Key Specific info
		class USceneComponent* SceneComponent;
		class USceneComponent* ParentSceneComponent;
		TMap<EMSTrailTransformChannel, FKeyHandle> IdxMap;
		TOptional<UE::MovieScene::FIntermediate3DTransform> DragStartCompTransform;
		TMap<EMSTrailTransformChannel, float> DragStartTransform;
		FFrameNumber FrameNumber;
		bool bDirty;

		// General curve info
		UMovieScene3DTransformSection* TrackSection;
		class FMovieSceneTransformTrail* OwningTrail;
	};

	void UpdateGizmoActorComponents(FKeyInfo* KeyInfo, UTransformGizmo* TransformGizmo);

	static UMSTrailKeyProperties* KeyProps;

	TMap<FFrameNumber, TUniquePtr<FKeyInfo>> Keys;

	FKeyInfo* CachedSelected;
	TWeakObjectPtr<class UTransformGizmo> ActiveTransformGizmo;

	FMovieSceneTransformTrail* OwningTrail;

	friend class UMSTrailTransformProxy;
};

UCLASS()
class UMSTrailTransformProxy : public UTransformProxy
{
	GENERATED_BODY()
public:
	struct FKeyDelegateHandles
	{
		FDelegateHandle OnTransformChangedHandle;
		FDelegateHandle OnBeginTransformEditSequenceHandle;
		FDelegateHandle OnEndTransformEditSequenceHandle;
	};

	virtual void AddKey(FDefaultMovieSceneTransformTrailTool::FKeyInfo* KeyInfo)
	{
		FKeyDelegateHandles KeyDelegateHandles;
		KeyDelegateHandles.OnTransformChangedHandle = OnTransformChanged.AddRaw(KeyInfo, &FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnKeyTransformChanged);
		KeyDelegateHandles.OnBeginTransformEditSequenceHandle = OnBeginTransformEdit.AddRaw(KeyInfo, &FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragStart);
		KeyDelegateHandles.OnEndTransformEditSequenceHandle = OnEndTransformEdit.AddRaw(KeyInfo, &FDefaultMovieSceneTransformTrailTool::FKeyInfo::OnDragEnd);
		KeysTracked.Add(KeyInfo, KeyDelegateHandles);
		AddComponent(KeyInfo->SceneComponent);
	}

	virtual void RemoveKey(FDefaultMovieSceneTransformTrailTool::FKeyInfo* KeyInfo)
	{
		OnTransformChanged.Remove(KeysTracked[KeyInfo].OnTransformChangedHandle);
		OnBeginTransformEdit.Remove(KeysTracked[KeyInfo].OnBeginTransformEditSequenceHandle);
		OnEndTransformEdit.Remove(KeysTracked[KeyInfo].OnEndTransformEditSequenceHandle);
		KeysTracked.Remove(KeyInfo);
		RemoveComponent(KeyInfo->SceneComponent);
	}

	virtual void RemoveComponent(USceneComponent* Component)
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
	
	const TMap<FDefaultMovieSceneTransformTrailTool::FKeyInfo*, FKeyDelegateHandles>& GetKeysTracked() const { return KeysTracked; }

	bool IsEmpty() const { return KeysTracked.Num() == 0; }

protected:
	TMap<FDefaultMovieSceneTransformTrailTool::FKeyInfo*, FKeyDelegateHandles> KeysTracked;
};

// TODO: make trails per-section, not per track
// TODO: add hierarchy reference
class FMovieSceneTransformTrail : public FTrail, public FGCObject
{
public:
	FMovieSceneTransformTrail(const FLinearColor& InColor, const bool bInIsVisible, TWeakObjectPtr<class UMovieScene3DTransformTrack> InWeakTrack, TSharedPtr<class ISequencer> InSequencer);

	// FTrail interface
	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) override;
	virtual FTrajectoryCache* GetTrajectoryTransforms() override { return TrajectoryCache.Get(); }
	virtual FTrajectoryDrawInfo* GetDrawInfo() override { return DrawInfo.Get(); }
	virtual TMap<FString, FInteractiveTrailTool*> GetTools() override;
	virtual TRange<double> GetEffectiveRange() const override { return CachedEffectiveRange; }
	// End FTrail interface

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	TSharedPtr<ISequencer> GetSequencer() { return WeakSequencer.Pin(); }

	friend class FDefaultMovieSceneTransformTrailTool;

private:
	class UMovieScene3DTransformSection* GetTransformSection() const;
	TRange<double> GetEffectiveTrackRange() const;

	TRange<double> CachedEffectiveRange;

	TUniquePtr<FDefaultMovieSceneTransformTrailTool> DefaultTrailTool;
	TUniquePtr<FCachedTrajectoryDrawInfo> DrawInfo;
	TUniquePtr<FArrayTrajectoryCache> TrajectoryCache;

	FGuid LastTransformTrackSig;
	TWeakObjectPtr<class UMovieScene3DTransformTrack> WeakTrack;
	TWeakPtr<class ISequencer> WeakSequencer;
	class USequencerInterrogationLinker* InterrogationLinker;
};
