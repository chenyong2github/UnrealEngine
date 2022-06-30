// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakInterfacePtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Layout/ArrangedChildren.h"
#include "SWorldPartitionEditorGrid.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"

class SWorldPartitionEditorGrid2D : public SWorldPartitionEditorGrid
{
protected:
	class FEditorCommands : public TCommands<FEditorCommands>
	{
	public:
		FEditorCommands();
	
		TSharedPtr<FUICommandInfo> CreateRegionFromSelection;
		TSharedPtr<FUICommandInfo> LoadSelectedRegions;
		TSharedPtr<FUICommandInfo> UnloadSelectedRegions;
		TSharedPtr<FUICommandInfo> UnloadHoveredRegion;
		TSharedPtr<FUICommandInfo> ConvertSelectedRegionsToActors;
		TSharedPtr<FUICommandInfo> MoveCameraHere;

		/**
		 * Initialize commands
		 */
		virtual void RegisterCommands() override;
	};

public:
	SWorldPartitionEditorGrid2D();
	~SWorldPartitionEditorGrid2D();

	void Construct(const FArguments& InArgs);

	virtual void CreateRegionFromSelection();
	virtual void LoadSelectedRegions();
	virtual void UnloadSelectedRegions();
	virtual void ConvertSelectedRegionsToActors();
	void MoveCameraHere();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual int32 PaintGrid(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	virtual uint32 PaintActors(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const;
	virtual uint32 PaintScaleRuler(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const;
	virtual uint32 PaintViewer(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const;
	virtual uint32 PaintSelection(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const;
	virtual int32 PaintSoftwareCursor(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	virtual FReply FocusSelection();
protected:
	void UpdateTransform() const;
	void UpdateSelectionBox();
	void ClearSelection();

	const TSharedRef<FUICommandList> CommandList;

	FSingleWidgetChildrenWithBasicLayoutSlot ChildSlot;

	FChildren* GetChildren()
	{
		return &ChildSlot;
	}

	void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
	{
		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(ChildSlot.GetWidget(), FVector2D::ZeroVector, AllottedGeometry.GetLocalSize()));
	}

	void FocusBox(const FBox& Box) const;

	mutable float Scale;
	mutable FVector2D Trans;
	
	mutable FBox2D ScreenRect;
	mutable FTransform2d WorldToScreen;
	mutable FTransform2d ScreenToWorld;

	bool bIsDragSelecting;
	bool bIsPanning;
	bool bShowActors;
	FVector2D MouseCursorPos;
	FVector2D MouseCursorPosWorld;
	FVector2D LastMouseCursorPosWorldDrag;
	FVector2D SelectionStart;
	FVector2D SelectionEnd;
	FBox SelectBox;
	FSlateFontInfo SmallLayoutFont;
	float TotalMouseDelta;

	struct FKeyFuncs : public BaseKeyFuncs<TWeakInterfacePtr<IWorldPartitionActorLoaderInterface>, TWeakInterfacePtr<IWorldPartitionActorLoaderInterface>, false>
	{
		static KeyInitType GetSetKey(ElementInitType Entry)
		{
			return Entry;
		}

		static bool Matches(KeyInitType A, KeyInitType B)
		{
			return A == B;
		}

		static uint32 GetKeyHash(KeyInitType Key)
		{
			return GetTypeHash(Key.GetWeakObjectPtr());
		}
	};

	using FLoaderAdapterSet = TSet<TWeakInterfacePtr<IWorldPartitionActorLoaderInterface>, FKeyFuncs>;
	FLoaderAdapterSet SelectedLoaderAdapters;
	
	// Updated every tick
	TSet<FGuid> ShownActorGuids;
	TSet<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> ShownLoaderAdapters;
	TSet<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> HighlightedLoaderAdapters;
	TWeakInterfacePtr<IWorldPartitionActorLoaderInterface> HoveredLoaderAdapter;
	FGuid HoveredActor;
};