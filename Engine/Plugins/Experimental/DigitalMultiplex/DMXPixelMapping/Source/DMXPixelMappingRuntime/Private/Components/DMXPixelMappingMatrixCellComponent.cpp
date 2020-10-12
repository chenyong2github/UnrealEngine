// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingMatrixCellComponent.h"

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

const FVector2D UDMXPixelMappingMatrixCellComponent::MixPixelSize = FVector2D(1.f);

UDMXPixelMappingMatrixCellComponent::UDMXPixelMappingMatrixCellComponent()
{
	SizeX = 100.f;
	SizeY = 100.f;

#if WITH_EDITOR
	RelativePositionX = 0.f;
	RelativePositionY = 0.f;

	Slot = nullptr;

	bLockInDesigner = true;

	ZOrder = 2;
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixCellComponent::PostLoad()
{
	Super::PostLoad();

	GetOutputTexture();
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

	GetOutputTexture();
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
	UpdateSurfaceBuffer([this](TArray<FColor>& InSurfaceBuffer, FIntRect& InSurfaceRect)
		{
			for (FColor& Color : InSurfaceBuffer)
			{
				Color = FColor::Black;
			}
		});

	SendDMX();
}

void UDMXPixelMappingMatrixCellComponent::SendDMX()
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();
	UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
	UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Parent);

	if (MatrixComponent != nullptr && DMXSubsystem != nullptr && FixturePatch != nullptr)
	{
		if (UDMXEntityFixtureType* ParentFixtureType = FixturePatch->ParentFixtureTypeTemplate)
		{
			int32 ActiveMode = FixturePatch->ActiveMode;

			check(ParentFixtureType->Modes.IsValidIndex(ActiveMode));

			const FDMXFixtureMode& FixtureMode = ParentFixtureType->Modes[ActiveMode];
			const FDMXFixtureMatrix& FixtureMatrixConfig = FixtureMode.FixtureMatrixConfig;

			// If there are any cell attribures
			int32 NumChannels = FixtureMatrixConfig.XCells * FixtureMatrixConfig.YCells;
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
							DMXSubsystem->SetMatrixCellValue(FixturePatch, CellCoordinate, MatrixComponent->AttributeR, Color.R);
						}

						if (MatrixComponent->AttributeGExpose)
						{
							DMXSubsystem->SetMatrixCellValue(FixturePatch, CellCoordinate, MatrixComponent->AttributeG, Color.G);
						}

						if (MatrixComponent->AttributeBExpose)
						{
							DMXSubsystem->SetMatrixCellValue(FixturePatch, CellCoordinate, MatrixComponent->AttributeB, Color.B);
						}
					}
					else if (MatrixComponent->ColorMode == EDMXColorMode::CM_Monochrome)
					{
						if (MatrixComponent->bMonochromeExpose)
						{
							// https://www.w3.org/TR/AERT/#color-contrast
							uint8 Intensity = (0.299 * Color.R + 0.587 * Color.G + 0.114 * Color.B);
							DMXSubsystem->SetMatrixCellValue(FixturePatch, CellCoordinate, MatrixComponent->MonochromeIntensity, Intensity);
						}
					}
				}

				// Send Extra Cell Attributes
				UDMXPixelMappingMatrixComponent* ParentMatrix = CastChecked<UDMXPixelMappingMatrixComponent>(Parent);
				for (const FDMXPixelMappingExtraAttribute& ExtraAttribute : ParentMatrix->ExtraCellAttributes)
				{
					DMXSubsystem->SetMatrixCellValue(FixturePatch, CellCoordinate, ExtraAttribute.Attribute, ExtraAttribute.Value);
				}
			}
		}
	}
}

void UDMXPixelMappingMatrixCellComponent::Render()
{
	RendererOutputTexture();
}

void UDMXPixelMappingMatrixCellComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

void UDMXPixelMappingMatrixCellComponent::RendererOutputTexture()
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

			const uint32 TexureSizeX = Texture->Resource->GetSizeX();
			const uint32 TexureSizeY = Texture->Resource->GetSizeY();

			const FVector2D Position = FVector2D(0.f, 0.f);
			const FVector2D Size = FVector2D(OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY());
			const FVector2D UV = FVector2D(PositionX / TexureSizeX, PositionY / TexureSizeY);

			const FVector2D UVSize(SizeX / TexureSizeX, SizeY / TexureSizeY);
			const FVector2D UVCellSize = UVSize / 2.f;

			const FIntPoint TargetSize(OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY());
			const FIntPoint TextureSize(1, 1);

			const bool bStaticCalculateUV = true;

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

			Renderer->DownsampleRender_GameThread(
				Texture->Resource,
				OutputTarget->Resource,
				OutputTarget->GameThread_GetRenderTargetResource(),
				ExposeFactor,
				InvertFactor,
				Position,
				Size,
				UV,
				UVSize,
				UVCellSize,
				TargetSize,
				TextureSize,
				CellBlendingQuality,
				bStaticCalculateUV,
				[=](TArray<FColor>& InSurfaceBuffer, FIntRect& InRect) { SetSurfaceBuffer(InSurfaceBuffer, InRect); }
			);
		}
	}
}

UTextureRenderTarget2D* UDMXPixelMappingMatrixCellComponent::GetOutputTexture()
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

void UDMXPixelMappingMatrixCellComponent::SetSize(const FVector2D& InSize)
{
	SizeX = FMath::RoundHalfToZero(InSize.X);
	SizeY = FMath::RoundHalfToZero(InSize.Y);

	SetSizeWithinBoundaryBox(InSize);
}

void UDMXPixelMappingMatrixCellComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
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

#undef LOCTEXT_NAMESPACE
