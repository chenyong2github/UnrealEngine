// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "HitProxies.h"
#include "ComponentVisualizer.h"
#include "Components/SplineComponent.h"

class AActor;
class FEditorViewportClient;
class FMenuBuilder;
class FPrimitiveDrawInterface;
class FSceneView;
class FUICommandList;
class FViewport;
class SWidget;
class USplineComponent;
struct FViewportClick;
struct FConvexVolume;

/** Base class for clickable spline editing proxies */
struct HSplineVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HSplineVisProxy(const UActorComponent* InComponent)
	: HComponentVisProxy(InComponent, HPP_Wireframe)
	{}
};

/** Proxy for a spline key */
struct HSplineKeyProxy : public HSplineVisProxy
{
	DECLARE_HIT_PROXY();

	HSplineKeyProxy(const UActorComponent* InComponent, int32 InKeyIndex) 
		: HSplineVisProxy(InComponent)
		, KeyIndex(InKeyIndex)
	{}

	int32 KeyIndex;
};

/** Proxy for a spline segment */
struct HSplineSegmentProxy : public HSplineVisProxy
{
	DECLARE_HIT_PROXY();

	HSplineSegmentProxy(const UActorComponent* InComponent, int32 InSegmentIndex)
		: HSplineVisProxy(InComponent)
		, SegmentIndex(InSegmentIndex)
	{}

	int32 SegmentIndex;
};

/** Proxy for a tangent handle */
struct HSplineTangentHandleProxy : public HSplineVisProxy
{
	DECLARE_HIT_PROXY();

	HSplineTangentHandleProxy(const UActorComponent* InComponent, int32 InKeyIndex, bool bInArriveTangent)
		: HSplineVisProxy(InComponent)
		, KeyIndex(InKeyIndex)
		, bArriveTangent(bInArriveTangent)
	{}

	int32 KeyIndex;
	bool bArriveTangent;
};

/** SplineComponent visualizer/edit functionality */
class COMPONENTVISUALIZERS_API FSplineComponentVisualizer : public FComponentVisualizer
{
public:
	FSplineComponentVisualizer();
	virtual ~FSplineComponentVisualizer();

	//~ Begin FComponentVisualizer Interface
	virtual void OnRegister() override;
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual void EndEditing() override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual bool HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	/** Handle click modified by Alt, Ctrl and/or Shift. The input HitProxy may not be on this component. */
	virtual bool HandleModifiedClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	/** Handle box select input */
	virtual bool HandleBoxSelect(const FBox& InBox, FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	/** Handle frustum select input */
	virtual bool HandleFrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	/** Return whether focus on selection should focus on bounding box defined by active visualizer */
	virtual bool HasFocusOnSelectionBoundingBox(FBox& OutBoundingBox) override;
	/** Pass snap input to active visualizer */
	virtual bool HandleSnapTo(const bool bInAlign, const bool bInUseLineTrace, const bool bInUseBounds, const bool bInUsePivot, AActor* InDestination) override;
	virtual TSharedPtr<SWidget> GenerateContextMenu() const override;
	virtual bool IsVisualizingArchetype() const override;
	//~ End FComponentVisualizer Interface

	/** Get the spline component we are currently editing */
	USplineComponent* GetEditedSplineComponent() const;

	const TSet<int32>& GetSelectedKeys() const { return SelectedKeys; }

protected:

	/** Transforms selected tangent by given translation */
	bool TransformSelectedTangent(const FVector& DeltaTranslate);

	/** Transforms selected tangent by given translate, rotate and scale */
	bool TransformSelectedKeys(const FVector& DeltaTranslate, const FRotator& DeltaRotate = FRotator::ZeroRotator, const FVector& DeltaScale = FVector::ZeroVector, bool bDuplicateKey = false);

	/** Snaps edited point and rot point to given world location, up vector, and tangent */
	void SnapTo(USplineComponent *SplineComp, int32 KeyIdx,
		const FVector& InLocation, const FVector& InUpVector = FVector::ZeroVector, bool bInAlignUpVector = false, const FVector& InTangentVector = FVector::ZeroVector, bool bInAlignTangentVector = false);

	/** Update the key selection state of the visualizer */
	void ChangeSelectionState(int32 Index, bool bIsCtrlHeld);

	/** Duplicates the selected spline key(s) */
	void DuplicateKey();

	void OnDeleteKey();
	bool CanDeleteKey() const;

	void OnDuplicateKey();
	bool IsKeySelectionValid() const;

	void OnAddKey();
	bool CanAddKey() const;

	void OnSnapToNearestSplinePoint(bool bAlign);
	bool CanSnapToNearestSplinePoint() const;

	void OnSnapAll(EAxis::Type InAxis);
	bool CanSnapAll() const;

	void OnLockAxis(EAxis::Type InAxis);
	bool IsLockAxisSet(EAxis::Type InAxis) const; 
	
	void OnResetToAutomaticTangent(EInterpCurveMode Mode);
	bool CanResetToAutomaticTangent(EInterpCurveMode Mode) const;

	void OnSetKeyType(EInterpCurveMode Mode);
	bool IsKeyTypeSet(EInterpCurveMode Mode) const;

	void OnSetVisualizeRollAndScale();
	bool IsVisualizingRollAndScale() const;

	void OnSetDiscontinuousSpline();
	bool IsDiscontinuousSpline() const;

	void OnResetToDefault();
	bool CanResetToDefault() const;

	/** Generate the submenu containing the available point types */
	void GenerateSplinePointTypeSubMenu(FMenuBuilder& MenuBuilder) const;

	/** Generate the submenu containing the available auto tangent types */
	void GenerateTangentTypeSubMenu(FMenuBuilder& MenuBuilder) const;

	/** Generate the submenu containing the available snap/align actions */
	void GenerateSnapAlignSubMenu(FMenuBuilder& MenuBuilder) const;
	
	/** Generate the submenu containing the lock axis types */
	void GenerateLockAxisSubMenu(FMenuBuilder& MenuBuilder) const;

	/** Output log commands */
	TSharedPtr<FUICommandList> SplineComponentVisualizerActions;

	/** Actor that owns the currently edited spline */
	TWeakObjectPtr<AActor> SplineOwningActor;

	/** Name of property on the actor that references the spline we are editing */
	FPropertyNameAndIndex SplineCompPropName;

	/** Index of keys we have selected */
	TSet<int32> SelectedKeys;

	/** Index of the last key we selected */
	int32 LastKeyIndexSelected;

	/** Index of segment we have selected */
	int32 SelectedSegmentIndex;

	/** Index of tangent handle we have selected */
	int32 SelectedTangentHandle;

	struct ESelectedTangentHandle
	{
		enum Type
		{
			None,
			Leave,
			Arrive
		};
	};

	/** The type of the selected tangent handle */
	ESelectedTangentHandle::Type SelectedTangentHandleType;

	/** Position on spline we have selected */
	FVector SelectedSplinePosition;

	/** Cached rotation for this point */
	FQuat CachedRotation;

	/** Whether we currently allow duplication when dragging */
	bool bAllowDuplication;

	/** Axis to fix when adding new spline points. Uses the value of the currently 
	    selected spline point's X, Y, or Z value when fix is not equal to none. */
	EAxis::Type AddKeyLockedAxis;

private:
	UProperty* SplineCurvesProperty;
};
