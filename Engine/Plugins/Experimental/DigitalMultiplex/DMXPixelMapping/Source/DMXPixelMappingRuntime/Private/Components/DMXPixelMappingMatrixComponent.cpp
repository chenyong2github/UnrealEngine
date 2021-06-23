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
	SizeX = 150.f;
	SizeY = 150.f;

	AttributeR.SetFromName("Red");
	AttributeG.SetFromName("Green");
	AttributeB.SetFromName("Blue");

	ColorMode = EDMXColorMode::CM_RGB;
	AttributeRExpose = true;
	AttributeGExpose = true;
	AttributeBExpose = true;

	bMonochromeExpose = true;

	Distribution = EDMXPixelMappingDistribution::TopLeftToRight;
}

void UDMXPixelMappingMatrixComponent::PostInitProperties()
{
	Super::PostInitProperties();

	LastPosition = FVector2D(PositionX, PositionY);
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

	LastPosition = FVector2D(PositionX, PositionY);

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
		else
		{
			FIntPoint NumCellsInActiveMode = FIntPoint(ModePtr->FixtureMatrixConfig.XCells, ModePtr->FixtureMatrixConfig.YCells);
			if (NumCellsInActiveMode != NumCells)
			{
				UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("Number of cells in %s no longer matches %s. %s will not function properly."), *GetName(), *FixturePatch->GetFixtureType()->Name, *GetName());
			}
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
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, CellSize) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, NumCells))
	{
		HandleSizeOrMatrixChanged();

		// Update size again from the new CellSize that results from handling size or matrix changes
		SizeX = CellSize.X * NumCells.X;
		SizeY = CellSize.Y * NumCells.Y;
	}
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, FixturePatchRef))
	{
		if (UDMXPixelMapping* PixelMapping = GetPixelMapping())
		{
			HandleSizeOrMatrixChanged();
			PixelMapping->OnEditorRebuildChildrenComponentsDelegate.ExecuteIfBound(this);
		}
	}
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXEntityReference, DMXLibrary))
	{
		if (UDMXPixelMapping* PixelMapping = GetPixelMapping())
		{
			HandleSizeOrMatrixChanged();
			PixelMapping->OnEditorRebuildChildrenComponentsDelegate.ExecuteIfBound(this);
		}
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
	if (UDMXPixelMappingFixtureGroupComponent* ParentFixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
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
	return Component && Component->IsA<UDMXPixelMappingFixtureGroupComponent>();
}

#if WITH_EDITOR
FString UDMXPixelMappingMatrixComponent::GetUserFriendlyName() const
{
	if (UDMXEntityFixturePatch* Patch = FixturePatchRef.GetFixturePatch())
	{
		return FString::Printf(TEXT("Fixture Matrix: %s"), *Patch->GetDisplayName());
	}

	return FString(TEXT("Fixture Matrix: No Fixture Patch"));
}
#endif // WITH_EDITOR

void UDMXPixelMappingMatrixComponent::Tick(float DeltaTime)
{
#if WITH_EDITOR
	// Test for property changes each tick

	if (UDMXPixelMapping * PixelMapping = GetPixelMapping())
	{
		if (PixelMapping->OnEditorRebuildChildrenComponentsDelegate.IsBound())
		{
			bool bShouldRebuildChildren = false;

			UDMXLibrary* DMXLibrary = FixturePatchRef.DMXLibrary;
			UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();

			if (DMXLibrary && FixturePatch)
			{
				if (UDMXEntityFixtureType * ParentFixtureType = FixturePatch->GetFixtureType())
				{
					if (!FixturePatch->GetActiveMode() && GetChildrenCount() > 0)
					{
						bShouldRebuildChildren = true;
					}
					else if (!ParentFixtureType->bFixtureMatrixEnabled && GetChildrenCount() > 0)
					{
						bShouldRebuildChildren = true;
					}
					else
					{
						const FDMXFixtureMode* FixtureMode = FixturePatch->GetActiveMode();

						if (FixtureMode)
						{
							const FDMXFixtureMatrix& FixtureMatrixConfig = FixtureMode->FixtureMatrixConfig;
							FIntPoint NewNumCells(FixtureMatrixConfig.XCells, FixtureMatrixConfig.YCells);

							if (NumCells != NewNumCells)
							{
								bShouldRebuildChildren = true;
							}
							else if (FixtureMatrixConfig.PixelMappingDistribution != Distribution)
							{
								bShouldRebuildChildren = true;
								Distribution = FixtureMatrixConfig.PixelMappingDistribution;
							}
						}
					}
				}
			}

			if (bShouldRebuildChildren)
			{
				HandleSizeOrMatrixChanged();

				LogInvalidProperties();
				
				PixelMapping->OnEditorRebuildChildrenComponentsDelegate.Execute(this);
			}
		}
	}
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixComponent::SetPosition(const FVector2D& NewPosition)
{
	PositionX = FMath::RoundHalfToZero(NewPosition.X);
	PositionY = FMath::RoundHalfToZero(NewPosition.Y);

	HandlePositionChanged();
}

void UDMXPixelMappingMatrixComponent::SetSize(const FVector2D& NewSize)
{
	SizeX = FMath::RoundHalfToZero(NewSize.X);
	SizeY = FMath::RoundHalfToZero(NewSize.Y);

	SizeX = FMath::Max(SizeX, 1.f);
	SizeY = FMath::Max(SizeY, 1.f);

	HandleSizeOrMatrixChanged();

	// Update size again from the new CellSize that results from handling size or matrix changes
	SizeX = CellSize.X * NumCells.X;
	SizeY = CellSize.Y * NumCells.Y;

#if WITH_EDITOR
	// Need to update the widget again, since size may have changed again
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
	}
#endif
}

void UDMXPixelMappingMatrixComponent::HandlePositionChanged()
{
#if WITH_EDITOR
	// Propagonate to children
	constexpr bool bUpdatePositionRecursive = false;
	ForEachChildOfClass<UDMXPixelMappingOutputComponent>([this](UDMXPixelMappingOutputComponent* ChildComponent)
		{
			if (ChildComponent->bLockInDesigner)
			{
				const FVector2D ChildOffset = ChildComponent->GetPosition() - LastPosition;
				ChildComponent->SetPosition(GetPosition() + ChildOffset);
			}
		}, bUpdatePositionRecursive);

	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
	}
#endif

	LastPosition.X = PositionX;
	LastPosition.Y = PositionY;
}

void UDMXPixelMappingMatrixComponent::HandleSizeOrMatrixChanged()
{
	NumCells = 0;

	if (UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch())
	{
		if (UDMXLibrary* DMXLibrary = FixturePatch->GetParentLibrary())
		{
			if (UDMXEntityFixtureType* ParentFixtureType = FixturePatch->GetFixtureType())
			{
				const FDMXFixtureMode* ModePtr = FixturePatch->GetActiveMode();
				if (ModePtr && ParentFixtureType->bFixtureMatrixEnabled)
				{
					const FDMXFixtureMatrix& FixtureMatrixConfig = ModePtr->FixtureMatrixConfig;

					NumCells = FIntPoint(FixtureMatrixConfig.XCells, FixtureMatrixConfig.YCells);
				}
			}
		}
	}

	CellSize = FVector2D::ZeroVector;
	if (NumCells.X > 0 && NumCells.Y > 0)
	{
		CellSize = FVector2D(FMath::RoundToFloat(GetSize().X / NumCells.X), FMath::RoundToFloat(GetSize().Y / NumCells.Y));

		// Propagonate to children
		constexpr bool bUpdateSizeRecursive = false;
		ForEachChildOfClass<UDMXPixelMappingMatrixCellComponent>([this](UDMXPixelMappingMatrixCellComponent* ChildComponent)
			{
				ChildComponent->SetSize(CellSize);
				ChildComponent->SetPosition(GetPosition() + FVector2D(CellSize * ChildComponent->GetCellCoordinate()));

			}, bUpdateSizeRecursive);
	}
}

#undef LOCTEXT_NAMESPACE
