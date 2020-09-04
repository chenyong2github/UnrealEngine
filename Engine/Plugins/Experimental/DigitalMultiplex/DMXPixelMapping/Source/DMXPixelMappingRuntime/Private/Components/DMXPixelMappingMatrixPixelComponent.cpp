// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingMatrixPixelComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "IDMXPixelMappingRenderer.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMappingTypes.h"
#include "DMXSubsystem.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityController.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"

#include "Widgets/Images/SImage.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Layout/SBox.h"

#if WITH_EDITOR
#include "SDMXPixelMappingEditorWidgets.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "DMXPixelMappingMatrixPixelComponent"

const FVector2D UDMXPixelMappingMatrixPixelComponent::MixPixelSize = FVector2D(1.f);

UDMXPixelMappingMatrixPixelComponent::UDMXPixelMappingMatrixPixelComponent()
{
	SizeX = 100.f;
	SizeY = 100.f;

#if WITH_EDITOR
	Slot = nullptr;

	bLockInDesigner = true;
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixPixelComponent::PostLoad()
{
	Super::PostLoad();

	GetOutputTexture();
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixPixelComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	FName&& PropertyName = PropertyChangedChainEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixPixelComponent, SizeX) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixPixelComponent, SizeY))
	{
		SetSizeWithinBoundaryBox(FVector2D(SizeX, SizeY));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixPixelComponent, PositionX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixPixelComponent, PositionY))
	{
		SetPositionInBoundaryBox(FVector2D(PositionX, PositionY));
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixPixelComponent, bVisibleInDesigner))
	{
		UpdateWidget();
	}
}

bool UDMXPixelMappingMatrixPixelComponent::CanEditChange(const FProperty* InProperty) const
{
	FString PropertyName = InProperty->GetName();

	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingMatrixPixelComponent, SizeX) ||
			PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingMatrixPixelComponent, SizeY) ||
			PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingMatrixPixelComponent, PositionX) ||
			PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingMatrixPixelComponent, PositionY)
			)
		{
			if (MatrixComponent->IsLockInDesigner() == false)
			{
				return !bLockInDesigner;
			}

			return false;
		}
		else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UDMXPixelMappingMatrixPixelComponent, bLockInDesigner))
		{
			return !MatrixComponent->IsLockInDesigner();
		}
	}

	return Super::CanEditChange(InProperty);
}

TSharedRef<SWidget> UDMXPixelMappingMatrixPixelComponent::BuildSlot(TSharedRef<SCanvas> InCanvas)
{
	CachedWidget = SNew(SBox);

	Slot = &InCanvas->AddSlot()
		[
			CachedWidget.ToSharedRef()
		];

	// Border settings
	Brush.DrawAs = ESlateBrushDrawType::Border;
	Brush.TintColor = FLinearColor::Blue;
	Brush.Margin = FMargin(1.f);

	Slot->Position(FVector2D(PositionX, PositionY));
	Slot->Size(FVector2D(SizeX, SizeY));

	UpdateWidget();

	return CachedWidget.ToSharedRef();
}

void UDMXPixelMappingMatrixPixelComponent::ToggleHighlightSelection(bool bIsSelected)
{
	if (bIsSelected)
	{
		Brush.Margin = FMargin(1.f);
		Brush.TintColor = FLinearColor::Green;
	}
	else
	{
		Brush.Margin = FMargin(1.f);
		Brush.TintColor = FLinearColor::Blue;
	}
}

bool UDMXPixelMappingMatrixPixelComponent::IsVisibleInDesigner() const 
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		if (bVisibleInDesigner == false)
		{
			return false;
		}
		
		return MatrixComponent->IsVisibleInDesigner();
	}

	return bVisibleInDesigner;
}

void UDMXPixelMappingMatrixPixelComponent::UpdateWidget()
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		// Hide in designer view
		if (!MatrixComponent->IsVisibleInDesigner() || !bVisibleInDesigner)
		{
			CachedWidget->SetContent(SNullWidget::NullWidget);
		}
		else
		{
			CachedWidget->SetContent(SNew(SDMXPixelMappingPixel)
				.Brush(&Brush)
				.PixelIndex(PixelIndex)
			);
		}
	}
}

bool UDMXPixelMappingMatrixPixelComponent::IsLockInDesigner() const
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
		return MatrixComponent->IsLockInDesigner() || bLockInDesigner;
	}

	return bLockInDesigner;
}

#endif // WITH_EDITOR

void UDMXPixelMappingMatrixPixelComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	GetOutputTexture();
}

const FName& UDMXPixelMappingMatrixPixelComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("MatrixPixel");
	return NamePrefix;
}

void UDMXPixelMappingMatrixPixelComponent::ResetDMX()
{
	UpdateSurfaceBuffer([this](TArray<FColor>& InSurfaceBuffer, FIntRect& InSurfaceRect)
	{
		for (FColor& Color : InSurfaceBuffer)
		{
			Color = FColor::Black;
		}
	});

	SendDMX();
}

void UDMXPixelMappingMatrixPixelComponent::SendDMX()
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();
	UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
	UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent);

	if (MatrixComponent != nullptr && DMXSubsystem != nullptr  && FixturePatch != nullptr)
	{
		if (UDMXEntityFixtureType * ParentFixtureType = FixturePatch->ParentFixtureTypeTemplate)
		{
			int32 ActiveMode = FixturePatch->ActiveMode;

			check(ParentFixtureType->Modes.IsValidIndex(ActiveMode));

			const FDMXFixtureMode& FixtureMode = ParentFixtureType->Modes[ActiveMode];
			const FDMXPixelMatrix& PixelMatrixConfig = FixtureMode.PixelMatrixConfig;

			// If there are any pixel functions
			int32 NumChannels = PixelMatrixConfig.XPixels * PixelMatrixConfig.YPixels;
			if (NumChannels > 0)
			{
				TArray<FColor> LocalSurfaceBuffer;
				GetSurfaceBuffer([this, &LocalSurfaceBuffer](const TArray<FColor>& InSurfaceBuffer, const FIntRect& InSurfaceRect)
				{
					LocalSurfaceBuffer = InSurfaceBuffer;
				});

				if (LocalSurfaceBuffer.Num() == 1)
				{
					const FColor& Color = LocalSurfaceBuffer[0];

					if (MatrixComponent->ColorMode == EDMXColorMode::CM_RGB)
					{
						if (MatrixComponent->AttributeRExpose)
						{
							DMXSubsystem->SetMatrixPixel(FixturePatch, PixelCoordinate, MatrixComponent->AttributeR, Color.R);
						}

						if (MatrixComponent->AttributeGExpose)
						{
							DMXSubsystem->SetMatrixPixel(FixturePatch, PixelCoordinate, MatrixComponent->AttributeG, Color.G);
						}

						if (MatrixComponent->AttributeBExpose)
						{
							DMXSubsystem->SetMatrixPixel(FixturePatch, PixelCoordinate, MatrixComponent->AttributeB, Color.B);
						}
					}
					else if (MatrixComponent->ColorMode == EDMXColorMode::CM_Monochrome)
					{
						if (MatrixComponent->MonochromeExpose)
						{
							// https://www.w3.org/TR/AERT/#color-contrast
							uint8 Intensity = (0.299 * Color.R + 0.587 * Color.G + 0.114 * Color.B);
							DMXSubsystem->SetMatrixPixel(FixturePatch, PixelCoordinate, MatrixComponent->MonochromeIntensity, Intensity);
						}
					}
				}
			}
		}
	}
}

void UDMXPixelMappingMatrixPixelComponent::Render()
{
	RendererOutputTexture();
}

void UDMXPixelMappingMatrixPixelComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

void UDMXPixelMappingMatrixPixelComponent::RendererOutputTexture()
{
	UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent);
	UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this);

	if (MatrixComponent != nullptr && RendererComponent != nullptr)
	{
		UTexture* Texture = RendererComponent->GetRendererInputTexture();
		const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = RendererComponent->GetRenderer();

		if (Texture != nullptr && Renderer.IsValid())
		{
			GetOutputTexture();

			uint32 TexureSizeX = Texture->Resource->GetSizeX();
			uint32 TexureSizeY = Texture->Resource->GetSizeY();

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
				ExposeFactor = FVector4(MatrixComponent->MonochromeExpose ? Expose : NoExpose);
				InvertFactor = FIntVector4(MatrixComponent->MonochromeInvert, MatrixComponent->MonochromeInvert, MatrixComponent->MonochromeInvert, 0);
			}

			Renderer->DownsampleRender_GameThread(
				Texture->Resource,
				OutputTarget->Resource,
				OutputTarget->GameThread_GetRenderTargetResource(),
				ExposeFactor,
				InvertFactor,
				0, 0,
				OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY(),
				PositionX / TexureSizeX, PositionY / TexureSizeY,
				SizeX / TexureSizeX, SizeY / TexureSizeY,
				FIntPoint(OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY()),
				FIntPoint(1, 1),
				[=](TArray<FColor>& InSurfaceBuffer, FIntRect& InRect) { SetSurfaceBuffer(InSurfaceBuffer, InRect); }
			);
		}
	}
}

UTextureRenderTarget2D* UDMXPixelMappingMatrixPixelComponent::GetOutputTexture()
{
	if (OutputTarget == nullptr)
	{
		const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("DstTarget"));
		OutputTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
		OutputTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		OutputTarget->InitCustomFormat(1, 1, EPixelFormat::PF_B8G8R8A8, false);
	}

	return OutputTarget;
}

FVector2D UDMXPixelMappingMatrixPixelComponent::GetSize()
{
	return FVector2D(SizeX, SizeY);
}

FVector2D UDMXPixelMappingMatrixPixelComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingMatrixPixelComponent::SetPosition(const FVector2D& InPosition)
{
	FVector2D DeltaPosition = FVector2D(InPosition.X - PositionX, InPosition.Y - PositionY);

	// SetPosition from parent rounded the values for us
	Super::SetPosition(InPosition);

#if WITH_EDITOR
	if (IsLockInDesigner())
	{
		if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
		{
			MatrixComponent->SetPositionBasedOnRelativePixel(this, DeltaPosition);
		}

		Slot->Position(FVector2D(PositionX, PositionY));
	}
	else
	{
		SetPositionInBoundaryBox(InPosition);
	}
#else
	SetPositionInBoundaryBox(InPosition);
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixPixelComponent::SetPositionFromParent(const FVector2D& InPosition)
{
	// SetPosition from parret rounded the values for us
	Super::SetPosition(InPosition);

#if WITH_EDITOR
	if (Slot != nullptr)
	{
		Slot->Position(FVector2D(PositionX, PositionY));
	}
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixPixelComponent::SetPositionInBoundaryBox(const FVector2D& InPosition)
{
	if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent))
	{
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
			Slot->Position(FVector2D(PositionX, PositionY));
		}
#endif // WITH_EDITOR
	}
}

void UDMXPixelMappingMatrixPixelComponent::SetSizeWithinBoundaryBox(const FVector2D& InSize)
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
			Slot->Size(FVector2D(SizeX, SizeY));
		}
#endif // WITH_EDITOR
	}
}

void UDMXPixelMappingMatrixPixelComponent::SetSizeFromParent(const FVector2D& InSize)
{
	// SetSize from parent rounded the values for us
	Super::SetSize(InSize);

#if WITH_EDITOR
	if (Slot != nullptr)
	{
		Slot->Size(FVector2D(SizeX, SizeY));
	}
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixPixelComponent::SetSize(const FVector2D& InSize)
{
	// SetSize from parent rounded the values for us
	Super::SetSize(InSize);

	SetSizeWithinBoundaryBox(InSize);
}

void UDMXPixelMappingMatrixPixelComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
	{
		RendererComponent->RendererInputTexture();
	}

	RenderAndSendDMX();
}

bool UDMXPixelMappingMatrixPixelComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
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

#undef LOCTEXT_NAMESPACE
