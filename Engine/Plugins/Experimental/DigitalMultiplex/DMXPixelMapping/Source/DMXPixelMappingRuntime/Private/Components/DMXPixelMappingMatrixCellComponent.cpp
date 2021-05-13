// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingMatrixCellComponent.h"

#include "DMXPixelMappingTypes.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"

#include "Engine/Texture.h"
#include "Widgets/Layout/SBox.h"

#if WITH_EDITOR
#include "SDMXPixelMappingEditorWidgets.h"
#endif // WITH_EDITOR


DECLARE_CYCLE_STAT(TEXT("Send Matrix Cell"), STAT_DMXPixelMaping_SendMatrixCell, STATGROUP_DMXPIXELMAPPING);

#define LOCTEXT_NAMESPACE "DMXPixelMappingMatrixPixelComponent"

const FVector2D UDMXPixelMappingMatrixCellComponent::MixPixelSize = FVector2D(1.f);

UDMXPixelMappingMatrixCellComponent::UDMXPixelMappingMatrixCellComponent()
	: DownsamplePixelIndex(0)
{
	SizeX = 10.f;
	SizeY = 10.f;

#if WITH_EDITOR
	RelativePositionX = 0.f;
	RelativePositionY = 0.f;

	Slot = nullptr;

	bLockInDesigner = true;

	ZOrder = 2;
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixCellComponent::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	void UpdateWidget();
#endif // WITH_EDITOR	
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixCellComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	FName&& PropertyName = PropertyChangedChainEvent.GetPropertyName();

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, bVisibleInDesigner))
	{
		UpdateWidget();
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, EditorColor))
	{
		Brush.TintColor = EditorColor;
	}
	
	if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, SizeX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, SizeY))
		{
			SetSizeWithinBoundaryBox(FVector2D(SizeX, SizeY));
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, PositionX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, PositionY))
		{
			if (UDMXPixelMappingOutputDMXComponent* ParentOutputComponent = Cast<UDMXPixelMappingOutputDMXComponent>(Parent))
			{
				float NewPositionX = ParentOutputComponent->GetPosition().X + RelativePositionX;
				float NewPositionY = ParentOutputComponent->GetPosition().Y + RelativePositionY;

				SetPositionInBoundaryBox(FVector2D(NewPositionX, NewPositionY));
			}
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<SWidget> UDMXPixelMappingMatrixCellComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	CachedWidget =
		SNew(SBox)
		.HeightOverride(SizeX)
		.WidthOverride(SizeY);

	Slot =
		&InCanvas->AddSlot()
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder)
		[
			CachedWidget.ToSharedRef()
		];

	// Border settings
	Brush.DrawAs = ESlateBrushDrawType::Border;
	Brush.TintColor = GetEditorColor(false);
	Brush.Margin = FMargin(1.f);

	Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
	CachedWidget->SetWidthOverride(SizeX);
	CachedWidget->SetHeightOverride(SizeY);

	UpdateWidget();

	return CachedWidget.ToSharedRef();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixCellComponent::ToggleHighlightSelection(bool bIsSelected)
{
	Super::ToggleHighlightSelection(bIsSelected);

	Brush.TintColor = GetEditorColor(bIsSelected);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixCellComponent::UpdateWidget()
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		// Make sure this always is on top of its parent
		if (ZOrder < MatrixComponent->GetZOrder())
		{
			ZOrder = MatrixComponent->GetZOrder() + 1;
		}

		// Hide in designer view
		if (!MatrixComponent->IsVisibleInDesigner() || !bVisibleInDesigner)
		{
			CachedWidget->SetContent(SNullWidget::NullWidget);
		}
		else
		{
			CachedWidget->SetContent(SNew(SDMXPixelMappingCell)
				.Brush(&Brush)
				.CellID(CellID)
			);
		}
	}
}
#endif // WITH_EDITOR

void UDMXPixelMappingMatrixCellComponent::PostParentAssigned()
{
	Super::PostParentAssigned();
}

#if WITH_EDITOR
FString UDMXPixelMappingMatrixCellComponent::GetUserFriendlyName() const
{
	if (UDMXEntityFixturePatch* Patch = FixturePatchMatrixRef.GetFixturePatch())
	{
		return FString::Printf(TEXT("%s: Cell %d"), *Patch->GetDisplayName(), CellID);
	}

	return FString(TEXT("Invalid Patch"));
}
#endif // WITH_EDITOR

const FName& UDMXPixelMappingMatrixCellComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("MatrixCell");
	return NamePrefix;
}

void UDMXPixelMappingMatrixCellComponent::ResetDMX()
{
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(RendererComponent))
	{
		return;
	}
	RendererComponent->ResetColorDownsampleBufferPixel(DownsamplePixelIndex);

	SendDMX();
}

void UDMXPixelMappingMatrixCellComponent::SendDMX()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXPixelMaping_SendMatrixCell);

	UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();
	UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent);
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(FixturePatch))
	{
		return;
	}

	if (!ensure(MatrixComponent))
	{
		return;
	}

	if (!ensure(RendererComponent))
	{
		return;
	}

	if (AttributeNameChannelMap.Num() == 0)
	{
		if (FixturePatch->GetMatrixCellChannelsAbsoluteWithValidation(CellCoordinate, AttributeNameChannelMap))
		{
			return;
		}
	}

	if (MatrixComponent->AttributeRExpose && !ByteOffsetR.IsSet())
	{
		ByteOffsetR = GetNumChannelsOfAttribute(FixturePatch, MatrixComponent->AttributeR.Name) - 1;
	}

	if (MatrixComponent->AttributeGExpose && !ByteOffsetG.IsSet())
	{
		ByteOffsetG = GetNumChannelsOfAttribute(FixturePatch, MatrixComponent->AttributeG.Name) - 1;
	}

	if (MatrixComponent->AttributeBExpose && !ByteOffsetB.IsSet())
	{
		ByteOffsetB = GetNumChannelsOfAttribute(FixturePatch, MatrixComponent->AttributeB.Name) - 1;
	}

	if (MatrixComponent->bMonochromeExpose && !ByteOffsetM.IsSet())
	{
		ByteOffsetM = GetNumChannelsOfAttribute(FixturePatch, MatrixComponent->MonochromeIntensity.Name) - 1;
	}


	if (const FDMXFixtureMode* ModePtr = FixturePatch->GetActiveMode())
	{
		const FDMXFixtureMatrix& FixtureMatrixConfig = ModePtr->FixtureMatrixConfig;

		// If there are any cell attributes
		const int32 NumChannels = FixtureMatrixConfig.XCells * FixtureMatrixConfig.YCells;
		if (NumChannels > 0)
		{
			const TOptional<FColor> Color = RendererComponent->GetDownsampleBufferPixel(DownsamplePixelIndex);

			if (Color.IsSet())
			{
				if (MatrixComponent->ColorMode == EDMXColorMode::CM_RGB)
				{
					if (MatrixComponent->AttributeRExpose)
					{
						const int32 ColorRValue = static_cast<int32>(Color->R) << (*ByteOffsetR * 8);

						FixturePatch->SendMatrixCellValueWithAttributeMap(CellCoordinate, MatrixComponent->AttributeR.Name, ColorRValue, AttributeNameChannelMap);
					}

					if (MatrixComponent->AttributeGExpose)
					{
						const int32 ColorGValue = static_cast<int32>(Color->G) << (*ByteOffsetG * 8);

						FixturePatch->SendMatrixCellValueWithAttributeMap(CellCoordinate, MatrixComponent->AttributeG.Name, ColorGValue, AttributeNameChannelMap);
					}

					if (MatrixComponent->AttributeBExpose)
					{
						const int32 ColorBValue = static_cast<int32>(Color->B) << (*ByteOffsetB * 8);

						FixturePatch->SendMatrixCellValueWithAttributeMap(CellCoordinate, MatrixComponent->AttributeB.Name, ColorBValue, AttributeNameChannelMap);
					}
				}
				else if (MatrixComponent->ColorMode == EDMXColorMode::CM_Monochrome)
				{
					if (MatrixComponent->bMonochromeExpose)
					{
						// https://www.w3.org/TR/AERT/#color-contrast
						const int32 Intensity = static_cast<int32>(0.299 * Color->R + 0.587 * Color->G + 0.114 * Color->B) << (*ByteOffsetM * 8);
						FixturePatch->SendMatrixCellValueWithAttributeMap(CellCoordinate, MatrixComponent->MonochromeIntensity.Name, Intensity, AttributeNameChannelMap);
					}
				}
			}

			// Send Extra Cell Attributes
			UDMXPixelMappingMatrixComponent* ParentMatrix = CastChecked<UDMXPixelMappingMatrixComponent>(Parent);
			for (const FDMXPixelMappingExtraAttribute& ExtraAttribute : ParentMatrix->ExtraCellAttributes)
			{
				FixturePatch->SendMatrixCellValueWithAttributeMap(CellCoordinate, ExtraAttribute.Attribute, ExtraAttribute.Value, AttributeNameChannelMap);
			}
		}
	}
}

void UDMXPixelMappingMatrixCellComponent::QueueDownsample()
{
	// Queue pixels into the downsample rendering
	UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent);
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

FVector2D UDMXPixelMappingMatrixCellComponent::GetSize() const
{
	return FVector2D(SizeX, SizeY);
}

FVector2D UDMXPixelMappingMatrixCellComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingMatrixCellComponent::SetPosition(const FVector2D& InPosition)
{
#if WITH_EDITOR
	if (IsLockInDesigner())
	{
		if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
		{
			if (!MatrixComponent->IsLockInDesigner() &&
				MatrixComponent->IsVisibleInDesigner())
			{
				MatrixComponent->SetPosition(InPosition);
			}
		}
	}
	else
	{
		SetPositionInBoundaryBox(InPosition);
	}
#else
	SetPositionInBoundaryBox(InPosition);
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixCellComponent::SetPositionFromParent(const FVector2D& InPosition)
{
	PositionX = FMath::RoundHalfToZero(InPosition.X);
	PositionY = FMath::RoundHalfToZero(InPosition.Y);

#if WITH_EDITOR
	if (Slot != nullptr)
	{
		Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
	}
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixCellComponent::SetPositionInBoundaryBox(const FVector2D& InPosition)
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		PositionX = InPosition.X;
		PositionY = InPosition.Y;

		// Left Border
		float LeftBorderPosition = MatrixComponent->PositionX;
		float PositionXLeftBorder = InPosition.X;
		if (PositionXLeftBorder <= LeftBorderPosition)
		{
			PositionX = LeftBorderPosition;
		}

		// Top Border
		float TopBorderPosition = MatrixComponent->PositionY;
		float TopYRightBoarder = InPosition.Y;
		if (TopYRightBoarder <= TopBorderPosition)
		{
			PositionY = TopBorderPosition;
		}

		MatrixComponent->SetSizeWithinMaxBoundaryBox();

#if WITH_EDITOR
		if (Slot != nullptr)
		{
			Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
		}

		RelativePositionX = PositionX - MatrixComponent->GetPosition().X;
		RelativePositionY = PositionY - MatrixComponent->GetPosition().Y;
#endif // WITH_EDITOR
	}
}

void UDMXPixelMappingMatrixCellComponent::SetSizeWithinBoundaryBox(const FVector2D& InSize)
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		if (SizeX <= MixPixelSize.X)
		{
			SizeX = MixPixelSize.X;
		}

		if (SizeY <= MixPixelSize.Y)
		{
			SizeY = MixPixelSize.Y;
		}

		MatrixComponent->SetSizeWithinMaxBoundaryBox();

#if WITH_EDITOR
		if (Slot != nullptr)
		{
			Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
		}
#endif // WITH_EDITOR
	}
}

void UDMXPixelMappingMatrixCellComponent::SetSizeFromParent(const FVector2D& InSize)
{
	SizeX = FMath::RoundHalfToZero(InSize.X);
	SizeY = FMath::RoundHalfToZero(InSize.Y);

#if WITH_EDITOR
	CachedWidget->SetWidthOverride(SizeX);
	CachedWidget->SetHeightOverride(SizeY);
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixCellComponent::SetPixelCoordinate(FIntPoint InPixelCoordinate)
{
	CellCoordinate = InPixelCoordinate;
	AttributeNameChannelMap.Empty();
}

void UDMXPixelMappingMatrixCellComponent::SetSize(const FVector2D& InSize)
{
	SizeX = FMath::RoundHalfToZero(InSize.X);
	SizeY = FMath::RoundHalfToZero(InSize.Y);

	SetSizeWithinBoundaryBox(InSize);
}

void UDMXPixelMappingMatrixCellComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent())
	{
		RendererComponent->RendererInputTexture();
	}

	RenderAndSendDMX();
}

bool UDMXPixelMappingMatrixCellComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	if (const UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Component))
	{
		if (MatrixComponent->FixturePatchMatrixRef.DMXLibrary == FixturePatchMatrixRef.DMXLibrary &&
			MatrixComponent->FixturePatchMatrixRef.GetFixturePatch() == FixturePatchMatrixRef.GetFixturePatch())
		{
			return true;
		}
	}

	return false;
}

UDMXPixelMappingRendererComponent* UDMXPixelMappingMatrixCellComponent::GetRendererComponent() const
{
	return Parent ? Cast<UDMXPixelMappingRendererComponent>(Parent->Parent) : nullptr;
}

#undef LOCTEXT_NAMESPACE
