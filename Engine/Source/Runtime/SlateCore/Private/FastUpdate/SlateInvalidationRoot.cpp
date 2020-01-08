// Copyright Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/SlateInvalidationRoot.h"
#include "Async/TaskGraphInterfaces.h"
#include "Application/SlateApplicationBase.h"
#include "Widgets/SWidget.h"
#include "Layout/Children.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Types/ReflectionMetadata.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(SLATECORE_API, Slate);

#if WITH_SLATE_DEBUGGING
bool GDumpUpdateList = false;
void HandleDumpUpdateList(const TArray<FString>& Args)
{
	GDumpUpdateList = true;
}

static FAutoConsoleCommand HandleDumpUpdateListCommand(
	TEXT("Slate.DumpUpdateList"),
	TEXT(""),
	FConsoleCommandWithArgsDelegate::CreateStatic(&HandleDumpUpdateList)
);
#endif

#if SLATE_CSV_TRACKER
	static int32 CascadeInvalidationEventAmount = 5;
	FAutoConsoleVariableRef CVarCascadeInvalidationEventAmount(
		TEXT("Slate.CSV.CascadeInvalidationEventAmount"),
		CascadeInvalidationEventAmount,
		TEXT("The amount of cascaded invalidated parents before we fire a CSV event."));
#endif

FSlateInvalidationRoot::FSlateInvalidationRoot()
	: CachedElementData(new FSlateCachedElementData)
	, InvalidationRootWidget(nullptr)
	, RootHittestGrid(nullptr)
	, FastPathGenerationNumber(INDEX_NONE)
	, bChildOrderInvalidated(false)
	, bNeedsSlowPath(true)
	, bNeedScreenPositionShift(false)
{
	FSlateApplicationBase::Get().OnInvalidateAllWidgets().AddRaw(this, &FSlateInvalidationRoot::OnInvalidateAllWidgets);

}

FSlateInvalidationRoot::~FSlateInvalidationRoot()
{
	ClearAllFastPathData(true);

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::ClearInvalidatedWidgets(*this);
#endif

	if (FSlateApplicationBase::IsInitialized())
	{
		FSlateApplicationBase::Get().OnInvalidateAllWidgets().RemoveAll(this);

		FSlateApplicationBase::Get().GetRenderer()->DestroyCachedFastPathElementData(CachedElementData);
	}
	else
	{
		delete CachedElementData;
	}

	CachedElementData = nullptr;
}

void FSlateInvalidationRoot::AddReferencedObjects(FReferenceCollector& Collector)
{
	CachedElementData->AddReferencedObjects(Collector);
}

FString FSlateInvalidationRoot::GetReferencerName() const
{
	static FString ReferencerName(TEXT("FSlateInvalidationRoot"));
	return ReferencerName;
}

void FSlateInvalidationRoot::InvalidateChildOrder()
{
	//if (GSlateEnableGlobalInvalidation)
	{
		if(!bNeedsSlowPath && !bChildOrderInvalidated)
		{
			//UE_LOG(LogSlate, Log, TEXT("Child order invalidated, Slow path needed"));
			//bNeedsSlowPath = true;
			bChildOrderInvalidated = true;
			if(!InvalidationRootWidget->Advanced_IsWindow())
			{
				InvalidationRootWidget->InvalidatePrepass();
			}

			if (!GSlateEnableGlobalInvalidation  && !InvalidationRootWidget->Advanced_IsWindow())
			{
				//InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::ChildOrder);
				InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::Layout);
			}

#if WITH_SLATE_DEBUGGING
			if (GSlateInvalidationDebugging && FastWidgetPathList.Num())
			{
				FSlateDebugging::WidgetInvalidated(*this, FastWidgetPathList[0], &FLinearColor::Red);
			}
#endif
		}
	}
}

void FSlateInvalidationRoot::InvalidateScreenPosition()
{
	bNeedScreenPositionShift = true;
}

int32 RecursiveFindParentWithChildOrderChange(const TArray<FWidgetProxy>& FastWidgetPathList, const FWidgetProxy& Proxy)
{
	if (Proxy.bChildOrderInvalid)
	{
		return Proxy.Index;
	}
	else if (Proxy.ParentIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	else
	{
		return RecursiveFindParentWithChildOrderChange(FastWidgetPathList, FastWidgetPathList[Proxy.ParentIndex]);
	}
}

void FSlateInvalidationRoot::RemoveWidgetFromFastPath(FWidgetProxy& Proxy)
{
	if (Proxy.Widget->PersistentState.CachedElementHandle.IsValid())
	{
		Proxy.Widget->PersistentState.CachedElementHandle.RemoveFromCache();
	}

	if (Proxy.Index == 0)
	{
		InvalidateRoot();
	}
	else
	{
		InvalidateChildOrder();
	}

	Proxy.Widget->FastPathProxyHandle = FWidgetProxyHandle();
	Proxy.Widget = nullptr;
}

void FSlateInvalidationRoot::InvalidateRoot()
{
	// Update the generation number.  This will effectively invalidate all proxy handles
	++FastPathGenerationNumber;

	InvalidationRootWidget->InvalidatePrepass();

	bNeedsSlowPath = true;
}

FSlateInvalidationResult FSlateInvalidationRoot::PaintInvalidationRoot(const FSlateInvalidationContext& Context)
{
	const int32 LayerId = 0;

	check(InvalidationRootWidget);
	check(RootHittestGrid);

	FSlateInvalidationResult Result;

	if (Context.bAllowFastPathUpdate)
	{
		Context.WindowElementList->PushCachedElementData(*CachedElementData);
	}

	SWidget* RootWidget = InvalidationRootWidget->Advanced_IsWindow() ? InvalidationRootWidget : &(*InvalidationRootWidget->GetAllChildren()->GetChildAt(0));

	if (bNeedScreenPositionShift)
	{
		SCOPED_NAMED_EVENT(Slate_InvalidateScreenPosition, FColor::Red);
		AdjustWidgetsDesktopGeometry(Context.PaintArgs->GetWindowToDesktopTransform());
		bNeedScreenPositionShift = false;
	}

	EFlowDirection NewFlowDirection = GSlateFlowDirection;
	if (RootWidget->GetFlowDirectionPreference() == EFlowDirectionPreference::Inherit)
	{
		NewFlowDirection = GSlateFlowDirectionShouldFollowCultureByDefault ? FLayoutLocalization::GetLocalizedLayoutDirection() : EFlowDirection::LeftToRight;
	}
	TGuardValue<EFlowDirection> FlowGuard(GSlateFlowDirection, NewFlowDirection);
	if (!Context.bAllowFastPathUpdate || bNeedsSlowPath || GSlateIsInInvalidationSlowPath)
	{
		SCOPED_NAMED_EVENT(Slate_PaintSlowPath, FColor::Red);

		//CSV_EVENT(Basic, "Slate Slow Path update");
#if WITH_SLATE_DEBUGGING
		FSlateDebugging::ClearInvalidatedWidgets(*this);
#endif
		ClearAllFastPathData(false);

		GSlateIsOnFastUpdatePath = false;
		bNeedsSlowPath = false;
		bChildOrderInvalidated = false;

		{
			if (Context.bAllowFastPathUpdate)
			{
				TGuardValue<bool> InSlowPathGuard(GSlateIsInInvalidationSlowPath, true);

				BuildFastPathList(RootWidget);

				if(GSlateEnableGlobalInvalidation)
				{
					InvalidationRootWidget->SlatePrepass(Context.LayoutScaleMultiplier);
				}
			}

			CachedMaxLayerId = PaintSlowPath(Context);

		}

		Result.bRepaintedWidgets = true;

	}
	else if (FastWidgetPathList.Num())
	{
		// We should not have been supplied a different root than the one we generated a path to
		check(RootWidget == FastWidgetPathList[0].Widget);

		Result.bRepaintedWidgets = PaintFastPath(Context);
	}

	if (Context.bAllowFastPathUpdate)
	{
		Context.WindowElementList->PopCachedElementData();

#if WITH_SLATE_DEBUGGING
		if (GSlateInvalidationDebugging)
		{
			if (!GSlateEnableGlobalInvalidation)
			{
				FSlateDebugging::DrawInvalidationRoot(*InvalidationRootWidget, CachedMaxLayerId, *Context.WindowElementList);
			}

			FSlateDebugging::DrawInvalidatedWidgets(*this, *Context.PaintArgs, *Context.WindowElementList);
		}
#endif
	}

	FinalUpdateList.Empty();

	Result.MaxLayerIdPainted = CachedMaxLayerId;
	return Result;
}

void FSlateInvalidationRoot::OnWidgetDestroyed(const SWidget* Widget)
{
	InvalidateChildOrder();

	// We need the index even if we've invalidated this root.  We need to clear out its proxy regardless
	const bool bEvenIfInvalid = true;
	const int32 ProxyIndex = Widget->FastPathProxyHandle.GetIndex(bEvenIfInvalid);
	if (FastWidgetPathList.IsValidIndex(ProxyIndex) && FastWidgetPathList[ProxyIndex].Widget == Widget)
	{
		FastWidgetPathList[ProxyIndex].Widget = nullptr;
	
	}
		
	Widget->PersistentState.CachedElementHandle.RemoveFromCache();
}

bool FSlateInvalidationRoot::PaintFastPath(const FSlateInvalidationContext& Context)
{
	SCOPED_NAMED_EVENT(SWidget_FastPathUpdate, FColor::Green);
	CSV_SCOPED_TIMING_STAT(Slate, FastPathUpdate);

	check(!bNeedsSlowPath);

	bool bWidgetsNeededRepaint = false;
	{
		TGuardValue<bool> OnFastPathGuard(GSlateIsOnFastUpdatePath, true);

		int32 LastParentIndex = 0;

#if WITH_SLATE_DEBUGGING
		if (GDumpUpdateList)
		{
			UE_LOG(LogSlate, Log, TEXT("Dumping Update List"));

			// The update list is put in reverse order 
			for (int32 ListIndex = FinalUpdateList.Num() - 1; ListIndex >= 0; --ListIndex)
			{
				const int32 MyIndex = FinalUpdateList[ListIndex];

				FWidgetProxy& WidgetProxy = FastWidgetPathList[MyIndex];

				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint | EWidgetUpdateFlags::NeedsVolatilePaint))
				{
					UE_LOG(LogSlate, Log, TEXT("Repaint %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetProxy.Widget));
				}
				else if (!WidgetProxy.bInvisibleDueToParentOrSelfVisibility)
				{
					if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate))
					{
						UE_LOG(LogSlate, Log, TEXT("ActiveTimer %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetProxy.Widget));
					}

					if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsTick))
					{
						UE_LOG(LogSlate, Log, TEXT("Tick %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetProxy.Widget));
					}
				}
			}

			GDumpUpdateList = false;
		}
#endif

		{
			// The update list is put in reverse order by ProcessInvalidation
			for (int32 ListIndex = FinalUpdateList.Num() - 1; ListIndex >= 0; --ListIndex)
			{
				const int32 MyIndex = FinalUpdateList[ListIndex];
				FWidgetProxy& WidgetProxy = FastWidgetPathList[MyIndex];

				// Check visibility, it may have been in the update list but a parent who was also in the update list already updated it.
				if (!WidgetProxy.bInvisibleDueToParentOrSelfVisibility && !WidgetProxy.bUpdatedSinceLastInvalidate && ensure(WidgetProxy.Widget))
				{
					if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint | EWidgetUpdateFlags::NeedsVolatilePaint))
					{
						bWidgetsNeededRepaint = true;
					}

					const int32 NewLayerId = WidgetProxy.Update(*Context.PaintArgs, MyIndex, *Context.WindowElementList);
					CachedMaxLayerId = FMath::Max(NewLayerId, CachedMaxLayerId);

					FWidgetProxy::MarkProxyUpdatedThisFrame(WidgetProxy, WidgetsNeedingUpdate);

					if (bNeedsSlowPath)
					{
						break;
					}
				}
			}
		}
	}

	if (bNeedsSlowPath)
	{
		SCOPED_NAMED_EVENT(Slate_PaintSlowPath, FColor::Red);
		CachedMaxLayerId = PaintSlowPath(Context);
	}

	return bWidgetsNeededRepaint;
}

void FSlateInvalidationRoot::BuildNewFastPathList_Recursive(FSlateInvalidationRoot& Root, FWidgetProxy& Proxy, int32 ParentIndex, int32& NextTreeIndex, TArray<FWidgetProxy>& CurrentFastPathList, TArray<FWidgetProxy, TMemStackAllocator<>>& NewFastPathList)
{
	if (Proxy.bChildOrderInvalid)
	{
		NextTreeIndex = Proxy.LeafMostChildIndex != INDEX_NONE ? Proxy.LeafMostChildIndex + 1 : NextTreeIndex + 1;
		Proxy.Widget->AssignIndicesToChildren(*this, ParentIndex, NewFastPathList, !Proxy.bInvisibleDueToParentOrSelfVisibility, Proxy.Widget->IsVolatileIndirectly());
	}
	else
	{ 
		const int32 PrevIndex = Proxy.Index;
		const int32 PrevParentIndex = Proxy.ParentIndex;
		Proxy.Index = NewFastPathList.Num();
		Proxy.ParentIndex = ParentIndex;
		//if (PrevIndex != Proxy.Index)
		{
			// Update the proxy handle
			Proxy.Widget->FastPathProxyHandle = FWidgetProxyHandle(Root, Proxy.Index);
		}

		 NewFastPathList.Add(MoveTemp(Proxy));

		for (int32 LocalChildIndex = 0; LocalChildIndex < Proxy.NumChildren; ++LocalChildIndex)
		{
			FWidgetProxy& ChildProxy = CurrentFastPathList[NextTreeIndex];

			if (!ChildProxy.bChildOrderInvalid)
			{
				++NextTreeIndex;
			}

			BuildNewFastPathList_Recursive(Root, ChildProxy, Proxy.Index, NextTreeIndex, CurrentFastPathList, NewFastPathList);
		}

		{
			FWidgetProxy& MyProxyRef = NewFastPathList[Proxy.Index];
			int32 LastIndex = NewFastPathList.Num() - 1;
			MyProxyRef.LeafMostChildIndex = LastIndex != Proxy.Index ? LastIndex : INDEX_NONE;
		}
	}
	
}

void FSlateInvalidationRoot::AdjustWidgetsDesktopGeometry(FVector2D WindowToDesktopTransform)
{
	FSlateLayoutTransform WindowToDesktop(WindowToDesktopTransform);

	for (FWidgetProxy& Proxy : FastWidgetPathList)
	{
		if (Proxy.Widget)
		{
			Proxy.Widget->PersistentState.DesktopGeometry = Proxy.Widget->PersistentState.AllottedGeometry;
			Proxy.Widget->PersistentState.DesktopGeometry.AppendTransform(WindowToDesktop);
		}
	}
}

#define VERIFY_CHILD_ORDER 0
void FSlateInvalidationRoot::BuildFastPathList(SWidget* RootWidget)
{
	SCOPED_NAMED_EVENT_TEXT("AssignFastPathIndices", FColor::Magenta);

	TSharedPtr<SWidget> Parent = RootWidget->GetParentWidget();
	// If the widget has no parent it is likely a window
	const bool bParentVisible = Parent.IsValid() ? Parent->GetVisibility().IsVisible() : true;
	const bool bParentVolatile = false;

	FMemMark Mark(FMemStack::Get());
	{
		// Update the generation number.  This will effectively invalidate all proxy handles
		++FastPathGenerationNumber;

		WidgetsNeedingUpdate.Empty();


#if VERIFY_CHILD_ORDER
		TArray<FWidgetProxy, TMemStackAllocator<>> Copy;
		Copy.Reserve(FastWidgetPathList.Num());
		RootWidget->AssignIndicesToChildren(*this, INDEX_NONE, Copy, bParentVisible, bParentVolatile);

		// Update the generation number.  This will effectively invalidate all proxy handles
		//++FastPathGenerationNumber;
#endif
		TArray<FWidgetProxy, TMemStackAllocator<>> TempList;
		TempList.Reserve(FastWidgetPathList.Num());

		if (FastWidgetPathList.Num() > 0)
		{
			int32 NextTreeIndex = 1;
			BuildNewFastPathList_Recursive(*this, FastWidgetPathList[0], INDEX_NONE, NextTreeIndex, FastWidgetPathList, TempList);
		}
		else
		{
			RootWidget->AssignIndicesToChildren(*this, INDEX_NONE, TempList, bParentVisible, bParentVolatile);
		}

#if VERIFY_CHILD_ORDER
		for (int i = 0; i < Copy.Num(); ++i)
		{
			ensureAlways(Copy[i].Widget == TempList[i].Widget);
		}
#endif
		FastWidgetPathList = MoveTemp(TempList);
	}
}

bool FSlateInvalidationRoot::ProcessInvalidation()
{
	SCOPED_NAMED_EVENT(Slate_InvalidationProcessing, FColor::Blue);
	CSV_SCOPED_TIMING_STAT(Slate, InvalidationProcessing);

	FinalUpdateList.Empty();

	bool bWidgetsNeedRepaint = false;

	if (!bNeedsSlowPath)
	{
		if (bChildOrderInvalidated)
		{
			SCOPED_NAMED_EVENT(Slate_InvalidationProcessing_SortChildren, FColor::Orange);

			struct FWidgetNeedingUpdate
			{
				FWidgetNeedingUpdate(SWidget* InWidget, EInvalidateWidgetReason InCurrentInvalidateReason, EWidgetUpdateFlags InUpdateFlags)
					: Widget(InWidget)
					, CurrentInvalidateReason(InCurrentInvalidateReason)
					, UpdateFlags(InUpdateFlags)
				{}

				SWidget* Widget;
				EInvalidateWidgetReason CurrentInvalidateReason;
				EWidgetUpdateFlags UpdateFlags;
			};

			// We need to store off all widgets needing update as the child order change may invalidate them all
			TArray<FWidgetNeedingUpdate, TMemStackAllocator<>> WidgetsNeedingUpdateCache;
			WidgetsNeedingUpdateCache.Reserve(WidgetsNeedingUpdate.Num());

			for(int32 WidgetIndex : WidgetsNeedingUpdate.GetRawData())
			{
				FWidgetProxy& WidgetProxy = FastWidgetPathList[WidgetIndex];
				if (WidgetProxy.Widget)
				{
					check(WidgetProxy.Widget->FastPathProxyHandle.GetInvalidationRoot() == this);
					WidgetsNeedingUpdateCache.Emplace(WidgetProxy.Widget, WidgetProxy.CurrentInvalidateReason, WidgetProxy.UpdateFlags);
				}
			}

			SWidget* RootWidget = InvalidationRootWidget->Advanced_IsWindow() ? InvalidationRootWidget : &(*InvalidationRootWidget->GetAllChildren()->GetChildAt(0));
			BuildFastPathList(RootWidget);

			for (FWidgetNeedingUpdate& WidgetNeedingUpdate : WidgetsNeedingUpdateCache)
			{
				const int32 NewIndex = WidgetNeedingUpdate.Widget->FastPathProxyHandle.GetIndex();
				FWidgetProxy& WidgetProxy = FastWidgetPathList[NewIndex];
				check(WidgetProxy.Widget == WidgetNeedingUpdate.Widget);
				WidgetProxy.CurrentInvalidateReason = WidgetNeedingUpdate.CurrentInvalidateReason;
				WidgetProxy.UpdateFlags = WidgetNeedingUpdate.UpdateFlags;
				WidgetProxy.bInUpdateList = true;
				WidgetsNeedingUpdate.Push(WidgetProxy);
			}

			bChildOrderInvalidated = false;
		}

#if SLATE_CSV_TRACKER
		FCsvProfiler::RecordCustomStat("Invalidate/InitialWidgets", CSV_CATEGORY_INDEX(Slate), WidgetsNeedingUpdate.Num(), ECsvCustomStatOp::Set);
		int32 Stat_TotalWidgetsInvalidated = 0;
		int32 Stat_NeedsRepaint = 0;
		int32 Stat_NeedsVolatilePaint = 0;
		int32 Stat_NeedsTick = 0;
		int32 Stat_NeedsActiveTimerUpdate = 0;
#endif

		while (WidgetsNeedingUpdate.Num() && !bNeedsSlowPath)
		{
#if SLATE_CSV_TRACKER
			Stat_TotalWidgetsInvalidated++;
#endif

			int32 MyIndex = WidgetsNeedingUpdate.Pop();
			FinalUpdateList.Add(MyIndex);
			FWidgetProxy& WidgetProxy = FastWidgetPathList[MyIndex];

			// Reset each widgets paint state
			// Must be done before actual painting because children can repaint 
			WidgetProxy.bUpdatedSinceLastInvalidate = false;
			WidgetProxy.bInUpdateList = false;

			// Widget could be null if it was removed and we are on the slow path
			if (WidgetProxy.Widget)
			{
				if (!GSlateEnableGlobalInvalidation && !InvalidationRootWidget->NeedsPrepass() && WidgetProxy.Widget->Advanced_IsInvalidationRoot())
				{
					WidgetProxy.CurrentInvalidateReason |= EInvalidateWidget::Layout;
				}
				
#if SLATE_CSV_TRACKER
				const int32 PreviousWidgetsNeedingUpdating = WidgetsNeedingUpdate.Num();
#endif

				bWidgetsNeedRepaint |= WidgetProxy.ProcessInvalidation(WidgetsNeedingUpdate, FastWidgetPathList, *this);

#if SLATE_CSV_TRACKER
				const int32 CurrentWidgetsNeedingUpdating = WidgetsNeedingUpdate.Num();
				const int32 AddedWidgets = CurrentWidgetsNeedingUpdating - PreviousWidgetsNeedingUpdating;

				if (AddedWidgets >= CascadeInvalidationEventAmount)
				{
					CSV_EVENT(Slate, TEXT("%s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetProxy.Widget));
				}

				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint))
				{
					Stat_NeedsRepaint++;
				}
				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsVolatilePaint) && !WidgetProxy.Widget->Advanced_IsInvalidationRoot())
				{
					Stat_NeedsVolatilePaint++;
				}
				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsTick))
				{
					Stat_NeedsTick++;
				}
				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate))
				{
					Stat_NeedsActiveTimerUpdate++;
				}
#endif

#if WITH_SLATE_DEBUGGING
				if (GSlateInvalidationDebugging)
				{
					if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint))
					{
						FSlateDebugging::WidgetInvalidated(*this, WidgetProxy);
					}
					else if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsVolatilePaint) && !WidgetProxy.Widget->Advanced_IsInvalidationRoot())
					{
						FSlateDebugging::WidgetInvalidated(*this, WidgetProxy, &FLinearColor::Blue);
					}
					else if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsTick))
					{
						FSlateDebugging::WidgetInvalidated(*this, WidgetProxy, &FLinearColor::White);
					}
					else if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate))
					{
						FSlateDebugging::WidgetInvalidated(*this, WidgetProxy, &FLinearColor::Green);
					}
				}
#endif
			}
		}

		WidgetsNeedingUpdate.Reset();

#if SLATE_CSV_TRACKER
		FCsvProfiler::RecordCustomStat("Invalidate/TotalWidgets", CSV_CATEGORY_INDEX(Slate), Stat_TotalWidgetsInvalidated, ECsvCustomStatOp::Set);
		FCsvProfiler::RecordCustomStat("Invalidate/NeedsRepaint", CSV_CATEGORY_INDEX(Slate), Stat_NeedsRepaint, ECsvCustomStatOp::Set);
		FCsvProfiler::RecordCustomStat("Invalidate/NeedsVolatilePaint", CSV_CATEGORY_INDEX(Slate), Stat_NeedsVolatilePaint, ECsvCustomStatOp::Set);
		FCsvProfiler::RecordCustomStat("Invalidate/NeedsTick", CSV_CATEGORY_INDEX(Slate), Stat_NeedsTick, ECsvCustomStatOp::Set);
		FCsvProfiler::RecordCustomStat("Invalidate/NeedsActiveTimerUpdate", CSV_CATEGORY_INDEX(Slate), Stat_NeedsActiveTimerUpdate, ECsvCustomStatOp::Set);
#endif
	}
	else
	{
		bWidgetsNeedRepaint = true;
	}

	return bWidgetsNeedRepaint;
}

void FSlateInvalidationRoot::ClearAllFastPathData(bool bClearResourcesImmediately)
{
	for (const FWidgetProxy& Proxy : FastWidgetPathList)
	{
		if (Proxy.Widget)
		{
			Proxy.Widget->PersistentState.CachedElementHandle = FSlateCachedElementsHandle::Invalid;
			if (bClearResourcesImmediately)
			{
				Proxy.Widget->FastPathProxyHandle = FWidgetProxyHandle();
			}
		}
	}

	FastWidgetPathList.Empty();
	WidgetsNeedingUpdate.Empty();
	CachedElementData->Empty();
	FinalUpdateList.Empty();
}

void FSlateInvalidationRoot::OnInvalidateAllWidgets(bool bClearResourcesImmediately)
{
	InvalidateChildOrder();

	InvalidationRootWidget->InvalidatePrepass();

	if (bClearResourcesImmediately)
	{
		ClearAllFastPathData(true);
	}
	bNeedsSlowPath = true;
}
