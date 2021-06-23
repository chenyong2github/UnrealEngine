// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingMatrixCellComponent.h"

#include "DMXPixelMappingTypes.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"

#if WITH_EDITOR
#include "DMXPixelMappingComponentWidget.h"
#include "SDMXPixelMappingComponentBox.h"
#include "SDMXPixelMappingComponentLabel.h"
#endif // WITH_EDITOR

#include "Engine/Texture.h"
#include "Widgets/Layout/SBox.h"


DECLARE_CYCLE_STAT(TEXT("Send Matrix Cell"), STAT_DMXPixelMaping_SendMatrixCell, STATGROUP_DMXPIXELMAPPING);

#define LOCTEXT_NAMESPACE "DMXPixelMappingMatrixPixelComponent"


UDMXPixelMappingMatrixCellComponent::UDMXPixelMappingMatrixCellComponent()
	: DownsamplePixelIndex(0)
{
#if WITH_EDITORONLY_DATA
	bLockInDesigner = true;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixCellComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY))
	{
		if (ComponentWidget.IsValid())
		{
			ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
		}
	}
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY))
	{
		if (ComponentWidget.IsValid())
		{
			ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
		}
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, CellID))
	{
		if (ComponentWidget.IsValid())
		{
			ComponentWidget->GetComponentBox()->SetIDText(FText::Format(LOCTEXT("CellID", "{0}"), CellID));
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixCellComponent::PreEditUndo()
{
	// Use default engine instead of parent class.
	// Let the prent matrix component handle it instead.
	UObject::PreEditUndo();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixCellComponent::PostEditUndo()
{
	// Use default engine instead of parent class.
	// Let the prent matrix component handle it instead.	
	UObject::PostEditUndo();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<FDMXPixelMappingComponentWidget> UDMXPixelMappingMatrixCellComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	ComponentWidget = Super::BuildSlot(InCanvas);

	// Expect super to construct the component widget
	if (ensureMsgf(ComponentWidget.IsValid(), TEXT("PixelMapping: Expected Super to construct a component widget, but didn't.")))
	{
		ComponentWidget->GetComponentLabel()->SetText(FText::GetEmpty());
		ComponentWidget->GetComponentBox()->SetIDText(FText::Format(LOCTEXT("CellID", "{0}"), CellID));
	}

	return ComponentWidget.ToSharedRef();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
bool UDMXPixelMappingMatrixCellComponent::IsVisible() const
{
	// Needs be over the matrix and over the group
	if (UDMXPixelMappingMatrixComponent* ParentMatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(GetParent()))
	{
		if (!ParentMatrixComponent->IsVisible())
		{
			return false;
		}
	}

	return Super::IsVisible();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
FLinearColor UDMXPixelMappingMatrixCellComponent::GetEditorColor() const
{
	if (bLockInDesigner)
	{
		// When locked in designer, always use the parent color. So when the parent shows an error color, show it too.
		if (UDMXPixelMappingMatrixComponent* ParentMatrixComponent = Cast< UDMXPixelMappingMatrixComponent>(GetParent()))
		{
			if (const TSharedPtr<FDMXPixelMappingComponentWidget>& ParentComponentWidget = ParentMatrixComponent->GetComponentWidget())
			{
				return ParentComponentWidget->GetColor();
			}
		}
	}

	return EditorColor;
}
#endif // WITH_EDITOR

bool UDMXPixelMappingMatrixCellComponent::IsOverParent() const
{
	// Needs be over the matrix and over the group
	if (UDMXPixelMappingMatrixComponent* ParentMatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(GetParent()))
	{
		const bool bIsParentMatrixOverGroup = ParentMatrixComponent->IsOverParent();

		return
			bIsParentMatrixOverGroup &&
			PositionX >= ParentMatrixComponent->GetPosition().X &&
			PositionY >= ParentMatrixComponent->GetPosition().Y &&
			PositionX + SizeX <= ParentMatrixComponent->GetPosition().X + ParentMatrixComponent->GetSize().X &&
			PositionY + SizeY <= ParentMatrixComponent->GetPosition().Y + ParentMatrixComponent->GetSize().Y;
	}

	return false;
}

void UDMXPixelMappingMatrixCellComponent::SetPosition(const FVector2D& NewPosition)
{
	Modify();

	PositionX = FMath::RoundHalfToZero(NewPosition.X);
	PositionY = FMath::RoundHalfToZero(NewPosition.Y);

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
	}
#endif
}

void UDMXPixelMappingMatrixCellComponent::SetSize(const FVector2D& NewSize)
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

const FName& UDMXPixelMappingMatrixCellComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("MatrixCell");
	return NamePrefix;
}

void UDMXPixelMappingMatrixCellComponent::ResetDMX()
{
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (RendererComponent)
	{
		RendererComponent->ResetColorDownsampleBufferPixel(DownsamplePixelIndex);
	}

	// No need to send dmx, that is done by the parent matrix
}

FString UDMXPixelMappingMatrixCellComponent::GetUserFriendlyName() const
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(GetParent()))
	{
		UDMXEntityFixturePatch* FixturePatch = MatrixComponent->FixturePatchRef.GetFixturePatch();
		
		if (FixturePatch)
		{
			return FString::Printf(TEXT("%s: Cell %d"), *FixturePatch->GetDisplayName(), CellID);
		}
	}

	return FString(TEXT("Invalid Patch"));
}

void UDMXPixelMappingMatrixCellComponent::QueueDownsample()
{
	// Queue pixels into the downsample rendering
	UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(GetParent());
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();

	if (!ensure(MatrixComponent))
	{
		return;
	}

	if (!ensure(RendererComponent))
	{
		return;
	}

	UTexture* InputTexture = RendererComponent->GetRendererInputTexture();
	if (!ensure(InputTexture))
	{
		return;
	}

	// Store downsample index
	DownsamplePixelIndex = RendererComponent->GetDownsamplePixelNum();

	const uint32 TextureSizeX = InputTexture->Resource->GetSizeX();
	const uint32 TextureSizeY = InputTexture->Resource->GetSizeY();
	check(TextureSizeX > 0 && TextureSizeY > 0);
	const FIntPoint PixelPosition = RendererComponent->GetPixelPosition(DownsamplePixelIndex);
	const FVector2D UV = FVector2D(PositionX / TextureSizeX, PositionY / TextureSizeY);
	const FVector2D UVSize(SizeX / TextureSizeX, SizeY / TextureSizeY);
	const FVector2D UVCellSize = UVSize / 2.f;
	constexpr bool bStaticCalculateUV = true;

	FVector4 ExposeFactor;
	FIntVector4 InvertFactor;
	if (MatrixComponent->ColorMode == EDMXColorMode::CM_RGB)
	{
		ExposeFactor = FVector4(MatrixComponent->AttributeRExpose ? 1.f : 0.f, MatrixComponent->AttributeGExpose ? 1.f : 0.f, MatrixComponent->AttributeBExpose ? 1.f : 0.f, 1.f);
		InvertFactor = FIntVector4(MatrixComponent->AttributeRInvert, MatrixComponent->AttributeGInvert, MatrixComponent->AttributeBInvert, 0);
	}
	else if (MatrixComponent->ColorMode == EDMXColorMode::CM_Monochrome)
	{
		static const FVector4 Expose(1.f, 1.f, 1.f, 1.f);
		static const FVector4 NoExpose(0.f, 0.f, 0.f, 0.f);
		ExposeFactor = FVector4(MatrixComponent->bMonochromeExpose ? Expose : NoExpose);
		InvertFactor = FIntVector4(MatrixComponent->bMonochromeInvert, MatrixComponent->bMonochromeInvert, MatrixComponent->bMonochromeInvert, 0);
	}
			
	FDMXPixelMappingDownsamplePixelParam DownsamplePixelParam
	{
		ExposeFactor,
		InvertFactor,
		PixelPosition,
		UV,
		UVSize,
		UVCellSize,
		CellBlendingQuality,
		bStaticCalculateUV
	};

	RendererComponent->AddPixelToDownsampleSet(MoveTemp(DownsamplePixelParam));
}

void UDMXPixelMappingMatrixCellComponent::SetCellCoordinate(FIntPoint InCellCoordinate)
{
	CellCoordinate = InCellCoordinate;
}

void UDMXPixelMappingMatrixCellComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent())
	{
		RendererComponent->RendererInputTexture();
	}

	RenderAndSendDMX();
}

bool UDMXPixelMappingMatrixCellComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* OtherComponent) const
{
	return OtherComponent && OtherComponent == GetParent();
}

UDMXPixelMappingRendererComponent* UDMXPixelMappingMatrixCellComponent::GetRendererComponent() const
{
	for (UDMXPixelMappingBaseComponent* ParentComponent = GetParent(); ParentComponent; ParentComponent = ParentComponent->GetParent())
	{
		if (UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(ParentComponent))
		{
			return RendererComponent;
		}
	}
	return nullptr;
}

TMap<FDMXAttributeName, float> UDMXPixelMappingMatrixCellComponent::CreateAttributeValues() const
{
	TMap<FDMXAttributeName, float> AttributeToNormalizedValueMap;

	if (UDMXPixelMappingMatrixComponent* ParentMatrix = Cast<UDMXPixelMappingMatrixComponent>(GetParent()))
	{
		UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
		if (RendererComponent)
		{
			// Get the color data from the rendered component
			FLinearColor PixelColor;
			if (RendererComponent->GetDownsampleBufferPixel(DownsamplePixelIndex, PixelColor))
			{
				if (ParentMatrix->ColorMode == EDMXColorMode::CM_RGB)
				{
					if (ParentMatrix->AttributeRExpose)
					{
						const float AttributeRValue = FMath::Clamp(PixelColor.R, 0.f, 1.f);
						AttributeToNormalizedValueMap.Add(ParentMatrix->AttributeR, AttributeRValue);
					}

					if (ParentMatrix->AttributeGExpose)
					{
						const float AttributeGValue = FMath::Clamp(PixelColor.G, 0.f, 1.f);
						AttributeToNormalizedValueMap.Add(ParentMatrix->AttributeG, AttributeGValue);
					}

					if (ParentMatrix->AttributeBExpose)
					{
						const float AttributeBValue = FMath::Clamp(PixelColor.B, 0.f, 1.f);
						AttributeToNormalizedValueMap.Add(ParentMatrix->AttributeB, AttributeBValue);
					}
				}
				else if (ParentMatrix->ColorMode == EDMXColorMode::CM_Monochrome)
				{
					if (ParentMatrix->bMonochromeExpose)
					{
						// https://www.w3.org/TR/AERT/#color-contrast
						float Intensity = 0.299f * PixelColor.R + 0.587f * PixelColor.G + 0.114f * PixelColor.B;
						Intensity = FMath::Clamp(Intensity, 0.f, 1.f);

						AttributeToNormalizedValueMap.Add(ParentMatrix->MonochromeIntensity, Intensity);
					}
				}
			}
		}
	}

	return AttributeToNormalizedValueMap;

}
#undef LOCTEXT_NAMESPACE
