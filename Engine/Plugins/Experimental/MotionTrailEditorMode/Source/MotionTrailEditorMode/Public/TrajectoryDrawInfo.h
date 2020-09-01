// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Math/Color.h"
#include "Math/Range.h"
#include "Math/Vector.h"
#include "Misc/Optional.h"

#include "SceneView.h"
#include "UnrealClient.h"

namespace UE
{
namespace MotionTrailEditor
{

class FTrail;  
struct FTrailHierarchyNode;
class FMapTrajectoryCache;
class FTrajectoryCache;

class FTrailScreenSpaceTransform
{
public:
	FTrailScreenSpaceTransform(const FSceneView* InView, FViewport* InViewport, const float InDPIScale = 1.0f)
		: View(InView)
		, HalfScreenSize((InViewport->GetSizeXY().X / InDPIScale) * 0.5, (InViewport->GetSizeXY().Y / InDPIScale) * 0.5)
	{}

	TOptional<FVector2D> ProjectPoint(const FVector& Point) const;

private:
	const FSceneView* View;
	FVector2D HalfScreenSize;
};

class FTrajectoryDrawInfo
{
public:

	FTrajectoryDrawInfo(const FLinearColor& InColor, const bool bInIsVisible)
		: Color(InColor)
		, bIsVisible(bInIsVisible)
		, CachedViewRange(TRange<double>::Empty())
	{}

	virtual ~FTrajectoryDrawInfo() {}

	struct FDisplayContext
	{
		FGuid YourNode;
		const FTrailScreenSpaceTransform& ScreenSpaceTransform;
		double SecondsPerTick;
		const TRange<double>& TimeRange;
		class FTrailHierarchy* TrailHierarchy;
	};

	virtual TArray<FVector> GetTrajectoryPointsForDisplay(const FDisplayContext& InDisplayContext) = 0;
	virtual void GetTickPointsForDisplay(const FDisplayContext& InDisplayContext, TArray<FVector2D>& Ticks, TArray<FVector2D>& TickTangents) = 0;

	// Optionally implemented methods
	void SetColor(const FLinearColor& InColor) { Color = InColor; }
	FLinearColor GetColor() const { return Color; }

	void SetIsVisible(bool bInIsVisible) { bIsVisible = bInIsVisible; }
	bool IsVisible() const { return bIsVisible; }

	virtual const TRange<double>& GetCachedViewRange() const { return CachedViewRange; }
protected:
	FLinearColor Color;
	bool bIsVisible;
	TRange<double> CachedViewRange;
};

class FCachedTrajectoryDrawInfo : public FTrajectoryDrawInfo
{
public:

	FCachedTrajectoryDrawInfo(const FLinearColor& InColor, const bool bInIsVisible, FTrajectoryCache* InTrajectoryCache)
		: FTrajectoryDrawInfo(InColor, bInIsVisible)
		, TrajectoryCache(InTrajectoryCache)
	{}

	virtual TArray<FVector> GetTrajectoryPointsForDisplay(const FDisplayContext& InDisplayContext) override;
	virtual void GetTickPointsForDisplay(const FDisplayContext& InDisplayContext, TArray<FVector2D>& Ticks, TArray<FVector2D>& TickNormals) override;

private:
	FTrajectoryCache* TrajectoryCache;
};

} // namespace MovieScene
} // namespace UE
