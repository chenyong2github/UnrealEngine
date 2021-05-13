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
#include "Types/SlateAttributeMetaData.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(SLATECORE_API, Slate);


#if WITH_SLATE_DEBUGGING
bool GSlateInvalidationRootDumpUpdateList = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootDumpUpdateList(
	TEXT("Slate.InvalidationRoot.DumpUpdateList"),
	GSlateInvalidationRootDumpUpdateList,
	TEXT("Each frame, log the widgets that will be updated.")
);
void DumpUpdateList(const FSlateInvalidationWidgetList& FastWidgetPathList, const TArray<FSlateInvalidationWidgetIndex>&);

bool GSlateInvalidationRootDumpUpdateListOnce = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootDumpUpdateListOnce(
	TEXT("Slate.InvalidationRoot.DumpUpdateListOnce"),
	GSlateInvalidationRootDumpUpdateListOnce,
	TEXT("Log the widgets that will be updated this frame.")
);

static FAutoConsoleCommand CVarHandleDumpUpdateListCommand_Deprecated(
	TEXT("Slate.DumpUpdateList"),
	TEXT("(Deprecated) use Slate.InvalidationRoot.DumpUpdateListOnce"),
	FConsoleCommandDelegate::CreateStatic([](){ GSlateInvalidationRootDumpUpdateListOnce = true; })
);

bool GSlateInvalidationRootDumpPreInvalidationList = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootDumpPreInvalidationList(
	TEXT("Slate.InvalidationRoot.DumpPreInvalidationList"),
	GSlateInvalidationRootDumpPreInvalidationList,
	TEXT("Each frame, log the widgets that are processed in the pre update phase.")
);
void LogPreInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex);

bool GSlateInvalidationRootDumpPrepassInvalidationList = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootDumpPrepassInvalidationList(
	TEXT("Slate.InvalidationRoot.DumpPrepassInvalidationList"),
	GSlateInvalidationRootDumpPrepassInvalidationList,
	TEXT("Each frame, log the widgets that are processed in the prepass update phase.")
);
void LogPrepassInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex);

bool GSlateInvalidationRootDumpPostInvalidationList = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootDumpPostInvalidationList(
	TEXT("Slate.InvalidationRoot.DumpPostInvalidationList"),
	GSlateInvalidationRootDumpPostInvalidationList,
	TEXT("Each frame, log the widgets that are processed in the post update phase.")
);
void LogPostInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex);
#endif

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
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
	TEXT("Slate.InvalidationRoot.VerifyValidWidgets"),
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

bool GSlateInvalidationRootVerifyWidgetsUpdateList = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifyWidgetsUpdateList(
	TEXT("Slate.InvalidationRoot.VerifyWidgetUpdateList"),
	GSlateInvalidationRootVerifyWidgetsUpdateList,
	TEXT("Each frame, verify that pre and post update list contains the correct information and they are sorted.")
);
void VerifyWidgetsUpdateList_BeforeProcessPreUpdate(const TSharedRef<SWidget>&, FSlateInvalidationWidgetList*, FSlateInvalidationWidgetPreHeap*, FSlateInvalidationWidgetPostHeap*, TArray<FSlateInvalidationWidgetIndex>&);
void VerifyWidgetsUpdateList_BeforeProcessPostUpdate(const TSharedRef<SWidget>&, FSlateInvalidationWidgetList*, FSlateInvalidationWidgetPreHeap*, FSlateInvalidationWidgetPostHeap*, TArray<FSlateInvalidationWidgetIndex>&);
void VerifyWidgetsUpdateList_AfterProcessPostUpdate(const TSharedRef<SWidget>&, FSlateInvalidationWidgetList*, FSlateInvalidationWidgetPreHeap*, FSlateInvalidationWidgetPostHeap*, TArray<FSlateInvalidationWidgetIndex>&);

bool GSlateInvalidationRootVerifySlateAttribute = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifySlateAttributes(
	TEXT("Slate.InvalidationRoot.VerifySlateAttribute"),
	GSlateInvalidationRootVerifySlateAttribute,
	TEXT("Each frame, verify that the widgets that have registered attribute are correctly updated once and the list contains all the widgets.")
);
void VerifySlateAttribute_BeforeProcessPreUpdate(FSlateInvalidationWidgetList& FastWidgetPathList);
void VerifySlateAttribute_AfterProcessPreUpdate(const FSlateInvalidationWidgetList& FastWidgetPathList);

#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING



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
 namespace Slate
 {
	bool EInvalidateWidgetReason_HasPreUpdateFlag(EInvalidateWidgetReason InvalidateReason)
	{
		return EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::AttributeRegistration | EInvalidateWidgetReason::ChildOrder);
	}

	bool EInvalidateWidgetReason_HasPostUpdateFlag(EInvalidateWidgetReason InvalidateReason)
	{
		static_assert(std::is_same<std::underlying_type_t<EInvalidateWidgetReason>, uint8>::value, "EInvalidateWidgetReason is not a uint8");
		const uint8 AnyPostUpdate = (0xFF & ~(uint8)EInvalidateWidgetReason::AttributeRegistration);
		return (((uint8)InvalidateReason & AnyPostUpdate) != 0);
	}
 }


/**
 *
 */
FSlateInvalidationRootList GSlateInvalidationRootListInstance;

FSlateInvalidationRoot::FSlateInvalidationRoot()
	: CachedElementData(new FSlateCachedElementData)
	, InvalidationRootWidget(nullptr)
	, RootHittestGrid(nullptr)
	, CachedMaxLayerId(0)
	, bNeedsSlowPath(true)
	, bNeedScreenPositionShift(false)
	, bProcessingPreUpdate(false)
	, bProcessingPrepassUpdate(false)
	, bProcessingPostUpdate(false)
	, bBuildingWidgetList(false)
	, bProcessingChildOrderInvalidation(false)
#if WITH_SLATE_DEBUGGING
	, LastPaintType(ESlateInvalidationPaintType::None)
#endif
{
	InvalidationRootHandle = FSlateInvalidationRootHandle(GSlateInvalidationRootListInstance.AddInvalidationRoot(this));
	FSlateApplicationBase::Get().OnInvalidateAllWidgets().AddRaw(this, &FSlateInvalidationRoot::HandleInvalidateAllWidgets);

	const FSlateInvalidationWidgetList::FArguments Arg = { GSlateInvalidationWidgetListMaxArrayElements, GSlateInvalidationWidgetListNumberElementLeftBeforeSplitting };
	FastWidgetPathList = MakeUnique<FSlateInvalidationWidgetList>(InvalidationRootHandle, Arg);
	WidgetsNeedingPreUpdate = MakeUnique<FSlateInvalidationWidgetPreHeap>(*FastWidgetPathList);
	WidgetsNeedingPrepassUpdate = MakeUnique<FSlateInvalidationWidgetPrepassHeap>(*FastWidgetPathList);
	WidgetsNeedingPostUpdate = MakeUnique<FSlateInvalidationWidgetPostHeap>(*FastWidgetPathList);

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
	// Invalidate all proxy handles
	FastWidgetPathList->Reset();
	InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::Prepass);
	bNeedsSlowPath = true;

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastInvalidationRootInvalidate(InvalidationRootWidget, Investigator, ESlateDebuggingInvalidateRootReason::ChildOrder);
#endif
	UE_TRACE_SLATE_ROOT_CHILDORDER_INVALIDATED(InvalidationRootWidget, Investigator);
}

void FSlateInvalidationRoot::InvalidateRootLayout(const SWidget* Investigator)
{
	InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::Prepass);
	bNeedsSlowPath = true; // with the loop before it should only do one slateprepass

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastInvalidationRootInvalidate(InvalidationRootWidget, Investigator, ESlateDebuggingInvalidateRootReason::Root);
#endif
	UE_TRACE_SLATE_ROOT_INVALIDATED(InvalidationRootWidget, Investigator);
}

void FSlateInvalidationRoot::InvalidateWidget(FWidgetProxy& Proxy, EInvalidateWidgetReason InvalidateReason)
{
	ensureMsgf(bProcessingChildOrderInvalidation == false, TEXT("A widget got invalidated while building the childorder."));

	if (!bNeedsSlowPath)
	{
		Proxy.CurrentInvalidateReason |= InvalidateReason;
		if (Slate::EInvalidateWidgetReason_HasPreUpdateFlag(InvalidateReason))
		{
			WidgetsNeedingPreUpdate->HeapPushUnique(Proxy);
		}

		if (!bProcessingPrepassUpdate && EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::Prepass))
		{
			WidgetsNeedingPrepassUpdate->PushBackUnique(Proxy);
		}

		if (Slate::EInvalidateWidgetReason_HasPostUpdateFlag(InvalidateReason))
		{
			WidgetsNeedingPostUpdate->PushBackOrHeapUnique(Proxy);
		}

		{
			SWidget* WidgetPtr = Proxy.GetWidget();
#if WITH_SLATE_DEBUGGING
			FSlateDebugging::BroadcastWidgetInvalidate(WidgetPtr, nullptr, InvalidateReason);
#endif
			UE_TRACE_SLATE_WIDGET_INVALIDATED(WidgetPtr, nullptr, InvalidateReason);
		}
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

		ClearAllFastPathData(!Context.bAllowFastPathUpdate);

		GSlateIsOnFastUpdatePath = false;
		bNeedsSlowPath = false;

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

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
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

bool FSlateInvalidationRoot::PaintFastPath(const FSlateInvalidationContext& Context)
{
	SCOPED_NAMED_EVENT(SWidget_FastPathUpdate, FColor::Green);
	CSV_SCOPED_TIMING_STAT(Slate, FastPathUpdate);

	check(!bNeedsSlowPath);

	bool bWidgetsNeededRepaint = false;
	{
#if WITH_SLATE_DEBUGGING
		if (GSlateInvalidationRootDumpUpdateList || GSlateInvalidationRootDumpUpdateListOnce)
		{
			DumpUpdateList(*FastWidgetPathList, FinalUpdateList);
		}
#endif

		TGuardValue<bool> OnFastPathGuard(GSlateIsOnFastUpdatePath, true);
		FSlateInvalidationWidgetList::FIndexRange PreviousPaintedWidgetRange;

		// The update list is put in reverse order by ProcessInvalidation
		for (int32 ListIndex = FinalUpdateList.Num() - 1; ListIndex >= 0; --ListIndex)
		{
			const FSlateInvalidationWidgetIndex MyIndex = FinalUpdateList[ListIndex];
			if (PreviousPaintedWidgetRange.IsValid())
			{
				// It's already been processed by the previous draw
				if (PreviousPaintedWidgetRange.Include(FSlateInvalidationWidgetSortOrder{*FastWidgetPathList , MyIndex}))
				{
					continue;
				}
			}

			FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[MyIndex];
			SWidget* WidgetPtr = WidgetProxy.GetWidget();

			// Check visibility, it may have been in the update list but a parent who was also in the update list already updated it.
			if (WidgetProxy.Visibility.IsVisible() && WidgetPtr)
			{
				const bool bNeedPaint = WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsRepaint | EWidgetUpdateFlags::NeedsVolatilePaint);
				bWidgetsNeededRepaint = bWidgetsNeededRepaint || bNeedPaint;

				if (bNeedPaint)
				{
					PreviousPaintedWidgetRange = FSlateInvalidationWidgetList::FIndexRange(*FastWidgetPathList, WidgetProxy.Index, WidgetProxy.LeafMostChildIndex);
				}

				const int32 NewLayerId = WidgetProxy.Update(*Context.PaintArgs, *Context.WindowElementList);
				CachedMaxLayerId = FMath::Max(NewLayerId, CachedMaxLayerId);

				if (bNeedsSlowPath)
				{
					break;
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

void FSlateInvalidationRoot::BuildFastPathWidgetList(TSharedRef<SWidget> RootWidget)
{
	TGuardValue<bool> Tmp(bBuildingWidgetList, true);

	// We do not care if update are requested. We need to redo all the data.
	WidgetsNeedingPreUpdate->Reset(false);
	WidgetsNeedingPrepassUpdate->Reset(false);
	WidgetsNeedingPostUpdate->Reset(false);
	FinalUpdateList.Reset();

	// Rebuild the list and update SlateAttribute
	FastWidgetPathList->BuildWidgetList(RootWidget);
}

void FSlateInvalidationRoot::ProcessPreUpdate()
{
#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetsUpdateList)
	{
		VerifyWidgetsUpdateList_BeforeProcessPreUpdate(GetRootWidget(), FastWidgetPathList.Get(), WidgetsNeedingPreUpdate.Get(), WidgetsNeedingPostUpdate.Get(), FinalUpdateList);
	}
	if (GSlateInvalidationRootVerifySlateAttribute)
	{
		VerifySlateAttribute_BeforeProcessPreUpdate(*FastWidgetPathList);
	}
#endif

	TGuardValue<bool> Tmp(bProcessingPreUpdate, true);

	TSharedRef<SWidget> RootWidget = GetRootWidget();
	if (FastWidgetPathList->GetRoot().Pin() != RootWidget)
	{
		BuildFastPathWidgetList(RootWidget);

		// Add the root to the update list (to prepass and paint it)
		check(RootWidget->GetProxyHandle().IsValid(&RootWidget.Get()));
		WidgetsNeedingPostUpdate->Reset(true); // we can clear the post list, because all widgets will be updated
		RootWidget->Invalidate(EInvalidateWidgetReason::Prepass);
	}
	else
	{
		{
#if WITH_SLATE_DEBUGGING
			if (GSlateInvalidationRootDumpPreInvalidationList)
			{
				UE_LOG(LogSlate, Log, TEXT("Dumping Pre Invalidation List"));
				UE_LOG(LogSlate, Log, TEXT("-------------------"));
			}
#endif

			FSlateInvalidationWidgetList::FWidgetAttributeIterator AttributeItt = FastWidgetPathList->CreateWidgetAttributeIterator();

			/** */
			struct FChildOriderInvalidationCallbackImpl : FSlateInvalidationWidgetList::IProcessChildOrderInvalidationCallback
			{
				FChildOriderInvalidationCallbackImpl(
					const FSlateInvalidationWidgetList& InWidgetList
					, FSlateInvalidationWidgetPreHeap& InPreUpdate
					, FSlateInvalidationWidgetPrepassHeap& InPrepassUpdate
					, FSlateInvalidationWidgetPostHeap& InPostUpdate
					, FSlateInvalidationWidgetList::FWidgetAttributeIterator& InAttributeItt)
					: WidgetList(InWidgetList)
					, PreUpdate(InPreUpdate)
					, PrepassUpdate(InPrepassUpdate)
					, PostUpdate(InPostUpdate)
					, AttributeItt(InAttributeItt)
				{}
				virtual ~FChildOriderInvalidationCallbackImpl() = default;
				const FSlateInvalidationWidgetList& WidgetList;
				FSlateInvalidationWidgetPreHeap& PreUpdate;
				FSlateInvalidationWidgetPrepassHeap& PrepassUpdate;
				FSlateInvalidationWidgetPostHeap& PostUpdate;
				FSlateInvalidationWidgetList::FWidgetAttributeIterator& AttributeItt;
				TArray<FSlateInvalidationWidgetPreHeap::FElement*> WidgetToResort;

				virtual void PreChildRemove(const FSlateInvalidationWidgetList::FIndexRange& Range) override
				{
					// The widgets got removed from the list. There is no need to update them anymore.
					//Also, their index will not be valid after this function.
					PreUpdate.RemoveRange(Range);
					PostUpdate.RemoveRange(Range);
					PrepassUpdate.RemoveRange(Range);
					AttributeItt.PreChildRemove(Range);
				}
				using FReIndexOperation = FSlateInvalidationWidgetList::IProcessChildOrderInvalidationCallback::FReIndexOperation;
				virtual void ProxiesReIndexed(const FReIndexOperation& Operation) override
				{
					// Re-index in Pre and Post list (modify the index and the sort value)
					FChildOriderInvalidationCallbackImpl const* Self = this;
					auto ReIndexIfNeeded = [&Operation, Self](FSlateInvalidationWidgetPreHeap::FElement& Element)
					{
						if (Operation.GetRange().Include(Element.GetWidgetSortOrder()))
						{
							Element.GetWidgetIndexRef() = Operation.ReIndex(Element.GetWidgetIndex());
							Element.GetWidgetSortOrderRef() = FSlateInvalidationWidgetSortOrder(Self->WidgetList, Element.GetWidgetIndex());
						}
					};
					PreUpdate.ForEachIndexes(ReIndexIfNeeded);
					PostUpdate.ForEachIndexes(ReIndexIfNeeded);
					PrepassUpdate.ForEachIndexes(ReIndexIfNeeded);
					AttributeItt.ReIndexed(Operation);
				}
				using FReSortOperation = FSlateInvalidationWidgetList::IProcessChildOrderInvalidationCallback::FReSortOperation;
				virtual void ProxiesPreResort(const FReSortOperation& Operation) override
				{
					// The sort order value will change but the order (operator<) is still valid.
					FChildOriderInvalidationCallbackImpl* Self = this;
					auto ReSortIfNeeded = [&Operation, Self](FSlateInvalidationWidgetPreHeap::FElement& Element)
					{
						if (Operation.GetRange().Include(Element.GetWidgetSortOrder()))
						{
							Self->WidgetToResort.Add(&Element);
						}
					};
					PreUpdate.ForEachIndexes(ReSortIfNeeded);
					PostUpdate.ForEachIndexes(ReSortIfNeeded);
					PrepassUpdate.ForEachIndexes(ReSortIfNeeded);
				}
				virtual void ProxiesPostResort()
				{
					for (FSlateInvalidationWidgetPreHeap::FElement* Element : WidgetToResort)
					{
						Element->GetWidgetSortOrderRef() = FSlateInvalidationWidgetSortOrder{ WidgetList, Element->GetWidgetIndex() };
					}
					WidgetToResort.Reset();
					AttributeItt.PostResort();
				}
				virtual void ProxiesBuilt(const FSlateInvalidationWidgetList::FIndexRange& Range) override
				{
					AttributeItt.ProxiesBuilt(Range);
				}

			}  ChildOrderInvalidationCallback{ *FastWidgetPathList, *WidgetsNeedingPreUpdate, *WidgetsNeedingPrepassUpdate, *WidgetsNeedingPostUpdate, AttributeItt };

			while((AttributeItt.IsValid() || WidgetsNeedingPreUpdate->Num() > 0) && !bNeedsSlowPath)
			{
				FSlateInvalidationWidgetSortOrder AttributeSortOrder = (AttributeItt.IsValid()) ? AttributeItt.GetCurrentSortOrder() : FSlateInvalidationWidgetSortOrder::LimitMax();
				FSlateInvalidationWidgetSortOrder NeedsUpdateSortOrder = (WidgetsNeedingPreUpdate->Num() > 0) ? WidgetsNeedingPreUpdate->HeapPeekElement().GetWidgetSortOrder() : FSlateInvalidationWidgetSortOrder::LimitMax();

				if (AttributeSortOrder == FSlateInvalidationWidgetSortOrder::LimitMax() && NeedsUpdateSortOrder == FSlateInvalidationWidgetSortOrder::LimitMax())
				{
					checkf(false, TEXT("An element inside the lists has an invalid sort order. Something went wrong."));
					WidgetsNeedingPreUpdate->Reset(true);
					bNeedsSlowPath = true;
					break;
				}

				// Process in order
				//1.Invalidation AttributeRegistration of NeedsUpdate
				//2.UpdateAttributes of AttributeSortOrder
				//3.Invalidation ChildOrder of NeedsUpdateSortOrder

				if (AttributeSortOrder <= NeedsUpdateSortOrder)
				{
					// Update Attributes
					//Note the attribute may still be in the list and will get remove in next loop tick. UpdateCollapsedAttributes and UpdateExpandedAttributes won't do anything.
					FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[AttributeItt.GetCurrentIndex()];
					if (SWidget* WidgetPtr = InvalidationWidget.GetWidget())
					{
						if (!InvalidationWidget.Visibility.IsCollapseIndirectly())
						{
							// if my parent is not collapse, then update my visible state
							FSlateAttributeMetaData::UpdateOnlyVisibilityAttributes(*WidgetPtr, FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidation);
							if (!InvalidationWidget.Visibility.IsCollapsed())
							{
#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
								ensureAlwaysMsgf(!GSlateInvalidationRootVerifySlateAttribute || InvalidationWidget.bDebug_AttributeUpdated == false, TEXT("Attribute should only be updated once per frame."));
								InvalidationWidget.bDebug_AttributeUpdated = true;
#endif
								FSlateAttributeMetaData::UpdateExceptVisibilityAttributes(*WidgetPtr, FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidation);
								AttributeItt.Advance();
							}
							else
							{
								AttributeItt.AdvanceToNextSibling();
							}
						}
						else
						{
							AttributeItt.AdvanceToNextParent();
						}
					}
					else
					{
						AttributeItt.Advance();
					}
				}
				else
				{
					// Process ChildOrder invalidation.

					const FSlateInvalidationWidgetIndex WidgetIndex = WidgetsNeedingPreUpdate->HeapPeek();
					FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[WidgetIndex];
					// It could have been destroyed
					if (SWidget* WidgetPtr = InvalidationWidget.GetWidget())
					{
#if WITH_SLATE_DEBUGGING
						if (GSlateInvalidationRootDumpPreInvalidationList)
						{
							LogPreInvalidationItem(*FastWidgetPathList, WidgetIndex);
						}
#endif

						if (EnumHasAnyFlags(InvalidationWidget.CurrentInvalidateReason, EInvalidateWidgetReason::AttributeRegistration))
						{
							FastWidgetPathList->ProcessAttributeRegistrationInvalidation(InvalidationWidget);
							EnumRemoveFlags(InvalidationWidget.CurrentInvalidateReason, EInvalidateWidgetReason::AttributeRegistration);

							// This element was removed or added, seek will assign the correct widget to be ticked next.
							AttributeItt.Seek(InvalidationWidget.Index);
							if (FastWidgetPathList->ShouldBeAddedToAttributeList(WidgetPtr))
							{
								// Do we still need to update this element, if not, then remove it from the update list.
								if (!Slate::EInvalidateWidgetReason_HasPreUpdateFlag(InvalidationWidget.CurrentInvalidateReason))
								{
									WidgetsNeedingPreUpdate->HeapPopDiscard();
								}

								// We should update the attribute of this proxy before doing the ChildOrder (if any).
								continue;
							}
						}

						if (EnumHasAnyFlags(InvalidationWidget.CurrentInvalidateReason, EInvalidateWidgetReason::ChildOrder))
						{
// Uncomment to see to be able to compare the list before and after when debugging
#if 0
							FMemMark Mark(FMemStack::Get());
							TArray<TTuple<FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder, TWeakPtr<SWidget>>, TMemStackAllocator<>> PreviousPreUpdate;
							TArray<TTuple<FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder, TWeakPtr<SWidget>>, TMemStackAllocator<>> PreviousPrepassUpdate;
							TArray<TTuple<FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder, TWeakPtr<SWidget>>, TMemStackAllocator<>> PreviousPostUpdate;
							PreviousPreUpdate.Reserve(WidgetsNeedingPreUpdate->Num());
							PreviousPrepassUpdate.Reserve(WidgetsNeedingPrepassUpdate->Num());
							PreviousPostUpdate.Reserve(WidgetsNeedingPostUpdate->Num());
							for (const auto& Element : WidgetsNeedingPreUpdate->GetRaw())
							{
								ensureAlwaysMsgf(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex()), TEXT("The element is invalid"));
								PreviousPreUpdate.Emplace(Element.GetWidgetIndex(), Element.GetWidgetSortOrder(), (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidgetAsShared());
							}
							for (const auto& Element : WidgetsNeedingPrepassUpdate->GetRaw())
							{
								ensureAlwaysMsgf(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex()), TEXT("The element is invalid."));
								PreviousPrepassUpdate.Emplace(Element.GetWidgetIndex(), Element.GetWidgetSortOrder(), (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidgetAsShared());
							}
							for (const auto& Element : WidgetsNeedingPostUpdate->GetRaw())
							{
								ensureAlwaysMsgf(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex()), TEXT("The element is invalid."));
								PreviousPostUpdate.Emplace(Element.GetWidgetIndex(), Element.GetWidgetSortOrder(), (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidgetAsShared());
							}
#endif

							TGuardValue<bool> ProcessChildOrderInvalidationGuardValue(bProcessingChildOrderInvalidation, true);
							FastWidgetPathList->ProcessChildOrderInvalidation(InvalidationWidget, ChildOrderInvalidationCallback);

							// This widget may not be valid anymore (got removed because it doesn't fulfill the requirement anymore ie. NullWidget).

							AttributeItt.FixCurrentWidgetIndex();
							// We need to keep it to run the layout calculation in FWidgetProxy::ProcessPostInvalidation
							//EnumRemoveFlags(InvalidationWidget.CurrentInvalidateReason, EInvalidateWidgetReason::ChildOrder);
						}
					}
					WidgetsNeedingPreUpdate->HeapPopDiscard();
				}
			}
		}
	}


#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetList)
	{
		VerifyWidgetList(RootWidget, InvalidationRootHandle, GetFastPathWidgetList());
	}

	if (GSlateInvalidationRootVerifyWidgetsIndex)
	{
		ensureMsgf(FastWidgetPathList->VerifyWidgetsIndex(), TEXT("We failed to verify that every widgets has the correct index."));
	}
	if (GSlateInvalidationRootVerifySlateAttribute)
	{
		VerifySlateAttribute_AfterProcessPreUpdate(*FastWidgetPathList);
	}
#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
}

void FSlateInvalidationRoot::ProcessPrepassUpdate()
{
	TGuardValue<bool> Tmp(bProcessingPrepassUpdate, true);

#if WITH_SLATE_DEBUGGING
	if (GSlateInvalidationRootDumpPostInvalidationList)
	{
		UE_LOG(LogSlate, Log, TEXT("Dumping Prepass Invalidation List"));
		UE_LOG(LogSlate, Log, TEXT("-------------------"));
	}
#endif

	FSlateInvalidationWidgetList::FIndexRange PreviousInvalidationWidgetRange;

	// It update forward (smallest index to biggest )
	while (WidgetsNeedingPrepassUpdate->Num())
	{
		const FSlateInvalidationWidgetPrepassHeap::FElement WidgetElement = WidgetsNeedingPrepassUpdate->HeapPop();
		if (PreviousInvalidationWidgetRange.IsValid())
		{
			// It's already been processed by the previous slate prepass
			if (PreviousInvalidationWidgetRange.Include(WidgetElement.GetWidgetSortOrder()))
			{
				continue;
			}
		}
		FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[WidgetElement.GetWidgetIndex()];
		PreviousInvalidationWidgetRange = FSlateInvalidationWidgetList::FIndexRange(*FastWidgetPathList, WidgetProxy.Index, WidgetProxy.LeafMostChildIndex);

		// Widget could be null if it was removed and we are on the slow path
		if (SWidget* WidgetPtr = WidgetProxy.GetWidget())
		{
#if WITH_SLATE_DEBUGGING
			if (GSlateInvalidationRootDumpPrepassInvalidationList)
			{
				LogPrepassInvalidationItem(*FastWidgetPathList, WidgetElement.GetWidgetIndex());
			}
#endif

			if (!WidgetProxy.Visibility.IsCollapsed() && WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsVolatilePrepass))
			{
				WidgetPtr->MarkPrepassAsDirty();
			}
			WidgetProxy.ProcessLayoutInvalidation(*WidgetsNeedingPostUpdate, *FastWidgetPathList, *this);
		}
	}
	WidgetsNeedingPrepassUpdate->Reset(true);
}

bool FSlateInvalidationRoot::ProcessPostUpdate()
{
#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetsUpdateList)
	{
		VerifyWidgetsUpdateList_BeforeProcessPostUpdate(GetRootWidget(), FastWidgetPathList.Get(), WidgetsNeedingPreUpdate.Get(), WidgetsNeedingPostUpdate.Get(), FinalUpdateList);
	}
#endif

	TGuardValue<bool> Tmp(bProcessingPostUpdate, true);
	bool bWidgetsNeedRepaint = false;

#if WITH_SLATE_DEBUGGING
	if (GSlateInvalidationRootDumpPostInvalidationList)
	{
		UE_LOG(LogSlate, Log, TEXT("Dumping Post Invalidation List"));
		UE_LOG(LogSlate, Log, TEXT("-------------------"));
	}
#endif

	// It update backward (biggest index to smallest)
	while (WidgetsNeedingPostUpdate->Num() && !bNeedsSlowPath)
	{
		const FSlateInvalidationWidgetIndex WidgetIndex = WidgetsNeedingPostUpdate->HeapPop();
		FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[WidgetIndex];

		// Widget could be null if it was removed and we are on the slow path
		if (SWidget* WidgetPtr = WidgetProxy.GetWidget())
		{
#if WITH_SLATE_DEBUGGING
			if (GSlateInvalidationRootDumpPostInvalidationList)
			{
				LogPostInvalidationItem(*FastWidgetPathList, WidgetIndex);
			}
#endif

			bWidgetsNeedRepaint |= WidgetProxy.ProcessPostInvalidation(*WidgetsNeedingPostUpdate, *FastWidgetPathList, *this);
			if (WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::AnyUpdate) && WidgetProxy.Visibility.IsVisible())
			{
				FinalUpdateList.Add(WidgetIndex);
			}
		}
	}
	WidgetsNeedingPostUpdate->Reset(true);

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetsUpdateList && !bNeedsSlowPath)
	{
		VerifyWidgetsUpdateList_AfterProcessPostUpdate(GetRootWidget(), FastWidgetPathList.Get(), WidgetsNeedingPreUpdate.Get(), WidgetsNeedingPostUpdate.Get(), FinalUpdateList);
	}
#endif

	return bWidgetsNeedRepaint;
}

bool FSlateInvalidationRoot::ProcessInvalidation()
{
	SCOPED_NAMED_EVENT(Slate_InvalidationProcessing, FColor::Blue);
	CSV_SCOPED_TIMING_STAT(Slate, InvalidationProcessing);

	bool bWidgetsNeedRepaint = false;

	if (!bNeedsSlowPath)
	{
		check(WidgetsNeedingPreUpdate);
		check(WidgetsNeedingPrepassUpdate);
		check(WidgetsNeedingPostUpdate);

		ProcessPreUpdate();

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
		if (GSlateInvalidationRootVerifyValidWidgets)
		{
			ensureMsgf(FastWidgetPathList->VerifyProxiesWidget(), TEXT("We failed to verify that every WidgetProxy has a valid SWidget"));
		}
#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	}

	if (!bNeedsSlowPath)
	{
		// Put all widgets in the VolatileUpdate list in the WidgetsNeedingPostUpdate
		WidgetsNeedingPrepassUpdate->Heapify();
		WidgetsNeedingPostUpdate->Heapify();
		{
			for (FSlateInvalidationWidgetList::FWidgetVolatileUpdateIterator Iterator = FastWidgetPathList->CreateWidgetVolatileUpdateIterator(true);
				Iterator.IsValid();
				Iterator.Advance())
			{
				FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[Iterator.GetCurrentIndex()];
				WidgetsNeedingPostUpdate->HeapPushUnique(InvalidationWidget);
				if (InvalidationWidget.bIsVolatilePrepass)
				{
					WidgetsNeedingPrepassUpdate->HeapPushUnique(InvalidationWidget);
				}
			}
		}
	}

	if (!bNeedsSlowPath)
	{
		ProcessPrepassUpdate();
	}

	if (!bNeedsSlowPath)
	{
		FinalUpdateList.Reset(WidgetsNeedingPostUpdate->Num());
		bWidgetsNeedRepaint = ProcessPostUpdate();
	}
	
	if (bNeedsSlowPath)
	{
		WidgetsNeedingPreUpdate->Reset(true);
		WidgetsNeedingPrepassUpdate->Reset(true);
		WidgetsNeedingPostUpdate->Reset(true);
		FinalUpdateList.Reset();
		CachedElementData->Empty();
		bWidgetsNeedRepaint = true;
	}

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetVisibility && !bNeedsSlowPath)
	{
		VerifyWidgetVisibility(GetFastPathWidgetList());
	}
	if (GSlateInvalidationRootVerifyWidgetVolatile && !bNeedsSlowPath)
	{
		VerifyWidgetVolatile(GetFastPathWidgetList(), FinalUpdateList);
	}
#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING

	return bWidgetsNeedRepaint;
}

void FSlateInvalidationRoot::ClearAllFastPathData(bool bClearResourcesImmediately)
{
	FastWidgetPathList->ForEachWidget([bClearResourcesImmediately](SWidget* Widget)
		{
			Widget->PersistentState.CachedElementHandle = FSlateCachedElementsHandle::Invalid;
			if (bClearResourcesImmediately)
			{
				Widget->FastPathProxyHandle = FWidgetProxyHandle();
			}
		});
	FastWidgetPathList->Reset();

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

	WidgetsNeedingPreUpdate->Reset(false);
	WidgetsNeedingPrepassUpdate->Reset(false);
	WidgetsNeedingPostUpdate->Reset(false);
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

	if (bClearResourcesImmediately)
	{
		ClearAllFastPathData(true);
	}

	bNeedsSlowPath = true;
}

#if WITH_SLATE_DEBUGGING
void DumpUpdateList(const FSlateInvalidationWidgetList& FastWidgetPathList, const TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	UE_LOG(LogSlate, Log, TEXT("Dumping Update List"));
	UE_LOG(LogSlate, Log, TEXT("-------------------"));
	// The update list is put in reverse order 
	for (int32 ListIndex = FinalUpdateList.Num() - 1; ListIndex >= 0; --ListIndex)
	{
		const FSlateInvalidationWidgetIndex MyIndex = FinalUpdateList[ListIndex];

		const FWidgetProxy& WidgetProxy = FastWidgetPathList[MyIndex];
		const SWidget* WidgetPtr = WidgetProxy.GetWidget();
		if (WidgetProxy.Visibility.IsVisible() && WidgetPtr)
		{
			if (WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsVolatilePaint))
			{
				UE_LOG(LogSlate, Log, TEXT("Volatile Repaint %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
			}
			else if (WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsRepaint))
			{
				UE_LOG(LogSlate, Log, TEXT("Repaint %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
			}
			else
			{
				if (WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsActiveTimerUpdate))
				{
					UE_LOG(LogSlate, Log, TEXT("ActiveTimer %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
				}

				if (WidgetPtr->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsTick))
				{
					UE_LOG(LogSlate, Log, TEXT("Tick %s"), *FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr));
				}
			}
		}
	}
	UE_LOG(LogSlate, Log, TEXT("-------------------"));

	GSlateInvalidationRootDumpUpdateListOnce = false;
}


void LogPreInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex)
{
	const FWidgetProxy& Proxy = FastWidgetPathList[WidgetIndex];

	if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::AttributeRegistration))
	{
		UE_LOG(LogSlate, Log, TEXT("  AttributeRegistration %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::ChildOrder))
	{
		UE_LOG(LogSlate, Log, TEXT("  Child Order %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else
	{
		UE_LOG(LogSlate, Log, TEXT("  [?] %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
}

void LogPrepassInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex)
{
	const FWidgetProxy& Proxy = FastWidgetPathList[WidgetIndex];
	UE_LOG(LogSlate, Log, TEXT("  Prepass %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
}

void LogPostInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex)
{
	const FWidgetProxy& Proxy = FastWidgetPathList[WidgetIndex];

	if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::Layout))
	{
		UE_LOG(LogSlate, Log, TEXT("  Layout %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::Visibility))
	{
		UE_LOG(LogSlate, Log, TEXT("  Visibility %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::Volatility))
	{
		UE_LOG(LogSlate, Log, TEXT("  Volatility %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::RenderTransform))
	{
		UE_LOG(LogSlate, Log, TEXT("  RenderTransform %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::Paint))
	{
		UE_LOG(LogSlate, Log, TEXT("  Paint %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else if (!Proxy.GetWidget()->HasAnyUpdateFlags(EWidgetUpdateFlags::AnyUpdate))
	{
		UE_LOG(LogSlate, Log, TEXT("  [?] %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
}
#endif

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING

#define UE_SLATE_LOG_ERROR_IF_FALSE(Test, FlagToReset, Message, ...) \
	ensureAlwaysMsgf((Test), Message, ##__VA_ARGS__); \
	if (!(Test)) \
	{ \
		UE_LOG(LogSlate, Error, Message, ##__VA_ARGS__); \
		FlagToReset = false; \
		return; \
	}

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

		UE_SLATE_LOG_ERROR_IF_FALSE(false, GSlateInvalidationRootVerifyWidgetList, TEXT("The updated list doesn't match a newly created list."));
	}
}

void VerifyHittest(SWidget* InvalidationRootWidget, FSlateInvalidationWidgetList& WidgetList, FHittestGrid* HittestGrid)
{
	check(InvalidationRootWidget);
	check(HittestGrid);

	UE_SLATE_LOG_ERROR_IF_FALSE(WidgetList.VerifySortOrder()
		, GSlateInvalidationRootVerifyHittestGrid
		, TEXT("The array's sort order for InvalidationRoot '%s' is not respected.")
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
		UE_SLATE_LOG_ERROR_IF_FALSE(Widget, GSlateInvalidationRootVerifyHittestGrid, TEXT("A widget is invalid in the HittestGrid"));

		FHittestWidgetSortData SortData = { Widget.Get(), Data.PrimarySort, Data.SecondarySort };
		HittestGridSortDatas.Add(MoveTemp(SortData));
	}

	// The order in the WidgetList is sorted. It's not the case of the HittestGrid.

	FSlateInvalidationWidgetSortOrder PreviousSecondarySort;
	const SWidget* LastWidget = nullptr;
	WidgetList.ForEachWidget([&HittestGridSortDatas, &PreviousSecondarySort, &LastWidget](const SWidget* Widget)
		{
			if (Widget && Widget->GetVisibility().IsHitTestVisible())
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

				UE_SLATE_LOG_ERROR_IF_FALSE(Widget->GetProxyHandle().GetWidgetSortOrder() == HittestGridSortDatas[FoundHittestIndex].SecondarySort
					, GSlateInvalidationRootVerifyHittestGrid
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

	UE_SLATE_LOG_ERROR_IF_FALSE(HittestGridSortDatas.Num() == 0
		, GSlateInvalidationRootVerifyHittestGrid
		, TEXT("The hittest grid of Root '%s' has widgets that are not inside the InvalidationRoot's widget list")
		, *FReflectionMetaData::GetWidgetPath(InvalidationRootWidget));
}

void VerifyWidgetVisibility(FSlateInvalidationWidgetList& WidgetList)
{
	WidgetList.ForEachInvalidationWidget([&WidgetList](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			if (SWidget* Widget = InvalidationWidget.GetWidget())
			{
				{
					const EVisibility WidgetVisibility = Widget->GetVisibility();
					bool bParentIsVisible = true;
					bool bParentIsCollapsed = false;

					const TSharedPtr<SWidget> ParentWidget = Widget->GetParentWidget();
					if (InvalidationWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
					{
						// Confirm that we have the correct parent
						UE_SLATE_LOG_ERROR_IF_FALSE(WidgetList.IsValidIndex(InvalidationWidget.ParentIndex)
							, GSlateInvalidationRootVerifyWidgetVisibility
							, TEXT("Widget '%s' Parent index is invalid.")
							, *FReflectionMetaData::GetWidgetDebugInfo(Widget));

						const FSlateInvalidationWidgetList::InvalidationWidgetType& ParentInvalidationWidget = WidgetList[InvalidationWidget.ParentIndex];
						UE_SLATE_LOG_ERROR_IF_FALSE(ParentWidget.Get() == ParentInvalidationWidget.GetWidget()
							, GSlateInvalidationRootVerifyWidgetVisibility
							, TEXT("Widget '%s' Parent is not '%s'.")
							, *FReflectionMetaData::GetWidgetDebugInfo(Widget)
							, *FReflectionMetaData::GetWidgetDebugInfo(ParentWidget.Get()));

						bParentIsVisible = ParentInvalidationWidget.Visibility.IsVisible();
						bParentIsCollapsed = ParentInvalidationWidget.Visibility.IsCollapsed();
					}
					else
					{
						UE_SLATE_LOG_ERROR_IF_FALSE(ParentWidget == nullptr || ParentWidget->Advanced_IsInvalidationRoot()
							, GSlateInvalidationRootVerifyWidgetVisibility
							, TEXT("Widget '%s' Parent is valid and is not an invalidation root.")
							, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					}

					UE_SLATE_LOG_ERROR_IF_FALSE(InvalidationWidget.Visibility.AreAncestorsVisible() == bParentIsVisible
						, GSlateInvalidationRootVerifyWidgetVisibility
						, TEXT("Widget '%s' AreAncestorsVisible flag is wrong.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					UE_SLATE_LOG_ERROR_IF_FALSE(InvalidationWidget.Visibility.IsVisible() == (bParentIsVisible && WidgetVisibility.IsVisible())
						, GSlateInvalidationRootVerifyWidgetVisibility
						, TEXT("Widget '%s' IsVisible flag is wrong.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					UE_SLATE_LOG_ERROR_IF_FALSE(InvalidationWidget.Visibility.IsCollapsed() == bParentIsCollapsed || WidgetVisibility == EVisibility::Collapsed
						, GSlateInvalidationRootVerifyWidgetVisibility
						, TEXT("Widget '%s' IsCollapsed flag is wrong.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					UE_SLATE_LOG_ERROR_IF_FALSE(InvalidationWidget.Visibility.IsCollapseIndirectly() == bParentIsCollapsed
					, GSlateInvalidationRootVerifyWidgetVisibility
						, TEXT("Widget '%s' IsCollapseIndirectly flag is wrong.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
				}
				{
					if (!InvalidationWidget.Visibility.IsVisible())
					{
						UE_SLATE_LOG_ERROR_IF_FALSE(!Widget->GetPersistentState().CachedElementHandle.HasCachedElements()
							, GSlateInvalidationRootVerifyWidgetVisibility
							, TEXT("Widget '%s' has cached element and is not visibled.")
							, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					}
				}
				{
					// Cache last frame visibility
					InvalidationWidget.bDebug_LastFrameVisible = InvalidationWidget.Visibility.IsVisible();
					InvalidationWidget.bDebug_LastFrameVisibleSet = true;
				}
			}
		});
}

void VerifyWidgetVolatile(FSlateInvalidationWidgetList& WidgetList, TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	SWidget* Root = WidgetList.GetRoot().Pin().Get();
	WidgetList.ForEachWidget([Root, &FinalUpdateList](SWidget* Widget)
		{
			if (Widget != Root && GSlateInvalidationRootVerifyWidgetVolatile)
			{
				{
					const bool bWasVolatile = Widget->IsVolatile();
					Widget->CacheVolatility();
					const bool bIsVolatile = Widget->IsVolatile();
					UE_SLATE_LOG_ERROR_IF_FALSE(bWasVolatile == bIsVolatile
						, GSlateInvalidationRootVerifyWidgetVolatile
						, TEXT("Widget '%s' volatily changed without an invalidation.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
				}

				const TSharedPtr<const SWidget> ParentWidget = Widget->GetParentWidget();
				UE_SLATE_LOG_ERROR_IF_FALSE(ParentWidget
					, GSlateInvalidationRootVerifyWidgetVolatile
					, TEXT("Parent widget of widget '%s' is invalid.")
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget));

				{
					const bool bShouldBeVolatileIndirectly = ParentWidget->IsVolatileIndirectly() || ParentWidget->IsVolatile();
					UE_SLATE_LOG_ERROR_IF_FALSE(Widget->IsVolatileIndirectly() == bShouldBeVolatileIndirectly
					, GSlateInvalidationRootVerifyWidgetVolatile
					, TEXT("Widget '%s' should be set as %s.")
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget)
					, (bShouldBeVolatileIndirectly ? TEXT("volatile indirectly") : TEXT("not volatile indirectly")));
				}

				if (Widget->IsVolatile() && !Widget->IsVolatileIndirectly())
				{
					UE_SLATE_LOG_ERROR_IF_FALSE(Widget->HasAnyUpdateFlags(EWidgetUpdateFlags::NeedsVolatilePaint)
						, GSlateInvalidationRootVerifyWidgetVolatile
						, TEXT("Widget '%s' is volatile but doesn't have the update flag NeedsVolatilePaint.")
						, *FReflectionMetaData::GetWidgetDebugInfo(Widget));

					if (Widget->GetProxyHandle().IsValid(Widget))
					{
						const bool bIsVisible = Widget->GetProxyHandle().GetProxy().Visibility.IsVisible();
						const bool bIsContains = FinalUpdateList.Contains(Widget->GetProxyHandle().GetWidgetIndex());
						UE_SLATE_LOG_ERROR_IF_FALSE(bIsContains || !bIsVisible
							, GSlateInvalidationRootVerifyWidgetVolatile
							, TEXT("Widget '%s' is volatile but is not in the update list.")
							, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
					}
				}
			}
		});
}

void VerifyWidgetsUpdateList_BeforeProcessPreUpdate(const TSharedRef<SWidget>& RootWidget,
	FSlateInvalidationWidgetList* FastWidgetPathList,
	FSlateInvalidationWidgetPreHeap* WidgetsNeedingPreUpdate,
	FSlateInvalidationWidgetPostHeap* WidgetsNeedingPostUpdate,
	TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	if (FastWidgetPathList->GetRoot().Pin() != RootWidget)
	{
		return;
	}

	for (FSlateInvalidationWidgetIndex WidgetIndex : FinalUpdateList)
	{
		UE_SLATE_LOG_ERROR_IF_FALSE(FastWidgetPathList->IsValidIndex(WidgetIndex)
			, GSlateInvalidationRootVerifyWidgetsUpdateList
			, TEXT("A WidgetIndex is invalid. The Widget can be invalid (because it's not been processed yet)."));
	}

	UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPreUpdate->IsValidHeap_Debug()
		, GSlateInvalidationRootVerifyWidgetsUpdateList
		, TEXT("The PreUpdate list need to stay a valid heap"));

	WidgetsNeedingPreUpdate->ForEachIndexes([FastWidgetPathList](const FSlateInvalidationWidgetPreHeap::FElement& Element)
		{
			UE_SLATE_LOG_ERROR_IF_FALSE(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex())
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("An element is not valid."));
			const FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[Element.GetWidgetIndex()];
			if (SWidget* Widget = (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidget())
			{
				UE_SLATE_LOG_ERROR_IF_FALSE(Widget->GetProxyHandle().GetWidgetSortOrder() == Element.GetWidgetSortOrder()
					, GSlateInvalidationRootVerifyWidgetsUpdateList
					, TEXT("The sort order of the widget '%s' do not matches what is in the heap.")
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
				UE_SLATE_LOG_ERROR_IF_FALSE(Widget->GetProxyHandle().GetWidgetIndex() == Element.GetWidgetIndex()
					, GSlateInvalidationRootVerifyWidgetsUpdateList
					, TEXT("The widget index of the widget '%s' do not matches what is in the heap.")
					, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
			}
		});

	FastWidgetPathList->ForEachInvalidationWidget([WidgetsNeedingPreUpdate](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPreUpdate->Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPreHeap
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("Widget '%s' is or is not in the PreUpdate but the flag say otherwise.")
				, *FReflectionMetaData::GetWidgetDebugInfo(InvalidationWidget.GetWidget()));
		});
}

void VerifyWidgetsUpdateList_BeforeProcessPostUpdate(const TSharedRef<SWidget>& RootWidget,
	FSlateInvalidationWidgetList* FastWidgetPathList,
	FSlateInvalidationWidgetPreHeap* WidgetsNeedingPreUpdate,
	FSlateInvalidationWidgetPostHeap* WidgetsNeedingPostUpdate,
	TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	if (FastWidgetPathList->GetRoot().Pin() != RootWidget)
	{
		return;
	}

	UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPostUpdate->IsValidHeap_Debug()
		, GSlateInvalidationRootVerifyWidgetsUpdateList
		, TEXT("The PostUpdate list need to stay a valid heap"));

	UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPreUpdate->Num() == 0
		, GSlateInvalidationRootVerifyWidgetsUpdateList
		, TEXT("The PreUpdate list should be empty"));

	UE_SLATE_LOG_ERROR_IF_FALSE(FinalUpdateList.Num() == 0
		, GSlateInvalidationRootVerifyWidgetsUpdateList
		, TEXT("The Final Update list should be empty."));

	WidgetsNeedingPostUpdate->ForEachIndexes([FastWidgetPathList](const FSlateInvalidationWidgetPreHeap::FElement& Element)
		{
			UE_SLATE_LOG_ERROR_IF_FALSE(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex())
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("An element is not valid."));

			const FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[Element.GetWidgetIndex()];
			const SWidget* Widget = InvalidationWidget.GetWidget();

			UE_SLATE_LOG_ERROR_IF_FALSE(Widget
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("Widget should be valid (should have been cleaned by PreProcess)."));
			UE_SLATE_LOG_ERROR_IF_FALSE(Widget->GetProxyHandle().GetWidgetSortOrder() == Element.GetWidgetSortOrder()
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("The sort order of the widget '%s' do not matches what is in the heap.")
				, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
			UE_SLATE_LOG_ERROR_IF_FALSE(Widget->GetProxyHandle().GetWidgetIndex() == Element.GetWidgetIndex()
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("The widget index of the widget '%s' do not matches what is in the heap.")
				, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
		});

	FastWidgetPathList->ForEachInvalidationWidget([WidgetsNeedingPostUpdate](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPostUpdate->Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPostHeap
				, GSlateInvalidationRootVerifyWidgetsUpdateList
				, TEXT("Widget '%s' is or is not in the PostUpdate but the flag say otherwise.")
				, *FReflectionMetaData::GetWidgetDebugInfo(InvalidationWidget.GetWidget()));
		});
}

void VerifyWidgetsUpdateList_AfterProcessPostUpdate(const TSharedRef<SWidget>& RootWidget,
	FSlateInvalidationWidgetList* FastWidgetPathList,
	FSlateInvalidationWidgetPreHeap* WidgetsNeedingPreUpdate,
	FSlateInvalidationWidgetPostHeap* WidgetsNeedingPostUpdate,
	TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	if (FastWidgetPathList->GetRoot().Pin() != RootWidget)
	{
		return;
	}

	UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPreUpdate->Num() == 0
		, GSlateInvalidationRootVerifyWidgetsUpdateList
		, TEXT("The list of Pre Update should already been processed."));
	UE_SLATE_LOG_ERROR_IF_FALSE(WidgetsNeedingPostUpdate->Num() == 0
		, GSlateInvalidationRootVerifyWidgetsUpdateList
		, TEXT("The list of Post Update should already been processed."));

	for(FSlateInvalidationWidgetIndex WidgetIndex : FinalUpdateList)
	{
		const FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[WidgetIndex];
		const SWidget* Widget = InvalidationWidget.GetWidget();

		UE_SLATE_LOG_ERROR_IF_FALSE(Widget
			, GSlateInvalidationRootVerifyWidgetsUpdateList
			, TEXT("Widget should be valid (should have been cleaned by PreProcess)."));
		UE_SLATE_LOG_ERROR_IF_FALSE(InvalidationWidget.CurrentInvalidateReason == EInvalidateWidgetReason::None
			, GSlateInvalidationRootVerifyWidgetsUpdateList
			, TEXT("The widget '%s' is in the update list and it still has a Invalidation Reason.")
			, * FReflectionMetaData::GetWidgetDebugInfo(Widget));
		UE_SLATE_LOG_ERROR_IF_FALSE(Widget->HasAnyUpdateFlags(EWidgetUpdateFlags::AnyUpdate)
			, GSlateInvalidationRootVerifyWidgetsUpdateList
			, TEXT("The widget '%s' is in the update list but doesn't have an update flag set.")
			, *FReflectionMetaData::GetWidgetDebugInfo(Widget));
	}
}

void VerifySlateAttribute_BeforeProcessPreUpdate(FSlateInvalidationWidgetList& FastWidgetPathList)
{
	FastWidgetPathList.ForEachInvalidationWidget([](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			InvalidationWidget.bDebug_AttributeUpdated = false;
		});
}

void VerifySlateAttribute_AfterProcessPreUpdate(const FSlateInvalidationWidgetList& FastWidgetPathList)
{
	const bool bElementIndexListValid = FastWidgetPathList.VerifyElementIndexList();
	UE_SLATE_LOG_ERROR_IF_FALSE(bElementIndexListValid
		, GSlateInvalidationRootVerifySlateAttribute
		, TEXT("The VerifySlateAttribute failed in post."));
}

#undef UE_SLATE_LOG_ERROR_IF_FALSE
#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING

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

