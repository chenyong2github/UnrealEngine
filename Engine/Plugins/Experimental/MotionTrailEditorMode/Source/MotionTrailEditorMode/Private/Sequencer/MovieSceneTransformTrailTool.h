// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MotionTrailEditorToolset.h"
#include "MovieSceneTracksComponentTypes.h"

#include "BaseGizmos/TransformProxy.h"

#include "MovieSceneTransformTrailTool.generated.h"

class UTransformProxy;

UCLASS()
class UMSTrailKeyProperties : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = ToolShowOptions)
	float KeySize = 10.0f;
};

namespace UE
{
namespace MotionTrailEditor
{

class FMovieSceneTransformTrail;

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

class FDefaultMovieSceneTransformTrailTool : public FInteractiveTrailTool
{
public:
	FDefaultMovieSceneTransformTrailTool(FMovieSceneTransformTrail* InOwningTrail)
		: OwningTrail(InOwningTrail)
	{}

	// Begin FInteractiveTrailTool interface
	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;
	virtual TArray<UObject*> GetStaticToolProperties() const override { return TArray<UObject*>{KeyProps}; }
	// End FInteractiveTrailTool interface

	TArray<UObject*> GetKeySceneComponents();

	void OnSectionChanged();
	void UpdateKeysInRange(class FTrajectoryCache* ParentTrajectoryCache, const TRange<double>& ViewRange);
private:

	void BuildKeys();
	bool ShouldRebuildKeys();
	void ClearSelection();
	void DirtyKeyTransforms();

	enum class EKeyUpdateType
	{
		FromComponentDelta,
		FromTrailCache
	};

public:
	struct FKeyInfo
	{
		FKeyInfo(const FFrameNumber InFrameNumber, class UMovieSceneSection* InSection, FMovieSceneTransformTrail* InOwningTrail);

		void OnKeyTransformChanged(UTransformProxy*, FTransform);
		void OnDragStart(UTransformProxy*);
		void OnDragEnd(UTransformProxy*);

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
		UMovieSceneSection* Section;
		FMovieSceneTransformTrail* OwningTrail;
	};

private:

	void UpdateGizmoActorComponents(FKeyInfo* KeyInfo, class UTransformGizmo* TransformGizmo);

	static UMSTrailKeyProperties* KeyProps;

	TMap<FFrameNumber, TUniquePtr<FKeyInfo>> Keys;
	FKeyInfo* CachedSelected;
	TWeakObjectPtr<class UTransformGizmo> ActiveTransformGizmo;
	FMovieSceneTransformTrail* OwningTrail;

	friend class ::UMSTrailTransformProxy;
};

} // namespace MovieScene
} // namespace UE

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

	virtual void AddKey(UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo* KeyInfo);
	virtual void RemoveKey(UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo* KeyInfo);
	virtual void RemoveComponent(USceneComponent* Component);

	const TMap<UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo*, FKeyDelegateHandles>& GetKeysTracked() const { return KeysTracked; }

	bool IsEmpty() const { return KeysTracked.Num() == 0; }

protected:
	TMap<UE::MotionTrailEditor::FDefaultMovieSceneTransformTrailTool::FKeyInfo*, FKeyDelegateHandles> KeysTracked;
};
