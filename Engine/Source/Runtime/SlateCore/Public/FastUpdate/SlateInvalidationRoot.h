// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WidgetProxy.h"
#include "FastUpdate/SlateInvalidationRootHandle.h"
#include "Rendering/DrawElements.h"

struct FSlateCachedElementData;
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

	FHittestGrid* GetHittestGrid() const { return RootHittestGrid; }

	/** FGCObject interface */
	SLATECORE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	SLATECORE_API virtual FString GetReferencerName() const override;

	SLATECORE_API void InvalidateRoot(const SWidget* Investigator = nullptr);

	/** Call to notify that the ordering of children somewhere in the hierarchy below this root has changed and the fast path is no longer valid */
	SLATECORE_API void InvalidateChildOrder(const SWidget* Investigator = nullptr);

	SLATECORE_API void InvalidateScreenPosition(const SWidget* Investigator = nullptr);

	bool NeedsSlowPath() const { return bNeedsSlowPath; }

	void RemoveWidgetFromFastPath(FWidgetProxy& Proxy);

	/** @return the cached draw elements for this window and its widget hierarchy*/
	FSlateCachedElementData& GetCachedElements() { return *CachedElementData; }
	const FSlateCachedElementData& GetCachedElements() const { return *CachedElementData; }
	const SWidget* GetInvalidationRootWidget() const;
	int32 GetFastPathGenerationNumber() const { return FastPathGenerationNumber; }
	FSlateInvalidationRootHandle GetInvalidationRootHandle() const { return InvalidationRootHandle; }

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
	virtual int32 PaintSlowPath(const FSlateInvalidationContext& Context) = 0;

	void SetInvalidationRootWidget(SWidget& InInvalidationRootWidget) { InvalidationRootWidget = &InInvalidationRootWidget; }
	void SetInvalidationRootHittestGrid(FHittestGrid& InHittestGrid) { RootHittestGrid = &InHittestGrid; }
	int32 GetCachedMaxLayerId() const { return CachedMaxLayerId; }

	SLATECORE_API bool ProcessInvalidation();

	SLATECORE_API void ClearAllFastPathData(bool bClearResourcesImmediately);

private:
	void HandleInvalidateAllWidgets(bool bClearResourcesImmediately);
protected:
	virtual void OnRootInvalidated() { }
private:
	bool PaintFastPath(const FSlateInvalidationContext& Context);

	void BuildFastPathList(SWidget* RootWidget);
	bool BuildNewFastPathList_Recursive(FSlateInvalidationRoot& Root, FWidgetProxy& Proxy, int32 ParentIndex, int32& NextTreeIndex, TArray<FWidgetProxy>& CurrentFastPathList, TArray<FWidgetProxy, TMemStackAllocator<>>& NewFastPathList);

	void AdjustWidgetsDesktopGeometry(FVector2D WindowToDesktopTransform);

private:
	TArray<FWidgetProxy> FastWidgetPathList;
	/** Index to widgets which are dirty, volatile, or need some sort of per frame update (such as a tick or timer) */
	FWidgetUpdateList WidgetsNeedingUpdate;

	TArray<int32, TInlineAllocator<100>> FinalUpdateList;

	FSlateCachedElementData* CachedElementData;

	SWidget* InvalidationRootWidget;

	FHittestGrid* RootHittestGrid;

	/**
	 * The purpose of this number is as a unique Id for all widget proxy handles to validate themselves.
	 * As widgets are added and removed, all their children are indirectly added or remove so it is necessary to invalidate all their handles. Bumping the generation number is an efficient way to do this compared to iterating all the handles
	 * The generation number is always incrementing
	 */
	int32 FastPathGenerationNumber;

	int32 CachedMaxLayerId;

	FSlateInvalidationRootHandle InvalidationRootHandle;

	bool bChildOrderInvalidated;
	bool bNeedsSlowPath;
	bool bNeedScreenPositionShift;

#if WITH_SLATE_DEBUGGING
	ESlateInvalidationPaintType LastPaintType;
#endif
#if UE_SLATE_DEBUGGING_CLEAR_ALL_FAST_PATH_DATA
	TArray<const SWidget*> FastWidgetPathToClearedBecauseOfDelay;
#endif

	static TArray<FSlateInvalidationRoot*> ClearUpdateList;
};
