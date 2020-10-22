// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingMatrixComponent.h"

#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "DMXPixelMappingRuntimeCommon.h"
#include "IDMXPixelMappingRenderer.h"
#include "DMXPixelMappingUtils.h"
#include "DMXPixelMappingTypes.h"
#include "DMXSubsystem.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolConstants.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMapping.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingMatrixComponent"

const FVector2D UDMXPixelMappingMatrixComponent::MinSize = FVector2D(1.f);
const FVector2D UDMXPixelMappingMatrixComponent::DefaultSize = FVector2D(500.f);

UDMXPixelMappingMatrixComponent::UDMXPixelMappingMatrixComponent()
{
	SizeX = SizeY = DefaultSize.X;
	PositionXCached = PositionX = 0.f;
	PositionYCached = PositionY = 0.f;

	SetNumCells(FIntPoint(1));

	ColorMode = EDMXColorMode::CM_RGB;
	AttributeRExpose = AttributeGExpose = AttributeBExpose = true;

	bMonochromeExpose = true;

	Distribution = EDMXPixelMappingDistribution::TopLeftToRight;

#if WITH_EDITOR
	bEditableEditorColor = true;
	bHighlighted = false;
	ZOrder = 1;
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixComponent::PostLoad()
{
	Super::PostLoad();

	PositionXCached = PositionX;
	PositionYCached = PositionY;

	ResizeOutputTarget(SizeX, SizeY);
}

void UDMXPixelMappingMatrixComponent::LogInvalidProperties()
{
	UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();
	if (FixturePatch && FixturePatch->IsValidLowLevel())
	{
		UDMXEntityFixtureType* FixtureType = FixturePatch->ParentFixtureTypeTemplate;
		if (FixtureType)
		{
			if (!FixturePatch->CanReadActiveMode())
			{
				UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("%s has no valid Active Mode set. %s will not receive DMX."), *FixturePatch->GetDisplayName(), *GetName());
			}
			else
			{
				const FDMXFixtureMode& Mode = FixtureType->Modes[FixturePatch->ActiveMode];
				FIntPoint NumCellsInActiveMode = FIntPoint(Mode.FixtureMatrixConfig.XCells, Mode.FixtureMatrixConfig.YCells);
				if (NumCellsInActiveMode != NumCells)
				{
					UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("Number of cells in %s no longer matches %s. %s will not function properly."), *GetName(), *FixtureType->GetDisplayName(), *GetName());
				}
			}
		}
		else
		{
			UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("%s has no valid Fixture Type set. %s will not receive DMX."), *FixturePatch->GetDisplayName(), *GetName());
		}
	}
	else
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("%s has no valid Fixture Patch set."), *GetName());
	}
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Note, property changes of fixture patch are listened for in tick

	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, FixturePatchMatrixRef))
	{
		check(PatchNameWidget.IsValid());
		PatchNameWidget->SetText(FText::FromString(GetUserFriendlyName()));

		if (UDMXPixelMapping* PixelMapping = GetPixelMapping())
		{
			UpdateNumCells();
			PixelMapping->OnEditorRebuildChildrenComponentsDelegate.ExecuteIfBound(this);
		}
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FDMXEntityReference, DMXLibrary))
	{
		if (UDMXPixelMapping* PixelMapping = GetPixelMapping())
		{
			UpdateNumCells();
			PixelMapping->OnEditorRebuildChildrenComponentsDelegate.ExecuteIfBound(this);
		}
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, bVisibleInDesigner))
	{
		UpdateWidget();

		// Update all children
		ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([](UDMXPixelMappingMatrixCellComponent* InComponent)
		{
			InComponent->UpdateWidget();
		}, false);
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, CellBlendingQuality))
	{
		// Update all children
		ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([&](UDMXPixelMappingMatrixCellComponent* InComponent)
		{
			InComponent->CellBlendingQuality = CellBlendingQuality;
		}, false);
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, EditorColor))
	{
		Brush.TintColor = EditorColor;

		ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([this](UDMXPixelMappingMatrixCellComponent* InComponent)
			{
				if (InComponent->EditorColor == PreviousEditorColor)
				{
					InComponent->EditorColor = EditorColor;
				}
			}, true);

		PreviousEditorColor = EditorColor;
	}	
	
	if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, PositionX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, PositionY))
		{
			SetPositionWithChildren();

			// Cache positions
			PositionXCached = PositionX;
			PositionYCached = PositionY;
		}

		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, SizeX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, SizeY))
		{
			SetSizeInternal(FVector2D(SizeX, SizeY));
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::RenderEditorPreviewTexture()
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
			}, false);

			Renderer->RenderPreview_GameThread(OutTarget->Resource, GroupRender);
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
const FText UDMXPixelMappingMatrixComponent::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<SWidget> UDMXPixelMappingMatrixComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
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
void UDMXPixelMappingMatrixComponent::ToggleHighlightSelection(bool bIsSelected)
{
	Super::ToggleHighlightSelection(bIsSelected);

	Brush.TintColor = GetEditorColor(bIsSelected);

	ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([bIsSelected](UDMXPixelMappingMatrixCellComponent* InComponent)
	{
		InComponent->ToggleHighlightSelection(bIsSelected);
	}, true);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::UpdateWidget()
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
}

void UDMXPixelMappingMatrixComponent::SendDMX()
{

	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->SendDMX();
		}
	}, false);

	// Send Extra Attributes
	if (UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure())
	{
		UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();
		

		TMap<FDMXAttributeName, int32> AttributeMap;
		for (const FDMXPixelMappingExtraAttribute& ExtraAttribute : ExtraAttributes)
		{
			AttributeMap.Add(ExtraAttribute.Attribute, ExtraAttribute.Value);
		}

		EDMXSendResult OutResult;
		DMXSubsystem->SendDMX(FixturePatch, AttributeMap, OutResult);
	}
}

void UDMXPixelMappingMatrixComponent::Render()
{
	ForEachChild([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (UDMXPixelMappingOutputComponent* Component = Cast<UDMXPixelMappingOutputComponent>(InComponent))
		{
			Component->Render();
		}
	}, false);
}

void UDMXPixelMappingMatrixComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

void UDMXPixelMappingMatrixComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	ResizeOutputTarget(SizeX, SizeY);

#if WITH_EDITOR
	AutoMapAttributes();
#endif // WITH_EDITOR
}

#if WITH_EDITOR
FString UDMXPixelMappingMatrixComponent::GetUserFriendlyName() const
{
	if (UDMXEntityFixturePatch* Patch = FixturePatchMatrixRef.GetFixturePatch())
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

			UDMXLibrary* DMXLibrary = FixturePatchMatrixRef.DMXLibrary;
			UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();

			if (DMXLibrary != nullptr && FixturePatch != nullptr)
			{
				if (UDMXEntityFixtureType * ParentFixtureType = FixturePatch->ParentFixtureTypeTemplate)
				{
					if (!FixturePatch->CanReadActiveMode() && GetChildrenCount() > 0)
					{
						bShouldRebuildChildren = true;
					}
					else if (!ParentFixtureType->bFixtureMatrixEnabled && GetChildrenCount() > 0)
					{
						bShouldRebuildChildren = true;
					}
					else
					{
						int32 ActiveMode = FixturePatch->ActiveMode;

						const FDMXFixtureMode& FixtureMode = ParentFixtureType->Modes[ActiveMode];
						const FDMXFixtureMatrix& FixtureMatrixConfig = FixtureMode.FixtureMatrixConfig;

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

			if (bShouldRebuildChildren)
			{
				UpdateNumCells();

				LogInvalidProperties();
				
				PixelMapping->OnEditorRebuildChildrenComponentsDelegate.Execute(this);
			}
		}
	}
#endif // WITH_EDITOR
}

UTextureRenderTarget2D* UDMXPixelMappingMatrixComponent::GetOutputTexture()
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

FVector2D UDMXPixelMappingMatrixComponent::GetSize() const
{
	return FVector2D(SizeX, SizeY);
}

FVector2D UDMXPixelMappingMatrixComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingMatrixComponent::SetSize(const FVector2D& InSize)
{
	Super::SetSize(InSize);
	SetSizeInternal(InSize);
}

void UDMXPixelMappingMatrixComponent::SetPosition(const FVector2D& InPosition)
{
	Super::SetPosition(InPosition);
	SetPositionWithChildren();

	PositionXCached = PositionX;
	PositionYCached = PositionY;
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::SetZOrder(int32 NewZOrder)
{
	// Adjust ZOrder on childs relatively. Alike childs always remain ordered above their parent
	int32 DeltaZOrder = NewZOrder - ZOrder;
	for (UDMXPixelMappingBaseComponent* BaseComponent : GetChildren())
	{
		UDMXPixelMappingMatrixCellComponent* PixelComponent = CastChecked<UDMXPixelMappingMatrixCellComponent>(BaseComponent);

		int32 NewChildZOrder = PixelComponent->GetZOrder() + DeltaZOrder;
		PixelComponent->SetZOrder(NewChildZOrder);
	}

	// Adjust ZOrder on self
	ZOrder = NewZOrder;
}
#endif //WITH_EDITOR

void UDMXPixelMappingMatrixComponent::SetSizeInternal(const FVector2D& InSize)
{
	if (InSize.X < MinSize.X)
	{
		SizeX = MinSize.X;
	}

	if (InSize.Y < MinSize.Y)
	{
		SizeY = MinSize.Y;
	}

	/* Pixel size needs to round since it may not be possible to get consistent pixel size throughout the matrix given
	 * the total size and number of desired pixels. Without this there may be artifacts in the output. */
	PixelSize = FVector2D(FMath::RoundHalfToZero(SizeX / (float)NumCells.X), FMath::RoundHalfToZero(SizeY / (float)NumCells.Y));

	ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([this](UDMXPixelMappingMatrixCellComponent* InComponent)
	{
		InComponent->SetSizeFromParent(PixelSize);
		InComponent->SetPositionFromParent(FVector2D(PositionX, PositionY) + FVector2D(PixelSize * InComponent->GetPixelCoordinate()));
	}, false);

#if WITH_EDITOR
	// Calculate total pixel size. This prevents unused space being rendered due to the rounded pixel size above.
	const uint32 TotalPixelSizeX = PixelSize.X * NumCells.X;
	const uint32 TotalPixelSizeY = PixelSize.Y * NumCells.Y;
	CachedWidget->SetWidthOverride(TotalPixelSizeX);
	CachedWidget->SetHeightOverride(TotalPixelSizeY);
	CachedLabelBox->SetWidthOverride(TotalPixelSizeX);

	ResizeOutputTarget(TotalPixelSizeX, TotalPixelSizeY);
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixComponent::ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY)
{
	UTextureRenderTarget2D* Target = GetOutputTexture();
	check(Target);
	
	if ((InSizeX > 0 && InSizeY > 0) && (Target->SizeX != InSizeX || Target->SizeY != InSizeY))
	{
		Target->ResizeTarget(InSizeX, InSizeY);
		Target->UpdateResourceImmediate();
	}
}

void UDMXPixelMappingMatrixComponent::SetPositionWithChildren()
{
	ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([this](UDMXPixelMappingMatrixCellComponent* InComponent)
	{
		FVector2D&& ComponentPosition = InComponent->GetPosition();
		FVector2D DeltaParentPosition = FVector2D(PositionX - PositionXCached, PositionY - PositionYCached);

		InComponent->SetPositionFromParent(ComponentPosition + DeltaParentPosition);
	}, false);

#if WITH_EDITOR
	Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixComponent::SetSizeWithinMaxBoundaryBox()
{
	FVector2D MaxSize = FVector2D::ZeroVector;

	ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([this, &MaxSize](UDMXPixelMappingMatrixCellComponent* InComponent)
	{
		FVector2D ComponentPosition = InComponent->GetPosition();
		FVector2D ComponentSize = InComponent->GetSize();

		FVector2D CheckingSize;
		CheckingSize.X = ComponentPosition.X + ComponentSize.X;
		CheckingSize.Y = ComponentPosition.Y + ComponentSize.Y;

		if (MaxSize.X < CheckingSize.X)
		{
			MaxSize.X = CheckingSize.X;
		}

		if (MaxSize.Y < CheckingSize.Y)
		{
			MaxSize.Y = CheckingSize.Y;
		}
	}, true);

	SizeX = MaxSize.X - PositionX;
	SizeY = MaxSize.Y - PositionY;

	if (SizeX < MinSize.X)
	{
		SizeX = MinSize.X;
	}

	if (SizeY < MinSize.Y)
	{
		SizeY = MinSize.Y;
	}

#if WITH_EDITOR
	CachedWidget->SetWidthOverride(SizeX);
	CachedWidget->SetHeightOverride(SizeY);
	CachedLabelBox->SetWidthOverride(SizeX);

	ResizeOutputTarget(SizeX, SizeY);
#endif // WITH_EDITOR
}

void UDMXPixelMappingMatrixComponent::SetPositionBasedOnRelativePixel(UDMXPixelMappingMatrixCellComponent* InMatrixPixelComponent, FVector2D InDelta)
{
	PositionX += InDelta.X;
	PositionY += InDelta.Y;

	ForEachComponentOfClass<UDMXPixelMappingMatrixCellComponent>([this, InMatrixPixelComponent](UDMXPixelMappingMatrixCellComponent* InComponent)
	{
		if (InMatrixPixelComponent != InComponent)
		{
			FVector2D&& ComponentPosition = InComponent->GetPosition();
			FVector2D DeltaParentPosition = FVector2D(PositionX - PositionXCached, PositionY - PositionYCached);

			InComponent->SetPositionFromParent(ComponentPosition + DeltaParentPosition);
		}
	}, false);

#if WITH_EDITOR
	Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
#endif // WITH_EDITOR

	PositionXCached = PositionX;
	PositionYCached = PositionY;
}

void UDMXPixelMappingMatrixComponent::SetNumCells(const FIntPoint& InNumCells)
{
	NumCells = InNumCells;
	PixelSize = FVector2D(SizeX / NumCells.X, SizeY / NumCells.Y);
}

void UDMXPixelMappingMatrixComponent::SetChildSizeAndPosition(UDMXPixelMappingMatrixCellComponent* InMatrixPixelComponent, const FIntPoint& InPixelCoordinate)
{
	InMatrixPixelComponent->SetPixelCoordinate(InPixelCoordinate);
	InMatrixPixelComponent->SetSizeFromParent(PixelSize);
	InMatrixPixelComponent->SetPositionFromParent(FVector2D(PositionX, PositionY) + FVector2D(PixelSize * InPixelCoordinate));
	InMatrixPixelComponent->FixturePatchMatrixRef = FixturePatchMatrixRef;
}

bool UDMXPixelMappingMatrixComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRendererComponent>();
}

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::UpdateNumCells()
{
	NumCells = 0;

	UDMXLibrary* DMXLibrary = FixturePatchMatrixRef.DMXLibrary;
	UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch();

	if (DMXLibrary != nullptr && FixturePatch != nullptr)
	{
		if (UDMXEntityFixtureType* ParentFixtureType = FixturePatch->ParentFixtureTypeTemplate)
		{
			if (FixturePatch->CanReadActiveMode() && ParentFixtureType->bFixtureMatrixEnabled)
			{
				int32 ActiveMode = FixturePatch->ActiveMode;

				const FDMXFixtureMode& FixtureMode = ParentFixtureType->Modes[ActiveMode];
				const FDMXFixtureMatrix& FixtureMatrixConfig = FixtureMode.FixtureMatrixConfig;

				NumCells = FIntPoint(FixtureMatrixConfig.XCells, FixtureMatrixConfig.YCells);
			}
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingMatrixComponent::AutoMapAttributes()
{
	if (UDMXEntityFixturePatch* FixturePatch = FixturePatchMatrixRef.GetFixturePatch())
	{
		if (FixturePatch->CanReadActiveMode())
		{
			if (UDMXEntityFixtureType* FixtureType = FixturePatch->ParentFixtureTypeTemplate)
			{
				Modify();

				const FDMXFixtureMode& Mode = FixtureType->Modes[FixturePatch->ActiveMode];

				int32 RedIndex = Mode.FixtureMatrixConfig.CellAttributes.IndexOfByPredicate([](const FDMXFixtureCellAttribute& Attribute) {
					return Attribute.Attribute.Name == "Red";
					});

				if (RedIndex != INDEX_NONE)
				{
					AttributeR.SetFromName("Red");
				}
				else
				{
					AttributeR.SetToNone();
				}


				int32 GreenIndex = Mode.FixtureMatrixConfig.CellAttributes.IndexOfByPredicate([](const FDMXFixtureCellAttribute& Attribute) {
					return Attribute.Attribute.Name == "Green";
					});

				if (GreenIndex != INDEX_NONE)
				{
					AttributeG.SetFromName("Green");
				}
				else
				{
					AttributeG.SetToNone();
				}


				int32 BlueIndex = Mode.FixtureMatrixConfig.CellAttributes.IndexOfByPredicate([](const FDMXFixtureCellAttribute& Attribute) {
					return Attribute.Attribute.Name == "Blue";
					});

				if (BlueIndex != INDEX_NONE)
				{
					AttributeB.SetFromName("Blue");
				}
				else
				{
					AttributeB.SetToNone();
				}
			}
		}
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
