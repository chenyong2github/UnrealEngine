// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WidgetProxy.h"
#include "Rendering/DrawElements.h"

struct FSlateCachedElementData;
class FSlateWindowElementList;
class FWidgetStyle;

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

class SLATECORE_VTABLE FSlateInvalidationRoot : public FGCObject, public FNoncopyable
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


	SLATECORE_API void InvalidateRoot();

	/** Call to notify that the ordering of children somewhere in the hierarchy below this root has changed and the fast path is no longer valid */
	SLATECORE_API void InvalidateChildOrder();

	SLATECORE_API void InvalidateScreenPosition();

	bool NeedsSlowPath() const { return bNeedsSlowPath; }

	void RemoveWidgetFromFastPath(FWidgetProxy& Proxy);

	/** @return the cached draw elements for this window and its widget hierarchy*/
	FSlateCachedElementData& GetCachedElements()
	{
		return *CachedElementData;
	}

	int32 GetFastPathGenerationNumber() const { return FastPathGenerationNumber; }

	SLATECORE_API FSlateInvalidationResult PaintInvalidationRoot(const FSlateInvalidationContext& Context);

	void OnWidgetDestroyed(const SWidget* Widget);
protected:
	virtual int32 PaintSlowPath(const FSlateInvalidationContext& Context) = 0;

	void SetInvalidationRootWidget(SWidget& InInvalidationRootWidget) { InvalidationRootWidget = &InInvalidationRootWidget; }
	void SetInvalidationRootHittestGrid(FHittestGrid& InHittestGrid) { RootHittestGrid = &InHittestGrid; }

	SLATECORE_API bool ProcessInvalidation();

	SLATECORE_API void ClearAllFastPathData(bool bClearResourcesImmediately);

private:

	void OnInvalidateAllWidgets(bool bClearResourcesImmediately);

	bool PaintFastPath(const FSlateInvalidationContext& Context);

	void BuildFastPathList(SWidget* RootWidget);
	void BuildNewFastPathList_Recursive(FSlateInvalidationRoot& Root, FWidgetProxy& Proxy, int32 ParentIndex, int32& NextTreeIndex, TArray<FWidgetProxy>& CurrentFastPathList, TArray<FWidgetProxy, TMemStackAllocator<>>& NewFastPathList);

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

	bool bChildOrderInvalidated;
	bool bNeedsSlowPath;
	bool bNeedScreenPositionShift;
};
