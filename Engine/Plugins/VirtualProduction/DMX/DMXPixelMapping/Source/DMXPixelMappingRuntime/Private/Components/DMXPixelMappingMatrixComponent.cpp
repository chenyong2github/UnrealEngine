// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingMatrixComponent.h"

#include "DMXPixelMapping.h"
#include "DMXPixelMappingRuntimeCommon.h"
#include "DMXPixelMappingRuntimeObjectVersion.h"
#include "DMXPixelMappingRuntimeUtils.h"
#include "DMXPixelMappingTypes.h"
#include "DMXSubsystem.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"

#if WITH_EDITOR
#include "DMXPixelMappingComponentWidget.h"
#endif // WITH_EDITOR

#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingMatrixComponent"

UDMXPixelMappingMatrixComponent::UDMXPixelMappingMatrixComponent()
{	
	SizeX = 32.f;
	SizeY = 32.f;

	AttributeR.SetFromName("Red");
	AttributeG.SetFromName("Green");
	AttributeB.SetFromName("Blue");

	ColorMode = EDMXColorMode::CM_RGB;
	AttributeRExpose = true;
	AttributeGExpose = true;
	AttributeBExpose = true;

	bMonochromeExpose = true;
}

void UDMXPixelMappingMatrixComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FDMXPixelMappingRuntimeObjectVersion::GUID);
	if(Ar.IsLoading())
	{
		if (Ar.CustomVer(FDMXPixelMappingRuntimeObjectVersion::GUID) < FDMXPixelMappingRuntimeObjectVersion::ChangePixelMappingMatrixComponentToFixturePatchReference)
		{
			// Upgrade from custom FixturePatchMatrixRef to FixturePatchRef
			FixturePatchRef.SetEntity(FixturePatchMatrixRef_DEPRECATED.GetFixturePatch());
		}
	}
}

void UDMXPixelMappingMatrixComponent::PostLoad()
{
	Super::PostLoad();

	// Add valid modulators to modulator classes, remove invalid modulators
	for (int32 IndexModulator = 0; Modulators.IsValidIndex(IndexModulator); )
	{
		if (Modulators[IndexModulator])
		{
			ModulatorClasses.Add(Modulators[IndexModulator]->GetClass());
			IndexModulator++;
		}
		else
		{
			Modulators.RemoveAt(IndexModulator);
			if (!Modulators.IsValidIndex(IndexModulator++))
			{
				// Removed the last element
				break;
			}
		}
	}
}

void UDMXPixelMappingMatrixComponent::PostInitProperties()
{
	Super::PostInitProperties();

	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// Listen to Fixture Type and Fixture Patch changes
		UDMXEntityFixtureType::GetOnFixtureTypeChanged().AddUObject(this, &UDMXPixelMappingMatrixComponent::OnFixtureTypeChanged);
		UDMXEntityFixturePatch::GetOnFixturePatchChanged().AddUObject(this, &UDMXPixelMappingMatrixComponent::OnFixturePatchChanged);
	}
}

void UDMXPixelMappingMatrixComponent::LogInvalidProperties()
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
	if (IsValid(FixturePatch))
	{
		const FDMXFixtureMode* ModePtr = FixturePatch->GetActiveMode();
		if (!ModePtr)
		{
			UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("%s has no valid Active Mode set. %s will not receive DMX."), *FixturePatch->GetDisplayName(), *GetName());
		}
		else if (!FixturePatch->GetFixtureType())
		{
			UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("%s has no valid Fixture Type set. %s will not receive DMX."), *FixturePatch->GetDisplayName(), *GetName());
		}
		else if(Children.Num() != ModePtr->FixtureMatrixConfig.XCells * ModePtr->FixtureMatrixConfig.YCells)
			{
				UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("Number of cells in %s no longer matches %s. %s will not function properly."), *GetName(), *FixturePatch->GetFixtureType()->Name, *GetName());
			}
		}
	else
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("%s has no valid Fixture Patch set."), *GetName());
	}
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Note, property changes of fixture patch are listened for in tick

	// Call the parent at the first place
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY))
	{
		HandlePositionChanged();
	}
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, CellSize))
	{
		HandleSizeChanged();

		// Update size again from the new CellSize that results from handling size or matrix changes
		SizeX = CellSize.X * CoordinateGrid.X;
		SizeY = CellSize.Y * CoordinateGrid.Y;
	}
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, FixturePatchRef) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, CoordinateGrid))
	{
		HandleMatrixChanged();
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXEntityReference, DMXLibrary))
	{
		HandleMatrixChanged();
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	// For consistency with GroupItem, handling modulator class changes in runtime utils
	FDMXPixelMappingRuntimeUtils::HandleModulatorPropertyChange(this, PropertyChangedChainEvent, ModulatorClasses, Modulators);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
const FText UDMXPixelMappingMatrixComponent::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}
#endif // WITH_EDITOR

bool UDMXPixelMappingMatrixComponent::IsOverParent() const
{
	// Needs be over the over the group
	if (UDMXPixelMappingFixtureGroupComponent* ParentFixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(GetParent()))
	{
		return
			PositionX >= ParentFixtureGroupComponent->GetPosition().X &&
			PositionY >= ParentFixtureGroupComponent->GetPosition().Y &&
			PositionX + SizeX <= ParentFixtureGroupComponent->GetPosition().X + ParentFixtureGroupComponent->GetSize().X &&
			PositionY + SizeY <= ParentFixtureGroupComponent->GetPosition().Y + ParentFixtureGroupComponent->GetSize().Y;
	}

	return false;
}

const FName& UDMXPixelMappingMatrixComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("Matrix");
	return NamePrefix;
}

void UDMXPixelMappingMatrixComponent::ResetDMX()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent)
	{
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->ResetDMX();
		}
	}, false);

	SendDMX();
}

void UDMXPixelMappingMatrixComponent::SendDMX()
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();

	if (FixturePatch)
	{
		// An array of attribute to value maps for each child, in order of the childs
		TArray<FDMXNormalizedAttributeValueMap> AttributeToValueMapArray;

		// Accumulate matrix cell data
		for (UDMXPixelMappingBaseComponent* Component : Children)
		{
			if (UDMXPixelMappingMatrixCellComponent* CellComponent = Cast<UDMXPixelMappingMatrixCellComponent>(Component))
			{
				FDMXNormalizedAttributeValueMap NormalizedAttributeToValueMap;
				NormalizedAttributeToValueMap.Map = CellComponent->CreateAttributeValues();

				AttributeToValueMapArray.Add(NormalizedAttributeToValueMap);
			}
		}

		// Apply matrix modulators
		for (UDMXModulator* Modulator : Modulators)
		{
			Modulator->ModulateMatrix(FixturePatch, AttributeToValueMapArray, AttributeToValueMapArray);
		}

		for (int32 IndexChild = 0; IndexChild < Children.Num(); IndexChild++)
		{
			if (UDMXPixelMappingMatrixCellComponent* CellComponent = Cast<UDMXPixelMappingMatrixCellComponent>(Children[IndexChild]))
			{
				// Relies on the order of childs and AttributeToValueMapArray didn't change during the lifetime of this function!
				check(AttributeToValueMapArray.IsValidIndex(IndexChild));

				TMap<int32, uint8> ChannelToValueMap;
				for (const TTuple<FDMXAttributeName, float>& AttributeValuePair : AttributeToValueMapArray[IndexChild].Map)
				{
					FDMXPixelMappingRuntimeUtils::ConvertNormalizedAttributeValueToChannelValue(FixturePatch, AttributeValuePair.Key, AttributeValuePair.Value, ChannelToValueMap);

					FixturePatch->SendNormalizedMatrixCellValue(CellComponent->CellCoordinate, AttributeValuePair.Key, AttributeValuePair.Value);
				}
			}
		}

		// Send normal modulators. This is important to allow modulators to generate attribute values
		TMap<FDMXAttributeName, float> ModulatorGeneratedAttributeValueMap;
		for (UDMXModulator* Modulator : Modulators)
		{
			Modulator->Modulate(FixturePatch, ModulatorGeneratedAttributeValueMap, ModulatorGeneratedAttributeValueMap);
		}

		TMap<int32, uint8> ChannelToValueMap;
		for (const TTuple<FDMXAttributeName, float>& AttributeValuePair : ModulatorGeneratedAttributeValueMap)
		{
			FDMXPixelMappingRuntimeUtils::ConvertNormalizedAttributeValueToChannelValue(FixturePatch, AttributeValuePair.Key, AttributeValuePair.Value, ChannelToValueMap);
		}

		if (UDMXLibrary* Library = FixturePatch->GetParentLibrary())
		{
			for (const FDMXOutputPortSharedRef& OutputPort : Library->GetOutputPorts())
			{
				OutputPort->SendDMX(FixturePatch->GetUniverseID(), ChannelToValueMap);
			}
		}
	}
}

void UDMXPixelMappingMatrixComponent::QueueDownsample()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->QueueDownsample();
		}
	}, false);
}

bool UDMXPixelMappingMatrixComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	if (const UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Component))
	{
		if (FixtureGroupComponent->DMXLibrary == FixturePatchRef.DMXLibrary)
		{
			return true;
		}
	}

	return false;
}

FString UDMXPixelMappingMatrixComponent::GetUserFriendlyName() const
{
	if (UDMXEntityFixturePatch* Patch = FixturePatchRef.GetFixturePatch())
	{
		return FString::Printf(TEXT("Fixture Matrix: %s"), *Patch->GetDisplayName());
	}

	return FString(TEXT("Fixture Matrix: No Fixture Patch"));
}

void UDMXPixelMappingMatrixComponent::SetPosition(const FVector2D& NewPosition)
{
	Modify();

	PositionX = FMath::RoundHalfToZero(NewPosition.X);
	PositionY = FMath::RoundHalfToZero(NewPosition.Y);

	HandlePositionChanged();
}

void UDMXPixelMappingMatrixComponent::SetSize(const FVector2D& NewSize)
{
	Modify();

	SizeX = FMath::RoundHalfToZero(NewSize.X);
	SizeY = FMath::RoundHalfToZero(NewSize.Y);

	SizeX = FMath::Max(SizeX, 1.f);
	SizeY = FMath::Max(SizeY, 1.f);

	//HandleMatrixChanged();
	HandleSizeChanged();
}

void UDMXPixelMappingMatrixComponent::HandlePositionChanged()
{
#if WITH_EDITOR
	// Propagonate to children
	constexpr bool bUpdateSizeRecursive = false;
	ForEachChildOfClass<UDMXPixelMappingMatrixCellComponent>([this](UDMXPixelMappingMatrixCellComponent* ChildComponent)
		{
			ChildComponent->SetPosition(GetPosition() + FVector2D(CellSize * ChildComponent->GetCellCoordinate()));

		}, bUpdateSizeRecursive);

	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
	}
#endif
}

void UDMXPixelMappingMatrixComponent::HandleSizeChanged()
{
	CellSize = FVector2D::ZeroVector;
	if (CoordinateGrid.X > 0 && CoordinateGrid.Y > 0)
	{
		CellSize = FVector2D(FMath::RoundToFloat(GetSize().X / CoordinateGrid.X), FMath::RoundToFloat(GetSize().Y / CoordinateGrid.Y));

	// Propagonate to children
		constexpr bool bUpdateSizeRecursive = false;
		ForEachChildOfClass<UDMXPixelMappingMatrixCellComponent>([this](UDMXPixelMappingMatrixCellComponent* ChildComponent)
		{
				ChildComponent->SetSize(CellSize);
				ChildComponent->SetPosition(GetPosition() + FVector2D(CellSize * ChildComponent->GetCellCoordinate()));

			}, bUpdateSizeRecursive);
	}

	if (Children.Num() > 0)
			{
		// Update size again from the new CellSize
		SizeX = CellSize.X * CoordinateGrid.X;
		SizeY = CellSize.Y * CoordinateGrid.Y;
			}
	else
	{
		// Set the default size
		SizeX = 150.f;
		SizeY = 150.f;
	}

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
	}
#endif
}

void UDMXPixelMappingMatrixComponent::HandleMatrixChanged()
{
	CoordinateGrid = 0;

	// Remove all existing children and rebuild them anew
	for (UDMXPixelMappingBaseComponent* Child : TArray<UDMXPixelMappingBaseComponent*>(Children))
	{
		RemoveChild(Child);
	}

	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
	UDMXEntityFixtureType* FixtureType = FixturePatch ? FixturePatch->GetFixtureType() : nullptr;
	const FDMXFixtureMode* ModePtr = FixturePatch ? FixturePatch->GetActiveMode() : nullptr;
	if (FixturePatch && FixtureType && ModePtr && ModePtr->bFixtureMatrixEnabled)
	{
		TArray<FDMXCell> MatrixCells;
		if (FixturePatch->GetAllMatrixCells(MatrixCells))
		{
			Distribution = ModePtr->FixtureMatrixConfig.PixelMappingDistribution;
			CoordinateGrid = FIntPoint(ModePtr->FixtureMatrixConfig.XCells, ModePtr->FixtureMatrixConfig.YCells);

			for (const FDMXCell& Cell : MatrixCells)
			{
				TSharedPtr<FDMXPixelMappingComponentTemplate> ComponentTemplate = MakeShared<FDMXPixelMappingComponentTemplate>(UDMXPixelMappingMatrixCellComponent::StaticClass());
				UDMXPixelMappingMatrixCellComponent* Component = ComponentTemplate->CreateComponent<UDMXPixelMappingMatrixCellComponent>(GetPixelMapping()->GetRootComponent());

				Component->CellID = Cell.CellID;
				Component->SetCellCoordinate(Cell.Coordinate);

				AddChild(Component);
			}
		}
	}

	HandleSizeChanged();

	GetOnMatrixChanged().Broadcast(GetPixelMapping(), this);
}

void UDMXPixelMappingMatrixComponent::OnFixtureTypeChanged(const UDMXEntityFixtureType* FixtureType)
{
	if (UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch())
	{
		if (FixturePatch->GetFixtureType() == FixtureType)
		{
			HandleMatrixChanged();

			LogInvalidProperties();
		}
	}
}

void UDMXPixelMappingMatrixComponent::OnFixturePatchChanged(const UDMXEntityFixturePatch* FixturePatch)
{
	if (FixturePatch)
	{
		HandleMatrixChanged();

		LogInvalidProperties();
	}
}

#undef LOCTEXT_NAMESPACE
