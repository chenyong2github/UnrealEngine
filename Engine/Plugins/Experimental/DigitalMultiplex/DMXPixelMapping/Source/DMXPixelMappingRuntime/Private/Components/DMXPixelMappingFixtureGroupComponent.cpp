// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingFixtureGroupComponent.h"

#include "IDMXPixelMappingRenderer.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Library/DMXLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingFixtureGroupComponent"

const FVector2D UDMXPixelMappingFixtureGroupComponent::MinGroupSize = FVector2D(1.f);

UDMXPixelMappingFixtureGroupComponent::UDMXPixelMappingFixtureGroupComponent()
{
	SizeX = 500.f;
	SizeY = 500.f;
	PositionXCached = PositionX = 0.f;
	PositionYCached = PositionY = 0.f;

#if WITH_EDITOR
	bEditableEditorColor = true;
#endif
}

void UDMXPixelMappingFixtureGroupComponent::PostLoad()
{
	Super::PostLoad();

	PositionXCached = PositionX;
	PositionYCached = PositionY;

	ResizeOutputTarget(SizeX, SizeY);
}

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, DMXLibrary))
	{
		check(LibraryNameWidget.IsValid());
		LibraryNameWidget->SetText(FText::FromString(GetUserFriendlyName()));
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, bVisibleInDesigner))
	{
		UpdateWidget();

		// Update all children
		ForEachComponentOfClass<UDMXPixelMappingFixtureGroupItemComponent>([](UDMXPixelMappingFixtureGroupItemComponent* InComponent)
		{
			InComponent->UpdateWidget();
		}, false);
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, CellBlendingQuality))
	{
		// Update all children
		ForEachComponentOfClass<UDMXPixelMappingFixtureGroupItemComponent>([&](UDMXPixelMappingFixtureGroupItemComponent* InComponent)
		{
			InComponent->CellBlendingQuality = CellBlendingQuality;
		}, false);
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, EditorColor))
	{
		Brush.TintColor = EditorColor;
	}

	if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, PositionX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, PositionY))
		{
			SetPositionWithChildren();

			// Cache positions
			PositionXCached = PositionX;
			PositionYCached = PositionY;
		}

		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, SizeX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, SizeY))
		{
			SetSizeWithinMinBoundaryBox();
		}
	}
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

void UDMXPixelMappingFixtureGroupComponent::Render()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->Render();
		}
	}, false);
}

void UDMXPixelMappingFixtureGroupComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	ResizeOutputTarget(SizeX, SizeY);
}

#if WITH_EDITOR
FString UDMXPixelMappingFixtureGroupComponent::GetUserFriendlyName() const
{
	if (DMXLibrary)
	{
		return FString::Printf(TEXT("Fixture Group: %s"), *DMXLibrary->GetName());
	}

	return FString("Fixture Group: No Library");
}
#endif // WITH_EDITOR

void UDMXPixelMappingFixtureGroupComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

#if WITH_EDITOR
const FText UDMXPixelMappingFixtureGroupComponent::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<SWidget> UDMXPixelMappingFixtureGroupComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	CachedWidget = 
		SNew(SBox)
		.HeightOverride(SizeX)
		.WidthOverride(SizeY);

	CachedLabelBox =
		SNew(SBox)
		.WidthOverride(SizeY)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::DownOnly)
			[
				SAssignNew(LibraryNameWidget, STextBlock)
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
			.Padding(FMargin(0.0f, -16.0f))
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
void UDMXPixelMappingFixtureGroupComponent::RenderEditorPreviewTexture()
{
	UTextureRenderTarget2D* OutTarget = GetOutputTexture();

	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
	{
		const TSharedPtr<IDMXPixelMappingRenderer>& Renderer = RendererComponent->GetRenderer();
		{
			TArray<FDMXPixelMappingRendererPreviewInfo> GroupRender;
			ForEachChild([this, &GroupRender](UDMXPixelMappingBaseComponent* InComponent) {
				if (UDMXPixelMappingOutputDMXComponent* Component = Cast<UDMXPixelMappingOutputDMXComponent>(InComponent))
				{
					FDMXPixelMappingRendererPreviewInfo Config;
					if (UTextureRenderTarget2D* OutputTeture = Component->GetOutputTexture())
					{
						Config.TextureResource = OutputTeture->Resource;
					}
					Config.TextureSize = Component->GetSize();
					Config.TexturePosition = Component->GetPosition() - GetPosition();

					GroupRender.Add(Config);
				}
			}, true);

			Renderer->RenderPreview_GameThread(OutTarget->Resource, GroupRender);
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupComponent::ToggleHighlightSelection(bool bIsSelected)
{
	if (bIsSelected)
	{
		Brush.TintColor = FLinearColor::Green;

		ForEachComponentOfClass<UDMXPixelMappingFixtureGroupItemComponent>([](UDMXPixelMappingFixtureGroupItemComponent* InComponent)
		{
			InComponent->ToggleHighlightSelection(true);
		}, false);
	}
	else
	{
		Brush.TintColor = FLinearColor::Blue;
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupComponent::UpdateWidget()
{
	// Hide in designer view
	if (bVisibleInDesigner == false)
	{
		CachedWidget->SetContent(SNullWidget::NullWidget);
	}
	else
	{
		CachedWidget->SetContent(SNew(SImage).Image(&Brush));
	}
}
#endif // WITH_EDITOR

UTextureRenderTarget2D* UDMXPixelMappingFixtureGroupComponent::GetOutputTexture()
{
	if (OutputTarget == nullptr)
	{
		const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("OutputTexture"));
		OutputTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
		OutputTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		OutputTarget->InitCustomFormat(10, 10, EPixelFormat::PF_B8G8R8A8, false);
	}

	return OutputTarget;
}

FVector2D UDMXPixelMappingFixtureGroupComponent::GetSize() const
{
	return FVector2D(SizeX, SizeY);
}

FVector2D UDMXPixelMappingFixtureGroupComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingFixtureGroupComponent::SetPosition(const FVector2D& InPosition)
{
	Super::SetPosition(InPosition);
	SetPositionWithChildren();

	PositionXCached = PositionX;
	PositionYCached = PositionY;
}

void UDMXPixelMappingFixtureGroupComponent::SetSize(const FVector2D& InSize)
{
	Super::SetSize(InSize);
	SetSizeWithinMinBoundaryBox();
}

#if WITH_EDITOR
void UDMXPixelMappingFixtureGroupComponent::SetZOrder(int32 NewZOrder)
{
	// Adjust ZOrder on childs relatively. Alike childs always remain ordered above their parent
	int32 DeltaZOrder = NewZOrder - ZOrder;
	for (UDMXPixelMappingBaseComponent* BaseComponent : GetChildren())
	{
		UDMXPixelMappingFixtureGroupItemComponent* PixelComponent = CastChecked<UDMXPixelMappingFixtureGroupItemComponent>(BaseComponent);

		int32 NewChildZOrder = PixelComponent->GetZOrder() + DeltaZOrder;
		PixelComponent->SetZOrder(NewChildZOrder);
	}

	// Adjust ZOrder on self
	ZOrder = NewZOrder;
}
#endif //WITH_EDITOR

void UDMXPixelMappingFixtureGroupComponent::ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY)
{
	UTextureRenderTarget2D* Target = GetOutputTexture();

	check(Target);

	if ((InSizeX > 0 && InSizeY > 0) && (Target->SizeX != InSizeX || Target->SizeY != InSizeY))
	{

		Target->ResizeTarget(InSizeX, InSizeY);
		Target->UpdateResourceImmediate();
	}
}

void UDMXPixelMappingFixtureGroupComponent::SetPositionWithChildren()
{
	ForEachComponentOfClass<UDMXPixelMappingFixtureGroupItemComponent>([this](UDMXPixelMappingFixtureGroupItemComponent* InComponent)
	{
		FVector2D&& ComponentPosition = InComponent->GetPosition();
		FVector2D DeltaParentPosition = FVector2D(PositionX - PositionXCached, PositionY - PositionYCached);
		
		InComponent->SetPositionFromParent(ComponentPosition + DeltaParentPosition);
	}, false);

#if WITH_EDITOR
	Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
#endif // WITH_EDITOR
}

bool UDMXPixelMappingFixtureGroupComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRendererComponent>();
}

void UDMXPixelMappingFixtureGroupComponent::SetSizeWithinMinBoundaryBox()
{
	FVector2D MinSize = FVector2D::ZeroVector;

	ForEachComponentOfClass<UDMXPixelMappingFixtureGroupItemComponent>([this, &MinSize](UDMXPixelMappingFixtureGroupItemComponent* InComponent)
	{
		FVector2D&& ComponentPosition = InComponent->GetPosition();
		FVector2D&& ComponentSize = InComponent->GetSize();

		FVector2D CheckingSize = FVector2D::ZeroVector;
		CheckingSize.X = ComponentPosition.X + ComponentSize.X;
		CheckingSize.Y = ComponentPosition.Y + ComponentSize.Y;

		if (CheckingSize.X > MinSize.X)
		{
			MinSize.X = CheckingSize.X;
		}

		if (CheckingSize.Y > MinSize.Y)
		{
			MinSize.Y = CheckingSize.Y;
		}
	}, false);

	FVector2D CurrentSize = FVector2D::ZeroVector;
	CurrentSize.X = PositionX + SizeX;
	CurrentSize.Y = PositionY + SizeY;

	if (CurrentSize.X < MinSize.X)
	{
		SizeX = MinSize.X - PositionX;
	}

	if (SizeX < MinGroupSize.X)
	{
		SizeX = MinGroupSize.X;
	}

	if (CurrentSize.Y < MinSize.Y)
	{
		SizeY = MinSize.Y - PositionY;
	}

	if (SizeY < MinGroupSize.Y)
	{
		SizeY = MinGroupSize.Y;
	}

#if WITH_EDITOR
	CachedWidget->SetWidthOverride(SizeX);
	CachedWidget->SetHeightOverride(SizeY);
	CachedLabelBox->SetWidthOverride(SizeX);
#endif // WITH_EDITOR

	ResizeOutputTarget(SizeX, SizeY);
}

#undef LOCTEXT_NAMESPACE
