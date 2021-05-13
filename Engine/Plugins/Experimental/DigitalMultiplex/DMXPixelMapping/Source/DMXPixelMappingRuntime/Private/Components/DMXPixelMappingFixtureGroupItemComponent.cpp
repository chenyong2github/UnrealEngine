// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"

#include "DMXPixelMappingTypes.h"
#include "DMXSubsystem.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"

#include "Engine/Texture.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"


DECLARE_CYCLE_STAT(TEXT("Send Fixture Group Item"), STAT_DMXPixelMaping_FixtureGroupItem, STATGROUP_DMXPIXELMAPPING);

#define LOCTEXT_NAMESPACE "DMXPixelMappingFixtureGroupItemComponent"

const FVector2D UDMXPixelMappingFixtureGroupItemComponent::MixPixelSize = FVector2D(1.f);

UDMXPixelMappingFixtureGroupItemComponent::UDMXPixelMappingFixtureGroupItemComponent()
	: DownsamplePixelIndex(0)
{
	SizeX = 10.f;
	SizeY = 10.f;
	PositionX = 0.f;
	PositionY = 0.f;

	ColorMode = EDMXColorMode::CM_RGB;
	AttributeRExpose = AttributeGExpose = AttributeBExpose = true;
	AttributeR.SetFromName("Red");
	AttributeG.SetFromName("Green");
	AttributeB.SetFromName("Blue");

	bMonochromeExpose = true;

#if WITH_EDITOR
	RelativePositionX = 0.f;
	RelativePositionY = 0.f;

	Slot = nullptr;

	bEditableEditorColor = true;

	ZOrder = 2;
#endif // WITH_EDITOR
}

bool UDMXPixelMappingFixtureGroupItemComponent::CheckForDuplicateFixturePatch(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent, FDMXEntityFixturePatchRef InFixturePatchRef)
{
	for(UDMXPixelMappingBaseComponent* Component : FixtureGroupComponent->Children)
	{
		UDMXPixelMappingFixtureGroupItemComponent* FixtureGroupItem = Cast<UDMXPixelMappingFixtureGroupItemComponent>(Component);
		if (FixtureGroupItem)
		{
			if (FixtureGroupItem == this)
			{
				continue;
			}
			if (FixtureGroupItem->FixturePatchRef == InFixturePatchRef)
			{
				return true;
			}
		}
	}
	return false;
}

void UDMXPixelMappingFixtureGroupItemComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	if (UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent())
	{
		for (UDMXPixelMappingBaseComponent* Component : RendererComponent->Children)
		{
			UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Component);
			if (FixtureGroupComponent)
			{
				if (CheckForDuplicateFixturePatch(FixtureGroupComponent, FixturePatchRef))
				{
					UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("FixturePatch %s already assigned to Renderer %s"),
						*FixturePatchRef.GetFixturePatch()->GetName(), *RendererComponent->GetName());
				}
			}
		}
	}

#if WITH_EDITOR
	UpdateWidget();
	AutoMapAttributes();
#endif // WITH_EDITOR
}

#if WITH_EDITOR
FString UDMXPixelMappingFixtureGroupItemComponent::GetUserFriendlyName() const
{
	if (UDMXEntityFixturePatch* Patch = FixturePatchRef.GetFixturePatch())
	{
		return Patch->GetDisplayName();
	}

	return FString(TEXT("Fixture Group Item: No Fixture Patch"));
}
#endif // WITH_EDITOR

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

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, FixturePatchRef))
	{
		check(PatchNameWidget.IsValid());
		PatchNameWidget->SetText(FText::FromString(GetUserFriendlyName()));
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, bVisibleInDesigner))
	{
		UpdateWidget();
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, EditorColor))
	{
		Brush.TintColor = EditorColor;
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeR))
	{
		ByteOffsetR.Reset();
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeG))
	{
		ByteOffsetG.Reset();
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeB))
	{
		ByteOffsetB.Reset();
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, MonochromeIntensity))
	{
		ByteOffsetM.Reset();
	}
	
	if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, SizeX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, SizeY))
		{
			SetSizeWithinBoundaryBox(FVector2D(SizeX, SizeY));
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, RelativePositionX) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, RelativePositionY))
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
TSharedRef<SWidget> UDMXPixelMappingFixtureGroupItemComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	constexpr FLinearColor NiceLightBlue = FLinearColor(0.678f, 0.847f, 0.901f, 0.25f);

	CachedWidget =
		SNew(SBox)
		.HeightOverride(SizeX)
		.WidthOverride(SizeY);

	CachedLabelBox =
		SNew(SBox)
		.Padding(FMargin(2.f, 1.f, 2.f, 1.f))
		.WidthOverride(SizeY)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::DownOnly)
			[
				SAssignNew(PatchNameWidget, STextBlock)
				.Text(FText::FromString(GetUserFriendlyName()))
			]
		];

	Slot =
		&InCanvas->AddSlot()
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		.ZOrder(ZOrder)
		[
			SNew(SOverlay)
			
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				CachedLabelBox.ToSharedRef()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				CachedWidget.ToSharedRef()
			]
		];

	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();

	if (FixturePatch && EditorColor == FLinearColor::Blue)
	{
		EditorColor = FixturePatch->EditorColor;
	}

	// Border settings
	Brush.DrawAs = ESlateBrushDrawType::Border;
	Brush.TintColor = GetEditorColor(false);
	Brush.Margin = FMargin(1.f);

	Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
	CachedWidget->SetWidthOverride(SizeX);
	CachedWidget->SetHeightOverride(SizeY);
	CachedLabelBox->SetWidthOverride(SizeX);

	UpdateWidget();

	return CachedWidget.ToSharedRef();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupItemComponent::ToggleHighlightSelection(bool bIsSelected)
{
	Super::ToggleHighlightSelection(bIsSelected);

	if (bIsSelected)
	{
		Brush.TintColor = FLinearColor::Green;
	}
	else
	{
		UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
		check(FixturePatch);

		Brush.TintColor = FixturePatch->EditorColor;
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
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
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupItemComponent::UpdateWidget()
{
	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
	{
		// Make sure this always is on top of its parent
		if (ZOrder < FixtureGroupComponent->GetZOrder())
		{
			ZOrder = FixtureGroupComponent->GetZOrder() + 1;
		}

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
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(RendererComponent))
	{
		return;
	}

	RendererComponent->ResetColorDownsampleBufferPixel(DownsamplePixelIndex);

	SendDMX();
}

void UDMXPixelMappingFixtureGroupItemComponent::SendDMX()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXPixelMaping_FixtureGroupItem);

	UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch();
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(FixturePatch))
	{
		return;
	}

	if (!ensure(RendererComponent))
	{
		return;
	}

	TMap<FDMXAttributeName, int32> AttributeMap;

	const TOptional<FColor> Color = RendererComponent->GetDownsampleBufferPixel(DownsamplePixelIndex);
	if (Color.IsSet())
	{
		if (AttributeRExpose && !ByteOffsetR.IsSet())
		{
			ByteOffsetR = GetNumChannelsOfAttribute(FixturePatch, AttributeR.Name) - 1;
		}

		if (AttributeGExpose && !ByteOffsetG.IsSet())
		{
			ByteOffsetG = GetNumChannelsOfAttribute(FixturePatch, AttributeG.Name) - 1;
		}

		if (AttributeBExpose && !ByteOffsetB.IsSet())
		{
			ByteOffsetB = GetNumChannelsOfAttribute(FixturePatch, AttributeB.Name) - 1;
		}

		if (bMonochromeExpose && !ByteOffsetM.IsSet())
		{
			ByteOffsetM = GetNumChannelsOfAttribute(FixturePatch, MonochromeIntensity.Name) - 1;
		}

		
		if (ColorMode == EDMXColorMode::CM_RGB)
		{
			if (AttributeRExpose)
			{
				AttributeMap.Add(AttributeR, int32(Color->R) << (*ByteOffsetR * 8));
			}

			if (AttributeGExpose)
			{
				AttributeMap.Add(AttributeG, int32(Color->G) << (*ByteOffsetG * 8));
			}

			if (AttributeBExpose)
			{
				AttributeMap.Add(AttributeB, int32(Color->B) << (*ByteOffsetB * 8));
			}
		}
		else if (ColorMode == EDMXColorMode::CM_Monochrome)
		{
			if (bMonochromeExpose)
			{					
				// https://www.w3.org/TR/AERT/#color-contrast
				const int32 Intensity = int32(0.299 * Color->R + 0.587 * Color->G + 0.114 * Color->B) << (*ByteOffsetM * 8);
				AttributeMap.Add(MonochromeIntensity, Intensity);
			}
		}
	}

	// Add offset functions
	for (const FDMXPixelMappingExtraAttribute& ExtraAttribute : ExtraAttributes)
	{
		AttributeMap.Add(ExtraAttribute.Attribute, ExtraAttribute.Value);
	}

	FixturePatch->SendDMX(AttributeMap);
}

void UDMXPixelMappingFixtureGroupItemComponent::QueueDownsample()
{
	// Queue pixels into the downsample rendering
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();

	if (!ensure(RendererComponent))
	{
		return;
	}

	UTexture* InputTexture = RendererComponent->GetRendererInputTexture();
	if (!ensure(InputTexture))
	{
		return;
	}

	// Store pixel position
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
	if (ColorMode == EDMXColorMode::CM_RGB)
	{
		ExposeFactor = FVector4(AttributeRExpose ? 1.f : 0.f, AttributeGExpose ? 1.f : 0.f, AttributeBExpose ? 1.f : 0.f, 1.f);
		InvertFactor = FIntVector4(AttributeRInvert, AttributeGInvert, AttributeBInvert, 0);
	}
	else if (ColorMode == EDMXColorMode::CM_Monochrome)
	{
		static const FVector4 Expose(1.f, 1.f, 1.f, 1.f);
		static const FVector4 NoExpose(0.f, 0.f, 0.f, 0.f);
		ExposeFactor = FVector4(bMonochromeExpose ? Expose : NoExpose);
		InvertFactor = FIntVector4(bMonochromeInvert, bMonochromeInvert, bMonochromeInvert, 0);
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

FVector2D UDMXPixelMappingFixtureGroupItemComponent::GetSize() const
{
	return FVector2D(SizeX, SizeY);
}

FVector2D UDMXPixelMappingFixtureGroupItemComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingFixtureGroupItemComponent::SetPosition(const FVector2D& InPosition)
{
#if WITH_EDITOR
	if (IsLockInDesigner())
	{
		if (UDMXPixelMappingFixtureGroupComponent* GroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Parent))
		{
			if (!GroupComponent->IsLockInDesigner() &&
				GroupComponent->IsVisibleInDesigner())
			{
				GroupComponent->SetPosition(InPosition);
			}
		}
	}
	else
	{
		Modify();

		SetPositionInBoundaryBox(InPosition);
	}
#else
	SetPositionInBoundaryBox(InPosition);
#endif // WITH_EDITOR
}

void UDMXPixelMappingFixtureGroupItemComponent::SetSize(const FVector2D& InSize)
{
	SizeX = FMath::RoundHalfToZero(InSize.X);
	SizeY = FMath::RoundHalfToZero(InSize.Y);

	SetSizeWithinBoundaryBox(InSize);
}

void UDMXPixelMappingFixtureGroupItemComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent())
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
		Modify();

		PositionX = InPosition.X;
		PositionY = InPosition.Y;

		// 1. Right Border
		float RightBorderPosition = FixtureGroupComponent->SizeX + FixtureGroupComponent->PositionX;
		float PositionXRightBorder = InPosition.X + SizeX;

		// 2. Left Border
		float LeftBorderPosition = FixtureGroupComponent->PositionX;
		float PositionXLeftBorder = InPosition.X;

		if (PositionXRightBorder >= RightBorderPosition)
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
			Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
		}

		RelativePositionX = PositionX - FixtureGroupComponent->GetPosition().X;
		RelativePositionY = PositionY - FixtureGroupComponent->GetPosition().Y;
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

void UDMXPixelMappingFixtureGroupItemComponent::SetPositionFromParent(const FVector2D& InPosition)
{
	PositionX = InPosition.X;
	PositionY = InPosition.Y;

#if WITH_EDITOR
	if (Slot != nullptr)
	{
		Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
	}
#endif // WITH_EDITOR
}

UDMXPixelMappingRendererComponent* UDMXPixelMappingFixtureGroupItemComponent::UDMXPixelMappingFixtureGroupItemComponent::GetRendererComponent() const
{
	return Parent ? Cast<UDMXPixelMappingRendererComponent>(Parent->Parent) : nullptr;;
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
			CachedWidget->SetWidthOverride(SizeX);
			CachedWidget->SetHeightOverride(SizeY);
			CachedLabelBox->SetWidthOverride(SizeX);
		}
#endif // WITH_EDITOR
	}
}

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupItemComponent::AutoMapAttributes()
{
	if (UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.GetFixturePatch())
	{
		const FDMXFixtureMode* ModePtr = FixturePatch->GetActiveMode();
		if (ModePtr)
		{
			Modify();

			const int32 RedIndex = ModePtr->Functions.IndexOfByPredicate([](const FDMXFixtureFunction& Function) {
				return Function.Attribute.Name == "Red";
				});

			if (RedIndex != INDEX_NONE)
			{
				AttributeR.SetFromName("Red");
			}

			const int32 GreenIndex = ModePtr->Functions.IndexOfByPredicate([](const FDMXFixtureFunction& Function) {
				return Function.Attribute.Name == "Green";
				});

			if (GreenIndex != INDEX_NONE)
			{
				AttributeG.SetFromName("Green");
			}

			const int32 BlueIndex = ModePtr->Functions.IndexOfByPredicate([](const FDMXFixtureFunction& Function) {
				return Function.Attribute.Name == "Blue";
				});

			if (BlueIndex != INDEX_NONE)
			{
				AttributeB.SetFromName("Blue");
			}
		}
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
