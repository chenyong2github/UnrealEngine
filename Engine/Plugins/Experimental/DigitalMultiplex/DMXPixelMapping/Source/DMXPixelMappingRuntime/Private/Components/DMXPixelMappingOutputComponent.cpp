// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingOutputComponent.h"

#include "DMXPixelMappingRuntimeObjectVersion.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "DMXPixelMappingComponentWidget.h"
#endif // WITH_EDITOR


#define LOCTEXT_NAMESPACE "DMXPixelMappingOutputComponent"


const FLinearColor FDMXOutputComponentColors::SelectedColor = FLinearColor::Green;

UDMXPixelMappingOutputComponent::UDMXPixelMappingOutputComponent()
	: CellBlendingQuality(EDMXPixelBlendingQuality::Low)
	, PositionX(0.f)
	, PositionY(0.f)
	, SizeX(1.f)
	, SizeY(1.f)
{
#if WITH_EDITOR
	bLockInDesigner = false;
	bVisibleInDesigner = true;
	ZOrder = 0;
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void UDMXPixelMappingOutputComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, bVisibleInDesigner))
	{
		constexpr bool bSetVisibilityRecursive = true;
		ForEachChildOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* ChildComponent) 
			{
				const EVisibility NewVisiblity = bVisibleInDesigner ? EVisibility::Visible : EVisibility::Collapsed;

				if (ComponentWidget.IsValid())
				{
					ComponentWidget->SetVisibility(NewVisiblity);
				}

				if (TSharedPtr<FDMXPixelMappingComponentWidget> ChildComponentWidget = ChildComponent->GetComponentWidget())
				{
					ChildComponentWidget->SetVisibility(NewVisiblity);
				}
			}, bSetVisibilityRecursive);
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, CellBlendingQuality))
	{
		// Propagonate to children
		constexpr bool bRecursive = true;
		ForEachChild([this](UDMXPixelMappingBaseComponent* ChildComponent)
			{
				if (UDMXPixelMappingOutputComponent* ChildOutputComponent = Cast<UDMXPixelMappingOutputComponent>(ChildComponent))
				{
					ChildOutputComponent->CellBlendingQuality = CellBlendingQuality;
				}
			}, bRecursive);
	}
}
#endif // WITH_EDITOR

void UDMXPixelMappingOutputComponent::PostRemovedFromParent()
{
	Super::PostRemovedFromParent();

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->RemoveFromCanvas();
	}
#endif
}

#if WITH_EDITOR
const FText UDMXPixelMappingOutputComponent::GetPaletteCategory()
{
	ensureMsgf(false, TEXT("You must implement GetPaletteCategory() in your child class"));

	return LOCTEXT("Uncategorized", "Uncategorized");
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<FDMXPixelMappingComponentWidget> UDMXPixelMappingOutputComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	if (!ComponentWidget.IsValid())
	{
		ComponentWidget = MakeShared<FDMXPixelMappingComponentWidget>();
		ComponentWidget->AddToCanvas(InCanvas, ZOrder);
		ComponentWidget->SetPosition(GetPosition());
		ComponentWidget->SetSize(GetSize());
		ComponentWidget->SetColor(GetEditorColor());
		ComponentWidget->SetLabelText(FText::FromString(GetUserFriendlyName()));
	}

	return ComponentWidget.ToSharedRef();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingOutputComponent::SetZOrder(int32 NewZOrder)
{
	const int32 DeltaZOrder = NewZOrder - ZOrder;
	
	ZOrder = NewZOrder;

	// Apply to the UI
	if (TSharedPtr<SConstraintCanvas> Canvas = FindRendererComponentCanvas())
	{
		if (ComponentWidget.IsValid())
		{
			ComponentWidget->SetZOrder(ZOrder);
		}
	}

	constexpr bool bRecursive = true;
	ForEachChild([DeltaZOrder, this](UDMXPixelMappingBaseComponent* ChildComponent)
		{
			if (UDMXPixelMappingOutputComponent* ChildOutputComponent = Cast<UDMXPixelMappingOutputComponent>(ChildComponent))
			{
				const int32 NewChildZOrder = ChildOutputComponent->GetZOrder() + DeltaZOrder;
				if (TSharedPtr<FDMXPixelMappingComponentWidget> ChildComponentWidget = ChildOutputComponent->GetComponentWidget())
				{
					ChildOutputComponent->SetZOrder(NewChildZOrder);

					// Apply to the UI
					if (ChildOutputComponent->ComponentWidget.IsValid())
					{
						ChildOutputComponent->ComponentWidget->SetZOrder(ZOrder);
					}
				}
			}
		}, true);

}
#endif // WITH_EDITOR

bool UDMXPixelMappingOutputComponent::IsOverParent() const
{
	// By default all components are over their parent.
	// E.g. Renderer is always over the root, group is always over the renderer.
	return true;
}

bool UDMXPixelMappingOutputComponent::IsOverPosition(const FVector2D& OtherPosition) const
{
	return
		PositionX <= OtherPosition.X &&
		PositionY <= OtherPosition.Y &&
		PositionX + SizeX >= OtherPosition.X &&
		PositionY + SizeY >= OtherPosition.Y;
}

bool UDMXPixelMappingOutputComponent::OverlapsComponent(UDMXPixelMappingOutputComponent* Other) const
{
	if (Other)
	{
		FVector2D ThisPosition = GetPosition();
		FVector2D OtherPosition = Other->GetPosition();

		FBox2D ThisBox = FBox2D(ThisPosition, ThisPosition + GetSize());
		FBox2D OtherBox = FBox2D(OtherPosition, OtherPosition + Other->GetSize());

		return ThisBox.Intersect(OtherBox);
	}

	return false;
}

void UDMXPixelMappingOutputComponent::SetPosition(const FVector2D& Position) 
{
	ensureMsgf(0, TEXT("SetPosition needs to be implemented in child classes."));
}

void UDMXPixelMappingOutputComponent::SetSize(const FVector2D& Size) 
{
	ensureMsgf(0, TEXT("SetSize needs to be implemented in child classes."));
}

UDMXPixelMappingRendererComponent* UDMXPixelMappingOutputComponent::FindRendererComponent() const
{
	UDMXPixelMappingRendererComponent* ParentRendererComponent = nullptr;

	if (Parent)
	{
		for (UDMXPixelMappingBaseComponent* ParentComponent = Parent; Parent; ParentComponent = ParentComponent->Parent)
		{
			ParentRendererComponent = Cast<UDMXPixelMappingRendererComponent>(ParentComponent);
			if (ParentRendererComponent)
			{
				break;
			}
		}
	}

	return ParentRendererComponent;
}

#if WITH_EDITOR
void UDMXPixelMappingOutputComponent::MakeHighestZOrderInComponentRect()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = FindRendererComponent())
	{
		constexpr bool bRecursive = true;
		RendererComponent->ForEachChildOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* InComponent)
			{
				if (InComponent == this)
				{
					return;
				}

				// Exclude children, they're updated when SetZOrder is called below
				for (UDMXPixelMappingBaseComponent* OtherParent = InComponent->Parent; OtherParent; OtherParent = OtherParent->Parent)
				{
					if (OtherParent == this)
					{
						return;
					}
				}

				if (this->OverlapsComponent(InComponent))
				{
					if (InComponent->ZOrder + 1 > ZOrder)
					{
						SetZOrder(InComponent->ZOrder + 1);
					}
				}
			}, bRecursive);
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedPtr<SConstraintCanvas> UDMXPixelMappingOutputComponent::FindRendererComponentCanvas() const
{
	UDMXPixelMappingRendererComponent* ParentRendererComponent = FindRendererComponent();
	if(ParentRendererComponent && ParentRendererComponent->GetComponentsCanvas().IsValid())
	{
		return ParentRendererComponent->GetComponentsCanvas();
	}

	return nullptr;
}
#endif // WITH_EDITOR

bool UDMXPixelMappingOutputComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return false;
}

#undef LOCTEXT_NAMESPACE
