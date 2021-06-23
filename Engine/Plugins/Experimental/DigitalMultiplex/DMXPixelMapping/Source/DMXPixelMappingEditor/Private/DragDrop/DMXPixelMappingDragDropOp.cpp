// Copyright Epic Games, Inc. All Rights Reserved.

#include "DragDrop/DMXPixelMappingDragDropOp.h"

#include "DMXPixelMappingComponentWidget.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "DragDrop/DMXPixelMappingGroupChildDragDropHelper.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"


TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(const FVector2D& InGraphSpaceDragOffset, const TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>>& InTemplates, UDMXPixelMappingBaseComponent* InParent)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();

	Operation->Templates = InTemplates;
	Operation->bWasCreatedAsTemplate = true;
	Operation->Parent = InParent;
	Operation->GraphSpaceDragOffset = InGraphSpaceDragOffset;
		
	Operation->Construct();
	Operation->SetDecoratorVisibility(false);

	return Operation;
}

TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(const FVector2D& InGraphSpaceDragOffset, const TArray<UDMXPixelMappingBaseComponent*>& InDraggedComponents)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();

	Operation->bWasCreatedAsTemplate = false;
	Operation->DraggedComponents = InDraggedComponents;
	Operation->GraphSpaceDragOffset = InGraphSpaceDragOffset;
	Operation->GroupChildDragDropHelper = FDMXPixelMappingGroupChildDragDropHelper::Create(Operation); // After setting dragged components

	Operation->Construct();
	Operation->SetDecoratorVisibility(false);

	return Operation;
}

void FDMXPixelMappingDragDropOp::SetDraggedComponents(const TArray<UDMXPixelMappingBaseComponent*>& InDraggedComponents)
{
	DraggedComponents = InDraggedComponents;
	Templates.Reset();

	// Rebuild the group child drag drop helper in case that's what was set
	GroupChildDragDropHelper = FDMXPixelMappingGroupChildDragDropHelper::Create(AsShared());
}

void FDMXPixelMappingDragDropOp::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(DraggedComponents);
}

void FDMXPixelMappingDragDropOp::LayoutOutputComponents(const FVector2D& GraphSpacePosition)
{
	if (DraggedComponents.Num() > 0)
	{
		if (UDMXPixelMappingOutputComponent* FirstComponent = Cast<UDMXPixelMappingOutputComponent>(DraggedComponents[0]))
		{
			const FVector2D Anchor = FirstComponent->GetPosition();

			// Move all to new position
			for (UDMXPixelMappingBaseComponent* Component : DraggedComponents)
			{
				if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(Component))
				{
					if (ensureMsgf(OutputComponent->GetClass() != UDMXPixelMappingMatrixComponent::StaticClass(),
						TEXT("Matrix Cells are not supported. Use the GroupChildDragDropHelper from this class instead")))
					{
						FVector2D AnchorOffset = Anchor - OutputComponent->GetPosition();

						const FVector2D NewPosition = GraphSpacePosition - AnchorOffset - GraphSpaceDragOffset;
						OutputComponent->SetPosition(NewPosition.RoundToVector());
					}
				}
			}
		}
	}
}
