// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonUILayoutPanelInfo.h"
#include "CommonUILayoutPanelSlot.h"
#include "Layout/Children.h"
#include "Layout/Geometry.h"
#include "Layout/Visibility.h"
#include "SlotBase.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SPanel.h"

class FArrangedChildren;
class FPaintArgs;
class FSlateWindowElementList;
class UCommonUILayout;
class UCommonUILayoutConstraintBase;
class UUserWidget;
struct FStreamableHandle;

class SCommonUILayoutPanel : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SCommonUILayoutPanel)
		: _AssociatedWorld(nullptr)
	{
		_Visibility = EVisibility::SelfHitTestInvisible;
	}
		SLATE_SLOT_ARGUMENT(FCommonUILayoutPanelSlot, Slots)
		SLATE_ARGUMENT(UWorld*, AssociatedWorld)
	SLATE_END_ARGS()

	SCommonUILayoutPanel();
	virtual ~SCommonUILayoutPanel();

	void Construct(const FArguments& InArgs);

	void ClearChildren();
	void RefreshChildren(const TArray<TObjectPtr<const UCommonUILayout>>& Layouts);

	FName FindUniqueIDForWidget(UUserWidget* Widget) const;
	TWeakObjectPtr<UUserWidget> FindUserWidgetWithUniqueID(const TSoftClassPtr<UUserWidget>& WidgetClass, const FName& UniqueID) const;

	// Begin SWidget overrides
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FChildren* GetChildren() override { return &Children; }
	// End SWidget overrides

protected:
	// Begin SWidget overrides.
	virtual FVector2D ComputeDesiredSize(float) const override;
	// End SWidget overrides.

	friend class UCommonUILayoutManager;
	void SetRootLayout(const TSharedPtr<SWidget>& InRootLayout) { RootPanel = InRootLayout; }

private:
	EActiveTimerReturnType ExecuteRefreshChildren(double InCurrentTime, float InDeltaTime);
	void LayoutChildren(const FVector2D& AllottedGeometrySize) const;

private:
	// TODO: Layout constraints evaluation happens during OnArrangeChildren callback which is const
	mutable TPanelChildren<FCommonUILayoutPanelSlot> Children;
	TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*> ChildrenMap;

	/** List of currently active layout constraints. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UCommonUILayoutConstraintBase>> LayoutConstraints;

	/** List of layouts that are currently active on this panel. */
	TArray<const UCommonUILayout*> ActiveLayouts;
	FCriticalSection ActiveLayoutsCriticalSection;

	struct FCommonUILayoutPreloadData
	{
		TSharedPtr<FStreamableHandle> StreamableHandle = nullptr;
	};

	TMap<const UCommonUILayout* /*Layout*/, FCommonUILayoutPreloadData /*Context*/> ActivePreloadLayouts;
	FCriticalSection PreloadLayoutsCriticalSection;

	TSharedPtr<FStreamableHandle> StreamingHandle;
	TWeakObjectPtr<UWorld> AssociatedWorld;
	TWeakPtr<SWidget> RootPanel;

	// Size of the layer ids reservation range. This range is applied to the layer id returned in the
	// paint function when the current max reserved layer id is reached.
	const int32 LayerIDReservationRange = 5000;
	mutable int32 CurrentReservedLayerID = 0;
};