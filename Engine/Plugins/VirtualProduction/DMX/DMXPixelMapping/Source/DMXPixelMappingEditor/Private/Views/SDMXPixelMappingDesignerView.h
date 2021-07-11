// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXPixelMappingSurface.h"

#include "DMXPixelMappingComponentReference.h"

#include "Templates/SharedPointer.h"

class FDMXPixelMappingToolkit;
class FDMXPixelMappingComponentReference;
class FDMXPixelMappingDragDropOp;
class SDMXPixelMappingRuler;
class SDMXPixelMappingTransformHandle;
class SDMXPixelMappingSourceTextureViewport;
class UDMXPixelMappingBaseComponent;
class UDMXPixelMappingFixtureGroupComponent;
class UDMXPixelMappingFixtureGroupItemComponent;
class UDMXPixelMappingOutputComponent;
class UDMXPixelMappingRendererComponent;

struct FOptionalSize;
class FHittestGrid;
class SCanvas;
class SDMXPixelMappingDesignerCanvas;
class SBox;
class SOverlay;


class SDMXPixelMappingDesignerView
	: public SDMXPixelMappingSurface
{
private:
	struct FComponentHitResult
	{
	public:
		TWeakObjectPtr<UDMXPixelMappingBaseComponent> Component;
		FArrangedWidget WidgetArranged;
		FName NamedSlot;

	public:
		FComponentHitResult()
			: WidgetArranged(SNullWidget::NullWidget, FGeometry())
		{}
	};
public:

	SLATE_BEGIN_ARGS(SDMXPixelMappingDesignerView) { }
	SLATE_END_ARGS()

public:
	/**
	 * Constructs the widget.
	 *
	 * @param InArgs The construction arguments.
	 */
	void Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit);

	// Begin SWidget interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End of SWidget interface

	//~ Begin SDMXPixelMappingSurface interface
	virtual void OnPaintBackground(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const override;
	virtual FSlateRect ComputeAreaBounds() const override;
	virtual int32 GetGraphRulePeriod() const override;
	virtual float GetGridScaleAmount() const override;
	virtual int32 GetSnapGridSize() const override;
	//~ End SDMXPixelMappingSurface interface

	/** The width of the preview screen for the UI */
	FOptionalSize GetPreviewAreaWidth() const;

	/** The height of the preview screen for the UI */
	FOptionalSize GetPreviewAreaHeight() const;

	void UpdateOutput(bool bForceUpdate);

	TSet<FDMXPixelMappingComponentReference> GetSelectedComponents() const;

	FDMXPixelMappingComponentReference GetSelectedComponent() const;

	float GetPreviewScale() const;

private:
	/** Returns the component under the cursor */
	UDMXPixelMappingOutputComponent* FindComponentUnderCursor() const;

	/** Returns the cursor position in graph space */
	bool GetGraphSpaceCursorPosition(FVector2D& OutGraphSpaceCursorPosition) const;

	void PopulateWidgetGeometryCache(FArrangedWidget& Root);

	void PopulateWidgetGeometryCache_Loop(FArrangedWidget& Parent);

	FGeometry GetDesignerGeometry() const;

	/** Called when a component was added */
	void OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component);

	/** Called when a component was removed */
	void OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component);

	EVisibility GetRulerVisibility() const;

	TSharedRef<SWidget> CreateOverlayUI();

	FText GetCursorPositionText() const;

	EVisibility GetCursorPositionTextVisibility() const;

	EVisibility IsZoomPanVisible() const;

	void GetPreviewAreaAndSize(FVector2D& Area, FVector2D& Size) const;

	FGeometry MakeGeometryWindowLocal(const FGeometry& WidgetGeometry) const;

	FReply HandleZoomToFitClicked();

	void OnSelectedComponentsChanged();

	/** Adds any pending selected Components to the selection set */
	void ResolvePendingSelectedComponents(bool bClearPreviousSelection = false);
	
	/** Updates the drag drop op to use the patches selected in the Palette */
	void HandleDragEnterFromDetailsOrPalette(const TSharedPtr<FDMXPixelMappingDragDropOp>& DragDropOp);

	void ClearExtensionWidgets();

	void CreateExtensionWidgetsForSelection();

	EVisibility GetExtensionCanvasVisibility() const;

	FVector2D GetExtensionPosition(TSharedPtr<SDMXPixelMappingTransformHandle> Handle);

	FVector2D GetExtensionSize(TSharedPtr<SDMXPixelMappingTransformHandle> Handle);

	bool GetComponentGeometry(FDMXPixelMappingComponentReference ComponentReference, FGeometry& Geometry);

	bool GetComponentGeometry(UDMXPixelMappingBaseComponent* BaseComponent, FGeometry& Geometry);

	/** Returns text for the currently hovered component */
	FText GetHoveredComponentNameText() const;

	/** Returns text for the currently hovered component's parent */
	FText GetHoveredComponentParentNameText() const;

private:
	/** The component that should be selected, but isn't selected yet */
	TWeakObjectPtr<UDMXPixelMappingBaseComponent> PendingSelectedComponent;

	/** The drag over DragDropOp that should be handled on the next tick*/
	TSharedPtr<FDMXPixelMappingDragDropOp> PendingDragDropOp;

	TSharedPtr<SCanvas> ExtensionWidgetCanvas;

	TSharedPtr<SDMXPixelMappingSourceTextureViewport> SourceTextureViewport;

	/** Canvas that holds the component widgets */
	TSharedPtr<SDMXPixelMappingDesignerCanvas> DesignCanvas;

	TSharedPtr<SBox> PreviewSizeConstraint;

	TSharedPtr<SOverlay> PreviewHitTestRoot;

	TSharedPtr<FHittestGrid> HittestGrid;

	TMap<TSharedRef<SWidget>, FArrangedWidget> CachedWidgetGeometry;

	TWeakObjectPtr<UDMXPixelMappingRendererComponent> CachedRendererComponent;

	/** The ruler bar at the top of the designer. */
	TSharedPtr<SDMXPixelMappingRuler> TopRuler;

	/** The ruler bar on the left side of the designer. */
	TSharedPtr<SDMXPixelMappingRuler> SideRuler;

	/** The position in local space where the user began dragging a widget */
	FVector2D DragAnchor;

	TArray<TSharedPtr<SDMXPixelMappingTransformHandle>> TransformHandles;

	/** Helper class to restore selection on scope */
	class FScopedRestoreSelection
	{
	public:
		FScopedRestoreSelection(TSharedRef<FDMXPixelMappingToolkit> ToolkitPtr, TSharedRef<SDMXPixelMappingDesignerView> DesignerView);
		~FScopedRestoreSelection();

	private:
		TWeakPtr<FDMXPixelMappingToolkit> WeakToolkit;
		TWeakPtr<SDMXPixelMappingDesignerView> WeakDesignerView;
		TArray<TWeakObjectPtr<UDMXPixelMappingBaseComponent>> CachedSelectedComponents;
	};
};

