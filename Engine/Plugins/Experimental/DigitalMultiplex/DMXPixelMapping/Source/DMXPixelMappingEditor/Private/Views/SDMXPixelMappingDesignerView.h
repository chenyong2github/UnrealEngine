// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXPixelMappingSurface.h"

class FDMXPixelMappingToolkit;
class FDMXPixelMappingComponentReference;
class FDMXPixelMappingDragDropOp;
class SDMXPixelMappingRuler;
class SDMXPixelMappingTransformHandle;
class SDMXPixelMappingSourceTextureViewport;
class UDMXPixelMappingBaseComponent;
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

	virtual ~SDMXPixelMappingDesignerView();

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

	const TSet<FDMXPixelMappingComponentReference>& GetSelectedComponents() const;

	FDMXPixelMappingComponentReference GetSelectedComponent() const;

	float GetPreviewScale() const;

private:
	bool FindComponentUnderCursor(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, TSubclassOf<UDMXPixelMappingOutputComponent> FindType, FComponentHitResult& HitResult);

	void PopulateWidgetGeometryCache(FArrangedWidget& Root);

	void PopulateWidgetGeometryCache_Loop(FArrangedWidget& Parent);

	FGeometry GetDesignerGeometry() const;

	void HandleChangeComponents(bool bIsSuccess);

	EVisibility GetRulerVisibility() const;

	TSharedRef<SWidget> CreateOverlayUI();

	FText GetCursorPositionText() const;

	EVisibility GetCursorPositionTextVisibility() const;

	EVisibility IsZoomPanVisible() const;

	void GetPreviewAreaAndSize(FVector2D& Area, FVector2D& Size) const;

	FGeometry MakeGeometryWindowLocal(const FGeometry& WidgetGeometry) const;

	FReply HandleZoomToFitClicked();

	void OnSelectedComponenetChanged();

	/** Adds any pending selected Components to the selection set */
	void ResolvePendingSelectedComponents(const FPointerEvent& MouseEvent);
	
	/** Adds a new component from a drag drop op from the palette */
	void AddComponentFromPalette(const FGeometry& MyGeometry, const TSharedPtr<FDMXPixelMappingDragDropOp>& TemplateDragDropOp);

	void DropComponent(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent);

	void ClearExtensionWidgets();

	void CreateExtensionWidgetsForSelection();

	EVisibility GetExtensionCanvasVisibility() const;

	FVector2D GetExtensionPosition(TSharedPtr<SDMXPixelMappingTransformHandle> Handle);

	FVector2D GetExtensionSize(TSharedPtr<SDMXPixelMappingTransformHandle> Handle);

	bool GetWidgetParentGeometry(FDMXPixelMappingComponentReference ComponentReference, FGeometry& Geometry);

	bool GetComponentGeometry(FDMXPixelMappingComponentReference ComponentReference, FGeometry& Geometry);

	bool GetComponentGeometry(UDMXPixelMappingBaseComponent* BaseComponent, FGeometry& Geometry);

	FText GetSelectedComponentNameText() const;
	FText GetSelectedComponentParentNameText() const;
	EVisibility GetTitleBarVisibility() const;

private:
	TSharedPtr<SCanvas> ExtensionWidgetCanvas;

	TSharedPtr<SDMXPixelMappingSourceTextureViewport> SourceTextureViewport;

	/** Canvas that holds the component widgets */
	TSharedPtr<SDMXPixelMappingDesignerCanvas> DesignCanvas;

	TSharedPtr<SBox> PreviewSizeConstraint;

	TSharedPtr<SOverlay> PreviewHitTestRoot;

	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

	TSharedPtr<FHittestGrid> HittestGrid;

	TMap<TSharedRef<SWidget>, FArrangedWidget> CachedWidgetGeometry;

	TWeakObjectPtr<UDMXPixelMappingRendererComponent> CachedRendererComponent;

	FDelegateHandle DelegateHandleChangeComponents;

	FDelegateHandle OnSelectedComponenetChangedHandle;

	/** The ruler bar at the top of the designer. */
	TSharedPtr<SDMXPixelMappingRuler> TopRuler;

	/** The ruler bar on the left side of the designer. */
	TSharedPtr<SDMXPixelMappingRuler> SideRuler;

	/** Cache last mouse position to be used as a paste drop location */
	FVector2D CachedMousePosition;

	/** The location in selected widget local space where the context menu was summoned. */
	FVector2D DragOffset;

	TWeakObjectPtr<UDMXPixelMappingBaseComponent> PendingSelectedComponent;

	/** True if an existing widget is being moved in its current container, or into a new container. */
	bool bMovingExistingWidget;

	/** If true, terminates any existing drag drop op without handling it */
	bool bRequestTerminateDragDrop;

	/** The position in screen space where the user began dragging a widget */
	FVector2D DraggingStartPositionScreenSpace;

	TArray<TSharedPtr<SDMXPixelMappingTransformHandle>> TransformHandles;
};
