// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trail.h"
#include "EditorViewportClient.h"

#include "SceneView.h"
#include "UnrealClient.h"

#include "Misc/Timespan.h"

// TODO: Add FTrailVisibilityManager (or no external class) to keep track of visible trails with members like Selection, Mask and AlwaysVisibleTrails and utility methods like IsTrailVisible
// TODO: Find minimal subtree to update to evaluate visible trails every tick, accumulate parent states for skipped trails with FAccumulatedParentStates structure
// TODO: Better support multiple parents when rendering with Visited

class UMotionTrailEditorMode;

namespace UE
{
namespace MotionTrailEditor
{

struct FTrailHierarchyNode
{
	FTrailHierarchyNode()
		: Parents()
		, Children()
	{}

	TArray<FGuid> Parents;
	TArray<FGuid> Children;
};

class ITrailHierarchyRenderer
{
public:
	virtual ~ITrailHierarchyRenderer() {}
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) = 0;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) = 0;
};

class FTrailHierarchyRenderer : public ITrailHierarchyRenderer
{
public:
	FTrailHierarchyRenderer(class FTrailHierarchy* InOwningHierarchy)
		: OwningHierarchy(InOwningHierarchy)
	{}

	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;

private:
	class FTrailHierarchy* OwningHierarchy;
};

class FTrailHierarchy
{
public:

	FTrailHierarchy(TWeakObjectPtr<UMotionTrailEditorMode> InWeakEditorMode)
		: ViewRange(TRange<double>::All())
		, RootTrailGuid(FGuid())
		, AllTrails()
		, Hierarchy()
		, SelectionMask()
		, TimingStats()
		, WeakEditorMode(InWeakEditorMode)
	{}

	virtual ~FTrailHierarchy() {}

	virtual void Initialize() = 0;
	virtual void Destroy() = 0; // TODO: make dtor?
	virtual ITrailHierarchyRenderer* GetRenderer() const = 0;
	virtual double GetSecondsPerFrame() const = 0;

	// Optionally implemented methods
	virtual void Update();

	virtual void AddTrail(const FGuid& Key, const FTrailHierarchyNode& Node, TUniquePtr<FTrail>&& TrailPtr);
	virtual void RemoveTrail(const FGuid& Key);

	TArray<FGuid> GetAllChildren(const FGuid& TrailGuid);

	const TRange<double>& GetViewRange() const { return ViewRange; }
	FGuid GetRootTrailGuid() const { return RootTrailGuid; }
	const TMap<FGuid, TUniquePtr<FTrail>>& GetAllTrails() const { return AllTrails; }
	const TMap<FGuid, FTrailHierarchyNode>& GetHierarchy() const { return Hierarchy; }
	const UMotionTrailEditorMode* GetEditorMode() const { return WeakEditorMode.Get(); }

	const TMap<FString, FTimespan>& GetTimingStats() const { return TimingStats; };
	TMap<FString, FTimespan>& GetTimingStats() { return TimingStats; }

	const TSet<FGuid>& GetSelectionMask() const { return SelectionMask; }

protected:

	TRange<double> ViewRange;

	FGuid RootTrailGuid;
	TMap<FGuid, TUniquePtr<FTrail>> AllTrails;
	TMap<FGuid, FTrailHierarchyNode> Hierarchy;
	TSet<FGuid> SelectionMask;

	TMap<FString, FTimespan> TimingStats;
	TWeakObjectPtr<UMotionTrailEditorMode> WeakEditorMode;
};

} // namespace MovieScene
} // namespace UE
