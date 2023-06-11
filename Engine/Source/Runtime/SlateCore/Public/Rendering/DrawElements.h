// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Fonts/ShapedTextFwd.h"
#include "Stats/Stats.h"
#include "Misc/MemStack.h"
#include "Styling/WidgetStyle.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/SlateRect.h"
#include "Layout/Clipping.h"
#include "Types/PaintArgs.h"
#include "Types/SlateVector2.h"
#include "Layout/Geometry.h"
#include "Rendering/RenderingCommon.h"
#include "Debugging/SlateDebugging.h"
#include "Rendering/SlateRenderBatch.h"
#include "DrawElementTextOverflowArgs.h"
#include "ElementBatcher.h"
#include "Widgets/WidgetPixelSnapping.h"
#include "Types/SlateVector2.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Fonts/FontCache.h"
#include "Rendering/ShaderResourceManager.h"
#include "DrawElementPayloads.h"
#endif

class FSlateRenderBatch;
class FSlateDrawLayerHandle;
class FSlateResourceHandle;
class FSlateWindowElementList;
class SWidget;
class SWindow;
struct FSlateBrush;
struct FSlateDataPayload;
struct FSlateGradientStop;



/**
 * FSlateDrawElement is the building block for Slate's rendering interface.
 * Slate describes its visual output as an ordered list of FSlateDrawElement s
 */
class FSlateDrawElement
{
	friend class FSlateWindowElementList;
public:
	
	enum ERotationSpace
	{
		/** Relative to the element.  (0,0) is the upper left corner of the element */
		RelativeToElement,
		/** Relative to the alloted paint geometry.  (0,0) is the upper left corner of the paint geometry */
		RelativeToWorld,
	};

	/**
	 * Creates a wireframe quad for debug purposes
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer				The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 */
	SLATECORE_API static void MakeDebugQuad( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, FLinearColor Tint = FLinearColor::White);

	/**
	 * Creates a box element based on the following diagram.  Allows for this element to be resized while maintain the border of the image
	 * If there are no margins the resulting box is simply a quad
	 *     ___LeftMargin    ___RightMargin
	 *    /                /
	 *  +--+-------------+--+
	 *  |  |c1           |c2| ___TopMargin
	 *  +--o-------------o--+
	 *  |  |             |  |
	 *  |  |c3           |c4|
	 *  +--o-------------o--+
	 *  |  |             |  | ___BottomMargin
	 *  +--+-------------+--+
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InBrush               Brush to apply to this element
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeBox( 
		FSlateWindowElementList& ElementList,
		uint32 InLayer,
		const FPaintGeometry& PaintGeometry,
		const FSlateBrush* InBrush,
		ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None,
		const FLinearColor& InTint = FLinearColor::White );

	SLATECORE_API static void MakeRotatedBox(
		FSlateWindowElementList& ElementList,
		uint32 InLayer, 
		const FPaintGeometry& PaintGeometry, 
		const FSlateBrush* InBrush, 
		ESlateDrawEffect,
		float Angle,
		UE::Slate::FDeprecateOptionalVector2DParameter InRotationPoint = TOptional<FVector2f>(),
		ERotationSpace RotationSpace = RelativeToElement,
		const FLinearColor& InTint = FLinearColor::White );

	/**
	 * Creates a text element which displays a string of a rendered in a certain font on the screen
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InText                The string to draw
	 * @param StartIndex            Inclusive index to start rendering from on the specified text
	 * @param EndIndex				Exclusive index to stop rendering on the specified text
	 * @param InFontInfo            The font to draw the string with
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const int32 StartIndex, const int32 EndIndex, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White );
	
	SLATECORE_API static void MakeText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White );

	FORCEINLINE static void MakeText(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FText& InText, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White)
	{
		MakeText(ElementList, InLayer, PaintGeometry, InText.ToString(), InFontInfo, InDrawEffects, InTint);
	}

	/**
	 * Creates a text element which displays a series of shaped glyphs on the screen
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InShapedGlyphSequence The shaped glyph sequence to draw
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeShapedText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FShapedGlyphSequenceRef& InShapedGlyphSequence, ESlateDrawEffect InDrawEffects, const FLinearColor& BaseTint, const FLinearColor& OutlineTint, FTextOverflowArgs TextOverflowArgs = FTextOverflowArgs());

	/**
	 * Creates a gradient element
	 *
	 * @param ElementList			   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param InGradientStops          List of gradient stops which define the element
	 * @param InGradientType           The type of gradient (I.E Horizontal, vertical)
	 * @param InDrawEffects            Optional draw effects to apply
	 * @param CornerRadius			   Rounds the corners of the box created by the gradient by the specified radius
	 */
	SLATECORE_API static void MakeGradient( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TArray<FSlateGradientStop> InGradientStops, EOrientation InGradientType, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, FVector4f CornerRadius = FVector4f(0.0f) );

	/**
	 * Creates a Hermite Spline element
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InStart               The start point of the spline (local space)
	 * @param InStartDir            The direction of the spline from the start point
	 * @param InEnd                 The end point of the spline (local space)
	 * @param InEndDir              The direction of the spline to the end point
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const UE::Slate::FDeprecateVector2DParameter InStart, const UE::Slate::FDeprecateVector2DParameter InStartDir, const UE::Slate::FDeprecateVector2DParameter InEnd, const UE::Slate::FDeprecateVector2DParameter InEndDir, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White);

	/**
	 * Creates a Bezier Spline element
	 *
	 * @param ElementList			The list in which to add elements
	 * @param InLayer               The layer to draw the element on
	 * @param PaintGeometry         DrawSpace position and dimensions; see FPaintGeometry
	 * @param InStart               The start point of the spline (local space)
	 * @param InStartDir            The direction of the spline from the start point
	 * @param InEnd                 The end point of the spline (local space)
	 * @param InEndDir              The direction of the spline to the end point
	 * @param InDrawEffects         Optional draw effects to apply
	 * @param InTint                Color to tint the element
	 */
	SLATECORE_API static void MakeCubicBezierSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const UE::Slate::FDeprecateVector2DParameter P0, const UE::Slate::FDeprecateVector2DParameter P1, const UE::Slate::FDeprecateVector2DParameter P2, const UE::Slate::FDeprecateVector2DParameter P3, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White);

	/** Just like MakeSpline but in draw-space coordinates. This is useful for connecting already-transformed widgets together. */
	SLATECORE_API static void MakeDrawSpaceSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const UE::Slate::FDeprecateVector2DParameter InStart, const UE::Slate::FDeprecateVector2DParameter InStartDir, const UE::Slate::FDeprecateVector2DParameter InEnd, const UE::Slate::FDeprecateVector2DParameter InEndDir, float InThickness = 0.0f, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White);

	/**
	 * Creates a line defined by the provided points
	 *
	 * @param ElementList              The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param Points                   Points that make up the lines.  The points are joined together. I.E if Points has A,B,C there the line is A-B-C.  To draw non-joining line segments call MakeLines multiple times
	 * @param InDrawEffects            Optional draw effects to apply
	 * @param InTint                   Color to tint the element
	 * @param bAntialias               Should antialiasing be applied to the line?
	 * @param Thickness                The thickness of the line
	 */
#if UE_ENABLE_SLATE_VECTOR_DEPRECATION_MECHANISMS
	SLATECORE_API static void MakeLines(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2d>& Points, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f);
#endif
	SLATECORE_API static void MakeLines(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TArray<FVector2f> Points, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint = FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f);


	/**
	 * Creates a line defined by the provided points
	 *
	 * @param ElementList              The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param Points                   Points that make up the lines.  The points are joined together. I.E if Points has A,B,C there the line is A-B-C.  To draw non-joining line segments call MakeLines multiple times
	 * @param PointColors              Vertex Color for each defined points
	 * @param InDrawEffects            Optional draw effects to apply
	 * @param InTint                   Color to tint the element
	 * @param bAntialias               Should antialiasing be applied to the line?
	 * @param Thickness                The thickness of the line
	 */
#if UE_ENABLE_SLATE_VECTOR_DEPRECATION_MECHANISMS
	SLATECORE_API static void MakeLines(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2d>& Points, const TArray<FLinearColor>& PointColors, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f);
#endif
	SLATECORE_API static void MakeLines(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TArray<FVector2f> Points, TArray<FLinearColor> PointColors, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White, bool bAntialias = true, float Thickness = 1.0f);

	/**
	 * Creates a viewport element which is useful for rendering custom data in a texture into Slate
	 *
	 * @param ElementList		   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param Viewport                 Interface for drawing the viewport
	 * @param InScale                  Draw scale to apply to the entire element
	 * @param InDrawEffects            Optional draw effects to apply
	 * @param InTint                   Color to tint the element
	 */
	SLATECORE_API static void MakeViewport( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TSharedPtr<const ISlateViewport> Viewport, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None, const FLinearColor& InTint=FLinearColor::White );

	/**
	 * Creates a custom element which can be used to manually draw into the Slate render target with graphics API calls rather than Slate elements
	 *
	 * @param ElementList		   The list in which to add elements
	 * @param InLayer                  The layer to draw the element on
	 * @param PaintGeometry            DrawSpace position and dimensions; see FPaintGeometry
	 * @param CustomDrawer		   Interface to a drawer which will be called when Slate renders this element
	 */
	SLATECORE_API static void MakeCustom( FSlateWindowElementList& ElementList, uint32 InLayer, TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer );
	
	SLATECORE_API static void MakeCustomVerts(FSlateWindowElementList& ElementList, uint32 InLayer, const FSlateResourceHandle& InRenderResourceHandle, const TArray<FSlateVertex>& InVerts, const TArray<SlateIndex>& InIndexes, ISlateUpdatableInstanceBuffer* InInstanceData, uint32 InInstanceOffset, uint32 InNumInstances, ESlateDrawEffect InDrawEffects = ESlateDrawEffect::None);

	SLATECORE_API static void MakePostProcessPass(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FVector4f& Params, int32 DownsampleAmount, const FVector4f CornerRadius = FVector4f(0.0f));

	FSlateDrawElement();
	SLATECORE_API ~FSlateDrawElement();

	FORCEINLINE int32 GetLayer() const { return LayerId; }

	FORCEINLINE EElementType GetElementType() const { return ElementType; }

	template<typename PayloadType>
	FORCEINLINE const PayloadType& GetDataPayload() const { return *(PayloadType*)DataPayload; }

	template<typename PayloadType>
	FORCEINLINE PayloadType& GetDataPayload() { return *(PayloadType*)DataPayload; }

	FORCEINLINE const FSlateRenderTransform& GetRenderTransform() const { return RenderTransform; }
	FORCEINLINE void SetRenderTransform(const FSlateRenderTransform& InRenderTransform) { RenderTransform = InRenderTransform; }

	FORCEINLINE UE::Slate::FDeprecateVector2DResult GetPosition() const
	{
		return UE::Slate::FDeprecateVector2DResult(Position);
	}

	FORCEINLINE void SetPosition(UE::Slate::FDeprecateVector2DParameter InPosition)
	{
		Position = InPosition;
	}

	FORCEINLINE UE::Slate::FDeprecateVector2DResult GetLocalSize() const
	{
		return UE::Slate::FDeprecateVector2DResult(LocalSize);
	}

	FORCEINLINE float GetScale() const { return Scale; }
	FORCEINLINE ESlateDrawEffect GetDrawEffects() const { return DrawEffects; }
	FORCEINLINE ESlateBatchDrawFlag GetBatchFlags() const { return BatchFlags; }
	FORCEINLINE bool IsPixelSnapped() const { return !EnumHasAllFlags(DrawEffects, ESlateDrawEffect::NoPixelSnapping); }

	FORCEINLINE int32 GetPrecachedClippingIndex() const { return ClipStateHandle.GetPrecachedClipIndex(); }
	FORCEINLINE void SetPrecachedClippingIndex(int32 InClippingIndex) { ClipStateHandle.SetPreCachedClipIndex(InClippingIndex); }

	FORCEINLINE void SetCachedClippingState(const FSlateClippingState* CachedState) { ClipStateHandle.SetCachedClipState(CachedState); }
	FORCEINLINE const FClipStateHandle& GetClippingHandle() const { return ClipStateHandle; }
	FORCEINLINE const int8 GetSceneIndex() const { return SceneIndex; }

	FORCEINLINE void SetIsCached(bool bInIsCached) { bIsCached = bInIsCached; }
	FORCEINLINE bool IsCached() const { return bIsCached; }

	FORCEINLINE FSlateLayoutTransform GetInverseLayoutTransform() const
	{
		return Inverse(FSlateLayoutTransform(Scale, Position));
	}

	void AddReferencedObjects(FReferenceCollector& Collector);

	/**
	 * Update element cached position with an arbitrary offset
	 *
	 * @param Element		   Element to update
	 * @param InOffset         Absolute translation delta
	 */
	void ApplyPositionOffset(UE::Slate::FDeprecateVector2DParameter InOffset);

	UE_DEPRECATED(4.23, "GetClippingIndex has been deprecated.  If you were using this please use GetPrecachedClippingIndex instead.")
	FORCEINLINE const int32 GetClippingIndex() const { return GetPrecachedClippingIndex(); }

	UE_DEPRECATED(4.23, "SetClippingIndex has been deprecated.  If you were using this please use SetPrecachedClippingIndex instead.")
	FORCEINLINE void SetClippingIndex(const int32 InClippingIndex) { SetPrecachedClippingIndex(InClippingIndex); }

private:
	void Init(FSlateWindowElementList& ElementList, EElementType InElementType, uint32 InLayer, const FPaintGeometry& PaintGeometry, ESlateDrawEffect InDrawEffects);

	static FVector2f GetRotationPoint( const FPaintGeometry& PaintGeometry, const TOptional<FVector2f>& UserRotationPoint, ERotationSpace RotationSpace );
	static FSlateDrawElement& MakeBoxInternal(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FSlateBrush* InBrush, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint);
private:
	FSlateDataPayload* DataPayload;
	FSlateRenderTransform RenderTransform;
	FVector2f Position;
	FVector2f LocalSize;
	int32 LayerId;
	FClipStateHandle ClipStateHandle;
	float Scale;
	int8 SceneIndex;
	ESlateDrawEffect DrawEffects;
	EElementType ElementType;
	// Misc data
	ESlateBatchDrawFlag BatchFlags;
	uint8 bIsCached : 1;
};

/**
 * Data holder for info used in cached renderbatches. Primarily clipping state, vertices and indices
 */
struct FSlateCachedFastPathRenderingData
{
	~FSlateCachedFastPathRenderingData()
	{
		CachedClipStates.Reset();
	}

	TArray<FSlateCachedClipState, TInlineAllocator<1>> CachedClipStates;
	FSlateVertexArray Vertices;
	FSlateIndexArray Indices;
};

struct FSlateCachedElementData;

/**
 * Cached list of elements corresponding to a particular widget. 
 * This class is used as an interfaces for renderbatches to interact with cached rendering data with
 * In particular verticies and indicies
 */
struct FSlateCachedElementList
{
	FSlateCachedElementList(FSlateCachedElementData* InParentData, const SWidget* InWidget)
		: OwningWidget(InWidget)
		, ParentData(InParentData)
		, CachedRenderingData(nullptr)
	{
	}

	void Initialize()
	{
		CachedRenderingData = new FSlateCachedFastPathRenderingData;
	}

	SLATECORE_API ~FSlateCachedElementList();

	void ClearCachedElements();

	FSlateCachedElementData* GetOwningData() { return ParentData; }

	FSlateRenderBatch& AddRenderBatch(int32 InLayer, const FShaderParams& InShaderParams, const FSlateShaderResource* InResource, ESlateDrawPrimitive InPrimitiveType, ESlateShader InShaderType, ESlateDrawEffect InDrawEffects, ESlateBatchDrawFlag InDrawFlags, int8 SceneIndex);

	void AddCachedClipState(FSlateCachedClipState& ClipStateToCache);

	void AddReferencedObjects(FReferenceCollector& Collector);

	/**
	 * Returns true if all typed containers are empty, else false
	 */
	SLATECORE_API bool IsEmpty();

	/**
	 * Returns number of elements in all containers summed
	 */
	SLATECORE_API int32 NumElements();
	
private:
	SLATECORE_API void DestroyCachedData();

public:
	/** List of source draw elements to create batches from */
	FSlateDrawElementMap DrawElements;

	TArray<int32> CachedRenderBatchIndices;

	/** The widget whose draw elements are in this list */
	const SWidget* OwningWidget;

	FSlateCachedElementData* ParentData;

	FSlateCachedFastPathRenderingData* CachedRenderingData;
};


/**
 * Handle used to uniquely identify a cached element list with some widget
 * Used when a widget gets invalidated / additional elements added
 * So we can modify that widget's existing cached element list
 */
struct FSlateCachedElementsHandle
{
	friend struct FSlateCachedElementData;

	static FSlateCachedElementsHandle Invalid;
	void ClearCachedElements();
	void RemoveFromCache();

	bool IsOwnedByWidget(const SWidget* Widget) const;

	bool IsValid() const { return Ptr.IsValid(); }
	bool HasCachedElements() const;

	bool operator!=(FSlateCachedElementsHandle& Other) const { return Ptr != Other.Ptr; }

	FSlateCachedElementsHandle() {}
private:
	FSlateCachedElementsHandle(TSharedRef<FSlateCachedElementList>& DataPtr)
		: Ptr(DataPtr)
	{
	}

private:
	TWeakPtr<FSlateCachedElementList> Ptr;
};

/**
 * Top level class responsible for cached elements within a particular invalidation root.
 * Equivalent to the plain FSlateBatchData in that it manages multiple renderbatches,
 * element lists (Per widget), & new element lists (Per invalidated widgets).
 * 
 * Note: Just as each window may have multiple invalidation roots, each window element list 
 * may have multiple 'FSlateCachedElementData' for each root
 */
struct FSlateCachedElementData
{
	friend class FSlateElementBatcher;
	friend struct FSlateCachedElementsHandle;

	/** Reset all cached data, except num elements, call EmptyCachedNumElements for that */
	void Empty();

	/** Create a new CachedElementList, occurs when adding a cached element whose top level draw widget is not already part of some cache */
	FSlateCachedElementsHandle AddCache(const SWidget* Widget);

	static const FSlateClippingState* GetClipStateFromParent(const FSlateClippingManager& ParentClipManager);

	FORCEINLINE void ValidateWidgetOwner(TSharedPtr<FSlateCachedElementList> List, const SWidget* CurrentWidget);

	template<EElementType ElementType>
	FSlateDrawElement& AddCachedElement(FSlateCachedElementsHandle& CacheHandle, const FSlateClippingManager& ParentClipManager, const SWidget* CurrentWidget)
	{
		TSharedPtr<FSlateCachedElementList> List = CacheHandle.Ptr.Pin(); 

#if WITH_SLATE_DEBUGGING
		ValidateWidgetOwner(List, CurrentWidget);
#endif

		FSlateDrawElementContainer& Container = List->DrawElements.Get<(uint8)ElementType>();
		FSlateDrawElement& NewElement = Container.Elements.AddDefaulted_GetRef();
		NewElement.SetIsCached(true);

		// Check if slow vs checking a flag on the list to see if it contains new data.
		ListsWithNewData.AddUnique(List.Get());

		const FSlateClippingState* ExistingClipState = FSlateCachedElementData::GetClipStateFromParent(ParentClipManager);

		if (ExistingClipState)
		{
			// We need to cache this clip state for the next time the element draws
			FSlateCachedClipState& CachedClipState = FindOrAddCachedClipState(ExistingClipState);
			List->AddCachedClipState(CachedClipState);
			NewElement.SetCachedClippingState(&CachedClipState.ClippingState.Get());
		}

		return NewElement;
	}

	FSlateRenderBatch& AddCachedRenderBatch(FSlateRenderBatch&& NewBatch, int32& OutIndex);
	void RemoveCachedRenderBatches(const TArray<int32>& CachedRenderBatchIndices);

	FSlateCachedClipState& FindOrAddCachedClipState(const FSlateClippingState* RefClipState);
	void CleanupUnusedClipStates();

	const TSparseArray<FSlateRenderBatch>& GetCachedBatches() const { return CachedBatches; }
	const TArray<TSharedPtr<FSlateCachedElementList>>& GetCachedElementLists() const { return CachedElementLists; }

	void AddReferencedObjects(FReferenceCollector& Collector);

	TArrayView<FSlateCachedElementList* const> GetListsWithNewData() const { return MakeArrayView(ListsWithNewData.GetData(), ListsWithNewData.Num()); }
private:
	/** Removes a cache node completely from the cache */
	void RemoveList(FSlateCachedElementsHandle& CacheHandle);

private:

	/** List of cached batches to submit for drawing */
	TSparseArray<FSlateRenderBatch> CachedBatches;

	/** List of cached element lists used to redraw when no invalidation occurs, each list corresponds to a particular widget. See 'FSlateCachedElementsHandle' */
	TArray<TSharedPtr<FSlateCachedElementList>> CachedElementLists;

	/** List of pointers to instances of the element lists above which have new data or have been invalidated */
	TArray<FSlateCachedElementList*, TInlineAllocator<50>> ListsWithNewData;

	/** List of clip states used later when rendering */
	TArray<FSlateCachedClipState> CachedClipStates;
};

/**
 * Represents a top level window and its draw elements.
 */
class FSlateWindowElementList : public FNoncopyable
{
	friend class FSlateElementBatcher;
public:
	/**
	 * Construct a new list of elements with which to paint a window.
	 *
	 * @param InPaintWindow		The window that owns the widgets being painted.  This is almost most always the same window that is being rendered to
	 * @param InRenderWindow	The window that we will be rendering to.
	 */
	SLATECORE_API explicit FSlateWindowElementList(const TSharedPtr<SWindow>& InPaintWindow);

	SLATECORE_API ~FSlateWindowElementList();

	/** @return Get the window that we will be painting */
	UE_DEPRECATED(4.21, "FSlateWindowElementList::GetWindow is not thread safe but window element lists are accessed on multiple threads.  Please call GetPaintWindow instead")
	FORCEINLINE TSharedPtr<SWindow> GetWindow() const
	{
		// check that we are in game thread or are in slate/movie loading thread
		check(IsInGameThread() || IsInSlateThread());
		return WeakPaintWindow.Pin();
	}

	/** @return Get the window that we will be painting */
	SWindow* GetPaintWindow() const
	{
		check(IsInGameThread() || IsInSlateThread());
		return WeakPaintWindow.IsValid() ? RawPaintWindow : nullptr;
	}

	/** @return Get the window that we will be rendering to.  Unless you are a slate renderer you probably want to use GetPaintWindow() */
	SWindow* GetRenderWindow() const
	{
		check(IsInGameThread() || IsInSlateThread());
		// Note: This assumes that the PaintWindow is safe to pin and is not accessed by another thread
		return RenderTargetWindow != nullptr ? RenderTargetWindow : GetPaintWindow();
	}

	/** @return Get the draw elements that we want to render into this window */
	const FSlateDrawElementMap& GetUncachedDrawElements() const
	{
		return UncachedDrawElements;
	}

	/** @return Get the window size that we will be painting */
	UE::Slate::FDeprecateVector2DResult GetWindowSize() const
	{
		return UE::Slate::FDeprecateVector2DResult(WindowSize);
	}

	template<typename PayloadType>
	PayloadType& CreatePayload(FSlateDrawElement& DrawElement)
	{
		// Elements are responsible for deleting their own payloads
		PayloadType* Payload;
		if (DrawElement.IsCached())
		{
			Payload = new PayloadType;
		}
		else
		{
			void* PayloadMem = MemManager.Alloc(sizeof(PayloadType), alignof(PayloadType));
			Payload = new(PayloadMem) PayloadType();
		}

		DrawElement.DataPayload = Payload;
		return *Payload;
	}

	/**
	 * Creates an uninitialized draw element if using caching will create a new cached draw list
	 * if needed (Whenever a top level draw widget's cache handle doesn't match the current cached handle).
	 */
	template<EElementType ElementType = EElementType::ET_NonMapped>
	FSlateDrawElement& AddUninitialized()
	{
		const bool bAllowCache = CachedElementDataListStack.Num() > 0 && WidgetDrawStack.Num() && !WidgetDrawStack.Top().bIsVolatile;

		if (bAllowCache)
		{
			// @todo get working with slate debugging
			return AddCachedElement<ElementType>();
		}
		else
		{
			FSlateDrawElementMap& Elements = UncachedDrawElements;
			FSlateDrawElementContainer& Container = Elements.Get<(uint8)ElementType>();
			const int32 InsertIdx = Container.Elements.AddDefaulted();

#if WITH_SLATE_DEBUGGING
			FSlateDebuggingElementTypeAddedEventArgs ElementTypeAddedArgs{*this, InsertIdx, ElementType};
			FSlateDebugging::ElementTypeAdded.Broadcast(ElementTypeAddedArgs);
#endif

			FSlateDrawElement& NewElement = Container.Elements[InsertIdx];
			return NewElement;
		}
	}

	/**
	 * Calls AddUninitialized, resolving template version based on enum
	 */
	SLATECORE_API FSlateDrawElement& AddUninitializedLookup(EElementType InElementType = EElementType::ET_NonMapped);


	//--------------------------------------------------------------------------
	// CLIPPING
	//--------------------------------------------------------------------------

	SLATECORE_API int32 PushClip(const FSlateClippingZone& InClipZone);
	SLATECORE_API int32 GetClippingIndex() const;
	int32 GetClippingStackDepth() const { return ClippingManager.GetStackDepth(); }
	SLATECORE_API TOptional<FSlateClippingState> GetClippingState() const;
	SLATECORE_API void PopClip();
	SLATECORE_API void PopClipToStackIndex(int32 Index);


	FSlateClippingManager& GetClippingManager() { return ClippingManager; }
	const FSlateClippingManager& GetClippingManager() const { return ClippingManager; }

	//--------------------------------------------------------------------------
	// PIXEL SNAPPING
	//--------------------------------------------------------------------------

	SLATECORE_API int32 PushPixelSnappingMethod(EWidgetPixelSnapping InPixelSnappingMethod);
	SLATECORE_API void PopPixelSnappingMethod();
	SLATECORE_API EWidgetPixelSnapping GetPixelSnappingMethod() const;
	
	//--------------------------------------------------------------------------
	// DEFERRED PAINTING
	//--------------------------------------------------------------------------

	/**
	 * Some widgets may want to paint their children after after another, loosely-related widget finished painting.
	 * Or they may want to paint "after everyone".
	 */
	struct FDeferredPaint
	{
	public:
		SLATECORE_API FDeferredPaint(const TSharedRef<const SWidget>& InWidgetToPaint, const FPaintArgs& InArgs, const FGeometry InAllottedGeometry, const FWidgetStyle& InWidgetStyle, bool InParentEnabled);

		SLATECORE_API int32 ExecutePaint(int32 LayerId, FSlateWindowElementList& OutDrawElements, const FSlateRect& MyCullingRect) const;

		SLATECORE_API FDeferredPaint Copy(const FPaintArgs& InArgs);

	private:
		// Used for making copies.
		FDeferredPaint(const FDeferredPaint& Copy, const FPaintArgs& InArgs);

		const TWeakPtr<const SWidget> WidgetToPaintPtr;
		const FPaintArgs Args;
		const FGeometry AllottedGeometry;
		const FWidgetStyle WidgetStyle;
		const bool bParentEnabled;
	};

	SLATECORE_API void QueueDeferredPainting(const FDeferredPaint& InDeferredPaint);

	int32 PaintDeferred(int32 LayerId, const FSlateRect& MyCullingRect);

	bool ShouldResolveDeferred() const { return bNeedsDeferredResolve; }

	SLATECORE_API void BeginDeferredGroup();
	SLATECORE_API void EndDeferredGroup();

	TArray< TSharedPtr<FDeferredPaint> > GetDeferredPaintList() const { return DeferredPaintList; }


	//--------------------------------------------------------------------------
	// FAST PATH
	//--------------------------------------------------------------------------

	/**
	 * Pushes the current widget that is painting onto the widget stack so we know what elements belong to each widget
	 * This information is used for caching later.
	 *
	 */
	SLATECORE_API void PushPaintingWidget(const SWidget& CurrentWidget, int32 StartingLayerId, FSlateCachedElementsHandle& CurrentCacheHandle);

	/**
	 * Pops the current painted widget off the stack
	 * @return true if an element was added while the widget was pushed
	 */
	SLATECORE_API FSlateCachedElementsHandle PopPaintingWidget(const SWidget& CurrentWidget);

	/** Pushes cached element data onto the stack.  Any draw elements cached after will use this cached element data until popped */
	void PushCachedElementData(FSlateCachedElementData& CachedElementData);
	void PopCachedElementData();

	//--------------------------------------------------------------------------
	// OTHER
	//--------------------------------------------------------------------------
	
	/**
	 * Remove all the elements from this draw list.
	 */
	SLATECORE_API void ResetElementList();


	FSlateBatchData& GetBatchData() { return BatchData; }
	FSlateBatchData& GetBatchDataHDR() { return BatchDataHDR; }

	SLATECORE_API void SetRenderTargetWindow(SWindow* InRenderTargetWindow);

	void AddReferencedObjects(FReferenceCollector& Collector);
	
	SLATECORE_API void SetIsInGameLayer(bool bInGameLayer);
	SLATECORE_API bool GetIsInGameLayer();

	TArrayView<FSlateCachedElementList* const> GetCurrentCachedElementWithNewData() const
	{ 
		if (GetCurrentCachedElementData() != nullptr)
		{
			return CachedElementDataList[CachedElementDataListStack.Top()]->GetListsWithNewData();
		}
		else
		{
			return TArrayView<FSlateCachedElementList* const>();
		}
	}
private:
	/** Adds a cached element, generating a new cached list for the widget at the top of the cache if needed */
	template<EElementType ElementType>
	FSlateDrawElement& AddCachedElement()
	{
		FSlateCachedElementData* CurrentCachedElementData = GetCurrentCachedElementData();
		check(CurrentCachedElementData);

		FWidgetDrawElementState& CurrentWidgetState = WidgetDrawStack.Top();
		check(!CurrentWidgetState.bIsVolatile);

		if (!CurrentWidgetState.CacheHandle.IsValid())
		{
			CurrentWidgetState.CacheHandle = CurrentCachedElementData->AddCache(CurrentWidgetState.Widget);
		}

		return CurrentCachedElementData->AddCachedElement<ElementType>(CurrentWidgetState.CacheHandle, GetClippingManager(), CurrentWidgetState.Widget);
	}

	TArrayView<FSlateCachedElementData* const> GetCachedElementDataList() const { return MakeArrayView(CachedElementDataList.GetData(), CachedElementDataList.Num()); }

	FSlateCachedElementData* GetCurrentCachedElementData() const { return CachedElementDataListStack.Num() ? CachedElementDataList[CachedElementDataListStack.Top()] : nullptr; }
private:
	/**
	* Window which owns the widgets that are being painted but not necessarily rendered to
	* Widgets are always rendered to the RenderTargetWindow
	*/
	TWeakPtr<SWindow> WeakPaintWindow;
	SWindow* RawPaintWindow;

	FMemStackBase MemManager;
	STAT(int32 MemManagerAllocatedMemory;)
	/**
	 * The window to render to.  This may be different from the paint window if we are displaying the contents of a window (or virtual window) onto another window
	 * The primary use case of this is thread safe rendering of widgets during times when the main thread is blocked (e.g loading movies)
	 * If this is null, the paint window is used
 	 */
	SWindow* RenderTargetWindow;

	/** Batched data used for rendering */
	FSlateBatchData BatchData;

	/** Batched data used for rendering */
	FSlateBatchData BatchDataHDR;

	/**  */
	FSlateClippingManager ClippingManager;

	/** Manages what pixel snapping method should be applied */
	TArray<EWidgetPixelSnapping, TInlineAllocator<4>> PixelSnappingMethodStack;
	
	/**
	 * Some widgets want their logical children to appear at a different "layer" in the physical hierarchy.
	 * We accomplish this by deferring their painting.
	 */
	TArray< TSharedPtr<FDeferredPaint> > DeferredPaintList;

	bool bNeedsDeferredResolve;
	TArray<int32> ResolveToDeferredIndex;

	// Begin Fast Path

	/** State of the current widget that is adding draw elements */
	struct FWidgetDrawElementState
	{
		FWidgetDrawElementState(FSlateCachedElementsHandle& InCurrentHandle, bool bInIsVolatile, const SWidget* InWidget)
			: CacheHandle(InCurrentHandle)
			, Widget(InWidget)
			, bIsVolatile(bInIsVolatile)
		{
		}

		FSlateCachedElementsHandle CacheHandle;
		const SWidget* Widget;
		bool bIsVolatile;
	};

	/** The uncached draw elements to be processed */
	FSlateDrawElementMap UncachedDrawElements;

	TArray<FWidgetDrawElementState, TInlineAllocator<50>> WidgetDrawStack;
	TArray<FSlateCachedElementData*, TInlineAllocator<4>> CachedElementDataList;
	TArray<int32, TInlineAllocator<4>> CachedElementDataListStack;
	// End Fast Path

	/** Store the size of the window being used to paint */
	FVector2f WindowSize;

	/** Store if currently drawing within Game Layer */
	bool bIsInGameLayer;
};
