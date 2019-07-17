// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateRenderBatch.h"

struct FSlateGradientStop
{
	FVector2D Position;
	FLinearColor Color;

	/**
	 * Construct a Gradient Stop from a Position and a Color.
	 * @param InPosition - The position in widget space for this stop. Both X and Y are used for a single-axis gradient.
						  A two stop gradient should go from (0,0), to (Width,Height).
	 * @param InColor	- The color to lerp towards at this stop.
	 */
	FSlateGradientStop(const FVector2D& InPosition, const FLinearColor& InColor)
		: Position(InPosition)
		, Color(InColor)
	{

	}
};
template <> struct TIsPODType<FSlateGradientStop> { enum { Value = true }; };


struct FSlateDataPayload
{
	virtual ~FSlateDataPayload() {}
	virtual void AddReferencedObjects(FReferenceCollector& Collector) {}
};

struct FSlateTintableElement
{
	FLinearColor Tint;

	FORCEINLINE void SetTint(const FLinearColor& InTint) { Tint = InTint; }
	FORCEINLINE FLinearColor GetTint() const { return Tint; }
};

struct FSlateBoxPayload : public FSlateDataPayload, public FSlateTintableElement
{
	FMargin Margin;
	FBox2D UVRegion;
	const FSlateShaderResourceProxy* ResourceProxy;
	ESlateBrushTileType::Type Tiling;
	ESlateBrushMirrorType::Type Mirroring;
	ESlateBrushDrawType::Type DrawType;

	const FMargin& GetBrushMargin() const { return Margin; }
	const FBox2D& GetBrushUVRegion() const { return UVRegion; }
	ESlateBrushTileType::Type GetBrushTiling() const { return Tiling; }
	ESlateBrushMirrorType::Type GetBrushMirroring() const { return Mirroring; }
	ESlateBrushDrawType::Type GetBrushDrawType() const { return DrawType; }
	const FSlateShaderResourceProxy* GetResourceProxy() const { return ResourceProxy; }

	void SetBrush(const FSlateBrush* InBrush)
	{
		check(InBrush);
		ensureMsgf(InBrush->GetDrawType() != ESlateBrushDrawType::NoDrawType, TEXT("This should have been filtered out earlier in the Make... call."));

		// Note: Do not store the brush.  It is possible brushes are destroyed after an element is enqueued for rendering
		Margin = InBrush->GetMargin();
		UVRegion = InBrush->GetUVRegion();
		Tiling = InBrush->GetTiling();
		Mirroring = InBrush->GetMirroring();
		DrawType = InBrush->GetDrawType();
		FSlateResourceHandle Handle = InBrush->GetRenderingResource();
		if (Handle.IsValid())
		{
			ResourceProxy = Handle.GetResourceProxy();
		}
		else
		{
			ResourceProxy = nullptr;
		}
	}

	FORCENOINLINE virtual ~FSlateBoxPayload()
	{
	}
};

struct FSlateTextPayload : public FSlateDataPayload, public FSlateTintableElement
{
	// The font to use when rendering
	FSlateFontInfo FontInfo;
	// Basic text data
	FString ImmutableText;

	const FSlateFontInfo& GetFontInfo() const { return FontInfo; }
	const TCHAR* GetText() const { return *ImmutableText; }
	int32 GetTextLength() const { return ImmutableText.Len(); }

	void SetText(const FString& InText, const FSlateFontInfo& InFontInfo, int32 InStartIndex, int32 InEndIndex)
	{
		FontInfo = InFontInfo;
		const int32 StartIndex = FMath::Min<int32>(InStartIndex, InText.Len());
		const int32 EndIndex = FMath::Min<int32>(InEndIndex, InText.Len());
		const int32 TextLength = (EndIndex > StartIndex) ? EndIndex - StartIndex : 0;
		if (TextLength > 0)
		{
			ImmutableText = InText.Mid(StartIndex, TextLength);
		}
	}

	void SetText(const FString& InText, const FSlateFontInfo& InFontInfo)
	{
		FontInfo = InFontInfo;
		ImmutableText = InText;
	}


	virtual void AddReferencedObjects(FReferenceCollector& Collector)
	{
		FontInfo.AddReferencedObjects(Collector);
	}
};


struct FSlateShapedTextPayload : public FSlateDataPayload, public FSlateTintableElement
{
	// Shaped text data
	FShapedGlyphSequencePtr ShapedGlyphSequence;

	FLinearColor OutlineTint;

	const FShapedGlyphSequencePtr& GetShapedGlyphSequence() const { return ShapedGlyphSequence; }
	FLinearColor GetOutlineTint() const { return OutlineTint; }

	void SetShapedText(FSlateWindowElementList& ElementList, const FShapedGlyphSequencePtr& InShapedGlyphSequence, const FLinearColor& InOutlineTint)
	{
		ShapedGlyphSequence = InShapedGlyphSequence;
		OutlineTint = InOutlineTint;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector)
	{
		if (ShapedGlyphSequence.IsValid())
		{
			const_cast<FShapedGlyphSequence*>(ShapedGlyphSequence.Get())->AddReferencedObjects(Collector);
		}
	}
};

struct FSlateGradientPayload : public FSlateDataPayload
{
	TArray<FSlateGradientStop> GradientStops;
	EOrientation GradientType;

	void SetGradient(const TArray<FSlateGradientStop>& InGradientStops, EOrientation InGradientType)
	{
		GradientStops = InGradientStops;
		GradientType = InGradientType;
	}
};

struct FSlateSplinePayload : public FSlateDataPayload, public FSlateTintableElement
{
	TArray<FSlateGradientStop> GradientStops;
	// Bezier Spline Data points. E.g.
	//
	//       P1 + - - - - + P2                P1 +
	//         /           \                    / \
	//     P0 *             * P3            P0 *   \   * P3
	//                                              \ /
	//                                               + P2	
	FVector2D P0;
	FVector2D P1;
	FVector2D P2;
	FVector2D P3;

	float Thickness;

	// Thickness
	void SetThickness(float InThickness) { Thickness = InThickness; }
	float GetThickness() const { return Thickness; }

	void SetCubicBezier(const FVector2D& InP0, const FVector2D& InP1, const FVector2D& InP2, const FVector2D& InP3, float InThickness, const FLinearColor& InTint)
	{
		Tint = InTint;
		P0 = InP0;
		P1 = InP1;
		P2 = InP2;
		P3 = InP3;
		Thickness = InThickness;
	}

	void SetHermiteSpline(const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness, const FLinearColor& InTint)
	{
		Tint = InTint;
		P0 = InStart;
		P1 = InStart + InStartDir / 3.0f;
		P2 = InEnd - InEndDir / 3.0f;
		P3 = InEnd;
		Thickness = InThickness;
	}

	void SetGradientHermiteSpline(const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness, const TArray<FSlateGradientStop>& InGradientStops)
	{
		P0 = InStart;
		P1 = InStart + InStartDir / 3.0f;
		P2 = InEnd - InEndDir / 3.0f;
		P3 = InEnd;
		Thickness = InThickness;
		GradientStops = InGradientStops;
	}
};


struct FSlateLinePayload : public FSlateDataPayload, public FSlateTintableElement
{ 
	TArray<FVector2D> Points;
	TArray<FLinearColor> PointColors;
	float Thickness;

	bool bAntialias;

	bool IsAntialiased() const { return bAntialias; }
	const TArray<FVector2D>& GetPoints() const { return Points; }
	const TArray<FLinearColor>& GetPointColors() const { return PointColors; }
	float GetThickness() const { return Thickness; }

	void SetThickness(float InThickness)
	{
		Thickness = InThickness;
	}

	void SetLines(const TArray<FVector2D>& InPoints, bool bInAntialias, const TArray<FLinearColor>* InPointColors = nullptr)
	{
		bAntialias = bInAntialias;
		Points = InPoints;
		if (InPointColors)
		{
			PointColors = *InPointColors;
		}
	}
};

struct FSlateViewportPayload : public FSlateDataPayload, public FSlateTintableElement
{
	FSlateShaderResource* RenderTargetResource;
	uint8 bAllowViewportScaling : 1;
	uint8 bViewportTextureAlphaOnly : 1;
	uint8 bRequiresVSync : 1;

	void SetViewport(const TSharedPtr<const ISlateViewport>& InViewport, const FLinearColor& InTint)
	{
		Tint = InTint;
		RenderTargetResource = InViewport->GetViewportRenderTargetTexture();
		bAllowViewportScaling = InViewport->AllowScaling();
		bViewportTextureAlphaOnly = InViewport->IsViewportTextureAlphaOnly();
		bRequiresVSync = InViewport->RequiresVsync();
	}
};

struct FSlateCustomDrawerPayload : public FSlateDataPayload
{
	// Custom drawer data
	TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer;

	void SetCustomDrawer(const TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe>& InCustomDrawer)
	{
		CustomDrawer = InCustomDrawer;
	}
};

struct FSlateLayerPayload : public FSlateDataPayload
{
	class FSlateDrawLayerHandle* LayerHandle;

	void SetLayer(FSlateDrawLayerHandle* InLayerHandle)
	{
		LayerHandle = InLayerHandle;
		checkSlow(LayerHandle);
	}

};

struct FSlateCachedBufferPayload : public FSlateDataPayload
{
	// Cached render data
	class FSlateRenderDataHandle* CachedRenderData;
	FVector2D CachedRenderDataOffset;

	// Cached Buffers
	void SetCachedBuffer(FSlateRenderDataHandle* InRenderDataHandle, const FVector2D& Offset)
	{
		check(InRenderDataHandle);

		CachedRenderData = InRenderDataHandle;
		CachedRenderDataOffset = Offset;
	}
};

struct FSlateCustomVertsPayload : public FSlateDataPayload
{
	const FSlateShaderResourceProxy* ResourceProxy;

	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;

	// Instancing support
	ISlateUpdatableInstanceBuffer* InstanceData;
	uint32 InstanceOffset;
	uint32 NumInstances;

	void SetCustomVerts(const FSlateShaderResourceProxy* InRenderProxy, const TArray<FSlateVertex>& InVerts, const TArray<SlateIndex>& InIndices, ISlateUpdatableInstanceBuffer* InInstanceData, uint32 InInstanceOffset, uint32 InNumInstances)
	{
		ResourceProxy = InRenderProxy;

		Vertices = InVerts;
		Indices = InIndices;

		InstanceData = InInstanceData;
		InstanceOffset = InInstanceOffset;
		NumInstances = InNumInstances;
	}
};

struct FSlatePostProcessPayload : public FSlateDataPayload
{
	// Post Process Data
	FVector4 PostProcessData;
	int32 DownsampleAmount;
};