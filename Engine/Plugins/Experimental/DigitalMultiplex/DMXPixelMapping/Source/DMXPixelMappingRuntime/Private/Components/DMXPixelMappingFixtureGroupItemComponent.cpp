// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "IDMXPixelMappingRenderer.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMappingTypes.h"
#include "DMXSubsystem.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXLibrary.h"

#include "Widgets/Images/SImage.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/ITransaction.h"
#include "Widgets/Layout/SBox.h"

const FVector2D UDMXPixelMappingFixtureGroupItemComponent::MixPixelSize = FVector2D(1.f);

UDMXPixelMappingFixtureGroupItemComponent::UDMXPixelMappingFixtureGroupItemComponent()
{
	SizeX = 100.f;
	SizeY = 100.f;
	PositionX = 0.f;
	PositionY = 0.f;

	ColorMode = EDMXColorMode::CM_RGB;
	AttributeRExpose = AttributeGExpose = AttributeBExpose = true;

#if WITH_EDITOR
	Slot = nullptr;
#endif // WITH_EDITOR
}

void UDMXPixelMappingFixtureGroupItemComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = GetFirstParentByClass<UDMXPixelMappingFixtureGroupComponent>(this))
	{
		FixturePatchRef = FixtureGroupComponent->SelectedFixturePatchRef;
	}

	SetPositionInBoundaryBox(FVector2D(PositionX, PositionY));
}

void UDMXPixelMappingFixtureGroupItemComponent::PostLoad()
{
	Super::PostLoad();
}

const FName& UDMXPixelMappingFixtureGroupItemComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("Fixture Item");
	return NamePrefix;
}

#if WITH_EDITOR

void UDMXPixelMappingFixtureGroupItemComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	FName&& PropertyName = PropertyChangedChainEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, SizeX) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, SizeY))
	{
		SetSizeWithinBoundaryBox(FVector2D(SizeX, SizeY));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, PositionX) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, PositionY))
	{
		//SetSizeWithinBoundaryBox(FVector2D(SizeX, SizeY));
		SetPositionInBoundaryBox(FVector2D(PositionX, PositionY));
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, bVisibleInDesigner))
	{
		UpdateWidget();
	}
}

TSharedRef<SWidget> UDMXPixelMappingFixtureGroupItemComponent::BuildSlot(TSharedRef<SCanvas> InCanvas)
{
	CachedWidget = SNew(SBox);

	Slot = &InCanvas->AddSlot()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.Padding(FMargin(0.0f, -20.0f))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(STextBlock)
				.Text(FText::FromString(GetName()))
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				CachedWidget.ToSharedRef()
			]
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

void UDMXPixelMappingFixtureGroupItemComponent::ToggleHighlightSelection(bool bIsSelected)
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

bool UDMXPixelMappingFixtureGroupItemComponent::IsVisibleInDesigner() const
{
	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
	{
		if (bVisibleInDesigner == false)
		{
			return false;
		}

		return FixtureGroupComponent->IsVisibleInDesigner();
	}

	return bVisibleInDesigner;
}

void UDMXPixelMappingFixtureGroupItemComponent::UpdateWidget()
{
	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
	{
		// Hide in designer view
		if (!FixtureGroupComponent->IsVisibleInDesigner() || !bVisibleInDesigner)
		{
			CachedWidget->SetContent(SNullWidget::NullWidget);
		}
		else
		{
			CachedWidget->SetContent(SNew(SImage).Image(&Brush));
		}
	}
}

#endif // WITH_EDITOR

void UDMXPixelMappingFixtureGroupItemComponent::ResetDMX()
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

void UDMXPixelMappingFixtureGroupItemComponent::SendDMX()
{
	if (UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure())
	{
		UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
		UDMXLibrary* DMXLibrary = FixturePatchRef.DMXLibrary;

		TArray<FColor> LocalSurfaceBuffer;
		GetSurfaceBuffer([this, &LocalSurfaceBuffer](const TArray<FColor>& InSurfaceBuffer, const FIntRect& InSurfaceRect)
		{
			LocalSurfaceBuffer = InSurfaceBuffer;
		});

		if (FixturePatch != nullptr && DMXLibrary != nullptr)
		{
			EDMXSendResult OutResult;

			TMap<FDMXAttributeName, int32> AttributeMap;

			if (LocalSurfaceBuffer.Num() == 1)
			{
				const FColor& Color = LocalSurfaceBuffer[0];

				if (ColorMode == EDMXColorMode::CM_RGB)
				{
					if (AttributeRExpose)
					{
						AttributeMap.Add(AttributeR, Color.R);
					}

					if (AttributeGExpose)
					{
						AttributeMap.Add(AttributeG, Color.G);
					}

					if (AttributeBExpose)
					{
						AttributeMap.Add(AttributeB, Color.B);
					}
				}
				else if (ColorMode == EDMXColorMode::CM_Monochrome)
				{
					if (MonochromeExpose)
					{
						// https://www.w3.org/TR/AERT/#color-contrast
						uint8 Intensity = (0.299 * Color.R + 0.587 * Color.G + 0.114 * Color.B);
						AttributeMap.Add(MonochromeIntensity, Intensity);
					}
				}
			}

			// Add offset functions
			for (const FDMXPixelMappingExtraAttribute& ExtraAttribute : ExtraAttributes)
			{
				AttributeMap.Add(ExtraAttribute.Attribute, ExtraAttribute.Value);
			}

			DMXSubsystem->SendDMX(FixturePatch, AttributeMap, OutResult);
		}
	}
}

void UDMXPixelMappingFixtureGroupItemComponent::Render()
{	
	RendererOutputTexture();
}

void UDMXPixelMappingFixtureGroupItemComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

void UDMXPixelMappingFixtureGroupItemComponent::RendererOutputTexture()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
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
			if (ColorMode == EDMXColorMode::CM_RGB)
			{
				ExposeFactor = FVector4(AttributeRExpose ? 1.f : 0.f, AttributeGExpose ? 1.f : 0.f, AttributeBExpose ? 1.f : 0.f, 1.f);
				InvertFactor = FIntVector4(AttributeRInvert, AttributeGInvert, AttributeBInvert, 0);
			}
			else if (ColorMode == EDMXColorMode::CM_Monochrome)
			{
				static const FVector4 Expose(1.f, 1.f, 1.f, 1.f);
				static const FVector4 NoExpose(0.f, 0.f, 0.f, 0.f);
				ExposeFactor = FVector4(MonochromeExpose ? Expose : NoExpose);
				InvertFactor = FIntVector4(MonochromeInvert, MonochromeInvert, MonochromeInvert, 0);
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

UTextureRenderTarget2D* UDMXPixelMappingFixtureGroupItemComponent::GetOutputTexture()
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

FVector2D UDMXPixelMappingFixtureGroupItemComponent::GetSize()
{
	return FVector2D(SizeX, SizeY);
}

FVector2D UDMXPixelMappingFixtureGroupItemComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingFixtureGroupItemComponent::SetPosition(const FVector2D& InPosition)
{
	Super::SetPosition(InPosition);
	SetPositionInBoundaryBox(InPosition);
}

void UDMXPixelMappingFixtureGroupItemComponent::SetSize(const FVector2D& InSize)
{
	Super::SetSize(InSize);
	SetSizeWithinBoundaryBox(InSize);
}

void UDMXPixelMappingFixtureGroupItemComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
	{
		RendererComponent->RendererInputTexture();
	}

	RenderAndSendDMX();
}


/**
 *  ---------------
 *  |             |
 *  |  --------   |
 *  |  |      |   |
 *  |  |      |   |
 *  |  --------   |
 *  ---------------
 *  Group item shoud be inside the parent
 */
void UDMXPixelMappingFixtureGroupItemComponent::SetPositionInBoundaryBox(const FVector2D& InPosition)
{
	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
	{
		// 1. Right Border
		float RightBorderPosition = FixtureGroupComponent->SizeX + FixtureGroupComponent->PositionX;
		float PositionXRightBoarder = InPosition.X + SizeX;

		// 2. Left Border
		float LeftBorderPosition = FixtureGroupComponent->PositionX;
		float PositionXLeftBorder = InPosition.X;

		if (PositionXRightBoarder >= RightBorderPosition)
		{
			PositionX = RightBorderPosition - SizeX;
		}
		else if (PositionXLeftBorder <= LeftBorderPosition)
		{
			PositionX = LeftBorderPosition;
		}

		// 3. Bottom Border
		float BottomBorderPosition = FixtureGroupComponent->SizeY + FixtureGroupComponent->PositionY;
		float PositionYRightBoarder = InPosition.Y + SizeY;

		// 4. Top Border
		float TopBorderPosition = FixtureGroupComponent->PositionY;
		float TopYRightBoarder = InPosition.Y;

		if (PositionYRightBoarder >= BottomBorderPosition)
		{
			PositionY = BottomBorderPosition - SizeY;
		}
		else if (TopYRightBoarder <= TopBorderPosition)
		{
			PositionY = TopBorderPosition;
		}

#if WITH_EDITOR
		if (Slot != nullptr)
		{
			Slot->Position(FVector2D(PositionX, PositionY));
		}
#endif // WITH_EDITOR
	}
}

bool UDMXPixelMappingFixtureGroupItemComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
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

void UDMXPixelMappingFixtureGroupItemComponent::SetSizeWithinBoundaryBox(const FVector2D& InSize)
{
	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
	{
		// 1. Right Border
		float RightBorderPosition = FixtureGroupComponent->SizeX + FixtureGroupComponent->PositionX;
		float PositionXRightBoarder = PositionX + InSize.X;

		if (PositionXRightBoarder >= RightBorderPosition)
		{
			SizeX = RightBorderPosition - PositionX;
		}
		else if (SizeX <= MixPixelSize.X)
		{
			SizeX = MixPixelSize.X;
		}

		// 2. Bottom Border
		float BottomBorderPosition = FixtureGroupComponent->SizeY + FixtureGroupComponent->PositionY;
		float PositionYRightBoarder = PositionY + InSize.Y;

		if (PositionYRightBoarder >= BottomBorderPosition)
		{
			SizeY = BottomBorderPosition - PositionY;
		}
		else if (SizeY <= MixPixelSize.Y)
		{
			SizeY = MixPixelSize.Y;
		}

#if WITH_EDITOR
		if (Slot != nullptr)
		{
			Slot->Size(FVector2D(SizeX, SizeY));
		}
#endif // WITH_EDITOR
	}
}
