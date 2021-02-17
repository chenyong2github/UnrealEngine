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
void VerifyWidgetsUpdateList_BeoreProcessPostUpdate(const TSharedRef<SWidget>&, FSlateInvalidationWidgetList*, FSlateInvalidationWidgetPreHeap*, FSlateInvalidationWidgetPostHeap*, TArray<FSlateInvalidationWidgetIndex>&);

bool GSlateInvalidationRootVerifySlateAttribute = false;
static FAutoConsoleVariableRef CVarSlateInvalidationRootVerifySlateAttributes(
	TEXT("Slate.InvalidationRoot.VerifySlateAttribute"),
	GSlateInvalidationRootVerifySlateAttribute,
	TEXT("Each frame, verify that the widgets that have registered attribute are correctly updated once and the list contains all the widgets.")
);
void VerifySlateAttribute_Pre(FSlateInvalidationWidgetList& FastWidgetPathList);
void VerifySlateAttribute_Post(FSlateInvalidationWidgetList const& FastWidgetPathList);

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
	 UE_NODISCARD EInvalidateWidgetReason EInvalidateWidgetReason_RemovePreUpdate(EInvalidateWidgetReason InvalidateReason)
	{
		static_assert(std::is_same<std::underlying_type_t<EInvalidateWidgetReason>, uint8>::value, "EInvalidateWidgetReason is not a uint8");
		const uint8 AnyPostUpdate = (0xFF & ~(uint8)EInvalidateWidgetReason::AttributeRegistration);
		return (EInvalidateWidgetReason)(((uint8)InvalidateReason) & AnyPostUpdate);
	}

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
	, FastPathGenerationNumber(INDEX_NONE)
	, CachedMaxLayerId(0)
	, bChildOrderInvalidated(false)
	, bNeedsSlowPath(true)
	, bNeedScreenPositionShift(false)
	, bProcessingPreUpdate(false)
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
	// Update the generation number. This will effectively invalidate all proxy handles
	++FastPathGenerationNumber;

	// Invalidate all proxy handles
	FastWidgetPathList->Reset();
	InvalidationRootWidget->InvalidatePrepass();
	InvalidationRootWidget->Invalidate(EInvalidateWidgetReason::Layout);
	bNeedsSlowPath = true;

#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastInvalidationRootInvalidate(InvalidationRootWidget, Investigator, ESlateDebuggingInvalidateRootReason::ChildOrder);
#endif
	UE_TRACE_SLATE_ROOT_CHILDORDER_INVALIDATED(InvalidationRootWidget, Investigator);
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

void FSlateInvalidationRoot::InvalidateWidget(FWidgetProxy& Proxy, EInvalidateWidgetReason InvalidateReason)
{
	ensureMsgf(bProcessingChildOrderInvalidation == false, TEXT("A widget got invalidated while building the childorder."));

	if (!bNeedsSlowPath)
	{
		Proxy.CurrentInvalidateReason |= InvalidateReason;
		if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::ChildOrder))
		{
			WidgetsNeedingPreUpdate->HeapPushUnique(Proxy);

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
		}
		else if (Slate::EInvalidateWidgetReason_HasPreUpdateFlag(InvalidateReason))
		{
			WidgetsNeedingPreUpdate->HeapPushUnique(Proxy);
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

				WidgetProxy.MarkProxyUpdatedThisFrame(*WidgetsNeedingPostUpdate);

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
		VerifySlateAttribute_Pre(*FastWidgetPathList);
	}
#endif

	TGuardValue<bool> Tmp(bProcessingPreUpdate, true);

	TSharedRef<SWidget> RootWidget = GetRootWidget();
	if (FastWidgetPathList->GetRoot().Pin() != RootWidget)
	{
		BuildFastPathWidgetList(RootWidget);

		// Add the root to the update list (to prepass and paint it)
		check(RootWidget->GetProxyHandle().IsValid(&RootWidget.Get()));
		RootWidget->InvalidatePrepass();
		InvalidateWidget(RootWidget->FastPathProxyHandle.GetProxy(), EInvalidateWidgetReason::Layout);
	}
	else
	{
		// Put Widget waiting for update back in WidgetsNeedingPostUpdate to ensure index order
		//and just in case Prepass need to be reexecuted.
		{
			for (FSlateInvalidationWidgetIndex WidgetIndex : FinalUpdateList)
			{
				FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[WidgetIndex];
				WidgetsNeedingPostUpdate->PushBackUnique(InvalidationWidget);
			}
			FinalUpdateList.Reset(WidgetsNeedingPostUpdate->Num());
		}

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
					, FSlateInvalidationWidgetPostHeap& InPostUpdate
					, FSlateInvalidationWidgetList::FWidgetAttributeIterator& InAttributeItt)
					: WidgetList(InWidgetList)
					, PreUpdate(InPreUpdate)
					, PostUpdate(InPostUpdate)
					, AttributeItt(InAttributeItt)
				{}
				virtual ~FChildOriderInvalidationCallbackImpl() = default;
				const FSlateInvalidationWidgetList& WidgetList;
				FSlateInvalidationWidgetPreHeap& PreUpdate;
				FSlateInvalidationWidgetPostHeap& PostUpdate;
				FSlateInvalidationWidgetList::FWidgetAttributeIterator& AttributeItt;
				TArray<FSlateInvalidationWidgetPreHeap::FElement*> WidgetToResort;

				virtual void PreChildRemove(const FSlateInvalidationWidgetList::FIndexRange& Range) override
				{
					// The widgets got removed from the list. There is no need to update them anymore.
					//Also, their index will not be valid after this function.
					PreUpdate.RemoveRange(Range);
					PostUpdate.RemoveRange(Range);
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

			}  ChildOriderInvalidationCallback{ *FastWidgetPathList, *WidgetsNeedingPreUpdate, *WidgetsNeedingPostUpdate, AttributeItt };

			while((AttributeItt.IsValid() || WidgetsNeedingPreUpdate->Num() > 0) && !bNeedsSlowPath)
			{
				FSlateInvalidationWidgetSortOrder AttributeSortOrder = (AttributeItt.IsValid()) ? AttributeItt.GetCurrentSortOrder() : FSlateInvalidationWidgetSortOrder::LimitMax();
				FSlateInvalidationWidgetSortOrder NeedUpdateSortOrder = (WidgetsNeedingPreUpdate->Num() > 0) ? WidgetsNeedingPreUpdate->HeapPeekElement().GetWidgetSortOrder() : FSlateInvalidationWidgetSortOrder::LimitMax();

				if (NeedUpdateSortOrder < AttributeSortOrder)
				{
					const FSlateInvalidationWidgetIndex WidgetIndex = WidgetsNeedingPreUpdate->HeapPop();
					FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[WidgetIndex];
					// It could have been destroyed since then
					if (InvalidationWidget.GetWidget())
					{
#if WITH_SLATE_DEBUGGING
						if (GSlateInvalidationRootDumpPreInvalidationList)
						{
							LogPreInvalidationItem(*FastWidgetPathList, WidgetIndex);
						}
#endif

						bool bIsInvalidationWidgetValid = true;
						if (EnumHasAnyFlags(InvalidationWidget.CurrentInvalidateReason, EInvalidateWidgetReason::ChildOrder))
						{
#if 0
						FMemMark Mark(FMemStack::Get());
						TArray<TTuple<FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder, TWeakPtr<SWidget>>, TMemStackAllocator<>> PreviousPreUpdate;
						TArray<TTuple<FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder, TWeakPtr<SWidget>>, TMemStackAllocator<>> PreviousPostUpdate;
						PreviousPreUpdate.Reserve(WidgetsNeedingPreUpdate->Num());
						PreviousPostUpdate.Reserve(WidgetsNeedingPostUpdate->Num());
						for (const auto& Element : WidgetsNeedingPreUpdate->GetRaw())
						{
							ensureMsgf(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex()), TEXT("F"));
							PreviousPreUpdate.Emplace(Element.GetWidgetIndex(), Element.GetWidgetSortOrder(), (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidgetAsShared());
						}
						for (const auto& Element : WidgetsNeedingPostUpdate->GetRaw())
						{
							ensureMsgf(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex()), TEXT("gF"));
							PreviousPostUpdate.Emplace(Element.GetWidgetIndex(), Element.GetWidgetSortOrder(), (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidgetAsShared());
						}
#endif

							TGuardValue<bool> Tasdfdmp(bProcessingChildOrderInvalidation, true);
							bIsInvalidationWidgetValid = FastWidgetPathList->ProcessChildOrderInvalidation(InvalidationWidget, ChildOriderInvalidationCallback);

							// This widget may not be valid anymore (got removed because it doesn't fulfill the requirement anymore ie. NullWidget).
						}

						if (EnumHasAnyFlags(InvalidationWidget.CurrentInvalidateReason, EInvalidateWidgetReason::AttributeRegistration) && bIsInvalidationWidgetValid)
						{
							FastWidgetPathList->ProcessAttributeRegistrationInvalidation(InvalidationWidget);
							AttributeItt.Seek(InvalidationWidget.Index);
						}

						AttributeItt.FixCurrentWidgetIndex();

						InvalidationWidget.CurrentInvalidateReason = Slate::EInvalidateWidgetReason_RemovePreUpdate(InvalidationWidget.CurrentInvalidateReason);
					}
				}
				else
				{
					FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[AttributeItt.GetCurrentIndex()];
					SWidget* WidgetPtr = InvalidationWidget.GetWidget();
					if (ensureMsgf(WidgetPtr, TEXT("Child order invalidation should have been called before we process this widget.")))
					{
						auto UpdateAttribute = [&InvalidationWidget, WidgetPtr]()
						{
#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
							if (GSlateInvalidationRootVerifySlateAttribute)
							{
								ensureMsgf(InvalidationWidget.bDebug_AttributeUpdated == false, TEXT("Attribute should only be updated once per frame."));
							}
#endif
							FSlateAttributeMetaData::UpdateAttributes(*WidgetPtr);
							InvalidationWidget.bDebug_AttributeUpdated = true;
						};

						if (WidgetPtr->IsFastPathVisible())
						{
							UpdateAttribute();
						}
						else if (InvalidationWidget.ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
						{
							// Is my parent visible, should I update the collapsed attributes
							const FSlateInvalidationWidgetList::InvalidationWidgetType& ParentInvalidationWidget = (*FastWidgetPathList)[InvalidationWidget.ParentIndex];
							if (ParentInvalidationWidget.Visibility.IsVisible() && ensure(ParentInvalidationWidget.GetWidget()) && ParentInvalidationWidget.GetWidget()->IsFastPathVisible())
							{
								FSlateAttributeMetaData::UpdateCollapsedAttributes(*WidgetPtr);
								if (WidgetPtr->IsFastPathVisible())
								{
									UpdateAttribute();
								}
							}
						}
					}
					AttributeItt.Advance();
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
		VerifySlateAttribute_Post(*FastWidgetPathList);
	}
#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
}

bool FSlateInvalidationRoot::ProcessPostUpdate()
{
	WidgetsNeedingPostUpdate->Heapify();

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
	if (GSlateInvalidationRootVerifyWidgetsUpdateList)
	{
		VerifyWidgetsUpdateList_BeoreProcessPostUpdate(GetRootWidget(), FastWidgetPathList.Get(), WidgetsNeedingPreUpdate.Get(), WidgetsNeedingPostUpdate.Get(), FinalUpdateList);
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
		FinalUpdateList.Add(WidgetIndex);
		FWidgetProxy& WidgetProxy = (*FastWidgetPathList)[WidgetIndex];

		// Reset each widgets paint state
		// Must be done before actual painting because children can repaint 
		WidgetProxy.bUpdatedSinceLastInvalidate = false;

		// Widget could be null if it was removed and we are on the slow path
		if (SWidget* WidgetPtr = WidgetProxy.GetWidget())
		{
#if WITH_SLATE_DEBUGGING
			if (GSlateInvalidationRootDumpPostInvalidationList)
			{
				LogPostInvalidationItem(*FastWidgetPathList, WidgetIndex);
			}
#endif

			const bool bIsInvalidationRoot = WidgetPtr->Advanced_IsInvalidationRoot();
			if (bIsInvalidationRoot && WidgetPtr != InvalidationRootWidget)
			{
				FSlateInvalidationRoot* InvalidationRoot = const_cast<FSlateInvalidationRoot*>(WidgetPtr->Advanced_AsInvalidationRoot());
				check(InvalidationRoot);
				// Prevent reentering call
				FSlateInvalidationWidgetPostHeap::FScopeWidgetCannotBeAdded Guard{ *WidgetsNeedingPostUpdate, WidgetProxy };
				InvalidationRoot->ProcessInvalidation();
			}

			bWidgetsNeedRepaint |= WidgetProxy.ProcessPostInvalidation(*WidgetsNeedingPostUpdate, *FastWidgetPathList, *this);
		}
	}
	WidgetsNeedingPostUpdate->Reset(true);

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
		check(WidgetsNeedingPostUpdate);

		ProcessPreUpdate();

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
		if (GSlateInvalidationRootVerifyValidWidgets)
		{
			ensureMsgf(FastWidgetPathList->VerifyProxiesWidget(), TEXT("We failed to verify that every WidgetProxy has a valid SWidget"));
		}
#endif //UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING

		bWidgetsNeedRepaint = ProcessPostUpdate();
	}
	else
	{
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

	InvalidationRootWidget->InvalidatePrepass();

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
	UE_LOG(LogSlate, Log, TEXT("-------------------"));

	GSlateInvalidationRootDumpUpdateListOnce = false;
}


void LogPreInvalidationItem(const FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationWidgetIndex WidgetIndex)
{
	const FWidgetProxy& Proxy = FastWidgetPathList[WidgetIndex];

	if (EnumHasAnyFlags(Proxy.CurrentInvalidateReason, EInvalidateWidgetReason::ChildOrder))
	{
		UE_LOG(LogSlate, Log, TEXT("  Child Order %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
	else
	{
		UE_LOG(LogSlate, Log, TEXT("  [?] %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
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
	else if (!EnumHasAnyFlags(Proxy.UpdateFlags, EWidgetUpdateFlags::AnyUpdate))
	{
		UE_LOG(LogSlate, Log, TEXT("  [?] %s"), *FReflectionMetaData::GetWidgetDebugInfo(Proxy.GetWidget()));
	}
}
#endif

#if UE_SLATE_WITH_INVALIDATIONWIDGETLIST_DEBUGGING
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
		if (ensureMsgf(Widget, TEXT("A widget is invalid in the HittestGrid")))
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

				ensureMsgf(Widget->GetProxyHandle().GetWidgetSortOrder() == HittestGridSortDatas[FoundHittestIndex].SecondarySort
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

	ensureMsgf(HittestGridSortDatas.Num() == 0, TEXT("The hittest grid of Root '%s' has widgets that are not inside the InvalidationRoot's widget list")
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
		ensureMsgf(FastWidgetPathList->IsValidIndex(WidgetIndex), TEXT("An element is not valid."));
		const FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[WidgetIndex];
		ensureMsgf(InvalidationWidget.CurrentInvalidateReason != EInvalidateWidgetReason::None || InvalidationWidget.UpdateFlags != EWidgetUpdateFlags::None
			, TEXT("A widget is in the update list but doesn't have an update flag set."));
	}

	ensureMsgf(WidgetsNeedingPreUpdate->IsValidHeap_Debug(), TEXT("The PreUpdate list need to stay a valid heap"));

	auto CheckIfValidInvalidation = [FastWidgetPathList](const FSlateInvalidationWidgetPreHeap::FElement& Element)
	{
		ensureMsgf(FastWidgetPathList->IsValidIndex(Element.GetWidgetIndex()), TEXT("An element is not valid."));
		const FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = (*FastWidgetPathList)[Element.GetWidgetIndex()];
		ensureMsgf(InvalidationWidget.CurrentInvalidateReason != EInvalidateWidgetReason::None || InvalidationWidget.UpdateFlags != EWidgetUpdateFlags::None
			, TEXT("A widget is in the update list but doesn't have an update flag set."));
		if (SWidget* Widget = (*FastWidgetPathList)[Element.GetWidgetIndex()].GetWidget())
		{
			ensureMsgf(Widget->GetProxyHandle().GetWidgetSortOrder() == Element.GetWidgetSortOrder(), TEXT("The sort order of the widget do not matches what is in the heap."));
			ensureMsgf(Widget->GetProxyHandle().GetWidgetIndex() == Element.GetWidgetIndex(), TEXT("The widget index of the widget do not matches what is in the heap."));
		}
	};
	WidgetsNeedingPreUpdate->ForEachIndexes(CheckIfValidInvalidation);
	WidgetsNeedingPostUpdate->ForEachIndexes(CheckIfValidInvalidation);

	FastWidgetPathList->ForEachInvalidationWidget([WidgetsNeedingPreUpdate, WidgetsNeedingPostUpdate](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			ensureMsgf(WidgetsNeedingPreUpdate->Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPreHeap
				, TEXT("A widget is or is not in the PreUpdate but the flag say otherwise."));
			ensureMsgf(WidgetsNeedingPostUpdate->Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPostHeap
				, TEXT("A widget is or is not in the PostUpdate but the flag say otherwise."));
		});
}

void VerifyWidgetsUpdateList_BeoreProcessPostUpdate(const TSharedRef<SWidget>& RootWidget,
	FSlateInvalidationWidgetList* FastWidgetPathList,
	FSlateInvalidationWidgetPreHeap* WidgetsNeedingPreUpdate,
	FSlateInvalidationWidgetPostHeap* WidgetsNeedingPostUpdate,
	TArray<FSlateInvalidationWidgetIndex>& FinalUpdateList)
{
	ensureMsgf(WidgetsNeedingPostUpdate->IsValidHeap_Debug(), TEXT("The PostUpdate list need to stay a valid heap"));

	VerifyWidgetsUpdateList_BeforeProcessPreUpdate(RootWidget, FastWidgetPathList, WidgetsNeedingPreUpdate, WidgetsNeedingPostUpdate, FinalUpdateList);
}

void VerifySlateAttribute_Pre(FSlateInvalidationWidgetList& FastWidgetPathList)
{
	FastWidgetPathList.ForEachInvalidationWidget([](FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
		{
			InvalidationWidget.bDebug_AttributeUpdated = false;
		});
}

void VerifySlateAttribute_Post(FSlateInvalidationWidgetList const& FastWidgetPathList)
{
	FastWidgetPathList.VerifyElementIndexList();
}


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

