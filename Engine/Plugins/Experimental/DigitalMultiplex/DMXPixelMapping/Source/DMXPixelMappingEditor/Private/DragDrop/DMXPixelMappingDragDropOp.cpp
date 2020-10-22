// Copyright Epic Games, Inc. All Rights Reserved.

#include "DragDrop/DMXPixelMappingDragDropOp.h"

#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"

TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(const TSharedPtr<FDMXPixelMappingComponentTemplate>& InTemplate, UDMXPixelMappingBaseComponent* InParent)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();

	Operation->Template = InTemplate;
	Operation->Parent = InParent;
	Operation->DefaultHoverText = InTemplate->Name;
	Operation->CurrentHoverText = InTemplate->Name;
	Operation->DragOffset = FVector2D::ZeroVector;

	Operation->Construct();

	return Operation;
}

TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(const TSet<FDMXPixelMappingComponentReference>& InComponentReferences)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();

	Operation->ComponentReferences = InComponentReferences;

	for (const FDMXPixelMappingComponentReference& ComponentReference : InComponentReferences)
	{
		Operation->DefaultHoverText = FText::FromString(ComponentReference.GetComponent()->GetName());
		Operation->CurrentHoverText = FText::FromString(ComponentReference.GetComponent()->GetName());
		break;
	}
	Operation->DragOffset = FVector2D::ZeroVector;

	Operation->Construct();

	return Operation;
}

void FDMXPixelMappingDragDropOp::UpdateDragOffset(const FVector2D& DragStartScreenspacePosition)
{
	FArrangedWidget ArrangedWidget = GetArrangedWidgetFromComponent();
	DragOffset = ArrangedWidget.Geometry.AbsoluteToLocal(DragStartScreenspacePosition);
}

void FDMXPixelMappingDragDropOp::SetComponentReferences(const TSet<FDMXPixelMappingComponentReference>& InComponentReferences)
{
	ComponentReferences = InComponentReferences;
	for (const FDMXPixelMappingComponentReference& ComponentReference : InComponentReferences)
	{
		DefaultHoverText = FText::FromString(ComponentReference.GetComponent()->GetName());
		CurrentHoverText = FText::FromString(ComponentReference.GetComponent()->GetName());
		break;
	}
}

FVector2D FDMXPixelMappingDragDropOp::GetDragOffset() const
{
	return DragOffset;
}

UDMXPixelMappingOutputComponent* FDMXPixelMappingDragDropOp::TryGetOutputComponent() const
{
	for (const FDMXPixelMappingComponentReference& ComponentReference : ComponentReferences)
	{
		return Cast<UDMXPixelMappingOutputComponent>(ComponentReference.GetComponent());
	}

	return nullptr;
}

UDMXPixelMappingBaseComponent* FDMXPixelMappingDragDropOp::TryGetBaseComponent() const
{
	for (const FDMXPixelMappingComponentReference& ComponentReference : ComponentReferences)
	{
		return Cast<UDMXPixelMappingBaseComponent>(ComponentReference.GetComponent());
	}

	return nullptr;
}

FArrangedWidget FDMXPixelMappingDragDropOp::GetArrangedWidgetFromComponent() const
{
	TSharedPtr<SWidget> WidgetToArrange;

	for (const FDMXPixelMappingComponentReference& ComponentReference : ComponentReferences)
	{
		if (UDMXPixelMappingOutputComponent* OutputComponent = Cast<UDMXPixelMappingOutputComponent>(ComponentReference.GetComponent()))
		{
			// Use the parent component for group item and pixel components if they're locked in designer
			if (OutputComponent->bLockInDesigner)
			{
				if (OutputComponent->GetClass() == UDMXPixelMappingFixtureGroupItemComponent::StaticClass() ||
					OutputComponent->GetClass() == UDMXPixelMappingMatrixCellComponent::StaticClass())
				{
					UDMXPixelMappingOutputComponent* ParentOutputComponent = CastChecked<UDMXPixelMappingOutputComponent>(OutputComponent->Parent);
					WidgetToArrange = ParentOutputComponent->GetCachedWidget();
				}
			}

			WidgetToArrange = OutputComponent->GetCachedWidget();
			break;
		}
	}

	FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
	GetArrangedWidget(WidgetToArrange.ToSharedRef(), ArrangedWidget);

	return ArrangedWidget;
}

bool FDMXPixelMappingDragDropOp::GetArrangedWidget(TSharedRef<SWidget> Widget, FArrangedWidget& ArrangedWidget) const
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