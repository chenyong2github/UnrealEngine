// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WidgetProxy.h"
#include "FastUpdate/SlateInvalidationRootHandle.h"
#include "FastUpdate/SlateInvalidationWidgetIndex.h"
#include "Rendering/DrawElements.h"

struct FSlateCachedElementData;
class FSlateInvalidationWidgetHeap;
class FSlateInvalidationWidgetList;
class FSlateWindowElementList;
class FWidgetStyle;

#define UE_SLATE_DEBUGGING_CLEAR_ALL_FAST_PATH_DATA 0

struct FSlateInvalidationContext
{
	FSlateInvalidationContext(FSlateWindowElementList& InWindowElementList, const FWidgetStyle& InWidgetStyle)
		: PaintArgs(nullptr)
		, WidgetStyle(InWidgetStyle)
		, WindowElementList(&InWindowElementList)
		, LayoutScaleMultiplier(1.0f)
		, IncomingLayerId(0)
		, bParentEnabled(true)
		, bAllowFastPathUpdate(false)
	{
	} 

	FSlateRect CullingRect;
	const FPaintArgs* PaintArgs;
	const FWidgetStyle& WidgetStyle;
	FSlateWindowElementList* WindowElementList;
	float LayoutScaleMultiplier;
	int32 IncomingLayerId;
	bool bParentEnabled;
	bool bAllowFastPathUpdate;
};

enum class ESlateInvalidationPaintType
{
	None,
	Slow,
	Fast,
};

struct FSlateInvalidationResult
{
	FSlateInvalidationResult()
		: MaxLayerIdPainted(0)
		, bRepaintedWidgets(false)
	{}

	/** The max layer id painted or cached */
	int32 MaxLayerIdPainted;
	/** If we had to repaint any widget */
	bool bRepaintedWidgets;
};

class FSlateInvalidationRoot : public FGCObject, public FNoncopyable
{
	friend class FSlateUpdateFastWidgetPathTask;
	friend class FSlateUpdateFastPathAndHitTestGridTask;
	friend class FWidgetProxyHandle;

public:
	SLATECORE_API FSlateInvalidationRoot();
	SLATECORE_API virtual ~FSlateInvalidationRoot();

	//~ Begin FGCObject interface
	SLATECORE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	SLATECORE_API virtual FString GetReferencerName() const override;
	//~ End FGCObject interface

	/** Rebuild the list and request a SlowPath. */
	UE_DEPRECATED(4.27, "InvalidateRoot is deprecated, use InvalidateRootChildOrder or InvalidateRootChildOrder")
	SLATECORE_API void InvalidateRoot(const SWidget* Investigator = nullptr);
	/** Rebuild the list and request a SlowPath. */
	SLATECORE_API void InvalidateRootChildOrder(const SWidget* Investigator = nullptr);
	/** Invalidate the layout, forcing the parent of the InvalidationRoot to be repainted. */
	SLATECORE_API void InvalidateRootLayout(const SWidget* Investigator = nullptr);
	/**
	 * Update the screen position of the SWidget owning the InvalidationRoot.
	 * This is faster then doing a SlowPath when only the DesktopGeometry changed.
	 */
	SLATECORE_API void InvalidateScreenPosition(const SWidget* Investigator = nullptr);

	/** @return if the InvalidationRoot will be rebuild, Prepass() and Paint will be called. */
	bool NeedsSlowPath() const { return bNeedsSlowPath; }

	/** @return the HittestGrid of the InvalidationRoot. */
	FHittestGrid* GetHittestGrid() const { return RootHittestGrid; }
	/** @return the cached draw elements for this window and its widget hierarchy. */
	FSlateCachedElementData& GetCachedElements() { return *CachedElementData; }
	/** @return the cached draw elements for this window and its widget hierarchy. */
	const FSlateCachedElementData& GetCachedElements() const { return *CachedElementData; }
	/** @return the invalidation root as a widget. */
	const SWidget* GetInvalidationRootWidget() const { return InvalidationRootWidget; }
	/** @return the the generation number the widget proxy handle should have to be valid. */
	int32 GetFastPathGenerationNumber() const { return FastPathGenerationNumber; }
	/** @return the Handle of the InvalidationRoot. */
	FSlateInvalidationRootHandle GetInvalidationRootHandle() const { return InvalidationRootHandle; }
	/** @return the list of widgets that are controlled by the InvalidationRoot. */
	const FSlateInvalidationWidgetList& GetFastPathWidgetList() const { return *FastWidgetPathList; }
	/** @return the widget that is the root of the InvalidationRoot. */
	SLATECORE_API const TSharedPtr<SWidget> GetFastPathWidgetListRoot() const;

	/** @return the cached draw elements for this window and its widget hierarchy */
	SLATECORE_API FSlateInvalidationResult PaintInvalidationRoot(const FSlateInvalidationContext& Context);

	void OnWidgetDestroyed(const SWidget* Widget);

	SLATECORE_API void Advanced_ResetInvalidation(bool bClearResourcesImmediately);

	SLATECORE_API static void ClearAllWidgetUpdatesPending();

#if WITH_SLATE_DEBUGGING
	/** @return the last paint type the invalidation root handle used. */
	ESlateInvalidationPaintType GetLastPaintType() const { return LastPaintType; }
	void SetLastPaintType(ESlateInvalidationPaintType Value) { LastPaintType = Value; }
#endif

protected:
	/** @return the children root widget of the Invalidation root. */
	virtual TSharedRef<SWidget> GetRootWidget() = 0;
	virtual int32 PaintSlowPath(const FSlateInvalidationContext& Context) = 0;

	void SetInvalidationRootWidget(SWidget& InInvalidationRootWidget) { InvalidationRootWidget = &InInvalidationRootWidget; }
	void SetInvalidationRootHittestGrid(FHittestGrid& InHittestGrid) { RootHittestGrid = &InHittestGrid; }
	int32 GetCachedMaxLayerId() const { return CachedMaxLayerId; }

	SLATECORE_API bool ProcessInvalidation();

	SLATECORE_API void ClearAllFastPathData(bool bClearResourcesImmediately);

	virtual void OnRootInvalidated() { }

private:
	FSlateInvalidationWidgetList& GetFastPathWidgetList() { return *FastWidgetPathList; }
	void HandleInvalidateAllWidgets(bool bClearResourcesImmediately);

	bool PaintFastPath(const FSlateInvalidationContext& Context);

	/** Call to notify that the ordering of children below this Widget has changed and the fast path is no longer valid. */
	void InvalidateWidgetChildOrder(TSharedRef<SWidget> Widget);
	void ProcessChildOrderUpdate();
	void BuildFastPathWidgetList(TSharedRef<SWidget> RootWidget);

	void AdjustWidgetsDesktopGeometry(FVector2D WindowToDesktopTransform);

private:
	/** List of all the Widget included by this SlateInvalidationRoot. */
	FSlateInvalidationWidgetList* FastWidgetPathList;
	/** Index to widgets which are dirty, volatile, or need some sort of per frame update (such as a tick or timer) */
	FSlateInvalidationWidgetHeap* WidgetsNeedingUpdate;
	/** Index to widgets that will be updated. */
	TArray<FSlateInvalidationWidgetIndex> FinalUpdateList;
	/** Widget that has ChildOrder invalidation. */
	TArray<TWeakPtr<SWidget>> WidgetsNeedingChildOrderUpdate;

	FSlateCachedElementData* CachedElementData;

	SWidget* InvalidationRootWidget;

	FHittestGrid* RootHittestGrid;

	/**
	 * The purpose of this number is as a unique Id for all widget proxy handles to validate themselves.
	 * As widgets are added and removed, all their children are indirectly added or remove so it is necessary to invalidate all their handles.
	 * Bumping the generation number is an efficient way to do this compared to iterating all the handles
	 * The generation number is always incrementing
	 */
	int32 FastPathGenerationNumber;

	int32 CachedMaxLayerId;

	FSlateInvalidationRootHandle InvalidationRootHandle;

	bool bChildOrderInvalidated;
	bool bNeedsSlowPath;
	bool bNeedScreenPositionShift;
	bool bProcessingChildOrderUpdate;

#if WITH_SLATE_DEBUGGING
	ESlateInvalidationPaintType LastPaintType;
	uint32 ProcessInvalidationFrameNumber;
#endif
#if UE_SLATE_DEBUGGING_CLEAR_ALL_FAST_PATH_DATA
	TArray<const SWidget*> FastWidgetPathToClearedBecauseOfDelay;
#endif

	static TArray<FSlateInvalidationRoot*> ClearUpdateList;
};
