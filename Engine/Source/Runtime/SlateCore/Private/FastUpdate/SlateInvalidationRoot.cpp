// Copyright Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/SlateInvalidationRoot.h"
#include "FastUpdate/SlateInvalidationRootHandle.h"
#include "FastUpdate/SlateInvalidationRootList.h"
#include "FastUpdate/SlateInvalidationWidgetHeap.h"
#include "FastUpdate/SlateInvalidationWidgetList.h"
#include "FastUpdate/SlateInvalidationWidgetSortOrder.h"
#include "Async/TaskGraphInterfaces.h"
#include "Application/SlateApplicationBase.h"
#include "Widgets/SWidget.h"
#include "Input/HittestGrid.h"
#include "Layout/Children.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Trace/SlateTrace.h"
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

bool GSlateInvalidationRootVerifyWidgetList = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyWidgetList(
	TEXT("Slate.InvalidationRoot.VerifyWidgetList"),
	GSlateInvalidationRootVerifyWidgetList,
	TEXT("Each frame, verify that the updated list doesn't match a newly created list.")
);
void VerifyWidgetList(TSharedRef<SWidget> RootWidget, FSlateInvalidationRootHandle InvalidationRootHandle, FSlateInvalidationWidgetList& WidgetList);

bool GSlateInvalidationRootVerifyWidgetsIndex = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyWidgetsIndex(
	TEXT("Slate.InvalidationRoot.VerifyWidgetsIndex"),
	GSlateInvalidationRootVerifyWidgetsIndex,
	TEXT("Each frame, verify that every widgets has the correct index.")
);

bool GSlateInvalidationRootVerifyValidWidgets = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyValidWidgets(
	TEXT("Slate.InvalidationRoot.VerifyValidWidges"),
	GSlateInvalidationRootVerifyValidWidgets,
	TEXT("Each frame, verify that every WidgetProxy has a valid SWidget.")
);

bool GSlateInvalidationRootVerifyHittestGrid = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyHittestGrid(
	TEXT("Slate.InvalidationRoot.VerifyHittestGrid"),
	GSlateInvalidationRootVerifyHittestGrid,
	TEXT("Each frame, verify the hittest grid.")
);
void VerifyHittest(SWidget* InvalidationRootWidget, FSlateInvalidationWidgetList& WidgetList, FHittestGrid* HittestGrid);

bool GSlateInvalidationRootVerifyWidgetVisibility = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyVisibility(
	TEXT("Slate.InvalidationRoot.VerifyWidgetVisibility"),
	GSlateInvalidationRootVerifyWidgetVisibility,
	TEXT("Each frame, verify that the cached visibility of the widgets is properly set.")
);
void VerifyWidgetVisibility(FSlateInvalidationWidgetList& WidgetList);

bool GSlateInvalidationRootVerifyWidgetVolatile = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyWidgetVolatile(
	TEXT("Slate.InvalidationRoot.VerifyWidgetVolatile"),
	GSlateInvalidationRootVerifyWidgetVolatile,
	TEXT("Each frame, verify that volatile widgets are mark properly and are in the correct list.")
);
void VerifyWidgetVolatile(FSlateInvalidationWidgetList& WidgetList, TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList);
#endif //WITH_SLATE_DEBUGGING

#if SLATE_CSV_TRACKER
static int32 CascadeInvalidationEventAmount = 5;
FAutoConsoleVariableRef CVarCascadeInvalidationEventAmount(
	TEXT("Slate.CSV.CascadeInvalidationEventAmount"),
	CascadeInvalidationEventAmount,
	TEXT("The amount of cascaded invalidated parents before we fire a CSV event."));
#endif //SLATE_CSV_TRACKER

int32 GSlateInvalidationWidgetListMaxArrayElements = 64;
FAutoConsoleVariableRef CVarSlateInvalidationWidgetListMaxArrayElements(
	TEXT("Slate.InvalidationList.MaxArrayElements"),
	GSlateInvalidationWidgetListMaxArrayElements,
	TEXT("With Global Invalidation, the preferred size of the elements array."));

int32 GSlateInvalidationWidgetListNumberElementLeftBeforeSplitting = 40;
FAutoConsoleVariableRef CVarSlateInvalidationWidgetListNumElementLeftBeforeSplitting(
	TEXT("Slate.InvalidationList.NumberElementLeftBeforeSplitting"),
	GSlateInvalidationWidgetListNumberElementLeftBeforeSplitting,
	TEXT("With Global Invalidation, when splitting, only split the array when the number of element left is under X."));

/**
 *
 */
FSlateInvalidationRootList GSlateInvalidationRootListInstance;
TArray<FSlateInvalidationRoot*> FSlateInvalidationRoot::ClearUpdateList;

FSlateInvalidationRoot::FSlateInvalidationRoot()
	: CachedElementData(new FSlateCachedElementData)
	, InvalidationRootWidget(nullptr)
	, RootHittestGrid(nullptr)
	, FastPathGenerationNumber(INDEX_NONE)
	, CachedMaxLayerId(0)
	, bChildOrderInvalidated(false)
	, bNeedsSlowPath(true)
	, bNeedScreenPositionShift(false)
	, bProcessingChildOrderUpdate(false)
#if WITH_SLATE_DEBUGGING
	, LastPaintType(ESlateInvalidationPaintType::None)
	, ProcessInvalidationFrameNumber(0)
#endif
{
	InvalidationRootHandle = FSlateInvalidationRootHandle(GSlateInvalidationRootListInstance.AddInvalidationRoot(this));
	FSlateApplicationBase::Get().OnInvalidateAllWidgets().AddRaw(this, &FSlateInvalidationRoot::HandleInvalidateAllWidgets);

	const FSlateInvalidationWidgetList::FArguments Arg = { GSlateInvalidationWidgetListMaxArrayElements, GSlateInvalidationWidgetListNumberElementLeftBeforeSplitting };
	FastWidgetPathList = new FSlateInvalidationWidgetList(InvalidationRootHandle, Arg);
	WidgetsNeedingUpdate = new FSlateInvalidationWidgetHeap(*FastWidgetPathList);

#if WITH_SLATE_DEBUGGING
	SetLastPaintType(ESlateInvalidationPaintType::None);
#endif
}

FSlateInvalidationRoot::~FSlateInvalidationRoot()
{
	ClearAllFastPathData(true);

#if UE_SLATE_DEBUGGING_CLEAR_ALL_FAST_PATH_DATA
	ensure(FastWidgetPathToClearedBecauseOfDelay.Num() == 0);
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

	delete FastWidgetPathList;
	delete WidgetsNeedingUpdate;

	GSlateInvalidationRootListInstance.RemoveInvalidationRoot(InvalidationRootHandle.GetUniqueId());
}

void FSlateInvalidationRoot::AddReferencedObjects(FReferenceCollector& Collector)
{
	CachedElementData->AddReferencedObjects(Collector);
}

FString FSlateInvalidationRoot::GetReferencerName() const
{
	return TEXT("FSlateInvalidationRoot");
}

void FSlateInvalidationRoot::InvalidateRoot(const SWidget* Investigator)
{
	InvalidateRootChildOrder(Investigator);
}

void FSlateInvalidationRoot::InvalidateRootChildOrder(const SWidget* Investigator)
{
	// Update the generation number. This will effectively invalidate all proxy handles
	++FastPathGenerationNumber;
	InvalidateRootLayout(Investigator);
}

void FSlateInvalidationRoot::InvalidateRootLayout(const SWidget* Investigator)
{
	InvalidationRootWidget->InvalidatePrepass();
	InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::Layout);
	bNeedsSlowPath = true;

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastInvalidationRootInvalidate(InvalidationRootWidget, Investigator, ESlateDebuggingInvalidateRootReason::Root);
#endif
	UE_TRACE_SLATE_ROOT_INVALIDATED(InvalidationRootWidget, Investigator);
}

void FSlateInvalidationRoot::InvalidateWidgetChildOrder(TSharedRef<SWidget> Widget)
{
	if (!bNeedsSlowPath)
	{
		ensureAlways(bProcessingChildOrderUpdate == false);
		WidgetsNeedingChildOrderUpdate.Add(Widget);

		if (!bChildOrderInvalidated)
		{
			bChildOrderInvalidated = true;
			if (!InvalidationRootWidget->Advanced_IsWindow())
			{
				InvalidationRootWidget->InvalidatePrepass();
			}

			if (!GSlateEnableGlobalInvalidation && !InvalidationRootWidget->Advanced_IsWindow())
			{
				InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::Layout);
			}
		}

#if WITH_SLATE_DEBUGGING
		FSlateDebugging::BroadcastInvalidationRootInvalidate(InvalidationRootWidget, &Widget.Get(), ESlateDebuggingInvalidateRootReason::ChildOrder);
#endif
		UE_TRACE_SLATE_ROOT_CHILDORDER_INVALIDATED(InvalidationRootWidget, &Widget.Get());
	}
}

void FSlateInvalidationRoot::InvalidateScreenPosition(const SWidget* Investigator)
{
	bNeedScreenPositionShift = true;

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastInvalidationRootInvalidate(InvalidationRootWidget, Investigator, ESlateDebuggingInvalidateRootReason::ScreenPosition);
#endif
}

const TSharedPtr<SWidget> FSlateInvalidationRoot::GetFastPathWidgetListRoot() const
{
	return GetFastPathWidgetList().GetRoot().Pin();
}

FSlateInvalidationResult FSlateInvalidationRoot::PaintInvalidationRoot(const FSlateInvalidationContext& Context)
{
	const int32 LayerId = 0;

	check(InvalidationRootWidget);
	check(RootHittestGrid);

#if WITH_SLATE_DEBUGGING
	SetLastPaintType(ESlateInvalidationPaintType::None);
#endif

	FSlateInvalidationResult Result;

	if (Context.bAllowFastPathUpdate)
	{
		Context.WindowElementList->PushCachedElementData(*CachedElementData);
	}

	TSharedRef<SWidget> RootWidget = GetRootWidget();

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
		ClearAllFastPathData(!Context.bAllowFastPathUpdate);

		GSlateIsOnFastUpdatePath = false;
		bNeedsSlowPath = false;
		bChildOrderInvalidated = false;

		{
			if (Context.bAllowFastPathUpdate)
			{
				TGuardValue<bool> InSlowPathGuard(GSlateIsInInvalidationSlowPath, true);

				BuildFastPathWidgetList(RootWidget);
			}

			CachedMaxLayerId = PaintSlowPath(Context);
#if WITH_SLATE_DEBUGGING
			SetLastPaintType(ESlateInvalidationPaintType::Slow);
#endif
		}

		Result.bRepaintedWidgets = true;

	}
	else if (!FastWidgetPathList->IsEmpty())
	{
		// We should not have been supplied a different root than the one we generated a path to
		check(RootWidget == FastWidgetPathList->GetRoot().Pin());

		Result.bRepaintedWidgets = PaintFastPath(Context);
	}

	if (Context.bAllowFastPathUpdate)
	{
		Context.WindowElementList->PopCachedElementData();
	}

	FinalUpdateList.Reset();

#if WITH_SLATE_DEBUGGING
	if (GSlateInvalidationRootVerifyHittestGrid && Context.bAllowFastPathUpdate)
	{
		VerifyHittest(InvalidationRootWidget, GetFastPathWidgetList(), GetHittestGrid());
	}
#endif

	Result.MaxLayerIdPainted = CachedMaxLayerId;
	return Result;
}

void FSlateInvalidationRoot::OnWidgetDestroyed(const SWidget* Widget)
{
	// We need the index even if we've invalidated this root.  We need to clear out its proxy regardless
	const FSlateInvalidationWidgetIndex ProxyIndex = Widget->GetProxyHandle().GetWidgetIndex();
	if (FastWidgetPathList->IsValidIndex(ProxyIndex))
	{
		FSlateInvalidationWidgetList::InvalidationWidgetType& Proxy = (*FastWidgetPathList)[ProxyIndex];
		if (Proxy.IsSameWidget(Widget))
		{
			Proxy.ResetWidget();
		}
	}
}

void FSlateInvalidationRoot::ClearAllWidgetUpdatesPending()
{
	// Once a frame we free the FinalUpdateList, any widget still in that list are
	// Volatile widgets or widgets that need constant Update. So we put them back in the WidgetsNeedingUpdate list
	for (FSlateInvalidationRoot* Root : ClearUpdateList)
	{
		if (int32 NumUpdatePending = Root->FinalUpdateList.Num())
		{
			for (FSlateInvalidationWidgetIndex index : Root->FinalUpdateList)
			{
				FWidgetProxy& Proxy = (*Root->FastWidgetPathList)[index];
				if (EnumHasAnyFlags(Proxy.UpdateFlags, EWidgetUpdateFlags::AnyUpdate))
				{
					Root->WidgetsNeedingUpdate->PushUnique(Proxy);
				}
			}
		}
		Root->FinalUpdateList.Empty();
	}
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
				const FSlateInvalidationWidgetIndex MyIndex = FinalUpdateList[ListIndex];

				FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[MyIndex];
				SWidget* WidgetPtr = WidgetProxy.GetWidget();
				if (WidgetPtr)
				{
					if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsVolatilePaint))
					{
						UE_LOG(LogSlate, Log, TEXT("Volatile Repaint %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
					}
					else if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint))
					{
						UE_LOG(LogSlate, Log, TEXT("Repaint %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
					}
					else if (WidgetPtr->IsFastPathVisible())
					{
						if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate))
						{
							UE_LOG(LogSlate, Log, TEXT("ActiveTimer %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
						}

						if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsTick))
						{
							UE_LOG(LogSlate, Log, TEXT("Tick %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
						}
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
				const FSlateInvalidationWidgetIndex MyIndex = FinalUpdateList[ListIndex];
				FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[MyIndex];

				// Check visibility, it may have been in the update list but a parent who was also in the update list already updated it.
				SWidget* WidgetPtr = WidgetProxy.GetWidget();
				if (!WidgetProxy.bUpdatedSinceLastInvalidate && WidgetPtr && WidgetPtr->IsFastPathVisible())
				{
					bWidgetsNeededRepaint = bWidgetsNeededRepaint || EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint | EWidgetUpdateFlags::NeedsVolatilePaint);

					const int32 NewLayerId = WidgetProxy.Update(*Context.PaintArgs, *Context.WindowElementList);
					CachedMaxLayerId = FMath::Max(NewLayerId, CachedMaxLayerId);

					WidgetProxy.MarkProxyUpdatedThisFrame(*WidgetsNeedingUpdate);

					if (bNeedsSlowPath)
					{
						break;
					}
				}
			}
		}
	}

	bool bExecuteSlowPath = bNeedsSlowPath;
	if (bExecuteSlowPath)
	{
		SCOPED_NAMED_EVENT(Slate_PaintSlowPath, FColor::Red);
		CachedMaxLayerId = PaintSlowPath(Context);
	}

#if WITH_SLATE_DEBUGGING
	SetLastPaintType(bExecuteSlowPath ? ESlateInvalidationPaintType::Slow : ESlateInvalidationPaintType::Fast);
#endif

	return bWidgetsNeededRepaint;
}

void FSlateInvalidationRoot::AdjustWidgetsDesktopGeometry(FVector2D WindowToDesktopTransform)
{
	FSlateLayoutTransform WindowToDesktop(WindowToDesktopTransform);

	FastWidgetPathList->ForEachWidget([WindowToDesktopTransform, &WindowToDesktop](SWidget* Widget)
		{
			Widget->PersistentState.DesktopGeometry.AppendTransform(WindowToDesktop);
		});
}


void FSlateInvalidationRoot::ProcessChildOrderUpdate()
{
	TSharedRef<SWidget> RootWidget = GetRootWidget();

	{
		TGuardValue<bool> Tmp(bProcessingChildOrderUpdate, true);
		if (FastWidgetPathList->GetRoot().Pin() != RootWidget)
		{
			FastWidgetPathList->BuildWidgetList(RootWidget);
		}
		else
		{
			FastWidgetPathList->ProcessChildOrderInvalidation(WidgetsNeedingChildOrderUpdate);
		}

		WidgetsNeedingChildOrderUpdate.Reset();
		bChildOrderInvalidated = false;
	}

#if WITH_SLATE_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetList)
	{
		VerifyWidgetList(RootWidget, InvalidationRootHandle, GetFastPathWidgetList());
	}

	if (GSlateInvalidationRootVerifyWidgetsIndex)
	{
		ensureMsgf(FastWidgetPathList->VerifyWidgetsIndex(), TEXT("We failed to verify that every widgets has the correct index."));
	}
#endif //WITH_SLATE_DEBUGGING
}

void FSlateInvalidationRoot::BuildFastPathWidgetList(TSharedRef<SWidget> RootWidget)
{
	FastWidgetPathList->BuildWidgetList(RootWidget);
	WidgetsNeedingChildOrderUpdate.Reset();
}

bool FSlateInvalidationRoot::ProcessInvalidation()
{
	SCOPED_NAMED_EVENT(Slate_InvalidationProcessing, FColor::Blue);
	CSV_SCOPED_TIMING_STAT(Slate, InvalidationProcessing);

	bool bWidgetsNeedRepaint = false;

	if (!bNeedsSlowPath)
	{
		ensure(bChildOrderInvalidated == (WidgetsNeedingChildOrderUpdate.Num() != 0));
		if (WidgetsNeedingChildOrderUpdate.Num())
		{
			SCOPED_NAMED_EVENT(Slate_InvalidationProcessing_ChildOrder, FColor::Blue);

			FMemMark Mark(FMemStack::Get());
			TArray<SWidget*, TMemStackAllocator<>> FastPathWidgetsNeedingUpdateCache;
			FastPathWidgetsNeedingUpdateCache.Reserve(WidgetsNeedingUpdate->Num());
			TArray<SWidget*, TMemStackAllocator<>> FastPathUpdateListCache;
			FastPathUpdateListCache.Reserve(FinalUpdateList.Num());

			for (const FSlateInvalidationWidgetHeap::FElement& Element : WidgetsNeedingUpdate->GetRaw())
			{
				FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[Element.Get<0>()];
				if (SWidget* Widget = InvalidationWidget.GetWidget())
				{
					FastPathWidgetsNeedingUpdateCache.Add(Widget);
					// we will remove it soon, clear it right now to remove another loop
					InvalidationWidget.bContainedByWidgetHeap = false;
				}
			}
			const bool bSetContainedByWidgetHeap = false;
			WidgetsNeedingUpdate->Reset(bSetContainedByWidgetHeap);

			for (FSlateInvalidationWidgetIndex WidgetIndex : FinalUpdateList)
			{
				FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[WidgetIndex];
				if (SWidget* Widget = InvalidationWidget.GetWidget())
				{
					FastPathUpdateListCache.Add(Widget);
				}
			}

			ProcessChildOrderUpdate();

			for (SWidget* Widget : FastPathWidgetsNeedingUpdateCache)
			{
				if (Widget->GetProxyHandle().IsValid(Widget) && Widget->GetProxyHandle().GetInvalidationRoot() == this)
				{
					WidgetsNeedingUpdate->PushUnique(Widget->GetProxyHandle().GetWidgetIndex());
				}
			}

			for (SWidget* Widget : FastPathUpdateListCache)
			{
				if (Widget->GetProxyHandle().IsValid(Widget) && Widget->GetProxyHandle().GetInvalidationRoot() == this)
				{
					WidgetsNeedingUpdate->PushUnique(Widget->GetProxyHandle().GetWidgetIndex());
				}
			}
		}
		else if (FinalUpdateList.Num() != 0)
		{
			// Put Widget waiting for update back in WidgetsNeedingUpdate to ensure index order and just in case, Prepass need to be reexecuted.
			for (FSlateInvalidationWidgetIndex WidgetIndex : FinalUpdateList)
			{
				WidgetsNeedingUpdate->PushUnique(WidgetIndex);
			}
		}
		FinalUpdateList.Reset(WidgetsNeedingUpdate->Num());

#if WITH_SLATE_DEBUGGING
		if (GSlateInvalidationRootVerifyValidWidgets)
		{
			ensureMsgf(FastWidgetPathList->VerifyProxiesWidget(), TEXT("We failed to verify that every WidgetProxy has a valid SWidget"));
		}
#endif

#if SLATE_CSV_TRACKER
		FCsvProfiler::RecordCustomStat("Invalidate/InitialWidgets", CSV_CATEGORY_INDEX(Slate), WidgetsNeedingUpdate->Num(), ECsvCustomStatOp::Set);
		int32 Stat_TotalWidgetsInvalidated = 0;
		int32 Stat_NeedsRepaint = 0;
		int32 Stat_NeedsVolatilePaint = 0;
		int32 Stat_NeedsTick = 0;
		int32 Stat_NeedsActiveTimerUpdate = 0;
#endif

		while (WidgetsNeedingUpdate->Num() && !bNeedsSlowPath)
		{
#if SLATE_CSV_TRACKER
			Stat_TotalWidgetsInvalidated++;
#endif

			FSlateInvalidationWidgetIndex MyIndex = WidgetsNeedingUpdate->Pop();
			FinalUpdateList.Add(MyIndex);
			FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[MyIndex];

			// Reset each widgets paint state
			// Must be done before actual painting because children can repaint 
			WidgetProxy.bUpdatedSinceLastInvalidate = false;

			// Widget could be null if it was removed and we are on the slow path
			if (SWidget* WidgetPtr = WidgetProxy.GetWidget())
			{
				const bool bIsInvalidationRoot = WidgetPtr->Advanced_IsInvalidationRoot();
				if (bIsInvalidationRoot && WidgetPtr != InvalidationRootWidget)
				{
					FSlateInvalidationRoot* InvalidationRoot = const_cast<FSlateInvalidationRoot*>(WidgetPtr->Advanced_AsInvalidationRoot());
					check(InvalidationRoot);
					// Prevent reentering call
					FSlateInvalidationWidgetHeap::FScopeWidgetCannotBeAdded Guard{ *WidgetsNeedingUpdate, WidgetProxy };
					InvalidationRoot->ProcessInvalidation();
				}

#if SLATE_CSV_TRACKER
				const int32 PreviousWidgetsNeedingUpdating = WidgetsNeedingUpdate->Num();
#endif

				bWidgetsNeedRepaint |= WidgetProxy.ProcessInvalidation(*WidgetsNeedingUpdate, *FastWidgetPathList, *this);

#if SLATE_CSV_TRACKER
				const int32 CurrentWidgetsNeedingUpdating = WidgetsNeedingUpdate->Num();
				const int32 AddedWidgets = CurrentWidgetsNeedingUpdating - PreviousWidgetsNeedingUpdating;

				if (AddedWidgets >= CascadeInvalidationEventAmount)
				{
					CSV_EVENT(Slate, TEXT("Invalidated %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
				}

				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsRepaint))
				{
					Stat_NeedsRepaint++;
				}
				if (EnumHasAnyFlags(WidgetProxy.UpdateFlags, EWidgetUpdateFlags::NeedsVolatilePaint) && !WidgetPtr->Advanced_IsInvalidationRoot())
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
			}
		}
		
		WidgetsNeedingUpdate->Reset(true);

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

#if WITH_SLATE_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetVisibility && !bNeedsSlowPath)
	{
		VerifyWidgetVisibility(GetFastPathWidgetList());
	}
	if (GSlateInvalidationRootVerifyWidgetVolatile && !bNeedsSlowPath)
	{
		VerifyWidgetVolatile(GetFastPathWidgetList(), FinalUpdateList);
	}
#endif

	return bWidgetsNeedRepaint;
}

void FSlateInvalidationRoot::ClearAllFastPathData(bool bClearResourcesImmediately)
{

	FastWidgetPathList->Reset();
	FastWidgetPathList->ForEachWidget([bClearResourcesImmediately](SWidget* Widget)
		{
			Widget->PersistentState.CachedElementHandle = FSlateCachedElementsHandle::Invalid;
			if (bClearResourcesImmediately)
			{
				Widget->FastPathProxyHandle = FWidgetProxyHandle();
			}
		});

#if UE_SLATE_DEBUGGING_CLEAR_ALL_FAST_PATH_DATA
	if (!bClearResourcesImmediately)
	{
		for (const FWidgetProxy& Proxy : FastWidgetPathList)
		{
			if (SWidget* Widget = Proxy.GetWidget())
			{
				if (Widget->FastPathProxyHandle.IsValid())
				{
					FastWidgetPathToClearedBecauseOfDelay.Add(Widget);
				}
			}
		}
	}
	else
	{
		for (const FWidgetProxy& Proxy : FastWidgetPathList)
		{
			FastWidgetPathToClearedBecauseOfDelay.RemoveSingleSwap(Proxy.GetWidget());
		}
	}
#endif


	if (!FastWidgetPathList->IsEmpty())
	{
		ClearUpdateList.RemoveSingleSwap(this, false);
	}
	WidgetsNeedingUpdate->Reset(false);
	FastWidgetPathList->Empty();
	CachedElementData->Empty();
	FinalUpdateList.Empty();
}

void FSlateInvalidationRoot::HandleInvalidateAllWidgets(bool bClearResourcesImmediately)
{
	Advanced_ResetInvalidation(bClearResourcesImmediately);
	OnRootInvalidated();
}

void FSlateInvalidationRoot::Advanced_ResetInvalidation(bool bClearResourcesImmediately)
{
	InvalidateRootChildOrder();

	InvalidationRootWidget->InvalidatePrepass();

	if (bClearResourcesImmediately)
	{
		ClearAllFastPathData(true);
	}

	bNeedsSlowPath = true;
}

#if WITH_SLATE_DEBUGGING
void VerifyWidgetList(TSharedRef<SWidget> RootWidget, FSlateInvalidationRootHandle InvalidationRootHandle, FSlateInvalidationWidgetList& WidgetList)
{
	FSlateInvalidationWidgetList List(InvalidationRootHandle, FSlateInvalidationWidgetList::FArguments{ 128, 128, 1000, false });
	List.BuildWidgetList(RootWidget);
	bool bIsIdentical = (List.DeapCompare(WidgetList));
	if (!bIsIdentical)
	{
		UE_LOG(LogSlate, Log, TEXT("**-- New Build List --**"));
		List.LogWidgetsList();
		UE_LOG(LogSlate, Log, TEXT("**-- Invaliation Root List --**"));
		WidgetList.LogWidgetsList();

		ensureMsgf(false, TEXT("The updated list doesn't match a newly created list."));
	}
}

void VerifyHittest(SWidget* InvalidationRootWidget, FSlateInvalidationWidgetList& WidgetList, FHittestGrid* HittestGrid)
{
	check(InvalidationRootWidget);
	check(HittestGrid);

	ensureAlwaysMsgf(WidgetList.VerifySortOrder()
		, TEXT("The array's sort order for InvalidationRoot '%d' is not respected.")
		, *FReflectionMetaData::GetWidgetPath(InvalidationRootWidget));

	TArray<FHittestGrid::FWidgetSortData> WeakHittestGridSortDatas = HittestGrid->GetAllWidgetSortDatas();

	struct FHittestWidgetSortData
	{
		const SWidget* Widget;
		int64 PrimarySort;
		FSlateInvalidationWidgetSortOrder SecondarySort;
	};

	FMemMark Mark(FMemStack::Get());
	TArray<FHittestWidgetSortData, TMemStackAllocator<>> HittestGridSortDatas;
	HittestGridSortDatas.Reserve(WeakHittestGridSortDatas.Num());

	// Widgets need to be valid in the hittestgrid
	for (const FHittestGrid::FWidgetSortData& Data : WeakHittestGridSortDatas)
	{
		TSharedPtr<SWidget> Widget = Data.WeakWidget.Pin();
		if (ensureAlwaysMsgf(Widget, TEXT("A widget is invalid in the HittestGrid")))
		{
			FHittestWidgetSortData SortData = { Widget.Get(), Data.PrimarySort, Data.SecondarySort };
			HittestGridSortDatas.Add(MoveTemp(SortData));
		}
	}

	// The order in the WidgetList is sorted. It's not the case of the HittestGrid.

	FSlateInvalidationWidgetSortOrder PreviousSecondarySort;
	const SWidget* LastWidget = nullptr;
	WidgetList.ForEachWidget([&HittestGridSortDatas, &PreviousSecondarySort, &LastWidget](const SWidget* Widget)
		{
			if (Widget->GetVisibility().IsHitTestVisible())
			{
				// Is the widget in the hittestgrid
				const int32 FoundHittestIndex = HittestGridSortDatas.IndexOfByPredicate([Widget](const FHittestWidgetSortData& HittestGrid)
					{
						return HittestGrid.Widget == Widget;
					});
				const bool bHasFoundWidget = HittestGridSortDatas.IsValidIndex(FoundHittestIndex);
				if (!bHasFoundWidget)
				{
					return;
				}

				ensureAlwaysMsgf(Widget->GetProxyHandle().GetWidgetSortOrder() == HittestGridSortDatas[FoundHittestIndex].SecondarySort
					, TEXT("The SecondarySort of widget '%s' doesn't match the SecondarySort inside the hittestgrid.")
					, *FReflectionMetaData::GetWidgetPath(Widget));

				LastWidget = Widget;
				PreviousSecondarySort = HittestGridSortDatas[FoundHittestIndex].SecondarySort;

				HittestGridSortDatas.RemoveAtSwap(FoundHittestIndex);
			}
		});

	const int32 FoundHittestIndex = HittestGridSortDatas.IndexOfByPredicate([InvalidationRootWidget](const FHittestWidgetSortData& HittestGrid)
		{
			return HittestGrid.Widget == InvalidationRootWidget;
		});
	if (HittestGridSortDatas.IsValidIndex(FoundHittestIndex))
	{
		HittestGridSortDatas.RemoveAtSwap(FoundHittestIndex);
	}

	ensureAlwaysMsgf(HittestGridSortDatas.Num() == 0, TEXT("The hittest grid of Root '%s' has widgets that are not inside the InvalidationRoot's widget list")
		, *FReflectionMetaData::GetWidgetPath(InvalidationRootWidget));
}

void VerifyWidgetVisibility(FSlateInvalidationWidgetList& WidgetList)
{
	WidgetList.ForEachInvalidationWidget([](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			if (InvalidationWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
			{
				if (SWidget* Widget = InvalidationWidget.GetWidget())
				{
					bool bSouldBeFastPathVisible = Widget->GetVisibility().IsVisible();
					TSharedPtr<SWidget> ParentWidget = Widget->GetParentWidget();
					if (ensure(ParentWidget))
					{
						bSouldBeFastPathVisible = bSouldBeFastPathVisible && ParentWidget->IsFastPathVisible();
					}

					if (Widget->IsFastPathVisible() != bSouldBeFastPathVisible)
					{
						// It's possible that one of the parent is volatile
						if (!Widget->IsVolatile() && !Widget->IsVolatileIndirectly())
						{
							ensureMsgf(false, TEXT("Widget '%s' should be %s.")
								, *FReflectionMetaData::GetWidgetDebugInfo(Widget)
								, (bSouldBeFastPathVisible ? TEXT("visible") : TEXT("hidden")));
						}
					}

					const bool bHasValidCachedElementHandle = Widget->IsFastPathVisible() || !Widget->GetPersistentState().CachedElementHandle.HasCachedElements();
					ensureMsgf(bHasValidCachedElementHandle, TEXT("Widget '%s' has cached element and is not visibled.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));

					// Cache last frame visibility
					InvalidationWidget.bDebug_LastFrameVisible = Widget->IsFastPathVisible();
					InvalidationWidget.bDebug_LastFrameVisibleSet = true;
				}
			}
		});
}

void VerifyWidgetVolatile(FSlateInvalidationWidgetList& WidgetList, TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	SWidget* Root = WidgetList.GetRoot().Pin().Get();
	WidgetList.ForEachWidget([Root , &FinalUpdateList](SWidget* Widget)
		{
			if (Widget != Root)
			{
				{
					const bool bWasVolatile = Widget->IsVolatile();
					Widget->CacheVolatility();
					const bool bIsVolatile = Widget->IsVolatile();
					ensureMsgf(bWasVolatile == bIsVolatile, TEXT("Widget '%s' volatily changed without an invalidation.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
				}

				const TSharedPtr<const SWidget> ParentWidget = Widget->GetParentWidget();
				if (ensure(ParentWidget))
				{
					const bool bShouldBeVolatileIndirectly = ParentWidget->IsVolatileIndirectly() || ParentWidget->IsVolatile();
					ensureMsgf(Widget->IsVolatileIndirectly() == bShouldBeVolatileIndirectly, TEXT("Widget '%s' should be set as %s.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget)
						, (bShouldBeVolatileIndirectly ? TEXT("volatile indirectly") : TEXT("not volatile indirectly")));
				}

				{
					if (Widget->IsVolatile() && !Widget->IsVolatileIndirectly())
					{
						//const bool bHasUpdateFlag = Widget->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsVolatilePaint);
						//ensureMsgf(bHasUpdateFlag, TEXT("Widget '%s' is volatile but doesn't have the update flag NeedsVolatilePaint.")
						//	, *FReflectionMetaData::GetWidgetDebugInfo(Widget));

						const bool bIsContains = FinalUpdateList.Contains(Widget->GetProxyHandle().GetWidgetIndex());
						ensureMsgf(bIsContains, TEXT("Widget '%s' is volatile but is not in the update list.")
							, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					}
				}
			}
		});
}
#endif //WITH_SLATE_DEBUGGING

/**
 * 
 */
FSlateInvalidationRootHandle::FSlateInvalidationRootHandle()
	: InvalidationRoot(nullptr)
	, UniqueId(INDEX_NONE)
{

}

FSlateInvalidationRootHandle::FSlateInvalidationRootHandle(int32 InUniqueId)
	: UniqueId(InUniqueId)
{
	InvalidationRoot = GSlateInvalidationRootListInstance.GetInvalidationRoot(UniqueId);
}

FSlateInvalidationRoot* FSlateInvalidationRootHandle::GetInvalidationRoot() const
{
	return GSlateInvalidationRootListInstance.GetInvalidationRoot(UniqueId);
}

