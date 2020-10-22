// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingOutputComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingOutputComponent"

UDMXPixelMappingOutputComponent::UDMXPixelMappingOutputComponent()
{
#if WITH_EDITOR
	Slot = nullptr;

	bLockInDesigner = false;
	bVisibleInDesigner = true;
	ZOrder = 0;

	CachedWidget =
		SNew(SBox)
		.HeightOverride(SizeX)
		.WidthOverride(SizeY);
#endif // WITH_EDITOR

	CellBlendingQuality = EDMXPixelBlendingQuality::Low;
}

#if WITH_EDITOR
const FText UDMXPixelMappingOutputComponent::GetPaletteCategory()
{
	ensureMsgf(false, TEXT("You must implement GetPaletteCategory() in your child class"));

	return LOCTEXT("Uncategorized", "Uncategorized");
}

TSharedRef<SWidget> UDMXPixelMappingOutputComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	ensureMsgf(false, TEXT("You must implement RebuildWidget() in your child class"));

	Slot = 
		&InCanvas->AddSlot()
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder)
		[
			CachedWidget.ToSharedRef()
		];

	return CachedWidget.ToSharedRef();
}

TSharedPtr<SWidget> UDMXPixelMappingOutputComponent::GetCachedWidget() const
{
	return CachedWidget;
}

void UDMXPixelMappingOutputComponent::SetZOrder(int32 NewZOrder)
{
	ZOrder = NewZOrder;

	if (Slot != nullptr && CachedWidget.IsValid())
	{
		Slot->ZOrder(NewZOrder);
	}
}

bool UDMXPixelMappingOutputComponent::CanEditChange(const FProperty* InProperty) const
{
	FString PropertyName = InProperty->GetName();

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingOutputComponent, SizeX) ||
		PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingOutputComponent, SizeY) ||
		PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingOutputComponent, PositionX) ||
		PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingOutputComponent, PositionY)
		)
	{
		return !bLockInDesigner;
	}

	return Super::CanEditChange(InProperty);
}

void UDMXPixelMappingOutputComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		// Round the values
		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX))
		{
			PositionX = FMath::RoundHalfToZero(PositionX);
		}
		else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY))
		{
			PositionY = FMath::RoundHalfToZero(PositionY);
		}
		else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX))
		{
			SizeX = FMath::RoundHalfToZero(SizeX);
		}
		else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY))
		{
			SizeY = FMath::RoundHalfToZero(SizeY);
		}
	}
}

#endif // WITH_EDITOR

FVector2D UDMXPixelMappingOutputComponent::GetSize() const
{
	ensureMsgf(false, TEXT("You must implement GetSize() in your child class"));

	return FVector2D(100.f);
}

void UDMXPixelMappingOutputComponent::SetSize(const FVector2D& InSize)
{
	SizeX = FMath::RoundHalfToZero(InSize.X);
	SizeY = FMath::RoundHalfToZero(InSize.Y);
}

void UDMXPixelMappingOutputComponent::SetPosition(const FVector2D& InPosition)
{
	PositionX = FMath::RoundHalfToZero(InPosition.X);
	PositionY = FMath::RoundHalfToZero(InPosition.Y);
}

void UDMXPixelMappingOutputComponent::SetSurfaceBuffer(TArray<FColor>& InSurfaceBuffer, FIntRect& InRect)
{
	FScopeLock ScopeLock(&SurfaceCS);
	SurfaceBuffer = MoveTemp(InSurfaceBuffer);
	SurfaceRect = InRect;
}

void UDMXPixelMappingOutputComponent::GetSurfaceBuffer(GetSurfaceSafeCallback Callback)
{
	FScopeLock ScopeLock(&SurfaceCS);
	Callback(SurfaceBuffer, SurfaceRect);
}

void UDMXPixelMappingOutputComponent::UpdateSurfaceBuffer(UpdateSurfaceSafeCallback Callback)
{
	FScopeLock ScopeLock(&SurfaceCS);
	Callback(SurfaceBuffer, SurfaceRect);
}

bool UDMXPixelMappingOutputComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return false;
}

#if WITH_EDITOR
const FLinearColor& UDMXPixelMappingOutputComponent::GetEditorColor(bool bHighlight) const
{
	if (bHighlight)
	{
		return FLinearColor::Green;
	}

	return EditorColor;
}
#endif

#undef LOCTEXT_NAMESPACE
