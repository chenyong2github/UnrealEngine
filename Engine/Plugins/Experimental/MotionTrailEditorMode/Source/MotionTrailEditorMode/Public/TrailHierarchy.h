// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trail.h"

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

struct FTrailHierarchyNode
{
	FTrailHierarchyNode()
		: Parents()
		, Children()
	{}

	TArray<FGuid> Parents;
	TArray<FGuid> Children;
};

class FTrailHierarchy
{
public:

	FTrailHierarchy(TWeakObjectPtr<class UMotionTrailEditorMode> InWeakEditorMode)
		: ViewRange(TRange<double>::All())
		, RootTrailGuid(FGuid())
		, AllTrails()
		, Hierarchy()
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

	const TRange<double>& GetViewRange() const { return ViewRange; }
	FGuid GetRootTrailGuid() const { return RootTrailGuid; }
	const TMap<FGuid, TUniquePtr<FTrail>>& GetAllTrails() const { return AllTrails; }
	const TMap<FGuid, FTrailHierarchyNode>& GetHierarchy() const { return Hierarchy; }
	const class UMotionTrailEditorMode* GetEditorMode() const { return WeakEditorMode.Get(); }

protected:

	TRange<double> ViewRange;

	FGuid RootTrailGuid;
	TMap<FGuid, TUniquePtr<FTrail>> AllTrails;
	TMap<FGuid, FTrailHierarchyNode> Hierarchy;

	TWeakObjectPtr<class UMotionTrailEditorMode> WeakEditorMode;
};
