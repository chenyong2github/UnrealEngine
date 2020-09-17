// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SDMXPixelMappingDesignerView.h"
#include "Widgets/SDMXPixelMappingDesignerCanvas.h"
#include "Widgets/SDMXPixelMappingSourceTextureViewport.h"
#include "Widgets/SDMXPixelMappingRuler.h"
#include "Widgets/SDMXPixelMappingPreviewViewport.h"
#include "Widgets/SDMXPixelMappingZoomPan.h"
#include "Widgets/SDMXPixelMappingTransformHandle.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixPixelComponent.h"
#include "DMXPixelMapping.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "DMXPixelMappingComponentReference.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Widgets/SCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Input/HittestGrid.h"
#include "Misc/IFilter.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "SDMXPixelMappingDesignerView"

class FSelectedComponentDragDropOp 
	: public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FSelectedComponentDragDropOp, FDecoratedDragDropOp)

	virtual ~FSelectedComponentDragDropOp() {}

	struct FDraggingWidgetReference
	{
		FDMXPixelMappingComponentReference ComponentReference;

		FVector2D DraggedOffset = FVector2D::ZeroVector;
	};

	struct FItem
	{
		/** The component being dragged */
		TWeakObjectPtr<UDMXPixelMappingBaseComponent> Component;

		/** The original parent of the widget. */
		FDMXPixelMappingComponentReference ComponentReference;

		/** The offset of the original click location, as a percentage of the widget's size. */
		FVector2D DraggedOffset = FVector2D::ZeroVector;
	};

	TArray<FItem> DraggedWidgets;

	static TSharedRef<FSelectedComponentDragDropOp> New(TSharedPtr<FDMXPixelMappingToolkit> InToolkit, const TArray<FDraggingWidgetReference>& References);
};

TSharedRef<FSelectedComponentDragDropOp> FSelectedComponentDragDropOp::New(TSharedPtr<FDMXPixelMappingToolkit> InToolkit, const TArray<FDraggingWidgetReference>& References)
{
	TSharedRef<FSelectedComponentDragDropOp> Operation = MakeShared<FSelectedComponentDragDropOp>();

	for (const FDraggingWidgetReference& Reference : References)
	{
		FItem DraggedWidget;
		DraggedWidget.Component = Reference.ComponentReference.GetComponent();
		DraggedWidget.DraggedOffset = Reference.DraggedOffset;
		Operation->DraggedWidgets.Add(DraggedWidget);
		Operation->SetDecoratorVisibility(false);
	}

	Operation->Construct();
	return Operation;
}

void SDMXPixelMappingDesignerView::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
{
	ToolkitWeakPtr = InToolkit;

	bMovingExistingWidget = false;

	DelegateHandleChangeComponents = InToolkit->GetOnComponenetAddedOrDeletedDelegate().AddSP(this, &SDMXPixelMappingDesignerView::HandleChangeComponents);
	OnSelectedComponenetChangedHandle = InToolkit->GetOnSelectedComponenetChangedDelegate().AddRaw(this, &SDMXPixelMappingDesignerView::OnSelectedComponenetChanged);

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
				.Visibility(this, &SDMXPixelMappingDesignerView::GetTitleBarVisibility)
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
							.Text(this, &SDMXPixelMappingDesignerView::GetSelectedComponentParentNameText)
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
							.Text(this, &SDMXPixelMappingDesignerView::GetSelectedComponentNameText)
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
	);

	ZoomToFit(true);

	HittestGrid = MakeShared<FHittestGrid>();
}

SDMXPixelMappingDesignerView::~SDMXPixelMappingDesignerView()
{
	if (DelegateHandleChangeComponents.IsValid())
	{
		if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
		{
			ToolkitPtr->GetOnComponenetAddedOrDeletedDelegate().Remove(DelegateHandleChangeComponents);
			ToolkitPtr->GetOnSelectedComponenetChangedDelegate().Remove(OnSelectedComponenetChangedHandle);
		}
	}
}

FReply SDMXPixelMappingDesignerView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnMouseButtonDown(MyGeometry, MouseEvent);

	bool bFoundWidgetUnderCursor = false;
	{
		// Narrow life scope of FWidgetHitResult so it doesn't keep a hard reference on any widget.
		FComponentHitResult HitResult;
		bFoundWidgetUnderCursor = FindComponentUnderCursor(MyGeometry, MouseEvent, UDMXPixelMappingOutputComponent::StaticClass(), HitResult);
		if (bFoundWidgetUnderCursor)
		{
			SelectedWidgetContextMenuLocation = HitResult.WidgetArranged.Geometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			
			if (UDMXPixelMappingMatrixPixelComponent* MatrixPixelComponent = Cast<UDMXPixelMappingMatrixPixelComponent>(HitResult.Component))
			{
				// If a matrix pixel component is selected and it is locked in designer, select the owning Matrix Component instead
				PendingSelectedComponent = MatrixPixelComponent->IsLockInDesigner() ? MatrixPixelComponent->Parent : HitResult.Component;
			}
			else
			{
				PendingSelectedComponent = HitResult.Component;
			}
		}
	}

	if (bFoundWidgetUnderCursor)
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			ResolvePendingSelectedComponents(MouseEvent);
		}

		DraggingStartPositionScreenSpace = MouseEvent.GetScreenSpacePosition();
	}
	else
	{
		// Clear the selection immediately if we didn't click anything.
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			// Clear any pending selected widgets
			PendingSelectedComponent = nullptr;

			TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin();
			check(ToolkitPtr.IsValid());

			// Switch to parent renderer as a active component
			if (UDMXPixelMappingRendererComponent* RendererComponent = ToolkitPtr->GetActiveRendererComponent())
			{
				TSet<FDMXPixelMappingComponentReference> SelectedComponents;
				SelectedComponents.Add(ToolkitWeakPtr.Pin()->GetReferenceFromComponent(RendererComponent));
				ToolkitWeakPtr.Pin()->SelectComponents(SelectedComponents);
			}
		}
	}

	// Capture mouse for the drag handle and general mouse actions
	return FReply::Handled().PreventThrottling().SetUserFocus(AsShared(), EFocusCause::Mouse).CaptureMouse(AsShared());
}

FReply SDMXPixelMappingDesignerView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (HasMouseCapture() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bMovingExistingWidget = false;
	}
	
	SDMXPixelMappingSurface::OnMouseButtonUp(MyGeometry, MouseEvent);

	ResolvePendingSelectedComponents(MouseEvent);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SDMXPixelMappingDesignerView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetCursorDelta().IsZero())
	{
		return FReply::Unhandled();
	}

	CachedMousePosition = MouseEvent.GetScreenSpacePosition();

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
			if (Component != nullptr && Component->Parent == nullptr)
			{
				bIsRootWidgetSelected = true;
				break;
			}
		}

		if (!bIsRootWidgetSelected)
		{
			bMovingExistingWidget = true;
			//Drag selected widgets
			return FReply::Handled().DetectDrag(AsShared(), EKeys::LeftMouseButton);
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
			if (ToolkitPtr->CanDeleteSelectedComponents(ToolkitPtr->GetSelectedComponents()))
			{
				ToolkitPtr->DeleteSelectedComponents(ToolkitPtr->GetSelectedComponents());
			}
		}
	}

	return FReply::Handled();
}


void SDMXPixelMappingDesignerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SDMXPixelMappingSurface::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	CachedWidgetGeometry.Reset();
	FArrangedWidget WindowWidgetGeometry(PreviewHitTestRoot.ToSharedRef(), GetDesignerGeometry());
	PopulateWidgetGeometryCache(WindowWidgetGeometry);

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

FReply SDMXPixelMappingDesignerView::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDMXPixelMappingSurface::OnDragDetected(MyGeometry, MouseEvent);

	using FDragWidget = FSelectedComponentDragDropOp::FDraggingWidgetReference;

	const TSet<FDMXPixelMappingComponentReference>& SelectedComponents = GetSelectedComponents();

	if (SelectedComponents.Num() > 0)
	{
		TArray<FDragWidget> DraggingWidgetCandidates;

		// Clear any pending selected widgets, the user has already decided what widget they want.
		PendingSelectedComponent = nullptr;

		for (FDMXPixelMappingComponentReference SelectedComponent : SelectedComponents)
		{
			UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(SelectedComponent.GetComponent());
			if (OutputComponent != nullptr &&
				OutputComponent->GetCachedWidget().IsValid())
			{
				FArrangedWidget ArrangedWidget = GetArrangedWidgetFromComponent(OutputComponent);
				SelectedWidgetContextMenuLocation = ArrangedWidget.Geometry.AbsoluteToLocal(DraggingStartPositionScreenSpace);

				FDragWidget DraggingWidget;
				DraggingWidget.ComponentReference = SelectedComponent;
				DraggingWidget.DraggedOffset = SelectedWidgetContextMenuLocation / ArrangedWidget.Geometry.GetLocalSize();
				DraggingWidgetCandidates.Add(DraggingWidget);
			}
		}

		TArray<FDragWidget> DraggingWidgets;

		for (const FDragWidget& Candidate : DraggingWidgetCandidates)
		{
			DraggingWidgets.Add(Candidate);
		}

		ClearExtensionWidgets();

		if (DraggingWidgets.Num())
		{
			TSharedRef<FSelectedComponentDragDropOp> DragOp = FSelectedComponentDragDropOp::New(ToolkitWeakPtr.Pin(), DraggingWidgets);
			return FReply::Handled().BeginDragDrop(DragOp);
		}
	}

	return FReply::Handled();
}

void SDMXPixelMappingDesignerView::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDragEnter(MyGeometry, DragDropEvent);
}

void SDMXPixelMappingDesignerView::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDragLeave(DragDropEvent);
}

FReply SDMXPixelMappingDesignerView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDragOver(MyGeometry, DragDropEvent);

	const bool bIsPreview = true;
	ProcessDropAndAddWidget(MyGeometry, DragDropEvent, bIsPreview);

	return FReply::Handled();
}

FReply SDMXPixelMappingDesignerView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SDMXPixelMappingSurface::OnDrop(MyGeometry, DragDropEvent);

	bMovingExistingWidget = false;

	if (TSharedPtr<FDMXPixelMappingToolkit> ToolkitPtr = ToolkitWeakPtr.Pin())
	{
		// Add 
		if (UDMXPixelMapping* PixelMapping = ToolkitPtr->GetDMXPixelMapping())
		{
			using FDragWidget = FSelectedComponentDragDropOp::FDraggingWidgetReference;

			TSharedPtr<FDMXPixelMappingDragDropOp> TemplateDragDropOp = DragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();

			// Add from Palette
			if (TemplateDragDropOp.IsValid() && TemplateDragDropOp->Component == nullptr)
			{
				// Try to get Active render component
				UDMXPixelMappingBaseComponent* Target = (TemplateDragDropOp->Parent.IsValid()) ? TemplateDragDropOp->Parent.Get() : ToolkitPtr->GetActiveRendererComponent();

				if (TemplateDragDropOp.IsValid())
				{
					if (Target != nullptr && PixelMapping->RootComponent != nullptr)
					{
						TSet<FDMXPixelMappingComponentReference> SelectedComponents;
						// special case for fixture group as we want to allow
						// multiple patches to be created on the fly by dragging
						if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Target))
						{
							for (FDMXEntityFixturePatchRef SelectedFixturePatchRef : FixtureGroupComponent->SelectedFixturePatchRef)
							{
								UDMXPixelMappingFixtureGroupItemComponent* Component = Cast<UDMXPixelMappingFixtureGroupItemComponent>(TemplateDragDropOp->Template->Create(PixelMapping->RootComponent));
								if (Component)
								{
									Component->FixturePatchRef = SelectedFixturePatchRef;
									const FName UniqueName = MakeUniqueObjectName(Component->GetOuter(), Component->GetClass(), FName(Component->FixturePatchRef.GetFixturePatch()->GetDisplayName()));
									const FString NewNameStr = UniqueName.ToString();
									Component->Rename(*NewNameStr);

									Target->AddChild(Component);
									Component->PostParentAssigned();
									SelectedComponents.Add(ToolkitWeakPtr.Pin()->GetReferenceFromComponent(Component));
								}
							}

							// if multiple drop, select the group
							if (SelectedComponents.Num() > 1)
							{
								SelectedComponents.Empty();
								SelectedComponents.Add(ToolkitWeakPtr.Pin()->GetReferenceFromComponent(FixtureGroupComponent));
							}
						}
						else
						{
							UDMXPixelMappingBaseComponent* Component = TemplateDragDropOp->Template->Create(PixelMapping->RootComponent);
							Target->AddChild(Component);
							Component->PostParentAssigned();

							SelectedComponents.Add(ToolkitWeakPtr.Pin()->GetReferenceFromComponent(Component));							
						}

						ToolkitPtr->HandleAddComponent(true);
						ToolkitPtr->SelectComponents(SelectedComponents);
						CreateExtensionWidgetsForSelection();
					}
				}
			}
		}
	}

	CachedRendererComponent = nullptr;
	UpdateOutput(false);

	return FReply::Handled();
}

bool SDMXPixelMappingDesignerView::FindComponentUnderCursor(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, TSubclassOf<UDMXPixelMappingOutputComponent> FindType, FComponentHitResult& HitResult)
{
	UDMXPixelMappingBaseComponent* ComponentUnderCursor = nullptr;

	TSharedPtr<SGraphNode> ResultNode;
	if (UDMXPixelMapping* PixelMapping = ToolkitWeakPtr.Pin()->GetDMXPixelMapping())
	{
		TArray<UDMXPixelMappingOutputComponent*> OutputComponents;
		PixelMapping->GetAllComponentsOfClass<UDMXPixelMappingOutputComponent>(OutputComponents);

		// Remove null entries
		OutputComponents.RemoveAll([](UDMXPixelMappingOutputComponent* Component) {
			return Component == nullptr;
			});

		// Find ZOrder Values
		TArray<int32> ZOrderValues;
		for (UDMXPixelMappingOutputComponent* OutputComponent : OutputComponents)
		{
			ZOrderValues.AddUnique(OutputComponent->GetZOrder());
		}

		ZOrderValues.Sort([](int32 A, int32 B) {
			return A > B;
			});

		for (int32 ZOrder : ZOrderValues)
		{
			TSet<TSharedRef<SWidget>> SubWidgetsSet;
			for (UDMXPixelMappingOutputComponent* OutputComponent : OutputComponents)
			{
				if (OutputComponent->GetClass()->IsChildOf(FindType) == false)
				{
					continue;
				}

				TSharedPtr<SWidget> Widget = OutputComponent->GetCachedWidget();
				if (!Widget.IsValid())
				{
					continue;
				}

				if (OutputComponent->GetZOrder() != ZOrder)
				{
					continue;
				}

				check(Widget.IsValid());
				SubWidgetsSet.Add(Widget.ToSharedRef());
			}

			TMap<TSharedRef<SWidget>, FArrangedWidget> Result;
			FindChildGeometries(MyGeometry, SubWidgetsSet, Result);

			if (Result.Num() > 0)
			{
				FArrangedChildren ArrangedChildren(EVisibility::Visible);
				Result.GenerateValueArray(ArrangedChildren.GetInternalArray());

				const int32 HoveredIndex = SWidget::FindChildUnderMouse(ArrangedChildren, MouseEvent);
				if (HoveredIndex != INDEX_NONE)
				{
					HitResult.WidgetArranged = ArrangedChildren[HoveredIndex];

					TSharedPtr<SWidget> WidgetUnderCursor = ArrangedChildren[HoveredIndex].Widget;
					HitResult.Component = PixelMapping->FindComponent(WidgetUnderCursor);

					return true;
				}
			}
		}
	}
	
	return false;
}

FArrangedWidget SDMXPixelMappingDesignerView::GetArrangedWidgetFromComponent(UDMXPixelMappingOutputComponent* OutputComponent) const
{
	TSharedPtr<SWidget> WidgetToArrange;

	// Use the parent component for group item and pixel components if they're locked in designer
	if (OutputComponent->bLockInDesigner)
	{
		if (OutputComponent->GetClass() == UDMXPixelMappingFixtureGroupItemComponent::StaticClass() ||
			OutputComponent->GetClass() == UDMXPixelMappingMatrixPixelComponent::StaticClass())
		{
			UDMXPixelMappingOutputComponent* Parent = CastChecked<UDMXPixelMappingOutputComponent>(OutputComponent->Parent);
			WidgetToArrange = Parent->GetCachedWidget();
		}
	}

	if (!WidgetToArrange.IsValid())
	{
		WidgetToArrange = OutputComponent->GetCachedWidget();
	}

	FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
	GetArrangedWidget(WidgetToArrange.ToSharedRef(), ArrangedWidget);

	return ArrangedWidget;
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
		HittestGrid->AddWidget(CurrentWidget.Widget, 0, 0, 0);
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
	TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin();
	check(Toolkit.IsValid());

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

void SDMXPixelMappingDesignerView::HandleChangeComponents(bool bIsSuccess)
{
	CachedRendererComponent = nullptr;
	UpdateOutput(false);
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
	if (const FArrangedWidget* CachedPreviewSurface = CachedWidgetGeometry.Find(PreviewSizeConstraint.ToSharedRef()))
	{
		const FGeometry& RootGeometry = CachedPreviewSurface->Geometry;
		const FVector2D CursorPos = RootGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());

		return FText::Format(LOCTEXT("CursorPositionFormat", "{0} x {1}"), FText::AsNumber(FMath::RoundToInt(CursorPos.X)), FText::AsNumber(FMath::RoundToInt(CursorPos.Y)));
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

void SDMXPixelMappingDesignerView::OnSelectedComponenetChanged()
{
	CreateExtensionWidgetsForSelection();
}

const TSet<FDMXPixelMappingComponentReference>& SDMXPixelMappingDesignerView::GetSelectedComponents() const
{
	check(ToolkitWeakPtr.Pin().IsValid());

	return ToolkitWeakPtr.Pin()->GetSelectedComponents();
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

FText SDMXPixelMappingDesignerView::GetSelectedComponentNameText() const
{
	FDMXPixelMappingComponentReference SelectedComponentRef = GetSelectedComponent();
	UDMXPixelMappingBaseComponent* BaseComponent = SelectedComponentRef.GetComponent();
	if (BaseComponent)
	{
		return FText::FromString(BaseComponent->GetName());
	}

	return FText();
}

FText SDMXPixelMappingDesignerView::GetSelectedComponentParentNameText() const
{
	FDMXPixelMappingComponentReference SelectedComponentRef = GetSelectedComponent();
	UDMXPixelMappingBaseComponent* BaseComponent = SelectedComponentRef.GetComponent();
	if (BaseComponent && BaseComponent->Parent)
	{
		return FText::FromString(BaseComponent->Parent->GetName());
	}

	return FText();
}

EVisibility SDMXPixelMappingDesignerView::GetTitleBarVisibility() const
{
	FDMXPixelMappingComponentReference SelectedComponentRef = GetSelectedComponent();
	UDMXPixelMappingBaseComponent* BaseComponent = SelectedComponentRef.GetComponent();

	if (BaseComponent && BaseComponent->Parent)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

void SDMXPixelMappingDesignerView::ResolvePendingSelectedComponents(const FPointerEvent& MouseEvent)
{
	if (PendingSelectedComponent.IsValid())
	{
		TSet<FDMXPixelMappingComponentReference> SelectedComponents;
		if (MouseEvent.IsShiftDown() || MouseEvent.IsControlDown())
		{
			SelectedComponents = ToolkitWeakPtr.Pin()->GetSelectedComponents();
		}
		SelectedComponents.Add(ToolkitWeakPtr.Pin()->GetReferenceFromComponent(PendingSelectedComponent.Get()));
		ToolkitWeakPtr.Pin()->SelectComponents(SelectedComponents);

		PendingSelectedComponent = nullptr;
	}
}

bool SDMXPixelMappingDesignerView::GetArrangedWidget(TSharedRef<SWidget> Widget, FArrangedWidget& ArrangedWidget) const
{
	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(Widget);
	if (!WidgetWindow.IsValid())
	{
		return false;
	}

	TSharedRef<SWindow> CurrentWindowRef = WidgetWindow.ToSharedRef();

	FWidgetPath WidgetPath;
	if (FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget, WidgetPath))
	{
		ArrangedWidget = WidgetPath.FindArrangedWidget(Widget).Get(FArrangedWidget::GetNullWidget());
		return true;
	}

	return false;
}

void SDMXPixelMappingDesignerView::ProcessDropAndAddWidget(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, const bool bIsPreview)
{
	TSharedPtr<FSelectedComponentDragDropOp> SelectedDragDropOp = DragDropEvent.GetOperationAs<FSelectedComponentDragDropOp>();
	if (SelectedDragDropOp.IsValid() && SelectedDragDropOp->DraggedWidgets.Num() > 0)
	{
		const FSelectedComponentDragDropOp::FItem& DraggedWidget = SelectedDragDropOp->DraggedWidgets[0];

		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(DraggedWidget.Component.Get()))
		{
			FGeometry WidgetUnderCursorGeometry = PreviewSizeConstraint->GetTickSpaceGeometry();
			FVector2D ScreenSpacePosition = DragDropEvent.GetScreenSpacePosition();
			FVector2D LocalPosition = WidgetUnderCursorGeometry.AbsoluteToLocal(ScreenSpacePosition);

			FArrangedWidget ArrangedWidget = GetArrangedWidgetFromComponent(OutputComponent);
			FVector2D Offset = DraggedWidget.DraggedOffset * ArrangedWidget.Geometry.GetLocalSize();

			FVector2D NewPosition = LocalPosition - Offset;

			OutputComponent->SetPosition(NewPosition);

			// Assign a new ZOrder to dropped components
			SDMXPixelMappingDesignerView::FComponentHitResult HitResult;
			FindComponentUnderCursor(MyGeometry, DragDropEvent, UDMXPixelMappingOutputComponent::StaticClass(), HitResult);

			if (UDMXPixelMappingOutputComponent* TargetOutputComponent = Cast<UDMXPixelMappingOutputComponent>(HitResult.Component))
			{
				if (OutputComponent != TargetOutputComponent &&
					OutputComponent->GetZOrder() <= TargetOutputComponent->GetZOrder())
				{
					int32 NewZOrder = TargetOutputComponent->GetZOrder() + 1;

					OutputComponent->Modify();
					OutputComponent->SetZOrder(NewZOrder);
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

	// With the current implementation, only one component could be selected
	if (!Selected.Num())
	{
		return;
	}

	UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Selected[0].GetComponent());
	if (OutputComponent == nullptr)
	{
		return;
	}

	// Add transform handles
	const float Offset = 10;
	TSharedPtr<SDMXPixelMappingDesignerView> Self = SharedThis(this);
	TransformHandles.Add(SNew(SDMXPixelMappingTransformHandle, Self, EDMXPixelMappingTransformDirection::CenterRight, FVector2D(Offset, 0)));
	TransformHandles.Add(SNew(SDMXPixelMappingTransformHandle, Self, EDMXPixelMappingTransformDirection::BottomCenter, FVector2D(0, Offset)));
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

EVisibility SDMXPixelMappingDesignerView::GetExtensionCanvasVisibility() const
{
	for (const FDMXPixelMappingComponentReference& Component : GetSelectedComponents())
	{
		UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component.GetComponent());

		if (OutputComponent == nullptr ||
			OutputComponent->IsVisibleInDesigner() == false ||
			OutputComponent->IsLockInDesigner() == true
			)
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

bool SDMXPixelMappingDesignerView::GetWidgetParentGeometry(FDMXPixelMappingComponentReference ComponentReference, FGeometry& Geometry)
{
	if (const UDMXPixelMappingBaseComponent* ComponentPreview = ComponentReference.GetComponent())
	{
		if (UDMXPixelMappingBaseComponent* Parent = ComponentPreview->Parent)
		{
			return GetComponentGeometry(Parent, Geometry);
		}
	}

	Geometry = GetDesignerGeometry();
	return true;
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
		TSharedPtr<SWidget> CachedPreviewWidget = OutputComponent->GetCachedWidget();
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

	return false;
}

#undef LOCTEXT_NAMESPACE
