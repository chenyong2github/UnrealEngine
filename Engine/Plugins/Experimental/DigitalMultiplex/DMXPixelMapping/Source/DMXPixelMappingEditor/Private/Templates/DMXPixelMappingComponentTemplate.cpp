// Copyright Epic Games, Inc. All Rights Reserved.

#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Components/DMXPixelMappingOutputComponent.h"

FDMXPixelMappingComponentTemplate::FDMXPixelMappingComponentTemplate(TSubclassOf<UDMXPixelMappingBaseComponent> InComponentClass)
	: ComponentClass(InComponentClass.Get())
{
	if (InComponentClass)
	{
		UDMXPixelMappingBaseComponent* DefaultComponent = ComponentClass->GetDefaultObject<UDMXPixelMappingBaseComponent>();

		Name = FText::FromString(DefaultComponent->GetNamePrefix().ToString());
	}
	else
	{
		Name = FText::FromString("UndefinedName");
	}
}

FText FDMXPixelMappingComponentTemplate::GetCategory() const
{
	if (ComponentClass.Get())
	{
		UDMXPixelMappingOutputComponent* OutputComponent = ComponentClass->GetDefaultObject<UDMXPixelMappingOutputComponent>();
		return OutputComponent->GetPaletteCategory();
	}
	else
	{
		UDMXPixelMappingOutputComponent* DefaultWidget = UDMXPixelMappingOutputComponent::StaticClass()->GetDefaultObject<UDMXPixelMappingOutputComponent>();
		return DefaultWidget->GetPaletteCategory();
	}
}

UDMXPixelMappingBaseComponent* FDMXPixelMappingComponentTemplate::Create(UDMXPixelMappingBaseComponent* InParentComponent)
{
	UDMXPixelMappingBaseComponent* DefaultComponent = ComponentClass->GetDefaultObject<UDMXPixelMappingBaseComponent>();
	FName UniqueName = MakeUniqueObjectName(InParentComponent, ComponentClass.Get(), DefaultComponent->GetNamePrefix());

	return NewObject<UDMXPixelMappingBaseComponent>(InParentComponent, ComponentClass.Get(), UniqueName, RF_Transactional | RF_Public);
}

