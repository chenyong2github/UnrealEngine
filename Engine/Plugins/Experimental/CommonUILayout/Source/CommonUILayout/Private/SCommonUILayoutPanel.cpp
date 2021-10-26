// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCommonUILayoutPanel.h"
#include "CommonUILayoutManager.h"
#include "CommonUILayout.h"
#include "CommonUILayoutLog.h"
#include "CommonUILayoutZOrder.h"

#include "Blueprint/UserWidget.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameplayTagContainer.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/LayoutUtils.h"
#include "SlateSettings.h"
#include "Types/PaintArgs.h"
#include "Widgets/Layout/SSafeZone.h"

SCommonUILayoutPanel::SCommonUILayoutPanel()
	: Children(this)
{
	SetCanTick(false);
	bCanSupportFocus = false;
}

SCommonUILayoutPanel::~SCommonUILayoutPanel()
{
	if (FStreamableHandle* StreamingHandlePtr = StreamingHandle.Get())
	{
		StreamingHandlePtr->CancelHandle();
	}

	ClearChildren();
}

void SCommonUILayoutPanel::Construct(const SCommonUILayoutPanel::FArguments& InArgs)
{
	AssociatedWorld = InArgs._AssociatedWorld;

	Children.Reserve(InArgs._Slots.Num());
	for (const FCommonUILayoutPanelSlot::FSlotArguments& Arg : InArgs._Slots)
	{
		FCommonUILayoutPanelSlot* Slot = Arg.GetSlot();
		ChildrenMap.Add(FCommonUILayoutPanelInfo(Slot->WidgetClass, Slot->UniqueID, Slot->ZOrder, Slot->bIsUsingSafeZone), Slot);
		Children.AddSlot(MoveTemp(const_cast<FCommonUILayoutPanelSlot::FSlotArguments&>(Arg)));
	}
}

void SCommonUILayoutPanel::ClearChildren()
{
	if (Children.Num())
	{
		Children.Empty();
		ChildrenMap.Empty();
		Invalidate(EInvalidateWidget::ChildOrder);
	}
}

void SCommonUILayoutPanel::RefreshChildren(const TArray<TObjectPtr<const UCommonUILayout>>& Layouts)
{
	// RefreshChildren can be called multiple times in one frame.
	// (ie: A layout is removed and a new one is pushed)
	// So we defer to the end of the frame before executing the refresh
	FScopeLock Lock(&ActiveLayoutsCriticalSection);

	// Store the latest layouts array so the next ExecuteResfreshChildren has the right list
	ActiveLayouts = Layouts;

	// No need to register a timer if one is already active
	// Note: This relies on the fact that only RefreshChildren creates an active timer
	if (!HasActiveTimers())
	{
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCommonUILayoutPanel::ExecuteRefreshChildren));
	}
}

FName SCommonUILayoutPanel::FindUniqueIDForWidget(UUserWidget* InWidget) const
{
	if (InWidget && Children.Num() > 0)
	{
		const TSharedRef<SWidget> InSlateWidget = InWidget->TakeWidget();
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			FCommonUILayoutPanelSlot& ChildSlot = Children[ChildIndex];
			const TSharedRef<SWidget>& ChildSWidget = ChildSlot.GetWidget();
			if (ChildSWidget == InSlateWidget)
			{
				return ChildSlot.UniqueID;
			}
		}
	}

	return FName();
}

TWeakObjectPtr<UUserWidget> SCommonUILayoutPanel::FindUserWidgetWithUniqueID(const TSoftClassPtr<UUserWidget>& WidgetClass, const FName& UniqueID) const
{
	FCommonUILayoutPanelInfo TargetInfo(WidgetClass, UniqueID);
	FCommonUILayoutPanelSlot* ChildSlot = ChildrenMap.FindRef(TargetInfo);
	return ChildSlot != nullptr ? ChildSlot->SpawnedWidget : nullptr;
}

void SCommonUILayoutPanel::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	LayoutChildren(AllottedGeometry.GetLocalSize());

	if (Children.Num() > 0)
	{
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			const FCommonUILayoutPanelSlot& ChildSlot = Children[ChildIndex];
			const TSharedRef<SWidget>& ChildSWidget = ChildSlot.GetWidget();
			const EVisibility ChildVisibility = ChildSWidget->GetVisibility();
			if (!ArrangedChildren.Accepts(ChildVisibility))
			{
				continue;
			}

			ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(
				ChildSWidget,
				ChildSlot.Position,
				ChildSlot.bAlwaysUseFullAllotedSize ? AllottedGeometry.GetLocalSize() : ChildSlot.GetSize()
			));
		}
	}
}

int32 SCommonUILayoutPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	SCOPED_NAMED_EVENT_TEXT("SCommonUILayoutPanel::OnPaint", FColor::Orange);

	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, ArrangedChildren);

	int32 CurrentLayerId = LayerId;
	const FPaintArgs NewArgs = Args.WithNewParent(this);
	const bool bForwardedEnabled = ShouldBeEnabled(bParentEnabled);

	for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
	{
		FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

		if (!IsChildWidgetCulled(MyCullingRect, CurWidget))
		{
			const int32 NewCurrentLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyCullingRect, OutDrawElements, ++CurrentLayerId, InWidgetStyle, bForwardedEnabled);
			CurrentLayerId = FMath::Max(CurrentLayerId, NewCurrentLayerId);
		}
	}

	while (CurrentLayerId > CurrentReservedLayerID)
	{
		CurrentReservedLayerID += LayerIDReservationRange;

		// FIXME: This is costly but required to redo the layerids since we could have overlaps
		//        with other widgets outside of DynamicHUD (ie: AthenaHUD) because the new
		//        widget added will increase the layerid internally but siblings will not,
		//        resulting in potential layerids overlap and sorting issues.
		if (const TSharedPtr<SWidget>& PinnedRootLayout = RootPanel.Pin())
		{
			if (SWidget* PinnedRootLayoutPtr = PinnedRootLayout.Get())
			{
				PinnedRootLayoutPtr->GetParentWidget()->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}

	return CurrentReservedLayerID;
}

FVector2D SCommonUILayoutPanel::ComputeDesiredSize(float) const
{
	FVector2D FinalDesiredSize(0.0f, 0.0f);

	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const FCommonUILayoutPanelSlot& Child = Children[ChildIndex];
		const TSharedRef<SWidget>& Widget = Child.GetWidget();
		const EVisibility ChildVisibilty = Widget->GetVisibility();

		// As long as the widgets are not collapsed, they should contribute to the desired size.
		if (ChildVisibilty != EVisibility::Collapsed)
		{
			FVector2D ChildDesiredSize = Child.GetWidget()->GetDesiredSize();
			FinalDesiredSize.X = FMath::Max(FinalDesiredSize.X, ChildDesiredSize.X);
			FinalDesiredSize.Y = FMath::Max(FinalDesiredSize.Y, ChildDesiredSize.Y);
		}
	}

	return FinalDesiredSize;
}

EActiveTimerReturnType SCommonUILayoutPanel::ExecuteRefreshChildren(double InCurrentTime, float InDeltaTime)
{
	SCOPED_NAMED_EVENT_TEXT("SCommonUILayoutPanel::ExecuteRefreshChildren", FColor::Purple);

	// Start by gathering the unallowed list
	TArray<TSoftClassPtr<UUserWidget>> UnallowedWidgetsAll;
	TMultiMap<TSoftClassPtr<UUserWidget>, FName /*UniqueID*/> UnallowedWidgetsIDs;
	for (const UCommonUILayout* Layout : ActiveLayouts)
	{
		if (Layout)
		{
			for (const FCommonUILayoutWidgetUnallowed& Unallowed : Layout->UnallowedWidgets)
			{
				// The ALL unallow flag overrides all the ID based unallow
				if (Unallowed.bIncludeAll)
				{
					UnallowedWidgetsIDs.Remove(Unallowed.Widget);
					UnallowedWidgetsAll.AddUnique(Unallowed.Widget);
				}
				// No need to add an ID version if it's already unallowed in ALL
				else if (!UnallowedWidgetsAll.Contains(Unallowed.Widget))
				{
					UnallowedWidgetsIDs.AddUnique(Unallowed.Widget, Unallowed.bUseUniqueID ? Unallowed.UniqueID : FName());
				}
			}
		}
	}

	// ...continue by calculating the keep loaded & visible widgets and layout constraints lists
	TArray<FCommonUILayoutPanelInfo> KeepLoadedWidgets;
	TArray<FCommonUILayoutPanelInfo> VisibleWidgets;
	TArray<FSoftObjectPath> VisibleWidgetsPath;
	LayoutConstraints.Empty();

	for (const UCommonUILayout* Layout : ActiveLayouts)
	{
		if (Layout)
		{
			for (const FCommonUILayoutWidget& Widget : Layout->Widgets)
			{
				const TSoftClassPtr<UUserWidget>& AllowedWidget = Widget.Widget;
				const int32 AllowedZOrder = Widget.ZOrder == ECommonUILayoutZOrder::Custom ? Widget.CustomZOrder : (int32)Widget.ZOrder;
				const FName AllowedUniqueID = Widget.bIsUnique ? Widget.UniqueID : FName();
				const bool bAllowedIsUsingSafeZone = Widget.bUseSafeZone;

				if (!AllowedWidget.IsNull())
				{
					if (!UnallowedWidgetsAll.Contains(AllowedWidget) && !UnallowedWidgetsIDs.FindPair(AllowedWidget, AllowedUniqueID))
					{
						VisibleWidgets.AddUnique(FCommonUILayoutPanelInfo(AllowedWidget, AllowedUniqueID, AllowedZOrder, bAllowedIsUsingSafeZone));
						VisibleWidgetsPath.AddUnique(AllowedWidget.ToSoftObjectPath());
					}

					KeepLoadedWidgets.AddUnique(FCommonUILayoutPanelInfo(AllowedWidget, AllowedUniqueID, AllowedZOrder, bAllowedIsUsingSafeZone));
				}

				if (UCommonUILayoutConstraintBase* Constraint = Widget.LayoutConstraint)
				{
					Constraint->SetInfo(AllowedWidget, AllowedUniqueID, AssociatedWorld);
					LayoutConstraints.Add(TWeakObjectPtr<UCommonUILayoutConstraintBase>(Constraint));
				}
			}
		}
	}

	// ...continue by hiding any children that are allowed but also unallowed
	//    & removing any active children that is not allowed anymore
	bool bChangedVisibilityOnAtLeastOne = false;
	bool bRemovedAtLeastOne = false;
	for (int32 Index = 0; Index < Children.Num(); )
	{
		FCommonUILayoutPanelSlot& Child = Children[Index];
		const FCommonUILayoutPanelInfo ChildInfo(Child.WidgetClass, Child.UniqueID);
		if (KeepLoadedWidgets.Contains(ChildInfo))
		{
			SWidget& ChildSWidget = Child.GetWidget().Get();
			const bool bNewIsVisible = VisibleWidgets.Contains(ChildInfo);
			if (ChildSWidget.GetVisibility().IsVisible() != bNewIsVisible)
			{
				ChildSWidget.SetVisibility(bNewIsVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);
				bChangedVisibilityOnAtLeastOne = true;
			}
			++Index;
		}
		else
		{
			ChildrenMap.Remove(ChildInfo);
			Children.RemoveAt(Index);
			bRemovedAtLeastOne = true;
		}
	}

	if (bRemovedAtLeastOne)
	{
		Invalidate(EInvalidateWidget::ChildOrder);
	}
	else if (bChangedVisibilityOnAtLeastOne)
	{
		Invalidate(EInvalidateWidget::Visibility);
	}

	// ...make sure every allowed widgets is loaded in memory
	if (FStreamableHandle* StreamingHandlePtr = StreamingHandle.Get())
	{
		StreamingHandlePtr->CancelHandle();
	}

	FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
	StreamingHandle = StreamableManager.RequestAsyncLoad(VisibleWidgetsPath, [this, VisibleWidgets]()
	{
		if (AssociatedWorld.IsValid() && !AssociatedWorld->bIsTearingDown)
		{
			// ...add any new children
			for (const FCommonUILayoutPanelInfo& VisibleInfo : VisibleWidgets)
			{
				if (!ChildrenMap.Contains(VisibleInfo) && VisibleInfo.WidgetClass.IsValid())
				{
					UUserWidget* NewWidget = CreateWidget(AssociatedWorld.Get(), VisibleInfo.WidgetClass.Get());
					AddNewChildren(VisibleInfo, NewWidget);
				}
			}

			// ...sort the children based on Z order
			SortChildren();

			// ...finally, invalidate the panel to trigger a paint with the new children/layout
			Invalidate(EInvalidateWidget::ChildOrder);
		}
	}, FStreamableManager::AsyncLoadHighPriority);

	// We only ever need to refresh children once in a frame.
	// Next RefreshChildren will register a new active timer in a future frame.
	return EActiveTimerReturnType::Stop;
}

FCommonUILayoutPanelSlot* SCommonUILayoutPanel::AddNewChildren(const FCommonUILayoutPanelInfo& Info, UUserWidget* NewWidget)
{
	if (!NewWidget)
	{
		return nullptr;
	}

	FCommonUILayoutPanelSlot::FSlotArguments NewSlot{ MakeUnique<FCommonUILayoutPanelSlot>() };
	if (Info.bIsUsingSafeZone)
	{
		NewSlot
		[
			SNew(SSafeZone)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Visibility(EVisibility::SelfHitTestInvisible)
			[
				NewWidget->TakeWidget()
			]
		];
	}
	else
	{
		NewSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				NewWidget->TakeWidget()
			]
		];
	}

	FCommonUILayoutPanelSlot* NewSlotPtr = NewSlot.GetSlot();
	NewSlotPtr->WidgetClass = Info.WidgetClass;
	NewSlotPtr->ZOrder = Info.ZOrder;;
	NewSlotPtr->UniqueID = Info.UniqueID;
	NewSlotPtr->SpawnedWidget = NewWidget;

	ChildrenMap.Add(Info, NewSlotPtr);
	Children.AddSlot(MoveTemp(NewSlot));

	return NewSlotPtr;
}

void SCommonUILayoutPanel::SortChildren()
{
	Children.StableSort([](const FCommonUILayoutPanelSlot& LHS, const FCommonUILayoutPanelSlot& RHS)
	{
		return LHS.ZOrder < RHS.ZOrder;
	});
}

void SCommonUILayoutPanel::LayoutChildren(const FVector2D& AllottedGeometrySize) const
{
	kiwi::Solver Solver;
	for (const TWeakObjectPtr<UCommonUILayoutConstraintBase>& Constraint : LayoutConstraints)
	{
		if (Constraint.IsValid())
		{
			Constraint->AddConstraints(Solver, ChildrenMap, AllottedGeometrySize, nullptr);
		}
	}

	Solver.updateVariables();

	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		FCommonUILayoutPanelSlot& Child = Children[ChildIndex];
		if (Child.bAreConstraintsDirty)
		{
			const float Left = Child.Left.value();
			const float Top = Child.Top.value();
			Child.Position = FVector2D(Left, Top);
			Child.bAreConstraintsDirty = false;
		}
	}
}
