// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/SCustomizableObjectLayoutGrid.h"
#include "MuCOE/CustomizableObjectEditorModule.h"
#include "MuCOE/CustomizableObjectLayout.h"
#include "Styling/AppStyle.h"
#include "Async/TaskGraphInterfaces.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"

#include "Input/CursorReply.h"

#include "Widgets/Input/SNumericEntryBox.h"

// Required for engine branch preprocessor defines.
#include "MuCO/UnrealPortabilityHelpers.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"

/** Simple representation of the backbuffer for drawin UVs. */
class FSlateCanvasRenderTarget : public FRenderTarget
{
public:
	FIntPoint GetSizeXY() const override
	{
		return ViewRect.Size();
	}

	/** Sets the texture that this target renders to */
	void SetRenderTargetTexture(FTexture2DRHIRef& InRHIRef)
	{
		RenderTargetTextureRHI = InRHIRef;
	}

	/** Clears the render target texture */
	void ClearRenderTargetTexture()
	{
		RenderTargetTextureRHI.SafeRelease();
	}

	/** Sets the viewport rect for the render target */
	void SetViewRect(const FIntRect& InViewRect)
	{
		ViewRect = InViewRect;
	}

	/** Gets the viewport rect for the render target */
	const FIntRect& GetViewRect() const
	{
		return ViewRect;
	}

private:
	FIntRect ViewRect;
};


/** Custom Slate drawing element. Holds a copy of all information required to draw UVs. */
class FUVCanvasDrawer : public ICustomSlateElement
{
public:
	~FUVCanvasDrawer()
	{
		delete RenderTarget;
	};

	/** Set the canvas area and all required data to paint the UVs.
	 * 
	 * All data will be copied.
	 */
	void Initialize(const FIntRect& InCanvasRect, const FVector2D& InOrigin, const FVector2D& InSize, const TArray<FVector2f>& InUVLayout);
	
private:
	void DrawRenderThread(FRHICommandListImmediate& RHICmdList, const void* RenderTarget) override;

	/** SlateElement initialized, can Draw during the DrawRenderThread call. */
	bool Initialized = false;

	/** Drawing origin. */
	FVector2D Origin;

	/** Drawing size. */
	FVector2D Size;

	/** Drawing UVLayout. */
	TArray<FVector2D> UVLayout;

	FSlateCanvasRenderTarget* RenderTarget = new FSlateCanvasRenderTarget();
};


void SCustomizableObjectLayoutGrid::Construct( const FArguments& InArgs )
{
	GridSize = InArgs._GridSize;
	Blocks = InArgs._Blocks;
	UVLayout = InArgs._UVLayout;
	UnassignedUVLayoutVertices = InArgs._UnassignedUVLayoutVertices;
	Mode = InArgs._Mode;
	BlockChangedDelegate = InArgs._OnBlockChanged;
	SelectionChangedDelegate = InArgs._OnSelectionChanged;
	SelectionColor = InArgs._SelectionColor;
	DeleteBlocksDelegate = InArgs._OnDeleteBlocks;
	AddBlockAtDelegate = InArgs._OnAddBlockAt;
	OnSetBlockPriority = InArgs._OnSetBlockPriority;

	HasDragged = false;
	Dragging = false;
	Resizing = false;
	ResizeCursor = false;
	Selecting = false;

	PaddingAmount = FVector2D::Zero();
	DistanceFromOrigin = FVector2D::Zero();
	Zoom = 1;

	UVCanvasDrawer = TSharedPtr<FUVCanvasDrawer, ESPMode::ThreadSafe>(new FUVCanvasDrawer());
}


SCustomizableObjectLayoutGrid::~SCustomizableObjectLayoutGrid()
{
	// UVCanvasDrawer can only be destroyed after drawing the last command
	ENQUEUE_RENDER_COMMAND(SafeDeletePreviewElement)(
		[UVCanvasDrawer = UVCanvasDrawer](FRHICommandListImmediate& RHICmdList) mutable
		{
			UVCanvasDrawer.Reset();
		}
	);
}


int32 SCustomizableObjectLayoutGrid::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	int32 RetLayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId,InWidgetStyle, bParentEnabled );

	bool bEnabled = ShouldBeEnabled( bParentEnabled );
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// Paint inside the border only. 
	const FVector2D BorderPadding = FVector2D(2,2);
	FPaintGeometry ForegroundPaintGeometry = AllottedGeometry.ToInflatedPaintGeometry( -BorderPadding );
	
	const FIntPoint GridSizePoint = GridSize.Get();
	const float OffsetX = BorderPadding.X;
	const FVector2D AreaSize =  AllottedGeometry.GetLocalSize() - 2.0f * BorderPadding;
	const float GridRatio = float(GridSizePoint.X) / float(GridSizePoint.Y);
	FVector2D Size;
	if ( AreaSize.X/GridRatio > AreaSize.Y )
	{
		Size.Y = AreaSize.Y;
		Size.X = AreaSize.Y*GridRatio;
	}
	else
	{
		Size.X =  AreaSize.X;
		Size.Y =  AreaSize.X/GridRatio;
	}

	FVector2D OldSize = Size;
	Size *= Zoom;

	float AuxCellSize = Size.X / GridSizePoint.X;
	
	// Drawing Offsets
	FVector2D Offset = (AreaSize-Size)/2.0f;
	FVector2D ZoomOffset = ((Size - OldSize) / 2.0f);
	
	// Drawing Origin
	FVector2D Origin = BorderPadding + Offset + PaddingAmount - DistanceFromOrigin;

	// Create line points
	TArray< FVector2D > LinePoints;
	LinePoints.SetNum(2);
	
	// Drawing Vertical Lines
	for( int32 LineIndex = 0; LineIndex < GridSizePoint.X + 1; LineIndex++ )
	{
		LinePoints[0] = FVector2D( Origin.X + LineIndex * AuxCellSize, Origin.Y );
		LinePoints[1] = FVector2D( Origin.X + LineIndex * AuxCellSize, Origin.Y + Size.Y );

		FSlateDrawElement::MakeLines( OutDrawElements,RetLayerId,AllottedGeometry.ToPaintGeometry(),
			LinePoints, ESlateDrawEffect::None,	FColor(150, 150, 150, 64), false, 2.0);
	}

	// Drawing Horizontal Lines
	for( int32 LineIndex = 0; LineIndex < GridSizePoint.Y + 1; LineIndex++ )
	{
		LinePoints[0] = FVector2D( Origin.X, Origin.Y + LineIndex * AuxCellSize );
		LinePoints[1] = FVector2D( Origin.X + Size.X, Origin.Y + LineIndex * AuxCellSize );

		FSlateDrawElement::MakeLines( OutDrawElements, RetLayerId, AllottedGeometry.ToPaintGeometry(),
			LinePoints,	ESlateDrawEffect::None, FColor(150, 150, 150, 64), false, 2.0);
	}

	RetLayerId++;

	// Draw UV using a CustomSlateElement on the RenderThread
	const float CanvasMinX = FMath::Max(0.0f, AllottedGeometry.AbsolutePosition.X);
	const float CanvasMinY = FMath::Max(0.0f, AllottedGeometry.AbsolutePosition.Y);
	const FIntRect CanvasRect(
		FMath::RoundToInt(CanvasMinX),
		FMath::RoundToInt(CanvasMinY),
		FMath::RoundToInt(CanvasMinX + AllottedGeometry.GetLocalSize().X * AllottedGeometry.Scale),
		FMath::RoundToInt(CanvasMinY + AllottedGeometry.GetLocalSize().Y * AllottedGeometry.Scale));
	
	UVCanvasDrawer->Initialize(CanvasRect, Origin * AllottedGeometry.Scale, Size * AllottedGeometry.Scale, UVLayout);
	FSlateDrawElement::MakeCustom(OutDrawElements, RetLayerId, UVCanvasDrawer);

	const auto MakeYellowSquareLine = [&](const TArray<FVector2D>& Points) -> void
	{
		FSlateDrawElement::MakeLines( OutDrawElements, RetLayerId, AllottedGeometry.ToPaintGeometry(),
			Points,	ESlateDrawEffect::None,	FColor(250, 230, 43, 255), true, 2.0);
	};

	TArray<FVector2D> SquareLinePoints;
	SquareLinePoints.SetNum(2);

	const FVector2D CrossSize = Size*0.01;
	for (const FVector2f& Vertex : UnassignedUVLayoutVertices)
	{
		SquareLinePoints[0] = (Origin + FVector2D(Vertex) * Size + FVector2D(CrossSize));
		SquareLinePoints[1]	= (Origin + FVector2D(Vertex) * Size - FVector2D(CrossSize) * FVector2D(1.0f, -1.0f));
		MakeYellowSquareLine(SquareLinePoints);
		
		SquareLinePoints[0]	= (Origin + FVector2D(Vertex) * Size - FVector2D(CrossSize));
		MakeYellowSquareLine(SquareLinePoints);

		SquareLinePoints[1]	= (Origin + FVector2D(Vertex) * Size + FVector2D(CrossSize) * FVector2D(1.0f, -1.0f));
		MakeYellowSquareLine(SquareLinePoints);
		
		SquareLinePoints[0]	= (Origin + FVector2D(Vertex) * Size + FVector2D(CrossSize));
		MakeYellowSquareLine(SquareLinePoints);
	}


	// Blocks
	const FSlateBrush* BlockBrush = FAppStyle::GetBrush("TextBlock.HighlightShape");
	const FVector2f PaintGeomPosition = AllottedGeometry.ToPaintGeometry().DrawPosition;
	const float PaintGeomScale = AllottedGeometry.ToPaintGeometry().DrawScale;
	
	for (const FCustomizableObjectLayoutBlock& Block : Blocks.Get())
	{
		FSlateRenderTransform GeomTransform = FSlateRenderTransform(1.0f, FVector2D(PaintGeomPosition + BlockRects[Block.Id].Rect.Min * PaintGeomScale));
		FPaintGeometry Geom(FSlateLayoutTransform(), GeomTransform, FVector2D(BlockRects[Block.Id].Rect.Size * PaintGeomScale), false );
		
		FSlateDrawElement::MakeBox(	OutDrawElements, RetLayerId, Geom, BlockBrush,
			DrawEffects, SelectedBlocks.Find(Block.Id)>=0? SelectionColor :FColor(230,199,75,155) );	

		if (Mode==ELGM_Edit)
		{
			FSlateRenderTransform HandleGeomTransform = FSlateRenderTransform(1.0f, FVector2D(PaintGeomPosition + BlockRects[Block.Id].HandleRect.Min * PaintGeomScale));
			FPaintGeometry HandleGeom(FSlateLayoutTransform(), HandleGeomTransform, FVector2D(BlockRects[Block.Id].HandleRect.Size * PaintGeomScale), false);
			
			FColor HandleRectColor = FColor(255, 96, 96, 255);
			bool bCanResize = SelectedBlocks.Num() == 1 && SelectedBlocks.Contains(Block.Id) && MouseOnBlock(Block.Id, CurrentMousePosition, true);

			if (bCanResize)
			{
				HandleRectColor = FColor(200, 0, 0, 255);
			}

			FSlateDrawElement::MakeBox(	OutDrawElements, RetLayerId, HandleGeom, BlockBrush,
				DrawEffects, HandleRectColor);


			//Selected block outline
			if (SelectedBlocks.Find(Block.Id) >= 0)
			{
				TArray<FVector2D> SelectionSquarePoints;
				SelectionSquarePoints.SetNum(2);

				FVector2D RectMin = FVector2D(BlockRects[Block.Id].Rect.Min);
				FVector2D RectMax = FVector2D(BlockRects[Block.Id].Rect.Min + BlockRects[Block.Id].Rect.Size);

				FVector2D TopRightCorner = FVector2D(RectMax.X, RectMin.Y);
				FVector2D BottomLeftCorner = FVector2D(RectMin.X, RectMax.Y);

				SelectionSquarePoints[0] = RectMin;
				SelectionSquarePoints[1] = TopRightCorner;
				MakeYellowSquareLine(SelectionSquarePoints);

				SelectionSquarePoints[0] = RectMax;
				MakeYellowSquareLine(SelectionSquarePoints);
				
				SelectionSquarePoints[1] = BottomLeftCorner;
				MakeYellowSquareLine(SelectionSquarePoints);
				
				SelectionSquarePoints[0] = RectMin;
				MakeYellowSquareLine(SelectionSquarePoints);
			}
		}
	}

	RetLayerId++;

	// Drawing Multi-Selection rect
	if (Mode == ELGM_Edit && Selecting)
	{
		TArray<FVector2D> SelectionSquarePoints;
		SelectionSquarePoints.SetNum(2);

		FVector2D RectMin = FVector2D(SelectionRect.Min);
		FVector2D RectSize = FVector2D(SelectionRect.Size);

		FVector2D TopLeft = RectMin;
		FVector2D TopRight = RectMin + FVector2D(RectSize.X, 0.0f);
		FVector2D BottomRight = RectMin + RectSize;
		FVector2D BottomLeft = RectMin + FVector2D(0.0f, RectSize.Y);

		SelectionSquarePoints[0] = TopLeft;
		SelectionSquarePoints[1] = TopRight;
		MakeYellowSquareLine(SelectionSquarePoints);

		SelectionSquarePoints[0] = BottomRight;
		MakeYellowSquareLine(SelectionSquarePoints);

		SelectionSquarePoints[1] = BottomLeft;
		MakeYellowSquareLine(SelectionSquarePoints);

		SelectionSquarePoints[0] = TopLeft;
		MakeYellowSquareLine(SelectionSquarePoints);
	}

	RetLayerId++;

	return RetLayerId - 1;
}


void SCustomizableObjectLayoutGrid::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	const FVector2D BorderPadding = FVector2D(2,2);
	const FVector2D AreaSize =  AllottedGeometry.Size - 2.0f * BorderPadding;
	const float GridRatio = float(GridSize.Get().X)/float(GridSize.Get().Y);
	FVector2D Size;
	if ( AreaSize.X/GridRatio > AreaSize.Y )
	{
		Size.Y = AreaSize.Y;
		Size.X = AreaSize.Y*GridRatio;
	}
	else
	{
		Size.X =  AreaSize.X;
		Size.Y =  AreaSize.X/GridRatio;
	}

	FVector2D OldSize = Size;
	Size *= Zoom;
	CellSize = Size.X/GridSize.Get().X;
	FVector2D Offset = (AreaSize-Size)/2.0f;
	FVector2D ZoomOffset = (Size - OldSize) / 2.0f;
	FVector2D Origin = BorderPadding + Offset + PaddingAmount - DistanceFromOrigin;
	DrawOrigin = Origin;

	BlockRects.Empty();

	const TArray<FCustomizableObjectLayoutBlock>& CurrentBlocks = Blocks.Get();
	for (const FCustomizableObjectLayoutBlock& Block : CurrentBlocks)
	{
		const FVector2f BlockMin(Block.Min);
		const FVector2f BlockMax(Block.Max);

		FBlockWidgetData BlockData;
		BlockData.Rect.Min = FVector2f(Origin) + BlockMin * CellSize + CellSize * 0.1f;
		BlockData.Rect.Size = (BlockMax - BlockMin) * CellSize - CellSize * 0.2f;

		float HandleRectSize = FMath::Log2(float(GridSize.Get().X))/10.0f;
		BlockData.HandleRect.Size = FVector2f(CellSize) * HandleRectSize;
		BlockData.HandleRect.Min = BlockData.Rect.Min + BlockData.Rect.Size - BlockData.HandleRect.Size;

		BlockRects.Add(Block.Id, BlockData );
	}

	// Update selection list
	for (int i=0; i<SelectedBlocks.Num();)
	{
		bool Found = false;
		for (const FCustomizableObjectLayoutBlock& Block : CurrentBlocks)
		{
			if (Block.Id == SelectedBlocks[i])
			{
				Found = true;
			}
		}

		if ( !Found )
		{
			SelectedBlocks.RemoveAt(i);
		}
		else
		{
			++i;
		}
	}

	if (Selecting)
	{
		CalculateSelectionRect();
	}

	SCompoundWidget::Tick( AllottedGeometry, InCurrentTime, InDeltaTime );
}


FReply SCustomizableObjectLayoutGrid::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (Mode == ELGM_Edit)
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			HasDragged = false;
			Dragging = false;
			Resizing = false;

			// To know if we clicked on a block
			bool ClickOnBlock = false;

			FVector2D Pos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			InitSelectionRect = Pos;

			//Reset Selection Rect
			SelectionRect.Size = FVector2f::Zero();
			SelectionRect.Min = FVector2f(Pos);

			// Handles selection must be detected on mouse down
			// We also check if we click on a block
			TArray<FGuid> SelectedBlockHandles;

			for (const FGuid& BlockId : SelectedBlocks)
			{
				if (MouseOnBlock(BlockId, Pos, true))
				{
					SelectedBlockHandles.Add(BlockId);
				}

				if (MouseOnBlock(BlockId, Pos))
				{
					if (SelectedBlocks.Contains(BlockId))
					{
						ClickOnBlock = true;
					}
				}
			}

			if (SelectedBlocks.Num() && ClickOnBlock)
			{
				Dragging = true;
				DragStart = Pos;

				if (SelectedBlocks.Num() == 1 && SelectedBlockHandles.Contains(SelectedBlocks[0]))
				{
					Resizing = true;
				}
			}
		}
		else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			//Mouse position
			FVector2D Pos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			FVector2D CellDelta = (Pos - DrawOrigin) / CellSize;

			// Create context menu
			const bool CloseAfterSelection = true;
			FMenuBuilder MenuBuilder(CloseAfterSelection, NULL, TSharedPtr<FExtender>(), false, &FCoreStyle::Get(), false);

			MenuBuilder.BeginSection("Block Management", LOCTEXT("GridActionsTitle", "Grid Actions"));
			{
				if (SelectedBlocks.Num())
				{
					FUIAction DeleteAction(FExecuteAction::CreateSP(this, &SCustomizableObjectLayoutGrid::DeleteSelectedBlocks));
					MenuBuilder.AddMenuEntry(LOCTEXT("DeleteBlocksLabel", "Delete"), LOCTEXT("DeleteBlocksTooltip", "Delete Selected Blocks"), FSlateIcon(), DeleteAction);

					FUIAction DuplicateAction(FExecuteAction::CreateSP(this, &SCustomizableObjectLayoutGrid::DuplicateBlocks));
					MenuBuilder.AddMenuEntry(LOCTEXT("DuplicateBlocksLabel", "Duplicate"), LOCTEXT("DuplicateBlocksTooltip", "Duplicate Selected Blocks"), FSlateIcon(), DuplicateAction);
				}
				else
				{
					FUIAction AddNewBlockAction(FExecuteAction::CreateSP(this, &SCustomizableObjectLayoutGrid::GenerateNewBlock, CellDelta));
					MenuBuilder.AddMenuEntry(LOCTEXT("AddNewBlockLabel", "Add Block"), LOCTEXT("AddNewBlockTooltip", "Add New Block"), FSlateIcon(), AddNewBlockAction);
				}
			}
			MenuBuilder.EndSection();

			MenuBuilder.BeginSection("Fixed Layout Strategy", LOCTEXT("BlockActionsTitle", "Fixed Layout Actions"));
			{
				if (SelectedBlocks.Num() && LayoutStrategy == ECustomizableObjectTextureLayoutPackingStrategy::Fixed)
				{
					MenuBuilder.AddWidget(
						SNew(SBox)
						.WidthOverride(125.0f)
						[
							SNew(SNumericEntryBox<int32>)
							.MinValue(0)
						.MaxValue(INT_MAX)
						.MaxSliderValue(100)
						.AllowSpin(SelectedBlocks.Num() == 1)
						.Value(this, &SCustomizableObjectLayoutGrid::GetBlockPriortyValue)
						.UndeterminedString(LOCTEXT("MultipleValues", "Multiples Values"))
						.OnValueChanged(this, &SCustomizableObjectLayoutGrid::OnBlockPriorityChanged)
						.ToolTipText(LOCTEXT("SetBlockPriorityTooltip", "Sets the block priority for a Fixed Layout Strategy"))
						.EditableTextBoxStyle(&FAppStyle::GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
						]
					, FText::FromString("Block Priority"), true);
				}
			}
			MenuBuilder.EndSection();

			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}
		else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
		{
			if (Zoom == 2)
			{
				Padding = true;
				PaddingStart = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			}
		}
	}

	return SCompoundWidget::OnMouseButtonDown( MyGeometry, MouseEvent );
}


FReply SCustomizableObjectLayoutGrid::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (Mode == ELGM_Show)
	{
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	if ( MouseEvent.GetEffectingButton()==EKeys::LeftMouseButton)
	{
		Dragging = false;
		Resizing = false;

		// Left Shif is pressed for multi selection
		bool bLeftShift = MouseEvent.GetModifierKeys().IsLeftShiftDown();

		// Screen to Widget Position
		FVector2D Pos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		
		// Selection before reset
		TArray<FGuid> OldSelection = SelectedBlocks;
		TArray<FGuid> OldPossibleSelection = PossibleSelectedBlocks;

		PossibleSelectedBlocks.Reset();

		// Reset selection if multi selection is not enabled
		if (Mode == ELGM_Edit && !bLeftShift && !HasDragged)
		{
			// Only one selected block allowed in edit mode.
			SelectedBlocks.Reset();
		}

		if (!Selecting)
		{
			if (!HasDragged)
			{
				//Backward iteration to select the block rendered in front of the rest
				const TArray<FCustomizableObjectLayoutBlock>& CurrentBlocks = Blocks.Get();
				for (int32 i = CurrentBlocks.Num() - 1; i > -1; --i)
				{
					if (MouseOnBlock(CurrentBlocks[i].Id, Pos))
					{
						PossibleSelectedBlocks.Add(CurrentBlocks[i].Id);
					}
				}

				bool bSameSelection = PossibleSelectedBlocks == OldPossibleSelection;

				for (int32 i = 0; i < PossibleSelectedBlocks.Num(); ++i)
				{
					if (bLeftShift || Mode == ELGM_Select)
					{
						if (PossibleSelectedBlocks.Num() == 1)
						{
							if (SelectedBlocks.Contains(PossibleSelectedBlocks[i]))
							{
								SelectedBlocks.Remove(PossibleSelectedBlocks[i]);
							}
							else
							{
								SelectedBlocks.Add(PossibleSelectedBlocks[i]);
								break;
							}
						}
						else
						{
							if (!SelectedBlocks.Contains(PossibleSelectedBlocks[i]))
							{
								SelectedBlocks.Add(PossibleSelectedBlocks[i]);
								break;
							}
						}
					}
					else
					{
						if (OldSelection.Num() == 0)
						{
							SelectedBlocks.Add(PossibleSelectedBlocks[0]);
						}

						if (bSameSelection)
						{
							if (OldSelection.Contains(PossibleSelectedBlocks[i]))
							{
								SelectedBlocks.Remove(PossibleSelectedBlocks[i]);

								if (i == PossibleSelectedBlocks.Num() - 1)
								{
									SelectedBlocks.Add(PossibleSelectedBlocks[0]);
									break;
								}
								else
								{
									SelectedBlocks.Add(PossibleSelectedBlocks[i + 1]);
								}
							}
						}
						else
						{
							if (OldSelection.Contains(PossibleSelectedBlocks[i]) && PossibleSelectedBlocks.Num() > 1)
							{
								SelectedBlocks.Remove(PossibleSelectedBlocks[i]);
							}
							else
							{
								SelectedBlocks.AddUnique(PossibleSelectedBlocks[i]);
								break;
							}
						}
					}
				}
			}
		}
		else
		{
			FBox2D SelectRect(FVector2D(SelectionRect.Min), FVector2D(SelectionRect.Min + SelectionRect.Size) );
			
			const TArray<FCustomizableObjectLayoutBlock>& CurrentBlocks = Blocks.Get();
			for (int32 i = 0; i < CurrentBlocks.Num(); ++i)
			{
				FBox2D CurrentBlock(FVector2D(BlockRects[CurrentBlocks[i].Id].Rect.Min), FVector2D(BlockRects[CurrentBlocks[i].Id].Rect.Min + BlockRects[CurrentBlocks[i].Id].Rect.Size));
				
				if (SelectedBlocks.Contains(CurrentBlocks[i].Id))
				{
					if (!SelectRect.Intersect(CurrentBlock) && !bLeftShift)
					{
						SelectedBlocks.Remove(CurrentBlocks[i].Id);
					}
				}
				else
				{
					if (SelectRect.Intersect(CurrentBlock))
					{
						SelectedBlocks.Add(CurrentBlocks[i].Id);
					}
				}
			}
		}

		// Executing selection delegate
		if (OldSelection != SelectedBlocks)
		{
			SelectionChangedDelegate.ExecuteIfBound(SelectedBlocks);
		}

		HasDragged = false;
		Selecting = false;
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		Padding = false;
	}

	return SCompoundWidget::OnMouseButtonUp( MyGeometry, MouseEvent );
}


FReply SCustomizableObjectLayoutGrid::OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	CurrentMousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (Mode != ELGM_Edit)
	{
		return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
	}

	if(MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		FVector2D Pos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

		if (Dragging && SelectedBlocks.Num())
		{
			FVector2D CellDelta = (Pos - DragStart) / CellSize;

			int CellDeltaX = CellDelta.X;
			int CellDeltaY = CellDelta.Y;

			DragStart += FVector2D(CellDeltaX * CellSize, CellDeltaY * CellSize);

			if (CellDeltaX || CellDeltaY)
			{
				HasDragged = true;

				const TArray<FCustomizableObjectLayoutBlock>& CurrentBlocks = Blocks.Get();

				if (!Resizing)
				{
					FIntRect TotalBlock;
					bool bFirstBlock = true;

					for (const FCustomizableObjectLayoutBlock& B : CurrentBlocks)
					{
						FIntRect Block(B.Min, B.Max);

						if (SelectedBlocks.Contains(B.Id))
						{
							if (bFirstBlock)
							{
								TotalBlock = Block;
								bFirstBlock = false;
							}

							TotalBlock.Min.X = FMath::Min(TotalBlock.Min.X, Block.Min.X);
							TotalBlock.Min.Y = FMath::Min(TotalBlock.Min.Y, Block.Min.Y);
							TotalBlock.Max.X = FMath::Max(TotalBlock.Max.X, Block.Max.X);
							TotalBlock.Max.Y = FMath::Max(TotalBlock.Max.Y, Block.Max.Y);
						}
					}

					FIntPoint Grid = GridSize.Get();
					FIntRect BlockMovement = TotalBlock;
					BlockMovement.Min.X = FMath::Max(0, FMath::Min(TotalBlock.Min.X + CellDeltaX, Grid.X - TotalBlock.Size().X));
					BlockMovement.Min.Y = FMath::Max(0, FMath::Min(TotalBlock.Min.Y + CellDeltaY, Grid.Y - TotalBlock.Size().Y));

					BlockMovement.Max = BlockMovement.Min + TotalBlock.Size();

					FIntRect AddMovement = BlockMovement - TotalBlock;

					for (const FCustomizableObjectLayoutBlock& B : CurrentBlocks)
					{
						if (SelectedBlocks.Find(B.Id) != INDEX_NONE)
						{
							FIntRect ResultBlock(B.Min, B.Max);
							ResultBlock.Max += AddMovement.Max;
							ResultBlock.Min += AddMovement.Min;

							BlockChangedDelegate.ExecuteIfBound(B.Id, ResultBlock);
						}
					}
				}
				else
				{
					for (const FCustomizableObjectLayoutBlock& B : CurrentBlocks)
					{
						FIntRect Block;
						for (const FGuid& Id : SelectedBlocks)
						{
							if (B.Id != Id)
							{
								continue;
							}

							Block.Min = B.Min;
							Block.Max = B.Max;

							FIntRect InitialBlock = Block;

							FIntPoint Grid = GridSize.Get();

							FIntPoint BlockSize = Block.Size();
							Block.Max.X = FMath::Max(Block.Min.X + 1, FMath::Min(Block.Max.X + CellDeltaX, Grid.X));
							Block.Max.Y = FMath::Max(Block.Min.Y + 1, FMath::Min(Block.Max.Y + CellDeltaY, Grid.Y));

							if (Block != InitialBlock)
							{
								BlockChangedDelegate.ExecuteIfBound(Id, Block);
							}

							break;
						}
					}
				}
			}
		}

		if (!Selecting && !Dragging)
		{
			bool ClickOnBlock = false;

			for (const FGuid& BlockId : SelectedBlocks)
			{
				if (MouseOnBlock(BlockId, Pos))
				{
					if (SelectedBlocks.Contains(BlockId))
					{
						ClickOnBlock = true;
					}
				}
			}

			int32 MovementSensitivity = 4;
			FVector2D MouseDiference = InitSelectionRect - Pos;
			MouseDiference = MouseDiference.GetAbs();

			if (!ClickOnBlock && (MouseDiference.X > MovementSensitivity || MouseDiference.Y > MovementSensitivity))
			{
				HasDragged = true;
				Selecting = true;
			}
		}
	}
	
	if (!Dragging && !Resizing && SelectedBlocks.Num()==1)
	{
		const TArray<FCustomizableObjectLayoutBlock>& CurrentBlocks = Blocks.Get();
		for (int i = CurrentBlocks.Num() - 1; i > -1; --i)
		{
			// Check for new created blocks
			if (BlockRects.Contains(CurrentBlocks[i].Id) && SelectedBlocks.Contains(CurrentBlocks[i].Id))
			{
				FVector2D Pos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
				if (MouseOnBlock(CurrentBlocks[i].Id, Pos, true))
				{
					ResizeCursor = true;
					break;
				}
			}

			ResizeCursor = false;
		}
	}

	// In case we lose focus
	if (Padding)
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton))
		{
			FVector2D Pos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			PaddingAmount += Pos - PaddingStart;
			PaddingStart = Pos;
		}
		else
		{
			Padding = false;
		}
	}

	if (!MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		Selecting = false;
		Dragging = false;

		if (Resizing)
		{
			ResizeCursor = false;
			Resizing = false;
		}
	}

	return SCompoundWidget::OnMouseMove( MyGeometry, MouseEvent );
}


FReply SCustomizableObjectLayoutGrid::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Mode == ELGM_Edit)
	{
		if (MouseEvent.GetWheelDelta() > 0)
		{
			if (Zoom < 2)
			{
				FVector2D GridCenter = DrawOrigin + (FVector2D((float)GridSize.Get().X, (float)GridSize.Get().Y) / 2.0f) * CellSize;
				DistanceFromOrigin = CurrentMousePosition - GridCenter;

				Zoom++;
			}
		}
		else
		{
			if (Zoom > 1)
			{
				DistanceFromOrigin = FVector2D::Zero();
				PaddingAmount = FVector2D::Zero();

				Zoom--;
			}
		}
	}

	return SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
}


FReply SCustomizableObjectLayoutGrid::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Mode != ELGM_Edit)
	{
		return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}
	
	if (InKeyEvent.IsLeftControlDown())
	{
		if (InKeyEvent.GetKey() == EKeys::D)
		{
			DuplicateBlocks();
		}
		else if (InKeyEvent.GetKey() == EKeys::N)
		{
			FVector2D MouseToCellPosition = (CurrentMousePosition - DrawOrigin) / CellSize;
			GenerateNewBlock(MouseToCellPosition);
		}
	}

	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		DeleteSelectedBlocks();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}


FCursorReply SCustomizableObjectLayoutGrid::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (ResizeCursor)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
	}
	else
	{
		return FCursorReply::Cursor(EMouseCursor::Default);
	}

	return FCursorReply::Unhandled();
}


FVector2D SCustomizableObjectLayoutGrid::ComputeDesiredSize(float NotUsed) const
{
	return FVector2D(200.0f, 200.0f);
}


void SCustomizableObjectLayoutGrid::SetSelectedBlock(FGuid block )
{
	SelectedBlocks.Reset();
	SelectedBlocks.Add( block );
}


void SCustomizableObjectLayoutGrid::SetSelectedBlocks( const TArray<FGuid>& blocks )
{
	SelectedBlocks = blocks;
}


TArray<FGuid> SCustomizableObjectLayoutGrid::GetSelectedBlocks() const
{
	return SelectedBlocks;
}


void SCustomizableObjectLayoutGrid::DeleteSelectedBlocks()
{
	DeleteBlocksDelegate.ExecuteIfBound();
}


void SCustomizableObjectLayoutGrid::GenerateNewBlock(FVector2D MousePosition)
{
	if (MousePosition.X > 0 && MousePosition.Y > 0 && MousePosition.X < GridSize.Get().X && MousePosition.Y < GridSize.Get().Y)
	{
		FIntPoint Min = FIntPoint(MousePosition.X, MousePosition.Y);
		FIntPoint Max = Min + FIntPoint(1, 1);

		AddBlockAtDelegate.ExecuteIfBound(Min, Max);

		SelectedBlocks.Add(Blocks.Get().Last().Id);
	}
}


void SCustomizableObjectLayoutGrid::DuplicateBlocks()
{
	if (SelectedBlocks.Num())
	{
		for (const FCustomizableObjectLayoutBlock& Block : Blocks.Get())
		{
			if (SelectedBlocks.Find(Block.Id) != INDEX_NONE)
			{
				AddBlockAtDelegate.ExecuteIfBound(Block.Min, Block.Max);
			}
		}
	}
}


void SCustomizableObjectLayoutGrid::CalculateSelectionRect()
{
	if (InitSelectionRect.X <= CurrentMousePosition.X)
	{
		if (InitSelectionRect.Y <= CurrentMousePosition.Y)
		{
			SelectionRect.Min = FVector2f(InitSelectionRect);
			SelectionRect.Size = FVector2f(CurrentMousePosition - InitSelectionRect);
		}
		else
		{
			SelectionRect.Min = FVector2f(InitSelectionRect.X, CurrentMousePosition.Y);

			FVector2f AuxVector(CurrentMousePosition.X, InitSelectionRect.Y);
			SelectionRect.Size = AuxVector - SelectionRect.Min;
		}
	}
	else
	{
		if (InitSelectionRect.Y <= CurrentMousePosition.Y)
		{
			SelectionRect.Min = FVector2f(CurrentMousePosition.X, InitSelectionRect.Y);

			FVector2f AuxVector(InitSelectionRect.X, CurrentMousePosition.Y);
			SelectionRect.Size = AuxVector - SelectionRect.Min;
		}
		else
		{
			SelectionRect.Min = FVector2f(CurrentMousePosition);
			SelectionRect.Size = FVector2f(InitSelectionRect - CurrentMousePosition);
		}
	}

}


void SCustomizableObjectLayoutGrid::SetBlocks(const FIntPoint& InGridSize, const TArray<FCustomizableObjectLayoutBlock>& InBlocks)
{
	GridSize = InGridSize;
	Blocks = InBlocks;
}


bool SCustomizableObjectLayoutGrid::MouseOnBlock(FGuid BlockId, FVector2D MousePosition, bool CheckResizeBlock) const
{
	FVector2f Min, Max;
	if (CheckResizeBlock)
	{
		Min = BlockRects[BlockId].HandleRect.Min;
		Max = Min + BlockRects[BlockId].HandleRect.Size;
	}
	else
	{
		Min = BlockRects[BlockId].Rect.Min;
		Max = Min + BlockRects[BlockId].Rect.Size;
	}

	if (MousePosition.X > Min.X && MousePosition.X<Max.X && MousePosition.Y>Min.Y && MousePosition.Y < Max.Y)
	{
		return true;
	}

	return false;
}


TOptional<int32> SCustomizableObjectLayoutGrid::GetBlockPriortyValue() const
{
	if (SelectedBlocks.Num())
	{
		TArray<FCustomizableObjectLayoutBlock> CurrentSelectedBlocks;

		for (const FCustomizableObjectLayoutBlock& Block : Blocks.Get())
		{
			if (SelectedBlocks.Contains(Block.Id))
			{
				CurrentSelectedBlocks.Add(Block);
			}
		}

		int32 BlockPriority = CurrentSelectedBlocks[0].Priority;
		bool bSamePriority = true;

		for (const FCustomizableObjectLayoutBlock& Block : CurrentSelectedBlocks)
		{
			if (Block.Priority != BlockPriority)
			{
				bSamePriority = false;
				break;
			}
		}

		if (bSamePriority)
		{
			return BlockPriority;
		}
	}

	return TOptional<int32>();
}


void SCustomizableObjectLayoutGrid::OnBlockPriorityChanged(int32 InValue)
{
	if (SelectedBlocks.Num())
	{
		OnSetBlockPriority.ExecuteIfBound(InValue);
	}
}


void SCustomizableObjectLayoutGrid::SetLayoutStrategy(ECustomizableObjectTextureLayoutPackingStrategy Strategy)
{
	LayoutStrategy = Strategy;
}


// Canvas Drawer --------------------------------------------------------------

void FUVCanvasDrawer::Initialize(const FIntRect& InCanvasRect, const FVector2D& InOrigin, const FVector2D& InSize, const TArray<FVector2f>& InUVLayout)
{
	Initialized = InCanvasRect.Size().X > 0 && InCanvasRect.Size().Y > 0;
	if (Initialized)
	{
		RenderTarget->SetViewRect(InCanvasRect);

		Origin = InOrigin;
		Size = InSize;

		// Convert data
		UVLayout.SetNum(InUVLayout.Num());
		for (int32 Index=0; Index<InUVLayout.Num(); ++Index)
		{
			UVLayout[Index] = FVector2D(InUVLayout[Index]);
		}
	}
}


void FUVCanvasDrawer::DrawRenderThread(class FRHICommandListImmediate& RHICmdList, const void* InWindowBackBuffer)
{
	//UE crashes if there are no UVs to draw due to DX
	if (Initialized && UVLayout.Num())
	{
		RenderTarget->SetRenderTargetTexture(*(FTexture2DRHIRef*)InWindowBackBuffer);

#if MUTABLE_CLEAN_ENGINE_BRANCH
		FCanvas Canvas(RenderTarget, nullptr, FGameTime(), GMaxRHIFeatureLevel);
#else
		FCanvas Canvas(RenderTarget, nullptr, 0, 0, 0, GMaxRHIFeatureLevel);
#endif

		const uint32 NumEdges = UVLayout.Num() / 2;

		Canvas.SetRenderTargetRect(RenderTarget->GetViewRect());

		FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Line);
		BatchedElements->AddReserveLines(NumEdges);

		const FLinearColor LineColor = FLinearColor::White;
		const FHitProxyId HitProxyId = Canvas.GetHitProxyId();

		FVector LinePoints[2];
		for (uint32 LineIndex = 0; LineIndex < NumEdges; ++LineIndex)
		{
			LinePoints[0] = FVector(Origin + UVLayout[LineIndex * 2 + 0] * Size, 0.0f);
			LinePoints[1] = FVector(Origin + UVLayout[LineIndex * 2 + 1] * Size, 0.0f);

			BatchedElements->AddLine(LinePoints[0], LinePoints[1], LineColor, HitProxyId);
		}

		Canvas.Flush_RenderThread(RHICmdList, true);

		RenderTarget->ClearRenderTargetTexture();
	}
}

#undef LOCTEXT_NAMESPACE
