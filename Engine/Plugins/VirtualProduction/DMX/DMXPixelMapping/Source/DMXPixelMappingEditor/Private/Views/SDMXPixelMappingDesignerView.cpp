// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SDMXPixelMappingDesignerView.h"

#include "DMXPixelMapping.h"
#include "DMXPixelMappingEditorUtils.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Toolkits/DMXPixelMappingToolkit.h"

#include "DMXPixelMappingComponentWidget.h"
#include "SDMXPixelMappingComponentBox.h"
#include "Widgets/SDMXPixelMappingDesignerCanvas.h"
#include "Widgets/SDMXPixelMappingSourceTextureViewport.h"
#include "Widgets/SDMXPixelMappingRuler.h"
#include "Widgets/SDMXPixelMappingZoomPan.h"
#include "Widgets/SDMXPixelMappingTransformHandle.h"

#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Input/HittestGrid.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingDesignerView"

void SDMXPixelMappingDesignerView::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
{
	SDMXPixelMappingSurface::Construct(SDMXPixelMappingSurface::FArguments()
		.AllowContinousZoomInterpolation(false)
		.Content()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
				.HAlign(HAlign_Fill)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SDMXPixelMappingDesignerView::GetHoveredComponentParentNameText)
							.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("BreadcrumbTrail.Delimiter"))
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SDMXPixelMappingDesignerView::GetHoveredComponentNameText)
							.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
						]
					]
				]
			]
			+SVerticalBox::Slot()
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.0f)
				.FillRow(1, 1.0f)

				// Corner
				+ SGridPanel::Slot(0, 0)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor(FLinearColor(FColor(48, 48, 48)))
				]

				// Top Ruler
				+ SGridPanel::Slot(1, 0)
				[
					SAssignNew(TopRuler, SDMXPixelMappingRuler)
					.Orientation(Orient_Horizontal)
					.Visibility(this, &SDMXPixelMappingDesignerView::GetRulerVisibility)
				]

				// Side Ruler
				+ SGridPanel::Slot(0, 1)
				[
					SAssignNew(SideRuler, SDMXPixelMappingRuler)
					.Orientation(Orient_Vertical)
					.Visibility(this, &SDMXPixelMappingDesignerView::GetRulerVisibility)
				]

				+ SGridPanel::Slot(1, 1)
				[
					SAssignNew(PreviewHitTestRoot, SOverlay)
					.Visibility(EVisibility::Visible)
					.Clipping(EWidgetClipping::ClipToBoundsAlways)
						
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SDMXPixelMappingZoomPan)
						.ZoomAmount(this, &SDMXPixelMappingDesignerView::GetZoomAmount)
						.ViewOffset(this, &SDMXPixelMappingDesignerView::GetViewOffset)
						.Visibility(this, &SDMXPixelMappingDesignerView::IsZoomPanVisible)

						[
							SNew(SOverlay)

							+ SOverlay::Slot()
							[
								SAssignNew(SourceTextureViewport, SDMXPixelMappingSourceTextureViewport, InToolkit)
							]

							+ SOverlay::Slot()
							[
								SAssignNew(PreviewSizeConstraint, SBox)
							]

							+ SOverlay::Slot()
							[
								SAssignNew(DesignCanvas, SDMXPixelMappingDesignerCanvas)
							]
						]
					]

					// A layer in the overlay where we put all the tools for the user
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(ExtensionWidgetCanvas, SCanvas)
						.Visibility(this, &SDMXPixelMappingDesignerView::GetExtensionCanvasVisibility)
					]

					// Designer overlay UI, toolbar, status messages, zoom level...etc
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						CreateOverlayUI()
					]
				]
			]
		]
	, InToolkit);

	ZoomToFit(true);

	HittestGrid = MakeShared<FHittestGrid>();

	// Bind to selection changes
	InToolkit->GetOnSelectedComponentsChangedDelegate().AddSP(this, &SDMXPixelMappingDesignerView::OnSelectedComponentsChanged);

	// Bind to component changes
	UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddSP(this, &SDMXPixelMappingDesignerView::OnComponentAdded);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddSP(this, &SDMXPixelMappingDesignerView::OnComponentRemoved);
}

FReply SDMXPixelMappingDesignerView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnMouseButtonDown(MyGeometry, MouseEvent);

	if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			UDMXPixelMappingOutputComponent* ClickedComponent = FindComponentUnderCursor();

			// Select and detect drag when something was clicked
			if (ClickedComponent)
			{
				PendingSelectedComponent = ClickedComponent;

				// Handle reclicking a selected component depending on type
				const bool bClickedSelectedComponent = [ClickedComponent, this]()
				{
					for (const FDMXPixelMappingComponentReference& SelectedComponentRef : GetSelectedComponents())
					{
						if (ClickedComponent == SelectedComponentRef.GetComponent())
						{
							return true;
						}
						else if (UDMXPixelMappingMatrixCellComponent* ClickedMatrixCellComponent = Cast<UDMXPixelMappingMatrixCellComponent>(ClickedComponent))
						{
							if (ClickedMatrixCellComponent->GetParent() == SelectedComponentRef.GetComponent())
							{
								return true;
							}
						}
						else if (UDMXPixelMappingFixtureGroupItemComponent* ClickedGroupItemComponent = Cast<UDMXPixelMappingFixtureGroupItemComponent>(ClickedComponent))
						{
							if (ClickedGroupItemComponent->GetParent() == SelectedComponentRef.GetComponent() &&
								ClickedGroupItemComponent->IsLockInDesigner())
							{
								return true;
							}
						}
					}

					return false;
				}();

				const bool bClearPreviousSelection = !MouseEvent.IsShiftDown() && !MouseEvent.IsControlDown() && !bClickedSelectedComponent;
				ResolvePendingSelectedComponents(bClearPreviousSelection);
				
				FVector2D GraphSpaceCursorPosition;
				if (GetGraphSpaceCursorPosition(GraphSpaceCursorPosition))
				{
					DragAnchor = GraphSpaceCursorPosition;

					return
						FReply::Handled()
						.PreventThrottling()
						.SetUserFocus(AsShared(), EFocusCause::Mouse)
						.CaptureMouse(AsShared())
						.DetectDrag(AsShared(), EKeys::LeftMouseButton);
				}
			}
		}
	}

	// Capture mouse for the drag handle and general mouse actions
	return 
		FReply::Handled()
		.PreventThrottling()
		.SetUserFocus(AsShared(), EFocusCause::Mouse)
		.CaptureMouse(AsShared());
}

FReply SDMXPixelMappingDesignerView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnMouseButtonUp(MyGeometry, MouseEvent);

	// Select the output component under the mouse
	PendingSelectedComponent = FindComponentUnderCursor();
	
	// Select the Renderer if no Output Component was under the mouse
	if (!PendingSelectedComponent.IsValid())
	{
		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
		{
			if (UDMXPixelMappingRendererComponent* RendererComponent = ToolkitPtr->GetActiveRendererComponent())
			{
				PendingSelectedComponent = RendererComponent;
			}
		}
	}

	const bool bClearPreviousSelection = !MouseEvent.IsShiftDown() && !MouseEvent.IsControlDown();
	ResolvePendingSelectedComponents(bClearPreviousSelection);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SDMXPixelMappingDesignerView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetCursorDelta().IsZero())
	{
		return FReply::Unhandled();
	}

	FReply SurfaceHandled = SDMXPixelMappingSurface::OnMouseMove(MyGeometry, MouseEvent);
	if (SurfaceHandled.IsEventHandled())
	{
		return SurfaceHandled;
	}

	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && HasMouseCapture())
	{
		bool bIsRootWidgetSelected = false;
		const TSet<FDMXPixelMappingComponentReference> SelectedComponentReferences = GetSelectedComponents();
		for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponentReferences)
		{
			UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent();
			if (Component && Component->GetParent())
			{
				bIsRootWidgetSelected = true;
				break;
			}
		}

		if (!bIsRootWidgetSelected)
		{
			//Drag selected widgets
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SDMXPixelMappingDesignerView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnMouseEnter(MyGeometry, MouseEvent);
}

void SDMXPixelMappingDesignerView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnMouseLeave(MouseEvent);
}

FReply SDMXPixelMappingDesignerView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
		{
			ToolkitPtr->DeleteSelectedComponents();

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SDMXPixelMappingDesignerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SDMXPixelMappingSurface::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin())
	{
		CachedWidgetGeometry.Reset();
		FArrangedWidget WindowWidgetGeometry(PreviewHitTestRoot.ToSharedRef(), GetDesignerGeometry());
		PopulateWidgetGeometryCache(WindowWidgetGeometry);

		// Handle drag over on tick
		if (PendingDragDropOp.IsValid())
		{
			FVector2D GraphSpaceCursorPosition;
			if (GetGraphSpaceCursorPosition(GraphSpaceCursorPosition))
			{
				// Layout childs with the group item drag drop helper if possible, else use the default layout method
				if (TSharedPtr<FDMXPixelMappingGroupChildDragDropHelper> GroupChildDragDropHelper = PendingDragDropOp->GetGroupChildDragDropHelper())
				{
					// If it was created from template it was dragged in from details or palette. In this case align the components.
					const bool bAlignComponents = PendingDragDropOp->WasCreatedAsTemplate();

					GroupChildDragDropHelper->LayoutComponents(GraphSpaceCursorPosition, bAlignComponents);
				}
				else
				{
					PendingDragDropOp->LayoutOutputComponents(GraphSpaceCursorPosition);
				}
			}

			PendingDragDropOp.Reset();
		}

		// Update the preview
		UpdateOutput(false);

		// Compute the origin in absolute space.
		FGeometry RootGeometry = CachedWidgetGeometry.FindChecked(PreviewSizeConstraint.ToSharedRef()).Geometry;
		FVector2D AbsoluteOrigin = MakeGeometryWindowLocal(RootGeometry).LocalToAbsolute(FVector2D::ZeroVector);

		GridOrigin = AbsoluteOrigin;

		// Roler position
		TopRuler->SetRuling(AbsoluteOrigin, 1.0f / GetPreviewScale());
		SideRuler->SetRuling(AbsoluteOrigin, 1.0f / GetPreviewScale());

		if (IsHovered())
		{
			// Get cursor in absolute window space.
			FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
			CursorPos = MakeGeometryWindowLocal(RootGeometry).LocalToAbsolute(RootGeometry.AbsoluteToLocal(CursorPos));

			TopRuler->SetCursor(CursorPos);
			SideRuler->SetCursor(CursorPos);
		}
		else
		{
			TopRuler->SetCursor(TOptional<FVector2D>());
			SideRuler->SetCursor(TOptional<FVector2D>());
		}
	}
}

FReply SDMXPixelMappingDesignerView::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnDragDetected(MyGeometry, MouseEvent);

	const TSet<FDMXPixelMappingComponentReference>& SelectedComponents = GetSelectedComponents();

	if (SelectedComponents.Num() > 0)
	{
		TArray<UDMXPixelMappingOutputComponent*> DraggedComponentCandidates;

		for (const FDMXPixelMappingComponentReference& SelectedComponent : GetSelectedComponents())
		{
			if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(SelectedComponent.GetComponent()))
			{				
				DraggedComponentCandidates.Add(OutputComponent);
			}
		}

		TArray<TWeakObjectPtr<UDMXPixelMappingBaseComponent>> DraggedComponents;
		for (UDMXPixelMappingOutputComponent* Candidate : DraggedComponentCandidates)
		{
			// Check the parent chain of each dragged component and ignore those that are children of other dragged components
			bool bIsChild = false;
			for (TWeakObjectPtr<UDMXPixelMappingBaseComponent> Parent = Candidate->GetParent(); Parent.IsValid(); Parent = Parent->GetParent())
			{
				bIsChild = DraggedComponentCandidates.ContainsByPredicate([Parent](const UDMXPixelMappingOutputComponent* BaseComponent)
					{ 
						return BaseComponent == Parent.Get();
					});

				if (bIsChild)
				{
					break;
				}
			}

			if (!bIsChild && !Candidate->IsLockInDesigner())
			{
				DraggedComponents.Add(Candidate);
			}
		}

		// We know the first element is the one that was selected via mouse
		if (DraggedComponents.Num() > 0)
		{
			// We rely on the first being the clicked component. There's an according comment in the DragDropOp and the detected drag is raised here.
			UDMXPixelMappingOutputComponent* ClickedComponent = CastChecked<UDMXPixelMappingOutputComponent>(DraggedComponents[0]);

			FVector2D GraphSpaceDragOffset = DragAnchor - ClickedComponent->GetPosition();

			TSharedRef<FDMXPixelMappingDragDropOp> DragDropOp = FDMXPixelMappingDragDropOp::New(GraphSpaceDragOffset, DraggedComponents);
			DragDropOp->SetDecoratorVisibility(false);

			// Clear any pending selected widgets
			PendingSelectedComponent = nullptr;

			return FReply::Handled().BeginDragDrop(DragDropOp);
		}
	}

	return FReply::Handled();
}

void SDMXPixelMappingDesignerView::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FDMXPixelMappingDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();
	if (DragDropOp.IsValid())
	{
		const FGeometry WidgetUnderCursorGeometry = PreviewSizeConstraint->GetTickSpaceGeometry();
		const FVector2D ScreenSpacePosition = DragDropEvent.GetScreenSpacePosition();
		const FVector2D LocalPosition = WidgetUnderCursorGeometry.AbsoluteToLocal(ScreenSpacePosition);

		// If this was dragged into designer, but has no components yet, it was dragged in from Palette or Details
		if (DragDropOp->WasCreatedAsTemplate())
		{
			HandleDragEnterFromDetailsOrPalette(DragDropOp);
		}
	}

	SDMXPixelMappingSurface::OnDragEnter(MyGeometry, DragDropEvent);
}

void SDMXPixelMappingDesignerView::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDragLeave(DragDropEvent);

	const TSharedPtr<FDMXPixelMappingDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();
	if (DragDropOp.IsValid())
	{		
		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
		{
			FScopedRestoreSelection(ToolkitPtr.ToSharedRef(), StaticCastSharedRef<SDMXPixelMappingDesignerView>(AsShared()));

			// If the drag drop op was dragged in from details or palette, remove the components
			if (DragDropOp->WasCreatedAsTemplate())
			{
				TSet<FDMXPixelMappingComponentReference> Parents;
				for (const TWeakObjectPtr<UDMXPixelMappingBaseComponent>& Component : DragDropOp->GetDraggedComponents())
				{
					if (Component.IsValid() && Component->GetParent())
					{
						Parents.Add(ToolkitPtr->GetReferenceFromComponent(Component->GetParent()));
					
						Component->GetParent()->RemoveChild(Component.Get());
					}
				}

				// Select parents instead
				ToolkitPtr->SelectComponents(Parents);
			}

			// Update the preview
			constexpr bool bForceUpdate = true;
			UpdateOutput(bForceUpdate);
		}
	}
}

FReply SDMXPixelMappingDesignerView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDragOver(MyGeometry, DragDropEvent);

	const TSharedPtr<FDMXPixelMappingDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();
	if (DragDropOp.IsValid())
	{
		// Handle the drag drop op on tick
		PendingDragDropOp = DragDropOp;
	}

	return FReply::Handled();
}

FReply SDMXPixelMappingDesignerView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDrop(MyGeometry, DragDropEvent);

	const TSharedPtr<FDMXPixelMappingDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();
	if (DragDropOp.IsValid())
	{
		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
		{
			FScopedRestoreSelection(ToolkitPtr.ToSharedRef(), StaticCastSharedRef<SDMXPixelMappingDesignerView>(AsShared()));

			CachedRendererComponent = nullptr;
			UpdateOutput(false);
		}
	}

	return FReply::Handled().EndDragDrop().SetUserFocus(AsShared());
}

UDMXPixelMappingOutputComponent* SDMXPixelMappingDesignerView::FindComponentUnderCursor() const
{
	UDMXPixelMappingOutputComponent* ComponentUnderCursor = nullptr;
	if (UDMXPixelMappingRendererComponent* RendererComponent = CachedRendererComponent.Get())
	{
		FVector2D GraphSpaceCursorPosition;
		if (GetGraphSpaceCursorPosition(GraphSpaceCursorPosition))
		{
			RendererComponent->ForEachChild([&ComponentUnderCursor, &GraphSpaceCursorPosition](UDMXPixelMappingBaseComponent* InComponent)
				{
					if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(InComponent))
					{
						if (!ComponentUnderCursor ||
							ComponentUnderCursor->GetZOrder() <= OutputComponent->GetZOrder())
						{
							if (OutputComponent->IsVisible() &&
								OutputComponent->IsOverPosition(GraphSpaceCursorPosition))
							{
								ComponentUnderCursor = OutputComponent;
							}
						}
					}
				}, true);
		}
	}

	return ComponentUnderCursor;
}

bool SDMXPixelMappingDesignerView::GetGraphSpaceCursorPosition(FVector2D& OutGraphSpaceCursorPosition) const
{
	if (const FArrangedWidget* CachedPreviewSurface = CachedWidgetGeometry.Find(PreviewSizeConstraint.ToSharedRef()))
	{
		const FGeometry& RootGeometry = CachedPreviewSurface->Geometry;
		OutGraphSpaceCursorPosition = RootGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());

		return true;
	}
	return false;
}

void SDMXPixelMappingDesignerView::PopulateWidgetGeometryCache(FArrangedWidget& Root)
{
	const FSlateRect Rect = PreviewHitTestRoot->GetTickSpaceGeometry().GetLayoutBoundingRect();
	const FSlateRect PaintRect = PreviewHitTestRoot->GetPaintSpaceGeometry().GetLayoutBoundingRect();
	HittestGrid->SetHittestArea(Rect.GetTopLeft(), Rect.GetSize(), PaintRect.GetTopLeft());
	HittestGrid->Clear();

	PopulateWidgetGeometryCache_Loop(Root);
}

void SDMXPixelMappingDesignerView::PopulateWidgetGeometryCache_Loop(FArrangedWidget& CurrentWidget)
{
	bool bIncludeInHitTestGrid = true;

	if (bIncludeInHitTestGrid)
	{
		HittestGrid->AddWidget(CurrentWidget.Widget, 0, 0, FSlateInvalidationWidgetSortOrder());
	}

	FArrangedChildren ArrangedChildren(EVisibility::All);
	CurrentWidget.Widget->ArrangeChildren(CurrentWidget.Geometry, ArrangedChildren);

	CachedWidgetGeometry.Add(CurrentWidget.Widget, CurrentWidget);

	for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
	{
		FArrangedWidget& SomeChild = ArrangedChildren[ChildIndex];
		PopulateWidgetGeometryCache_Loop(SomeChild);
	}
}

FGeometry SDMXPixelMappingDesignerView::GetDesignerGeometry() const
{
	return PreviewHitTestRoot->GetTickSpaceGeometry();
}

void SDMXPixelMappingDesignerView::OnPaintBackground(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	SDMXPixelMappingSurface::OnPaintBackground(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId);
}

FSlateRect SDMXPixelMappingDesignerView::ComputeAreaBounds() const
{
	return FSlateRect(0, 0, GetPreviewAreaWidth().Get(), GetPreviewAreaHeight().Get());
}

int32 SDMXPixelMappingDesignerView::GetGraphRulePeriod() const
{
	return 10; // Parent override
}

float SDMXPixelMappingDesignerView::GetGridScaleAmount() const
{
	return 1.0f; // Parent override
}

int32 SDMXPixelMappingDesignerView::GetSnapGridSize() const
{
	return 4; // Parent override
}

FOptionalSize SDMXPixelMappingDesignerView::GetPreviewAreaWidth() const
{
	FVector2D Area, Size;
	GetPreviewAreaAndSize(Area, Size);

	return Area.X;
}

FOptionalSize SDMXPixelMappingDesignerView::GetPreviewAreaHeight() const
{
	FVector2D Area, Size;
	GetPreviewAreaAndSize(Area, Size);

	return Area.Y;
}

void SDMXPixelMappingDesignerView::UpdateOutput(bool bForceUpdate)
{
	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin())
	{
		UDMXPixelMappingRendererComponent* RendererComponent = Toolkit->GetActiveRendererComponent();
		if (!CachedRendererComponent.IsValid())
		{
			CachedRendererComponent = RendererComponent;
		}

		if (RendererComponent)
		{
			if (bForceUpdate || RendererComponent != CachedRendererComponent)
			{
				DesignCanvas->ClearChildren();

				DesignCanvas->AddSlot()
					[
						RendererComponent->TakeWidget()
					];
			}
		}
		else
		{
			DesignCanvas->ClearChildren();
		}

		CachedRendererComponent = RendererComponent;
	}
}

void SDMXPixelMappingDesignerView::OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	CachedRendererComponent = nullptr;
	UpdateOutput(true);
}

void SDMXPixelMappingDesignerView::OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	CachedRendererComponent = nullptr;
	UpdateOutput(true);
}

EVisibility SDMXPixelMappingDesignerView::GetRulerVisibility() const
{
	return EVisibility::Visible;
}

TSharedRef<SWidget> SDMXPixelMappingDesignerView::CreateOverlayUI()
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(0)
		+SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6, 2, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
				.Text(this, &SDMXPixelMappingDesignerView::GetZoomText)
				.ColorAndOpacity(this, &SDMXPixelMappingDesignerView::GetZoomTextColorAndOpacity)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(40, 2, 0, 0)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("BoldCondensed"), 14))
				.Text(this, &SDMXPixelMappingDesignerView::GetCursorPositionText)
				.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.25f))
				.Visibility(this, &SDMXPixelMappingDesignerView::GetCursorPositionTextVisibility)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpacer)
				.Size(FVector2D(1, 1))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "ViewportMenu.Button")
				.ToolTipText(LOCTEXT("ZoomToFit_ToolTip", "Zoom To Fit"))
				.OnClicked(this, &SDMXPixelMappingDesignerView::HandleZoomToFitClicked)
				.ContentPadding(FEditorStyle::Get().GetMargin("ViewportMenu.SToolBarButtonBlock.Button.Padding"))
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("UMGEditor.ZoomToFit"))
				]
			]
		];
}

FText SDMXPixelMappingDesignerView::GetCursorPositionText() const
{
	FVector2D CursorPosition;
	if (GetGraphSpaceCursorPosition(CursorPosition))
	{
		return FText::Format(LOCTEXT("CursorPositionFormat", "{0} x {1}"), FText::AsNumber(FMath::RoundToInt(CursorPosition.X)), FText::AsNumber(FMath::RoundToInt(CursorPosition.Y)));
	}
	return FText();
}

EVisibility SDMXPixelMappingDesignerView::GetCursorPositionTextVisibility() const
{
	return IsHovered() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

EVisibility SDMXPixelMappingDesignerView::IsZoomPanVisible() const
{
	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin())
	{
		if (UDMXPixelMappingRendererComponent* RendererComponent = Toolkit->GetActiveRendererComponent())
		{
			if (RendererComponent->GetRendererInputTexture() != nullptr)
			{
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

void SDMXPixelMappingDesignerView::GetPreviewAreaAndSize(FVector2D& Area, FVector2D& Size) const
{
	check(SourceTextureViewport.IsValid());

	Area = FVector2D(SourceTextureViewport->GetPreviewAreaWidth().Get(), SourceTextureViewport->GetPreviewAreaHeight().Get());
	Size = Area;
}

float SDMXPixelMappingDesignerView::GetPreviewScale() const
{
	return GetZoomAmount();
}

FGeometry SDMXPixelMappingDesignerView::MakeGeometryWindowLocal(const FGeometry& WidgetGeometry) const
{
	FGeometry NewGeometry = WidgetGeometry;

	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(SharedThis(this));
	if (WidgetWindow.IsValid())
	{
		TSharedRef<SWindow> CurrentWindowRef = WidgetWindow.ToSharedRef();

		NewGeometry.AppendTransform(FSlateLayoutTransform(Inverse(CurrentWindowRef->GetPositionInScreen())));
	}

	return NewGeometry;
}

FReply SDMXPixelMappingDesignerView::HandleZoomToFitClicked()
{
	ZoomToFit(/*bInstantZoom*/ false);
	return FReply::Handled();
}

void SDMXPixelMappingDesignerView::OnSelectedComponentsChanged()
{
	CreateExtensionWidgetsForSelection();
}

TSet<FDMXPixelMappingComponentReference> SDMXPixelMappingDesignerView::GetSelectedComponents() const
{
	if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
	{
		return ToolkitPtr->GetSelectedComponents();
	}
	
	return TSet<FDMXPixelMappingComponentReference>();
}

FDMXPixelMappingComponentReference SDMXPixelMappingDesignerView::GetSelectedComponent() const
{
	const TSet<FDMXPixelMappingComponentReference>& SelectedComponents = GetSelectedComponents();

	// Only return a selected widget when we have only a single item selected.
	if (SelectedComponents.Num() == 1)
	{
		for (TSet<FDMXPixelMappingComponentReference>::TConstIterator SetIt(SelectedComponents); SetIt; ++SetIt)
		{
			return *SetIt;
		}
	}

	return FDMXPixelMappingComponentReference();
}

FText SDMXPixelMappingDesignerView::GetHoveredComponentNameText() const
{
	UDMXPixelMappingOutputComponent* OutputComponent = FindComponentUnderCursor();
	if (OutputComponent)
	{
		return FText::FromString(OutputComponent->GetUserFriendlyName());
	}

	return FText();
}

FText SDMXPixelMappingDesignerView::GetHoveredComponentParentNameText() const
{
	UDMXPixelMappingOutputComponent* OutputComponent = FindComponentUnderCursor();
	if (OutputComponent && OutputComponent->GetParent())
	{
		return FText::FromString(OutputComponent->GetParent()->GetUserFriendlyName());
	}

	return FText();
}

void SDMXPixelMappingDesignerView::ResolvePendingSelectedComponents(bool bClearPreviousSelection)
{
	if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
	{
		if (PendingSelectedComponent.IsValid())
		{
			if (UDMXPixelMappingMatrixCellComponent* MatrixCellComponent = Cast<UDMXPixelMappingMatrixCellComponent>(PendingSelectedComponent))
			{
				// If a Matrix Cell is selected, select the owning Matrix Component instead
				PendingSelectedComponent = MatrixCellComponent->GetParent();
			}
			else if (UDMXPixelMappingFixtureGroupItemComponent* GroupItemComponent = Cast<UDMXPixelMappingFixtureGroupItemComponent>(PendingSelectedComponent))
			{
				// If an Item Component is selected and is lock in designer, select the owning Group Component instead
				if (GroupItemComponent->IsLockInDesigner())
				{
					PendingSelectedComponent = GroupItemComponent->GetParent();
				}
			}

			TSet<FDMXPixelMappingComponentReference> SelectedComponents;

			// Add the newly selected component first. This is important, e.g. for drag drop when iterating and using this as base
			SelectedComponents.Add(ToolkitWeakPtr.Pin()->GetReferenceFromComponent(PendingSelectedComponent.Get()));

			if (!bClearPreviousSelection)
			{
				SelectedComponents.Append(ToolkitWeakPtr.Pin()->GetSelectedComponents());
			}
			ToolkitWeakPtr.Pin()->SelectComponents(SelectedComponents);

			PendingSelectedComponent = nullptr;
		}
	}
}

void SDMXPixelMappingDesignerView::HandleDragEnterFromDetailsOrPalette(const TSharedPtr<FDMXPixelMappingDragDropOp>& TemplateDragDropOp)
{
	if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
	{
		if (TemplateDragDropOp.IsValid())
		{
			if (UDMXPixelMapping* PixelMapping = ToolkitPtr->GetDMXPixelMapping())
			{
				// Find the target on which the component should be created from its template
				UDMXPixelMappingBaseComponent* Target = (TemplateDragDropOp->Parent.IsValid()) ? TemplateDragDropOp->Parent.Get() : ToolkitPtr->GetActiveRendererComponent();

				if (Target && PixelMapping->RootComponent)
				{
					TArray<UDMXPixelMappingBaseComponent*> NewComponents;
					if (TemplateDragDropOp->GetDraggedComponents().Num() == 0)
					{
						// Create new components if they were first dragged in
						NewComponents = ToolkitPtr->CreateComponentsFromTemplates(PixelMapping->GetRootComponent(), Target, TemplateDragDropOp->GetTemplates());
					}
					else
					{
						// Use the existing components if the components are reentering
						for (const TWeakObjectPtr<UDMXPixelMappingBaseComponent>& BaseComponent : TemplateDragDropOp->GetDraggedComponents())
						{
							if (BaseComponent.IsValid())
							{
								NewComponents.Add(BaseComponent.Get());
							}
						}
					}
					
					// Build an array of all new componets for dragging
					TArray<TWeakObjectPtr<UDMXPixelMappingBaseComponent>> DraggedComponents;
					for (UDMXPixelMappingBaseComponent* Component : NewComponents)
					{
						DraggedComponents.Add(Component);
					}

					TemplateDragDropOp->SetDraggedComponents(DraggedComponents);

					// Find if only matrix components and its childs are dragged
					TArray<UDMXPixelMappingBaseComponent*> NewMatrices;
					for (UDMXPixelMappingBaseComponent* NewComponent : NewComponents)
					{
						if (UDMXPixelMappingMatrixComponent* NewMatrix = Cast<UDMXPixelMappingMatrixComponent>(NewComponent))
						{
							NewMatrices.Add(NewMatrix);
						}
						else if (UDMXPixelMappingMatrixCellComponent* NewMatrixCell = Cast<UDMXPixelMappingMatrixCellComponent>(NewComponent))
						{
							continue;
						}
						else
						{
							NewMatrices.Reset();
							break;
						}
					}

					// Update selection depending on types
					TSet<FDMXPixelMappingComponentReference> NewSelection;
					if (NewMatrices.Num() > 0)
					{					
						// When only matrices and its childs are dragged, just select the matrix
						for (UDMXPixelMappingBaseComponent* NewMatrix : NewMatrices)
						{
							NewSelection.Add(ToolkitPtr->GetReferenceFromComponent(NewMatrix));
						}
					}
					else
					{
						// Select all new components
						for (UDMXPixelMappingBaseComponent* NewComponent : NewComponents)
						{
							NewSelection.Add(ToolkitPtr->GetReferenceFromComponent(NewComponent));
						}
					}

					ToolkitPtr->SelectComponents(NewSelection);
				}
			}
		}
	}
}

void SDMXPixelMappingDesignerView::ClearExtensionWidgets()
{
	ExtensionWidgetCanvas->ClearChildren();
}

void SDMXPixelMappingDesignerView::CreateExtensionWidgetsForSelection()
{
	// Remove all the current extension widgets
	ClearExtensionWidgets();

	// Get the selected widgets as an array
	const TArray<FDMXPixelMappingComponentReference>& Selected = GetSelectedComponents().Array();

	constexpr int32 NumSelectedItems = 1;
	if (Selected.Num() == NumSelectedItems)
	{
		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Selected[0].GetComponent()))
		{
			if (OutputComponent->IsVisible() &&	!OutputComponent->IsLockInDesigner())
			{
				// Add transform handles
				constexpr float Offset = 10.f;
				const TSharedPtr<SDMXPixelMappingDesignerView> Self = SharedThis(this);
				TransformHandles.Add(SNew(SDMXPixelMappingTransformHandle, Self, EDMXPixelMappingTransformDirection::CenterRight, FVector2D(Offset, 0.f)));
				TransformHandles.Add(SNew(SDMXPixelMappingTransformHandle, Self, EDMXPixelMappingTransformDirection::BottomCenter, FVector2D(0.f, Offset)));
				TransformHandles.Add(SNew(SDMXPixelMappingTransformHandle, Self, EDMXPixelMappingTransformDirection::BottomRight, FVector2D(Offset, Offset)));

				// Add Widgets to designer surface
				for (TSharedPtr<SDMXPixelMappingTransformHandle>& Handle : TransformHandles)
				{
					ExtensionWidgetCanvas->AddSlot()
						.Position(TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP(this, &SDMXPixelMappingDesignerView::GetExtensionPosition, Handle)))
						.Size(TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP(this, &SDMXPixelMappingDesignerView::GetExtensionSize, Handle)))
						[
							Handle.ToSharedRef()
						];
				}
			}
		}
	}
}

EVisibility SDMXPixelMappingDesignerView::GetExtensionCanvasVisibility() const
{
	for (const FDMXPixelMappingComponentReference& Component : GetSelectedComponents())
	{
		UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component.GetComponent());

		if (!OutputComponent ||
			!OutputComponent->IsVisible() ||
			OutputComponent->IsLockInDesigner())
		{
			return EVisibility::Hidden;
		}
	}
	return EVisibility::SelfHitTestInvisible;
}

FVector2D SDMXPixelMappingDesignerView::GetExtensionPosition(TSharedPtr<SDMXPixelMappingTransformHandle> Handle)
{
	const FDMXPixelMappingComponentReference& SelectedComponent = GetSelectedComponent();

	if (SelectedComponent.IsValid())
	{
		FGeometry SelectedComponentGeometry;
		FGeometry SelectedComponentParentGeometry;

		if (GetComponentGeometry(SelectedComponent, SelectedComponentGeometry))
		{
			FVector2D FinalPosition(0, 0);


			FVector2D WidgetPosition;

			// Get the initial offset based on the location around the selected object.
			switch (Handle->GetTransformDirection())
			{
			case EDMXPixelMappingTransformDirection::CenterRight:
				WidgetPosition = FVector2D(SelectedComponentGeometry.GetLocalSize().X, SelectedComponentGeometry.GetLocalSize().Y * 0.5f);
				break;
			case EDMXPixelMappingTransformDirection::BottomLeft:
				WidgetPosition = FVector2D(0, SelectedComponentGeometry.GetLocalSize().Y);
				break;
			case EDMXPixelMappingTransformDirection::BottomCenter:
				WidgetPosition = FVector2D(SelectedComponentGeometry.GetLocalSize().X * 0.5f, SelectedComponentGeometry.GetLocalSize().Y);
				break;
			case EDMXPixelMappingTransformDirection::BottomRight:
				WidgetPosition = SelectedComponentGeometry.GetLocalSize();
				break;
			}

			FVector2D SelectedWidgetScale = SelectedComponentGeometry.GetAccumulatedRenderTransform().GetMatrix().GetScale().GetVector();

			FVector2D ApplicationScaledOffset = Handle->GetOffset() * GetDesignerGeometry().Scale;

			FVector2D LocalOffsetFull = ApplicationScaledOffset / SelectedWidgetScale;
			FVector2D PositionFullOffset = GetDesignerGeometry().AbsoluteToLocal(SelectedComponentGeometry.LocalToAbsolute(WidgetPosition + LocalOffsetFull));
			FVector2D LocalOffsetHalf = (ApplicationScaledOffset / 2.0f) / SelectedWidgetScale;
			FVector2D PositionHalfOffset = GetDesignerGeometry().AbsoluteToLocal(SelectedComponentGeometry.LocalToAbsolute(WidgetPosition + LocalOffsetHalf));

			FVector2D PivotCorrection = PositionHalfOffset - (PositionFullOffset + FVector2D(5.0f, 5.0f));

			FinalPosition = PositionFullOffset + PivotCorrection;

			return FinalPosition;
		}
	}

	return FVector2D(0, 0);
}

FVector2D SDMXPixelMappingDesignerView::GetExtensionSize(TSharedPtr<SDMXPixelMappingTransformHandle> Handle)
{
	return Handle->GetDesiredSize();
}

bool SDMXPixelMappingDesignerView::GetComponentGeometry(FDMXPixelMappingComponentReference ComponentReference, FGeometry& Geometry)
{
	if (UDMXPixelMappingBaseComponent* ComponentPreview = ComponentReference.GetComponent())
	{
		return GetComponentGeometry(ComponentPreview, Geometry);
	}

	return false;
}

bool SDMXPixelMappingDesignerView::GetComponentGeometry(UDMXPixelMappingBaseComponent* BaseComponent, FGeometry& Geometry)
{
	if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(BaseComponent))
	{
		if (TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget = OutputComponent->GetComponentWidget())
		{
			TSharedPtr<SWidget> CachedPreviewWidget = ComponentWidget->GetComponentBox();
			if (CachedPreviewWidget.IsValid())
			{
				const FArrangedWidget* ArrangedWidget = CachedWidgetGeometry.Find(CachedPreviewWidget.ToSharedRef());
				if (ArrangedWidget)
				{
					Geometry = ArrangedWidget->Geometry;
					return true;
				}
			}
		}
	}
	
	return false;
}

SDMXPixelMappingDesignerView::FScopedRestoreSelection::FScopedRestoreSelection(TSharedRef<FDMXPixelMappingToolkit> ToolkitPtr, TSharedRef<SDMXPixelMappingDesignerView> DesignerView)
	: WeakToolkit(ToolkitPtr)
	, WeakDesignerView(DesignerView)
{
	if (TSharedPtr<FDMXPixelMappingToolkit> PinnedToolkit = WeakToolkit.Pin())
	{
		for (const FDMXPixelMappingComponentReference& ComponentReference : PinnedToolkit->GetSelectedComponents())
		{
			CachedSelectedComponents.Add(ComponentReference.GetComponent());
		}
	}
}

SDMXPixelMappingDesignerView::FScopedRestoreSelection::~FScopedRestoreSelection()
{
	if (TSharedPtr<FDMXPixelMappingToolkit> PinnedToolkit = WeakToolkit.Pin())
	{
		TArray<UDMXPixelMappingBaseComponent*> RemovedComponents;
		TSet<FDMXPixelMappingComponentReference> ValidComponents;

		if (UDMXPixelMapping* PixelMapping = PinnedToolkit->GetDMXPixelMapping())
		{
			TArray<UDMXPixelMappingBaseComponent*> ComponentsInPixelMapping;
			PixelMapping->GetAllComponentsOfClass<UDMXPixelMappingBaseComponent>(ComponentsInPixelMapping);

			for (UDMXPixelMappingBaseComponent* Component : ComponentsInPixelMapping)
			{
				const bool bComponentStillExists =
					CachedSelectedComponents.ContainsByPredicate([Component](const TWeakObjectPtr<UDMXPixelMappingBaseComponent>& WeakCachedComponent)
						{
							return
								WeakCachedComponent.IsValid() &&
								Component == WeakCachedComponent.Get();
						});

				if (bComponentStillExists)
				{
					ValidComponents.Add(PinnedToolkit->GetReferenceFromComponent(Component));
				}
				else
				{
					RemovedComponents.Add(Component);
				}
			}

			if (ValidComponents.Num() == 0)
			{
				// All were removed, select the the parent if possible or the renderer
				UDMXPixelMappingBaseComponent** ComponentWithParentPtr = RemovedComponents.FindByPredicate([&ComponentsInPixelMapping](UDMXPixelMappingBaseComponent* Component)
					{
						return
							Component &&
							Component->GetParent() &&
							ComponentsInPixelMapping.Contains(Component->GetParent());
					});

				if (ComponentWithParentPtr)
				{
					ValidComponents.Add(PinnedToolkit->GetReferenceFromComponent((*ComponentWithParentPtr)->GetParent()));
				}
				else
				{
					// Select the renderer
					UDMXPixelMappingRendererComponent* RendererComponent = PinnedToolkit->GetActiveRendererComponent();
					if (RendererComponent)
					{
						ValidComponents.Add(PinnedToolkit->GetReferenceFromComponent(RendererComponent));
					}
				}

				PinnedToolkit->SelectComponents(ValidComponents);
			}
		}
	}

	if (TSharedPtr<SDMXPixelMappingDesignerView> PinnedDesignerView = WeakDesignerView.Pin())
	{
		PinnedDesignerView->CreateExtensionWidgetsForSelection();
	}
}

#undef LOCTEXT_NAMESPACE
