// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/SInvalidationPanel.h"
#include "Rendering/DrawElements.h"
#include "Misc/App.h"
#include "Application/SlateApplicationBase.h"
#include "Styling/CoreStyle.h"
#include "Layout/WidgetPath.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Types/ReflectionMetadata.h"
#include "Rendering/SlateObjectReferenceCollector.h"

DECLARE_CYCLE_STAT(TEXT("SInvalidationPanel::Paint"), STAT_SlateInvalidationPaint, STATGROUP_Slate);

DEFINE_LOG_CATEGORY_STATIC(LogSlateInvalidationPanel, Log, All);

#if WITH_SLATE_DEBUGGING

/** True if we should allow widgets to be cached in the UI at all. */
static int32 InvalidationPanelsEnabled = 1;
FAutoConsoleVariableRef CVarEnableInvalidationPanels(
	TEXT("Slate.EnableInvalidationPanels"),
	InvalidationPanelsEnabled,
	TEXT("Whether to attempt to cache any widgets through invalidation panels."));

static int32 AlwaysInvalidate = 0;
FAutoConsoleVariableRef CVarAlwaysInvalidate(
	TEXT("Slate.AlwaysInvalidate"),
	AlwaysInvalidate,
	TEXT("Forces invalidation panels to cache, but to always invalidate."));

#endif // WITH_SLATE_DEBUGGING



SInvalidationPanel::SInvalidationPanel()
	: EmptyChildSlot(this)
	, HittestGrid()
	, bCanCache(true)
	, bPaintedSinceLastPrepass(true)
	, bWasCachable(false)
{
	bHasCustomPrepass = true;
	SetInvalidationRootWidget(*this);
	SetInvalidationRootHittestGrid(HittestGrid);
	SetCanTick(false);

	FSlateApplicationBase::Get().OnGlobalInvalidationToggled().AddRaw(this, &SInvalidationPanel::OnGlobalInvalidationToggled);
}

void SInvalidationPanel::Construct( const FArguments& InArgs )
{
	ChildSlot
	[
		InArgs._Content.Widget
	];

#if SLATE_VERBOSE_NAMED_EVENTS
	DebugName = InArgs._DebugName;
	DebugTickName = InArgs._DebugName + TEXT("_Tick");
	DebugPaintName = InArgs._DebugName + TEXT("_Paint");
#endif
}

SInvalidationPanel::~SInvalidationPanel()
{
	InvalidateRoot();

	if (FSlateApplicationBase::IsInitialized())
	{
		FSlateApplicationBase::Get().OnGlobalInvalidationToggled().RemoveAll(this);
	}
}

#if WITH_SLATE_DEBUGGING
bool SInvalidationPanel::AreInvalidationPanelsEnabled()
{
	return InvalidationPanelsEnabled != 0;
}

void SInvalidationPanel::EnableInvalidationPanels(bool bEnable)
{
	InvalidationPanelsEnabled = bEnable;
}
#endif

bool SInvalidationPanel::GetCanCache() const
{
	// Note: checking for FastPathProxyHandle being valid prevents nested invalidation panels from being a thing.  They are not needed anymore since invalidation panels do not redraw everything inside it just because one thing invalidates
	// In global invalidation this code makes no sense so we don't bother running it because everything is in an "invalidation panel" at the window level
#if WITH_SLATE_DEBUGGING
	// Disable invalidation panels if global invalidation is turned on
	return bCanCache && !GSlateEnableGlobalInvalidation && !GetProxyHandle().IsValid() && InvalidationPanelsEnabled;
#else
	return bCanCache && !GSlateEnableGlobalInvalidation && !GetProxyHandle().IsValid();
#endif
}

void SInvalidationPanel::OnGlobalInvalidationToggled(bool bGlobalInvalidationEnabled)
{
	InvalidateRoot();

	ClearAllFastPathData(false);
}

bool SInvalidationPanel::UpdateCachePrequisites(FSlateWindowElementList& OutDrawElements, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, int32 LayerId) const
{
	bool bNeedsRecache = false;
#if WITH_SLATE_DEBUGGING
	if (AlwaysInvalidate == 1)
	{
		bNeedsRecache = true;
	}
#endif

	// We only need to re-cache if the incoming layer is higher than the maximum layer Id we cached at,
	// we do this so that widgets that appear and live behind your invalidated UI don't constantly invalidate
	// everything above it.
	if (LayerId > LastIncomingLayerId)
	{
		LastIncomingLayerId = LayerId;
		bNeedsRecache = true;
	}

	if ( AllottedGeometry.GetLocalSize() != LastAllottedGeometry.GetLocalSize() || AllottedGeometry.GetAccumulatedRenderTransform() != LastAllottedGeometry.GetAccumulatedRenderTransform() )
	{
		LastAllottedGeometry = AllottedGeometry;
		bNeedsRecache = true;
	}

	// If our clip rect changes size, we've definitely got to invalidate.
	const FVector2D ClipRectSize = MyCullingRect.GetSize().RoundToVector();
	if ( ClipRectSize != LastClipRectSize )
	{
		LastClipRectSize = ClipRectSize;
		bNeedsRecache = true;
	}

	TOptional<FSlateClippingState> ClippingState = OutDrawElements.GetClippingState();
	if (LastClippingState != ClippingState)
	{
		LastClippingState = ClippingState;
		bNeedsRecache = true;
	}
	
	return bNeedsRecache;
}

void SInvalidationPanel::SetCanCache(bool InCanCache)
{
	bCanCache = InCanCache;
}

FChildren* SInvalidationPanel::GetChildren()
{
	if (GetCanCache() && !NeedsPrepass())
	{
		return &EmptyChildSlot;
	}
	else
	{
		return SCompoundWidget::GetChildren();
	}
}

FChildren* SInvalidationPanel::GetAllChildren()
{
	return SCompoundWidget::GetChildren();
}

int32 SInvalidationPanel::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
#if SLATE_VERBOSE_NAMED_EVENTS
	SCOPED_NAMED_EVENT_FSTRING(DebugPaintName, FColor::Purple);
#endif
	SCOPE_CYCLE_COUNTER(STAT_SlateInvalidationPaint);

	bPaintedSinceLastPrepass = true;
	SInvalidationPanel* MutableThis = const_cast<SInvalidationPanel*>(this);

	const bool bCanCacheThisFrame = GetCanCache();
	if (bCanCacheThisFrame != bWasCachable)
	{
		MutableThis->InvalidateRoot();

		bWasCachable = bCanCacheThisFrame;
	}

	if(bCanCacheThisFrame)
	{
		// Copy hit test grid settings from the root
		const bool bHittestCleared = HittestGrid.SetHittestArea(Args.RootGrid.GetGridOrigin(), Args.RootGrid.GetGridSize(), Args.RootGrid.GetGridWindowOrigin());

		FPaintArgs NewArgs = Args.WithNewHitTestGrid(HittestGrid);

		// Copy the current user index into the new grid since nested hit test grids should inherit their parents user id
		NewArgs.GetHittestGrid().SetUserIndex(Args.RootGrid.GetUserIndex());
		check(!GSlateEnableGlobalInvalidation);

		const bool bRequiresRecache = UpdateCachePrequisites(OutDrawElements, AllottedGeometry, MyCullingRect, LayerId);
		if (bHittestCleared || bRequiresRecache)
		{
			// @todo: Overly aggressive?
			MutableThis->InvalidateRoot();
		}

		// The root widget is our child.  We are not the root because we could be in a parent invalidation panel.  If we are nested in another invalidation panel, our OnPaint was called by that panel
		FSlateInvalidationContext Context(OutDrawElements, InWidgetStyle);
		Context.bParentEnabled = bParentEnabled;
		Context.bAllowFastPathUpdate = true;
		Context.LayoutScaleMultiplier = GetPrepassLayoutScaleMultiplier();
		Context.PaintArgs = &NewArgs;
		Context.IncomingLayerId = LayerId;
		Context.CullingRect = MyCullingRect;

		const FSlateInvalidationResult Result = MutableThis->PaintInvalidationRoot(Context);

		// add our widgets to the root hit test grid
		Args.RootGrid.AppendGrid(HittestGrid);

		return Result.MaxLayerIdPainted;
	}
	else
	{
#if SLATE_VERBOSE_NAMED_EVENTS
		SCOPED_NAMED_EVENT_TEXT("SInvalidationPanel Uncached", FColor::Emerald);
#endif
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}
}

void SInvalidationPanel::SetContent(const TSharedRef< SWidget >& InContent)
{
	ChildSlot
	[
		InContent
	];

	InvalidateRoot();
}

bool SInvalidationPanel::CustomPrepass(float LayoutScaleMultiplier)
{
	bPaintedSinceLastPrepass = false;

	if (GetCanCache())
	{
		ProcessInvalidation();

		return NeedsPrepass();
	}
	else
	{
		return true;
	}
}

bool SInvalidationPanel::Advanced_IsInvalidationRoot() const
{
	return GetCanCache();
}

int32 SInvalidationPanel::PaintSlowPath(const FSlateInvalidationContext& Context)
{
	return SCompoundWidget::OnPaint(*Context.PaintArgs, GetPaintSpaceGeometry(), Context.CullingRect, *Context.WindowElementList, Context.IncomingLayerId, Context.WidgetStyle, Context.bParentEnabled);
}
