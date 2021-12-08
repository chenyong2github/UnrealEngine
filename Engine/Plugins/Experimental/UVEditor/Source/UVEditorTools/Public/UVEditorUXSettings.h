// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UVEDITORTOOLS_API FUVEditorUXSettings
{
public:
	// The following are UV Editor specific style items that don't, strictly, matter to the SlateStyleSet,
	// but seemed appropriate to place here.

	// General Display Properties

	// Position to place the 2D camera far plane relative to world z
	static const float CameraFarPlaneWorldZ;;

	// The near plane gets positioned some proportion to z = 0. We don't use a constant value because our depth offset values are percentage-based
	// Lower proportion move the plane nearer to world z
	// Note: This serves as an upper bound for all other depth offsets - higher than this value risks being clipped
	static const float CameraNearPlaneProportionZ;;


	// 2D Unwrap Display Properties
	static const float UnwrapTriangleOpacity;
	static const float UnwrapTriangleDepthOffset;
	static const float UnwrapTriangleOpacityWithBackground;

	static const float WireframeDepthOffset;
	static const float UnwrapBoundaryHueShift;
	static const float UnwrapBoundarySaturation;
	static const float UnwrapBoundaryValue;
	static const FColor UnwrapTriangleFillColor;
	static const FColor UnwrapTriangleWireframeColor;

	static FLinearColor GetTriangleColorByTargetIndex(int32 TargetIndex);
	static FLinearColor GetWireframeColorByTargetIndex(int32 TargetIndex);
	static FLinearColor GetBoundaryColorByTargetIndex(int32 TargetIndex);

	// Selection Highlighting Properties
	static const float SelectionTriangleOpacity;
	static const FColor SelectionTriangleFillColor;
	static const FColor SelectionTriangleWireframeColor;

	static const float SelectionHoverTriangleOpacity;
	static const FColor SelectionHoverTriangleFillColor;
	static const FColor SelectionHoverTriangleWireframeColor;

	static const float LivePreviewHighlightThickness;
	static const float LivePreviewHighlightPointSize;
	static const float LivePreviewHighlightDepthOffset;

	static const float SelectionLineThickness;
	static const float SelectionPointThickness;
	static const float SelectionWireframeDepthBias;
	static const float SelectionTriangleDepthBias;
	static const float SelectionHoverWireframeDepthBias;
	static const float SelectionHoverTriangleDepthBias;

	static const FColor LivePreviewExistingSeamColor;
	static const float LivePreviewExistingSeamThickness;
	static const float LivePreviewExistingSeamDepthBias;

	// These are currently used by the seam tool but can be generally used by
	// tools for displaying paths.
	static const FColor ToolLockedPathColor;
	static const float ToolLockedPathThickness;
	static const float ToolLockedPathDepthBias;
	static const FColor ToolExtendPathColor;
	static const float ToolExtendPathThickness;
	static const float ToolExtendPathDepthBias;
	static const FColor ToolCompletionPathColor;

	static const float ToolPointSize;

	// Sew Action styling
	static const float SewLineHighlightThickness;
	static const float SewLineDepthOffset;
	static const FColor SewSideLeftColor;
	static const FColor SewSideRightColor;

	// Grid 
	static const float AxisThickness;
	static const float GridMajorThickness;
	static const FColor XAxisColor;
	static const FColor YAxisColor;
	static const FColor GridMajorColor;
	static const FColor GridMinorColor;
	static const int32 GridSubdivisionsPerLevel;
	static const int32 GridLevels;


	// Background
	static const float BackgroundQuadDepthOffset;

private:

	FUVEditorUXSettings();
	~FUVEditorUXSettings();
};