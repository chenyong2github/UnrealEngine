// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rendering/DrawElements.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "HAL/IConsoleManager.h"
#include "Types/ReflectionMetadata.h"
#include "Fonts/ShapedTextFwd.h"
#include "Fonts/FontCache.h"
#include "Rendering/SlateObjectReferenceCollector.h"
#include "Debugging/SlateDebugging.h"
#include "Application/SlateApplicationBase.h"

DECLARE_CYCLE_STAT(TEXT("FSlateDrawElement::Make Time"), STAT_SlateDrawElementMakeTime, STATGROUP_SlateVerbose);
DECLARE_CYCLE_STAT(TEXT("FSlateDrawElement::MakeCustomVerts Time"), STAT_SlateDrawElementMakeCustomVertsTime, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("FSlateDrawElement::Prebatch Time"), STAT_SlateDrawElementPrebatchTime, STATGROUP_Slate);

DEFINE_STAT(STAT_SlateBufferPoolMemory);
DEFINE_STAT(STAT_SlateCachedDrawElementMemory);

static bool IsResourceObjectValid(UObject*& InObject)
{
	if (InObject != nullptr && (InObject->IsPendingKillOrUnreachable() || InObject->HasAnyFlags(RF_BeginDestroyed)))
	{
		UE_LOG(LogSlate, Warning, TEXT("Attempted to access resource for %s which is pending kill, unreachable or pending destroy"), *InObject->GetName());
		return false;
	}

	return true;
}

static bool ShouldCull(const FSlateWindowElementList& ElementList)
{
	const FSlateClippingManager& ClippingManager = ElementList.GetClippingManager();
	const int32 CurrentIndex = ClippingManager.GetClippingIndex();
	if (CurrentIndex != INDEX_NONE)
	{
		const FSlateClippingState& ClippingState = ClippingManager.GetClippingStates()[CurrentIndex];
		return ClippingState.HasZeroArea();
	}

	return false;
}

static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry)
{
	const FVector2D& LocalSize = PaintGeometry.GetLocalSize();
	if (LocalSize.X == 0 || LocalSize.Y == 0)
	{
		return true;
	}

	return ShouldCull(ElementList);
}

static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FSlateBrush* InBrush)
{
	if (ShouldCull(ElementList, PaintGeometry))
	{
		return true;
	}

	if (InBrush->GetDrawType() == ESlateBrushDrawType::NoDrawType)
	{
		return true;
	}

	UObject* ResourceObject = InBrush->GetResourceObject();
	if (!IsResourceObjectValid(ResourceObject))
	{
		return true;
	}

	return false;
}


static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FLinearColor& InTint)
{
	if (InTint.A == 0 || ShouldCull(ElementList, PaintGeometry))
	{
		return true;
	}

	return false;
}

static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FLinearColor& InTint, const FString& InText)
{
	if (InTint.A == 0 || InText.Len() == 0 || ShouldCull(ElementList, PaintGeometry))
	{
		return true;
	}

	return false;
}

static bool ShouldCull(const FSlateWindowElementList& ElementList, const FPaintGeometry& PaintGeometry, const FSlateBrush* InBrush, const FLinearColor& InTint)
{
	if (InTint.A == 0 || ShouldCull(ElementList, PaintGeometry, InBrush))
	{
		return true;
	}

	return false;
}



FSlateWindowElementList::FSlateWindowElementList(const TSharedPtr<SWindow>& InPaintWindow)
	: WeakPaintWindow(InPaintWindow)
	, RawPaintWindow(InPaintWindow.Get())
	, MemManager(0)
#if STATS
	, MemManagerAllocatedMemory(0)
#endif
	, RenderTargetWindow(nullptr)
	, bNeedsDeferredResolve(false)
	, ResolveToDeferredIndex()
	, WindowSize(FVector2D(0.0f, 0.0f))
	//, bReportReferences(true)
{
	if (InPaintWindow.IsValid())
	{
		WindowSize = InPaintWindow->GetSizeInScreen();
	}

	// Only keep UObject resources alive if this window element list is born on the game thread.
/*
	if (IsInGameThread())
	{
		ResourceGCRoot = MakeUnique<FWindowElementGCObject>(this);
	}*/
}

FSlateWindowElementList::~FSlateWindowElementList()
{
	/*if (ResourceGCRoot.IsValid())
	{
		ResourceGCRoot->ClearOwner();
	}*/
}

void FSlateDrawElement::Init(FSlateWindowElementList& ElementList, EElementType InElementType, uint32 InLayer, const FPaintGeometry& PaintGeometry, ESlateDrawEffect InDrawEffects)
{
	RenderTransform = PaintGeometry.GetAccumulatedRenderTransform();
	Position = PaintGeometry.DrawPosition;
	Scale = PaintGeometry.DrawScale;
	LocalSize = PaintGeometry.GetLocalSize();
	ClipStateHandle.SetPreCachedClipIndex(ElementList.GetClippingIndex());

	LayerId = InLayer;

	ElementType = InElementType;
	DrawEffects = InDrawEffects;
	
	// Calculate the layout to render transform as this is needed by several calculations downstream.
	const FSlateLayoutTransform InverseLayoutTransform(Inverse(FSlateLayoutTransform(Scale, Position)));

	// This is a workaround because we want to keep track of the various Scenes 
	// in use throughout the UI. We keep a synchronized set with the render thread on the SlateRenderer and 
	// use indices to synchronize between them.
	FSlateRenderer* Renderer = FSlateApplicationBase::Get().GetRenderer();
	checkSlow(Renderer);
	SceneIndex = Renderer->GetCurrentSceneIndex();

	BatchFlags = ESlateBatchDrawFlag::None;
	BatchFlags |= static_cast<ESlateBatchDrawFlag>(static_cast<uint32>(InDrawEffects) & static_cast<uint32>(ESlateDrawEffect::NoBlending | ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::NoGamma | ESlateDrawEffect::InvertAlpha));

	static_assert(((__underlying_type(ESlateDrawEffect))ESlateDrawEffect::NoBlending) == ((__underlying_type(ESlateBatchDrawFlag))ESlateBatchDrawFlag::NoBlending), "Must keep ESlateBatchDrawFlag and ESlateDrawEffect partial matches");
	static_assert(((__underlying_type(ESlateDrawEffect))ESlateDrawEffect::PreMultipliedAlpha) == ((__underlying_type(ESlateBatchDrawFlag))ESlateBatchDrawFlag::PreMultipliedAlpha), "Must keep ESlateBatchDrawFlag and ESlateDrawEffect partial matches");
	static_assert(((__underlying_type(ESlateDrawEffect))ESlateDrawEffect::NoGamma) == ((__underlying_type(ESlateBatchDrawFlag))ESlateBatchDrawFlag::NoGamma), "Must keep ESlateBatchDrawFlag and ESlateDrawEffect partial matches");
	static_assert(((__underlying_type(ESlateDrawEffect))ESlateDrawEffect::InvertAlpha) == ((__underlying_type(ESlateBatchDrawFlag))ESlateBatchDrawFlag::InvertAlpha), "Must keep ESlateBatchDrawFlag and ESlateDrawEffect partial matches");
	if ((InDrawEffects & ESlateDrawEffect::ReverseGamma) != ESlateDrawEffect::None)
	{
		BatchFlags |= ESlateBatchDrawFlag::ReverseGamma;
	}
}

void FSlateDrawElement::ApplyPositionOffset(const FVector2D& InOffset)
{
	SetPosition(GetPosition() + InOffset);
	RenderTransform = Concatenate(RenderTransform, InOffset);

	// Recompute cached layout to render transform
	const FSlateLayoutTransform InverseLayoutTransform(Inverse(FSlateLayoutTransform(Scale, Position)));
}

void FSlateDrawElement::MakeDebugQuad( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry)
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();
	ElementList.CreatePayload<FSlateBoxPayload>(Element);

	Element.Init(ElementList, EElementType::ET_DebugQuad, InLayer, PaintGeometry, ESlateDrawEffect::None);
}

FSlateDrawElement& FSlateDrawElement::MakeBoxInternal(
	FSlateWindowElementList& ElementList,
	uint32 InLayer,
	const FPaintGeometry& PaintGeometry,
	const FSlateBrush* InBrush,
	ESlateDrawEffect InDrawEffects,
	const FLinearColor& InTint
)
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	EElementType ElementType = (InBrush->DrawAs == ESlateBrushDrawType::Border) ? EElementType::ET_Border : EElementType::ET_Box;

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	const FMargin& Margin = InBrush->GetMargin();
	FSlateBoxPayload& BoxPayload = ElementList.CreatePayload<FSlateBoxPayload>(Element);

	Element.Init(ElementList, ElementType, InLayer, PaintGeometry, InDrawEffects);

	BoxPayload.SetTint(InTint);
	BoxPayload.SetBrush(InBrush);

	return Element;
}

void FSlateDrawElement::MakeBox(
	FSlateWindowElementList& ElementList,
	uint32 InLayer, 
	const FPaintGeometry& PaintGeometry, 
	const FSlateBrush* InBrush,
	ESlateDrawEffect InDrawEffects, 
	const FLinearColor& InTint)
{

	if (ShouldCull(ElementList, PaintGeometry, InBrush, InTint))
	{
		return;
	}

	MakeBoxInternal(ElementList, InLayer, PaintGeometry, InBrush, InDrawEffects, InTint);
}

void FSlateDrawElement::MakeBox( 
	FSlateWindowElementList& ElementList,
	uint32 InLayer, 
	const FPaintGeometry& PaintGeometry, 
	const FSlateBrush* InBrush, 
	const FSlateResourceHandle& InRenderingHandle,
	ESlateDrawEffect InDrawEffects, 
	const FLinearColor& InTint )
{
	MakeBox(ElementList, InLayer, PaintGeometry, InBrush, InDrawEffects, InTint);
}

void FSlateDrawElement::MakeRotatedBox(
	FSlateWindowElementList& ElementList,
	uint32 InLayer,
	const FPaintGeometry& PaintGeometry,
	const FSlateBrush* InBrush,
	ESlateDrawEffect InDrawEffects,
	float Angle2D,
	TOptional<FVector2D> InRotationPoint,
	ERotationSpace RotationSpace,
	const FLinearColor& InTint)
{

	if (ShouldCull(ElementList, PaintGeometry, InBrush, InTint))
	{
		return;
	}

	FSlateDrawElement& DrawElement = MakeBoxInternal(ElementList, InLayer, PaintGeometry, InBrush, InDrawEffects, InTint);
	
	if (Angle2D != 0.0f)
	{
		const FVector2D RotationPoint = GetRotationPoint(PaintGeometry, InRotationPoint, RotationSpace);
		const FSlateRenderTransform RotationTransform = Concatenate(Inverse(RotationPoint), FQuat2D(Angle2D), RotationPoint);
		DrawElement.SetRenderTransform(Concatenate(RotationTransform, DrawElement.GetRenderTransform()));
	}
}

void FSlateDrawElement::MakeText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const int32 StartIndex, const int32 EndIndex, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint )
{
	SCOPE_CYCLE_COUNTER(STAT_SlateDrawElementMakeTime)
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList, PaintGeometry, InTint, InText))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();
	FSlateTextPayload& DataPayload = ElementList.CreatePayload<FSlateTextPayload>(Element);

	DataPayload.SetTint(InTint);
	DataPayload.SetText(InText, InFontInfo, StartIndex, EndIndex);

	Element.Init(ElementList, EElementType::ET_Text, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeText( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FString& InText, const FSlateFontInfo& InFontInfo, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint )
{
	SCOPE_CYCLE_COUNTER(STAT_SlateDrawElementMakeTime)
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	// Don't try and render empty text
	if (InText.Len() == 0)
	{
		return;
	}

	if (ShouldCull(ElementList, PaintGeometry, InTint, InText))
	{
		return;
	}

	// Don't do anything if there the font would be completely transparent 
	if (InTint.A == 0 && !InFontInfo.OutlineSettings.IsVisible())
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateTextPayload& DataPayload = ElementList.CreatePayload<FSlateTextPayload>(Element);

	DataPayload.SetTint(InTint);
	DataPayload.SetText(InText, InFontInfo);

	Element.Init(ElementList, EElementType::ET_Text, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeShapedText(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FShapedGlyphSequenceRef& InShapedGlyphSequence, ESlateDrawEffect InDrawEffects, const FLinearColor& BaseTint, const FLinearColor& OutlineTint)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateDrawElementMakeTime)
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (InShapedGlyphSequence->GetGlyphsToRender().Num() == 0)
	{
		return;
	}

	if (ShouldCull(ElementList, PaintGeometry))
	{
		return;
	}

	// Don't do anything if there the font would be completely transparent 
	if ((BaseTint.A == 0 && InShapedGlyphSequence->GetFontOutlineSettings().OutlineSize == 0) || 
		(BaseTint.A == 0 && OutlineTint.A == 0))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateShapedTextPayload& DataPayload = ElementList.CreatePayload<FSlateShapedTextPayload>(Element);
	DataPayload.SetTint(BaseTint);
	DataPayload.SetShapedText(ElementList, InShapedGlyphSequence, OutlineTint);

	Element.Init(ElementList, EElementType::ET_ShapedText, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeGradient( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TArray<FSlateGradientStop> InGradientStops, EOrientation InGradientType, ESlateDrawEffect InDrawEffects )
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList, PaintGeometry))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateGradientPayload& DataPayload = ElementList.CreatePayload<FSlateGradientPayload>(Element);

	DataPayload.SetGradient(InGradientStops, InGradientType);

	Element.Init(ElementList, EElementType::ET_Gradient, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeSpline( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint )
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}
	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateSplinePayload& DataPayload = ElementList.CreatePayload<FSlateSplinePayload>(Element);

	DataPayload.SetHermiteSpline(InStart, InStartDir, InEnd, InEndDir, InThickness, InTint);

	Element.Init(ElementList, EElementType::ET_Spline, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeCubicBezierSpline(FSlateWindowElementList & ElementList, uint32 InLayer, const FPaintGeometry & PaintGeometry, const FVector2D & P0, const FVector2D & P1, const FVector2D & P2, const FVector2D & P3, float InThickness, ESlateDrawEffect InDrawEffects, const FLinearColor & InTint)
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}
	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateSplinePayload& DataPayload = ElementList.CreatePayload<FSlateSplinePayload>(Element);

	DataPayload.SetCubicBezier(P0, P1, P2, P3, InThickness, InTint);

	Element.Init(ElementList, EElementType::ET_Spline, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeDrawSpaceSpline( FSlateWindowElementList& ElementList, uint32 InLayer, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, float InThickness, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint )
{
	MakeSpline( ElementList, InLayer, FPaintGeometry(), InStart, InStartDir, InEnd, InEndDir, InThickness, InDrawEffects, InTint );
}

void FSlateDrawElement::MakeDrawSpaceGradientSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, const TArray<FSlateGradientStop>& InGradientStops, float InThickness, ESlateDrawEffect InDrawEffects)
{
	const FPaintGeometry PaintGeometry;
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateSplinePayload& DataPayload = ElementList.CreatePayload<FSlateSplinePayload>(Element);
	DataPayload.SetGradientHermiteSpline(InStart, InStartDir, InEnd, InEndDir, InThickness, InGradientStops);

	Element.Init(ElementList, EElementType::ET_Spline, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeDrawSpaceGradientSpline(FSlateWindowElementList& ElementList, uint32 InLayer, const FVector2D& InStart, const FVector2D& InStartDir, const FVector2D& InEnd, const FVector2D& InEndDir, const FSlateRect InClippingRect, const TArray<FSlateGradientStop>& InGradientStops, float InThickness, ESlateDrawEffect InDrawEffects)
{
	const FPaintGeometry PaintGeometry;
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateSplinePayload& DataPayload = ElementList.CreatePayload<FSlateSplinePayload>(Element);
	DataPayload.SetGradientHermiteSpline(InStart, InStartDir, InEnd, InEndDir, InThickness, InGradientStops);

	Element.Init(ElementList, EElementType::ET_Spline, InLayer, PaintGeometry, InDrawEffects);
}

void FSlateDrawElement::MakeLines(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2D>& Points, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint, bool bAntialias, float Thickness)
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateLinePayload& DataPayload = ElementList.CreatePayload<FSlateLinePayload>(Element);

	DataPayload.SetTint(InTint);
	DataPayload.SetThickness(Thickness);
	DataPayload.SetLines(Points, bAntialias, nullptr);

	ESlateDrawEffect DrawEffects = InDrawEffects;
	if (bAntialias)
	{
		// If the line is to be anti-aliased, we cannot reliably snap
		// the generated vertices.
		DrawEffects |= ESlateDrawEffect::NoPixelSnapping;
	}

	Element.Init(ElementList, EElementType::ET_Line, InLayer, PaintGeometry, DrawEffects);
}

void FSlateDrawElement::MakeLines( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const TArray<FVector2D>& Points, const TArray<FLinearColor>& PointColors, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint, bool bAntialias, float Thickness )
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateLinePayload& DataPayload = ElementList.CreatePayload<FSlateLinePayload>(Element);
	DataPayload.SetTint(InTint);
	DataPayload.SetThickness(Thickness);
	DataPayload.SetLines(Points, bAntialias, &PointColors);

	Element.Init(ElementList, EElementType::ET_Line, InLayer, PaintGeometry, InDrawEffects);

}

void FSlateDrawElement::MakeViewport( FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, TSharedPtr<const ISlateViewport> Viewport, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint )
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();
	FSlateViewportPayload& DataPayload = ElementList.CreatePayload<FSlateViewportPayload>(Element);

	DataPayload.SetViewport(Viewport, InTint);

	Element.Init(ElementList, EElementType::ET_Viewport, InLayer, PaintGeometry, InDrawEffects);
}


void FSlateDrawElement::MakeCustom( FSlateWindowElementList& ElementList, uint32 InLayer, TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> CustomDrawer )
{
	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlateCustomDrawerPayload& DataPayload = ElementList.CreatePayload<FSlateCustomDrawerPayload>(Element);
	DataPayload.SetCustomDrawer(CustomDrawer);
	
	Element.Init(ElementList, EElementType::ET_Custom, InLayer, FPaintGeometry(), ESlateDrawEffect::None);
	Element.RenderTransform = FSlateRenderTransform();
}


void FSlateDrawElement::MakeCustomVerts(FSlateWindowElementList& ElementList, uint32 InLayer, const FSlateResourceHandle& InRenderResourceHandle, const TArray<FSlateVertex>& InVerts, const TArray<SlateIndex>& InIndexes, ISlateUpdatableInstanceBuffer* InInstanceData, uint32 InInstanceOffset, uint32 InNumInstances, ESlateDrawEffect InDrawEffects)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateDrawElementMakeCustomVertsTime);
	
	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();
	FSlateCustomVertsPayload& DataPayload = ElementList.CreatePayload<FSlateCustomVertsPayload>(Element);

	const FSlateShaderResourceProxy* RenderingProxy = InRenderResourceHandle.GetResourceProxy();
	DataPayload.SetCustomVerts(RenderingProxy, InVerts, InIndexes, InInstanceData, InInstanceOffset, InNumInstances);

	Element.Init(ElementList, EElementType::ET_CustomVerts, InLayer, FPaintGeometry(), InDrawEffects);
	Element.RenderTransform = FSlateRenderTransform();
}

void FSlateDrawElement::MakePostProcessPass(FSlateWindowElementList& ElementList, uint32 InLayer, const FPaintGeometry& PaintGeometry, const FVector4& Params, int32 DownsampleAmount)
{
	PaintGeometry.CommitTransformsIfUsingLegacyConstructor();

	if (ShouldCull(ElementList))
	{
		return;
	}

	FSlateDrawElement& Element = ElementList.AddUninitialized();

	FSlatePostProcessPayload& DataPayload = ElementList.CreatePayload<FSlatePostProcessPayload>(Element);
	DataPayload.DownsampleAmount = DownsampleAmount;
	DataPayload.PostProcessData = Params;

	Element.Init(ElementList, EElementType::ET_PostProcessPass, InLayer, PaintGeometry, ESlateDrawEffect::None);
}

FSlateDrawElement::FSlateDrawElement()
	: DataPayload(nullptr)
	, bIsCached(false)
{

}

FSlateDrawElement::~FSlateDrawElement()
{
	if (bIsCached)
	{
		delete DataPayload;
	}
	else if (DataPayload)
	{
		// Allocated by a memstack so we just need to call the destructor manually
		DataPayload->~FSlateDataPayload();
	}
}

FVector2D FSlateDrawElement::GetRotationPoint(const FPaintGeometry& PaintGeometry, const TOptional<FVector2D>& UserRotationPoint, ERotationSpace RotationSpace)
{
	FVector2D RotationPoint(0, 0);

	const FVector2D& LocalSize = PaintGeometry.GetLocalSize();

	switch (RotationSpace)
	{
		case RelativeToElement:
		{
			// If the user did not specify a rotation point, we rotate about the center of the element
			RotationPoint = UserRotationPoint.Get(LocalSize * 0.5f);
		}
		break;
		case RelativeToWorld:
		{
			// its in world space, must convert the point to local space.
			RotationPoint = TransformPoint(Inverse(PaintGeometry.GetAccumulatedRenderTransform()), UserRotationPoint.Get(FVector2D::ZeroVector));
		}
		break;
	default:
		check(0);
		break;
	}

	return RotationPoint;
}

void FSlateDrawElement::AddReferencedObjects(FReferenceCollector& Collector)
{
	if(DataPayload)
	{
		DataPayload->AddReferencedObjects(Collector);
	}
}


FSlateDrawElement& FSlateWindowElementList::AddUninitialized()
{
	const bool bAllowCache = CachedElementDataListStack.Num() > 0 && WidgetDrawStack.Num() && !WidgetDrawStack.Top().bIsVolatile;

	if (bAllowCache)
	{
		// @todo get working with slate debugging
		return AddCachedElement();
	}
	else
	{
		FSlateDrawElementArray& Elements = UncachedDrawElements;
		const int32 InsertIdx = Elements.AddDefaulted();

#if WITH_SLATE_DEBUGGING
		FSlateDebugging::ElementAdded.Broadcast(*this, InsertIdx);
#endif

		FSlateDrawElement& NewElement = Elements[InsertIdx];
		return Elements[InsertIdx];
	}
}


FSlateWindowElementList::FDeferredPaint::FDeferredPaint( const TSharedRef<const SWidget>& InWidgetToPaint, const FPaintArgs& InArgs, const FGeometry InAllottedGeometry, const FWidgetStyle& InWidgetStyle, bool InParentEnabled )
	: WidgetToPaintPtr( InWidgetToPaint )
	, Args( InArgs )
	, AllottedGeometry( InAllottedGeometry )
	, WidgetStyle( InWidgetStyle )
	, bParentEnabled( InParentEnabled )
{
#if WITH_SLATE_DEBUGGING
	// We need to perform this update here, because otherwise we'll warn that this widget
	// was not painted along the fast path, which, it will be, but later because it's deferred,
	// but we need to go ahead and update the painted frame to match the current one, so
	// that we don't think this widget was forgotten.
	const_cast<SWidget&>(InWidgetToPaint.Get()).Debug_UpdateLastPaintFrame();
#endif
}

FSlateWindowElementList::FDeferredPaint::FDeferredPaint(const FDeferredPaint& Copy, const FPaintArgs& InArgs)
	: WidgetToPaintPtr(Copy.WidgetToPaintPtr)
	, Args(InArgs)
	, AllottedGeometry(Copy.AllottedGeometry)
	, WidgetStyle(Copy.WidgetStyle)
	, bParentEnabled(Copy.bParentEnabled)
{
}

int32 FSlateWindowElementList::FDeferredPaint::ExecutePaint(int32 LayerId, FSlateWindowElementList& OutDrawElements, const FSlateRect& MyCullingRect) const
{
	TSharedPtr<const SWidget> WidgetToPaint = WidgetToPaintPtr.Pin();
	if ( WidgetToPaint.IsValid() )
	{
		return WidgetToPaint->Paint( Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, WidgetStyle, bParentEnabled );
	}

	return LayerId;
}

FSlateWindowElementList::FDeferredPaint FSlateWindowElementList::FDeferredPaint::Copy(const FPaintArgs& InArgs)
{
	return FDeferredPaint(*this, InArgs);
}


void FSlateWindowElementList::QueueDeferredPainting( const FDeferredPaint& InDeferredPaint )
{
	DeferredPaintList.Add(MakeShareable(new FDeferredPaint(InDeferredPaint)));
}

int32 FSlateWindowElementList::PaintDeferred(int32 LayerId, const FSlateRect& MyCullingRect)
{
	bNeedsDeferredResolve = false;

	int32 ResolveIndex = ResolveToDeferredIndex.Pop(false);

	for ( int32 i = ResolveIndex; i < DeferredPaintList.Num(); ++i )
	{
		LayerId = DeferredPaintList[i]->ExecutePaint(LayerId, *this, MyCullingRect);
	}

	for ( int32 i = DeferredPaintList.Num() - 1; i >= ResolveIndex; --i )
	{
		DeferredPaintList.RemoveAt(i, 1, false);
	}

	return LayerId;
}

void FSlateWindowElementList::BeginDeferredGroup()
{
	ResolveToDeferredIndex.Add(DeferredPaintList.Num());
}

void FSlateWindowElementList::EndDeferredGroup()
{
	bNeedsDeferredResolve = true;
}

void FSlateWindowElementList::PushPaintingWidget(const SWidget& CurrentWidget, int32 StartingLayerId, FSlateCachedElementListNode* CurrentCacheNode)
{
	FSlateCachedElementData* CurrentCachedElementData = GetCurrentCachedElementData();
	if (CurrentCachedElementData)
	{
		const FWidgetDrawElementState& PreviousState = WidgetDrawStack.Num() ? WidgetDrawStack.Top() : FWidgetDrawElementState(nullptr, false, nullptr);

		WidgetDrawStack.Emplace(CurrentCacheNode, CurrentWidget.IsVolatileIndirectly() || CurrentWidget.IsVolatile(), &CurrentWidget);

		// When a widget is pushed reset its draw elements.  They are being recached or possibly going away
		if (CurrentCacheNode != nullptr)
		{
#if WITH_SLATE_DEBUGGING
			check(CurrentCacheNode->GetValue().Widget == &CurrentWidget);
#endif
			CurrentCachedElementData->ResetCache(CurrentCacheNode);
		}
	}
}


FSlateCachedElementListNode* FSlateWindowElementList::PopPaintingWidget()
{
	FSlateCachedElementData* CurrentCachedElementData = GetCurrentCachedElementData();
	if (CurrentCachedElementData)
	{
		return WidgetDrawStack.Pop().CacheNode;
	}

	return nullptr;
}

/*
int32 FSlateWindowElementList::PushBatchPriortyGroup(const SWidget& CurrentWidget)
{
	int32 NewPriorityGroup = 0;
/ *
	if (GSlateEnableGlobalInvalidation)
	{
		NewPriorityGroup = BatchDepthPriorityStack.Add_GetRef(CurrentWidget.FastPathProxyHandle.IsValid() ? CurrentWidget.FastPathProxyHandle.GetIndex() : 0);
	}
	else
	{
		NewPriorityGroup = BatchDepthPriorityStack.Add_GetRef(MaxPriorityGroup + 1);
		//NewPriorityGroup = BatchDepthPriorityStack.Add_GetRef(0);
	}

	// Should be +1 or the first overlay slot will not appear on top of stuff below it?
	// const int32 NewPriorityGroup = BatchDepthPriorityStack.Add_GetRef(BatchDepthPriorityStack.Num() ? BatchDepthPriorityStack.Top()+1 : 1);

	MaxPriorityGroup = FMath::Max(NewPriorityGroup, MaxPriorityGroup);* /
	return NewPriorityGroup;
}

int32 FSlateWindowElementList::PushAbsoluteBatchPriortyGroup(int32 BatchPriorityGroup)
{
	return 0;// return BatchDepthPriorityStack.Add_GetRef(BatchPriorityGroup);
}

void FSlateWindowElementList::PopBatchPriortyGroup()
{
	//BatchDepthPriorityStack.Pop();
}*/

FSlateDrawElement& FSlateWindowElementList::AddCachedElement()
{
	FSlateCachedElementData* CurrentCachedElementData = GetCurrentCachedElementData();
	check(CurrentCachedElementData);

	FWidgetDrawElementState& CurrentWidgetState = WidgetDrawStack.Top();
	check(!CurrentWidgetState.bIsVolatile);

	if (CurrentWidgetState.CacheNode == nullptr)
	{
		CurrentWidgetState.CacheNode = CurrentCachedElementData->AddCache(CurrentWidgetState.Widget);
	}

	return CurrentCachedElementData->AddCachedElement(CurrentWidgetState.CacheNode, GetClippingManager(), CurrentWidgetState.Widget);
}

void FSlateWindowElementList::PushCachedElementData(FSlateCachedElementData& CachedElementData)
{
	const int32 Index = CachedElementDataList.AddUnique(&CachedElementData);
	CachedElementDataListStack.Push(Index);
}

void FSlateWindowElementList::PopCachedElementData()
{
	CachedElementDataListStack.Pop();
}


int32 FSlateWindowElementList::PushClip(const FSlateClippingZone& InClipZone)
{
	const int32 NewClipIndex = ClippingManager.PushClip(InClipZone);

	return NewClipIndex;
}

int32 FSlateWindowElementList::GetClippingIndex() const
{
	return ClippingManager.GetClippingIndex();
}

TOptional<FSlateClippingState> FSlateWindowElementList::GetClippingState() const
{
	return ClippingManager.GetActiveClippingState();
}

void FSlateWindowElementList::PopClip()
{
	ClippingManager.PopClip();
}

void FSlateWindowElementList::PopClipToStackIndex(int32 Index)
{
	ClippingManager.PopToStackIndex(Index);
}


void FSlateWindowElementList::SetRenderTargetWindow(SWindow* InRenderTargetWindow)
{
	check(IsThreadSafeForSlateRendering());
	RenderTargetWindow = InRenderTargetWindow;
}

DECLARE_MEMORY_STAT(TEXT("FSlateWindowElementList MemManager"), STAT_FSlateWindowElementListMemManager, STATGROUP_SlateMemory);
DECLARE_DWORD_COUNTER_STAT(TEXT("FSlateWindowElementList MemManager Count"), STAT_FSlateWindowElementListMemManagerCount, STATGROUP_SlateMemory);

void FSlateWindowElementList::ResetElementList()
{
	QUICK_SCOPE_CYCLE_COUNTER(Slate_ResetElementList);

	check(IsThreadSafeForSlateRendering());

	DeferredPaintList.Reset();

	BatchData.ResetData();

	ClippingManager.ResetClippingState();

	UncachedDrawElements.Reset();

#if STATS
	const int32 DeltaMemory = MemManager.GetByteCount() - MemManagerAllocatedMemory;
	INC_DWORD_STAT(STAT_FSlateWindowElementListMemManagerCount);
	INC_MEMORY_STAT_BY(STAT_FSlateWindowElementListMemManager, DeltaMemory);

	MemManagerAllocatedMemory = MemManager.GetByteCount();
#endif

	MemManager.Flush();
	
	CachedElementDataList.Empty();

	check(WidgetDrawStack.Num() == 0);

	RenderTargetWindow = nullptr;
}


void FSlateWindowElementList::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FSlateDrawElement& Element : UncachedDrawElements)
	{
		Element.AddReferencedObjects(Collector);
	}
}

static const FSlateClippingState* GetClipStateFromParent(const FSlateClippingManager& ParentClipManager)
{
	const int32 ClippingIndex = ParentClipManager.GetClippingIndex();

	if(ClippingIndex != INDEX_NONE)
	{
		return &ParentClipManager.GetClippingStates()[ClippingIndex];
	}
	else
	{
		return nullptr;
	}
}

FSlateCachedElementListNode* FSlateCachedElementData::AddCache(const SWidget* Widget)
{
#if WITH_SLATE_DEBUGGING
	for (FSlateCachedElementList& CachedElementList : CachedElementLists)
	{
		ensure(CachedElementList.Widget != Widget);
	}
#endif

	FSlateCachedElementListNode* NewNode = new FSlateCachedElementListNode(FSlateCachedElementList(this, Widget));

	CachedElementLists.AddTail(NewNode);
	NewNode->GetValue().Initialize();

	return NewNode;
}

FSlateDrawElement& FSlateCachedElementData::AddCachedElement(FSlateCachedElementListNode* CacheNode, const FSlateClippingManager& ParentClipManager, const SWidget* CurrentWidget)
{
#if WITH_SLATE_DEBUGGING
	check(CacheNode->GetValue().Widget == CurrentWidget);
	check(CurrentWidget->GetParentWidget().IsValid());
#endif

	FSlateCachedElementList& List = CacheNode->GetValue();
	FSlateDrawElement& NewElement = List.DrawElements.AddDefaulted_GetRef();
	NewElement.SetIsCached(true);

	List.bNewData = true;
	const FSlateClippingState* ExistingClipState = GetClipStateFromParent(ParentClipManager);

	if (ExistingClipState)
	{
		// We need to cache this clip state for the next time the element draws
		FSlateCachedClipState& CachedClipState = FindOrAddCachedClipState(ExistingClipState);
		List.AddCachedClipState(CachedClipState);
		NewElement.SetCachedClippingState(&CachedClipState.ClippingState);
	}

	return NewElement;
}

void FSlateCachedElementData::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FSlateCachedElementList& CachedElementList : CachedElementLists)
	{
		CachedElementList.AddReferencedObjects(Collector);
	}
}

FSlateCachedClipState& FSlateCachedElementData::FindOrAddCachedClipState(const FSlateClippingState* RefClipState)
{
	for (auto& CachedState : CachedClipStates)
	{
		if (CachedState->ClippingState == *RefClipState)
		{
			CachedState->BeginUsingState();
			return *CachedState;
		}
	}

	return *CachedClipStates.Emplace_GetRef(MakeUnique<FSlateCachedClipState>(*RefClipState));
}

void FSlateCachedElementData::CleanupUnusedClipStates()
{
	for(int32 CachedStateIdx = 0; CachedStateIdx < CachedClipStates.Num();)
	{
		const TUniquePtr<FSlateCachedClipState>& CachedState = CachedClipStates[CachedStateIdx];
		if (CachedState->GetUsageCount() == 0)
		{
			CachedClipStates.RemoveAtSwap(CachedStateIdx);
		}
		else
		{
			++CachedStateIdx;
		}
	}
}

FSlateCachedElementList::~FSlateCachedElementList()
{
	DestroyCachedVertexData();
	Widget->PersistentState.CachedElementListNode = nullptr;
}

void FSlateCachedElementList::Reset()
{
	DrawElements.Reset();

	CachedBatches.Reset();


	// Destroy vertex data in a thread safe way
	DestroyCachedVertexData();

	CachedRenderingData = new FSlateCachedFastPathRenderingData;

#if 0 // enable this if you want to know why a widget is invalidated after it has been drawn but before it has been batched (probably a child or parent invalidating a relation)
	if (ensure(!bNewData))
	{
		UE_LOG(LogSlate, Log, TEXT("Cleared out data in cached ElementList for Widget: %s before it was batched"), *Widget->GetTag().ToString());
	}
#endif

	bNewData = false;
}

FSlateRenderBatch& FSlateCachedElementList::AddRenderBatch(int32 InLayer, const FShaderParams& InShaderParams, const FSlateShaderResource* InResource, ESlateDrawPrimitive InPrimitiveType, ESlateShader InShaderType, ESlateDrawEffect InDrawEffects, ESlateBatchDrawFlag InDrawFlags, int8 SceneIndex)
{
	return CachedBatches.Emplace_GetRef(InLayer, InShaderParams, InResource, InPrimitiveType, InShaderType, InDrawEffects, InDrawFlags, SceneIndex, &CachedRenderingData->Vertices, &CachedRenderingData->Indices, CachedRenderingData->Vertices.Num(), CachedRenderingData->Indices.Num());
}

void FSlateCachedElementList::AddCachedClipState(FSlateCachedClipState& ClipStateToCache)
{
	CachedRenderingData->CachedClipStates.Add(&ClipStateToCache);
}

void FSlateCachedElementList::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FSlateDrawElement& Element : DrawElements)
	{
		Element.AddReferencedObjects(Collector);
	}
}

void FSlateCachedElementList::DestroyCachedVertexData()
{
	if (CachedRenderingData)
	{
		if (FSlateApplicationBase::IsInitialized())
		{
			FSlateApplicationBase::Get().GetRenderer()->DestroyCachedFastPathRenderingData(CachedRenderingData);
		}
		else
		{
			delete CachedRenderingData;
		}
	}

	CachedRenderingData = nullptr;
}
