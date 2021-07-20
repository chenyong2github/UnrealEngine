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
		const EVisibility NewVisiblity = IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;

		if (ComponentWidget.IsValid())
		{
			ComponentWidget->SetVisibility(NewVisiblity);
		}

		constexpr bool bSetVisibilityRecursive = true;
		ForEachChildOfClass<UDMXPixelMappingOutputComponent>([NewVisiblity](UDMXPixelMappingOutputComponent* ChildComponent)
			{
				if (TSharedPtr<FDMXPixelMappingComponentWidget> ChildComponentWidget = ChildComponent->GetComponentWidget())
				{
					ChildComponentWidget->SetVisibility(NewVisiblity);
				}
			}, bSetVisibilityRecursive);
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, CellBlendingQuality))
	{
		// Propagonate to children
		constexpr bool bSetCellBlendingQualityToChildsRecursive = true;
		ForEachChildOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* ChildComponent)
			{
				ChildComponent->CellBlendingQuality = CellBlendingQuality;
			}, bSetCellBlendingQualityToChildsRecursive);
	}

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX) ||
			PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY) ||
			PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX) ||
			PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY))
		{
			PositionX = FMath::RoundHalfToZero(PositionX);
			PositionY = FMath::RoundHalfToZero(PositionY);

			SizeX = FMath::RoundHalfToZero(SizeX);
			SizeY = FMath::RoundHalfToZero(SizeY);

			// Remove self if not over parent
			if (!IsOverParent())
			{
				Modify();

				if (ensureMsgf(HasValidParent(), TEXT("No valid Parent when trying to remove PixelMapping Component %s."), *GetUserFriendlyName()))
				{
					GetParent()->Modify();
					GetParent()->RemoveChild(this);
				}
			}
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingOutputComponent::PreEditUndo()
{
	Super::PreEditUndo();

	PreEditUndoChildren = Children;
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingOutputComponent::PostEditUndo()
{
	Super::PostEditUndo();

	EVisibility NewVisibility = IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;

	UpdateComponentWidget(NewVisibility);

	if (Children.Num() < PreEditUndoChildren.Num())
	{	
		// Undo Add Children (remove them again)
		for (UDMXPixelMappingBaseComponent* PreEditUndoChild : PreEditUndoChildren)
		{
			if (UDMXPixelMappingOutputComponent* PreEditUndoOutputComponent = Cast<UDMXPixelMappingOutputComponent>(PreEditUndoChild))
			{
				const EVisibility NewChildVisibility = PreEditUndoOutputComponent->IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;

				constexpr bool bWithChildrenRecursive = false;
				PreEditUndoOutputComponent->UpdateComponentWidget(NewChildVisibility, bWithChildrenRecursive);
			}	
		}
	}
	else if (Children.Num() > PreEditUndoChildren.Num())
	{
		// Undo Remove Children (add them again)
		for (UDMXPixelMappingBaseComponent* PreEditUndoChild : PreEditUndoChildren)
		{
			if (UDMXPixelMappingOutputComponent* PreEditUndoOutputComponent = Cast<UDMXPixelMappingOutputComponent>(PreEditUndoChild))
			{
				const EVisibility NewChildVisibility = IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;

				constexpr bool bWithChildrenRecursive = false;
				PreEditUndoOutputComponent->UpdateComponentWidget(NewChildVisibility, bWithChildrenRecursive);
			}
		}
	}

	PreEditUndoChildren.Reset();
}
#endif // WITH_EDITOR

void UDMXPixelMappingOutputComponent::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->RemoveFromCanvas();

		// Should have released all references by now
		ensureMsgf(ComponentWidget.GetSharedReferenceCount() == 1, TEXT("Detected Reference to Component Widget the component is destroyed."));
	}
#endif
}

bool UDMXPixelMappingOutputComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return false;
}

void UDMXPixelMappingOutputComponent::AddChild(UDMXPixelMappingBaseComponent* InComponent)
{
	Super::AddChild(InComponent);

#if WITH_EDITOR
	if (UDMXPixelMappingOutputComponent* ChildOutputComponent = Cast<UDMXPixelMappingOutputComponent>(InComponent))
	{
		const EVisibility NewChildVisibility = ChildOutputComponent->IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;

		ChildOutputComponent->UpdateComponentWidget(NewChildVisibility);
	}	
#endif // WITH_EDITOR
}

void UDMXPixelMappingOutputComponent::RemoveChild(UDMXPixelMappingBaseComponent* InComponent)
{
	Super::RemoveChild(InComponent);

#if WITH_EDITOR
	if (UDMXPixelMappingOutputComponent* ChildOutputComponent = Cast<UDMXPixelMappingOutputComponent>(InComponent))
	{
		ChildOutputComponent->UpdateComponentWidget(EVisibility::Collapsed);
	}
#endif // WITH_EDITOR
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
	}

	EVisibility NewVisibility = IsVisible() ? EVisibility::Visible : EVisibility::Collapsed;

	UpdateComponentWidget(NewVisibility);

	return ComponentWidget.ToSharedRef();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
bool UDMXPixelMappingOutputComponent::IsVisible() const
{
	const bool bAssignedToParent = GetParent() && GetParent()->Children.Contains(this);

	return bVisibleInDesigner && bAssignedToParent;
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
		}, bRecursive);

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

	if (GetParent())
	{
		for (UDMXPixelMappingBaseComponent* ParentComponent = GetParent(); ParentComponent; ParentComponent = ParentComponent->GetParent())
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
				for (UDMXPixelMappingBaseComponent* OtherParent = InComponent->GetParent(); OtherParent; OtherParent = OtherParent->GetParent())
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

#if WITH_EDITOR
void UDMXPixelMappingOutputComponent::UpdateComponentWidget(EVisibility NewVisibility, bool bWithChildrenRecursive)
{
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetVisibility(NewVisibility);
		ComponentWidget->SetPosition(GetPosition());
		ComponentWidget->SetSize(GetSize());
		ComponentWidget->SetColor(GetEditorColor());
		ComponentWidget->SetLabelText(FText::FromString(GetUserFriendlyName()));
	}

	if (bWithChildrenRecursive)
	{
		for (UDMXPixelMappingBaseComponent* ChildComponent : Children)
		{
			if (UDMXPixelMappingOutputComponent* ChildOutputComponent = Cast<UDMXPixelMappingOutputComponent>(ChildComponent))
			{
				// Recursive for all
				ChildOutputComponent->UpdateComponentWidget(NewVisibility, bWithChildrenRecursive);
			}
		}
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
