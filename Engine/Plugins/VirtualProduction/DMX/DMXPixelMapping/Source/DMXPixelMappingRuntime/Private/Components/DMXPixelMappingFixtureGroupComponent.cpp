// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingFixtureGroupComponent.h"

#include "IDMXPixelMappingRenderer.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Library/DMXLibrary.h"

#if WITH_EDITOR
#include "DMXPixelMappingComponentWidget.h"
#endif // WITH_EDITOR

#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingFixtureGroupComponent"


UDMXPixelMappingFixtureGroupComponent::UDMXPixelMappingFixtureGroupComponent()
{
	SizeX = 500.f;
	SizeY = 500.f;
}

void UDMXPixelMappingFixtureGroupComponent::PostInitProperties()
{
	Super::PostInitProperties();

	LastPosition = FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingFixtureGroupComponent::PostLoad()
{
	Super::PostLoad();

	LastPosition = FVector2D(PositionX, PositionY);
}

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY))
	{
		HandlePositionChanged();
	}
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY))
	{
		if (ComponentWidget.IsValid())
		{
			ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
		}
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, DMXLibrary))
	{
		if (ComponentWidget.IsValid())
		{
			ComponentWidget->SetLabelText(FText::FromString(GetUserFriendlyName()));
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupComponent::PostEditUndo()
{
	Super::PostEditUndo();

	// Update last position, so the next will be set correctly on children
	LastPosition = GetPosition();
}
#endif // WITH_EDITOR

const FName& UDMXPixelMappingFixtureGroupComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("Fixture Group");
	return NamePrefix;
}

void UDMXPixelMappingFixtureGroupComponent::ResetDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent)
	{
		if (UDMXPixelMappingOutputComponent * Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->ResetDMX();
		}
	}, false);
}

void UDMXPixelMappingFixtureGroupComponent::SendDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->SendDMX();
		}
	}, false);
}

void UDMXPixelMappingFixtureGroupComponent::QueueDownsample()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->QueueDownsample();
		}
	}, false);
}

void UDMXPixelMappingFixtureGroupComponent::SetPosition(const FVector2D& NewPosition)
{
	Modify();

	PositionX = FMath::RoundHalfToZero(NewPosition.X);
	PositionY = FMath::RoundHalfToZero(NewPosition.Y);

	HandlePositionChanged();
}

void UDMXPixelMappingFixtureGroupComponent::SetSize(const FVector2D& NewSize)
{
	Modify();

	SizeX = FMath::RoundHalfToZero(NewSize.X);
	SizeY = FMath::RoundHalfToZero(NewSize.Y);

	SizeX = FMath::Max(SizeX, 1.f);
	SizeY = FMath::Max(SizeY, 1.f);

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
	}
#endif
}

void UDMXPixelMappingFixtureGroupComponent::HandlePositionChanged()
{
#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
	}
#endif

	// Propagonate to children
	constexpr bool bUpdatePositionRecursive = false;
	ForEachChildOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* ChildComponent)
		{
			const FVector2D ChildOffset = ChildComponent->GetPosition() - LastPosition;
			ChildComponent->SetPosition(GetPosition() + ChildOffset);

		}, bUpdatePositionRecursive);

	LastPosition = GetPosition();
}

FString UDMXPixelMappingFixtureGroupComponent::GetUserFriendlyName() const
{
	if (DMXLibrary)
	{
		return FString::Printf(TEXT("Fixture Group: %s"), *DMXLibrary->GetName());
	}

	return FString("Fixture Group: No Library");
}

#if WITH_EDITOR
const FText UDMXPixelMappingFixtureGroupComponent::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}
#endif // WITH_EDITOR

bool UDMXPixelMappingFixtureGroupComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRendererComponent>();
}

#undef LOCTEXT_NAMESPACE
