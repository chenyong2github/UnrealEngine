// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingScreenComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "DMXPixelMappingRuntimeCommon.h"
#include "IDMXPixelMappingRenderer.h"
#include "DMXPixelMappingUtils.h"
#include "DMXPixelMappingTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolConstants.h"
#include "Library/DMXEntityFixtureType.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"

#if WITH_EDITOR
#include "SDMXPixelMappingEditorWidgets.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "DMXPixelMappingScreenComponent"

const FVector2D UDMXPixelMappingScreenComponent::MixGridSize = FVector2D(1.f);

#if WITH_EDITORONLY_DATA
const uint32 UDMXPixelMappingScreenComponent::MaxGridUICells = 40 * 40;
#endif

UDMXPixelMappingScreenComponent::UDMXPixelMappingScreenComponent()
{
	SizeX = 100;
	SizeY = 100;

	NumXCells = 10;
	NumYCells = 10;

	PixelFormat = EDMXCellFormat::PF_RGB;
	bIgnoreAlphaChannel = true;

	RemoteUniverse = 1;
	StartAddress = 1;
	PixelIntensity = 1;
	AlphaIntensity = 1;
	Distribution = EDMXPixelMappingDistribution::TopLeftToRight;

#if WITH_EDITOR
	bIsUpdateWidgetRequested = false;
	Slot = nullptr;

	bEditableEditorColor = true;
#endif // WITH_EDITOR
}

void UDMXPixelMappingScreenComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	ResizeOutputTarget(NumXCells, NumYCells);
}

void UDMXPixelMappingScreenComponent::Tick(float DeltaTime)
{
#if WITH_EDITOR
	if (bIsUpdateWidgetRequested)
	{
		UpdateWidget();

		bIsUpdateWidgetRequested = false;
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
void UDMXPixelMappingScreenComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumXCells) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumYCells))
	{
		ResizeOutputTarget(NumXCells, NumYCells);
	}
	
	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumXCells) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumYCells) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, RemoteUniverse) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, StartAddress) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, Distribution) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PixelFormat) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, bShowAddresses) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, bShowUniverse) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, bVisibleInDesigner)
		)
	{
		bIsUpdateWidgetRequested = true;
	}
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, EditorColor))
	{
		Brush.TintColor = EditorColor;
	}

	if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{

		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PositionX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PositionY))
		{
			Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
		}

		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, SizeX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, SizeY))
		{
			CachedWidget->SetWidthOverride(SizeX);
			CachedWidget->SetHeightOverride(SizeY);
			CachedLabelBox->SetWidthOverride(SizeX);
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
const FText UDMXPixelMappingScreenComponent::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<SWidget> UDMXPixelMappingScreenComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
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
void UDMXPixelMappingScreenComponent::ToggleHighlightSelection(bool bIsSelected)
{
	Super::ToggleHighlightSelection(bIsSelected);

	Brush.TintColor = GetEditorColor(bIsSelected);
}
#endif // WITH_EDITOR

#if WITH_EDITOR
TSharedRef<SWidget> UDMXPixelMappingScreenComponent::ConstructGrid()
{
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
				SNew(STextBlock)
				.Text(FText::FromString(GetUserFriendlyName()))
			]
		];

	if ((NumXCells * NumYCells) > MaxGridUICells)
	{
		return SNew(SOverlay)
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
				SNew(SDMXPixelMappingSimpleScreenLayout)
				.NumXCells(NumXCells)
				.NumYCells(NumYCells)
				.Brush(&Brush)
				.RemoteUniverse(RemoteUniverse)
				.StartAddress(StartAddress)
			];
	}
	else
	{
		return SNew(SOverlay)
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
				SNew(SDMXPixelMappingScreenLayout)
				.NumXCells(NumXCells)
				.NumYCells(NumYCells)
				.Distribution(Distribution)
				.PixelFormat(PixelFormat)
				.Brush(&Brush)
				.RemoteUniverse(RemoteUniverse)
				.StartAddress(StartAddress)
				.bShowAddresses(bShowAddresses)
				.bShowUniverse(bShowUniverse)
			];			
		
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXPixelMappingScreenComponent::UpdateWidget()
{
	// Hide in designer view
	if (bVisibleInDesigner == false)
	{
		CachedWidget->SetContent(SNullWidget::NullWidget);
	}
	else
	{
		CachedWidget->SetContent(ConstructGrid());
	}
}
#endif // WITH_EDITOR

const FName& UDMXPixelMappingScreenComponent::GetNamePrefix()
{
	static FName NamePrefix = TEXT("DMX Screen");
	return NamePrefix;
}

void UDMXPixelMappingScreenComponent::PostLoad()
{
	Super::PostLoad();

	ResizeOutputTarget(NumXCells, NumYCells);
}

void UDMXPixelMappingScreenComponent::AddColorToSendBuffer(const FColor& InColor, TArray<uint8>& OutDMXSendBuffer)
{
	if (PixelFormat == EDMXCellFormat::PF_R)
	{
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXCellFormat::PF_G)
	{
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXCellFormat::PF_B)
	{
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXCellFormat::PF_RG)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXCellFormat::PF_RB)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXCellFormat::PF_GB)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXCellFormat::PF_GR)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXCellFormat::PF_BR)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXCellFormat::PF_BG)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXCellFormat::PF_RGB)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXCellFormat::PF_BRG)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXCellFormat::PF_GRB)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXCellFormat::PF_GBR)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXCellFormat::PF_RGBA)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(bIgnoreAlphaChannel ? 0 : InColor.A);
	}
	else if (PixelFormat == EDMXCellFormat::PF_GBRA)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(bIgnoreAlphaChannel ? 0 : InColor.A);
	}
	else if (PixelFormat == EDMXCellFormat::PF_BRGA)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(bIgnoreAlphaChannel ? 0 : InColor.A);
	}
	else if (PixelFormat == EDMXCellFormat::PF_GRBA)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(bIgnoreAlphaChannel ? 0 : InColor.A);
	}
}

void UDMXPixelMappingScreenComponent::ResetDMX()
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
void UDMXPixelMappingScreenComponent::SendDMX()
{
	if (RemoteUniverse < 0)
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("RemoteUniverse < 0"));
		return;
	}

	if (!ProtocolName)
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("ProtocolName is not valid"));
		return;
	}

	TArray<FColor> SortedList;
	GetSurfaceBuffer([this, &SortedList](const TArray<FColor>& InSurfaceBuffer, const FIntRect& InSurfaceRect)
	{
		int32 NumXPanelsLocal = InSurfaceRect.Width();
		int32 NumYPanelsLocal = InSurfaceRect.Height();
		FDMXPixelMappingUtils::TextureDistributionSort<FColor>(Distribution, NumXPanelsLocal, NumYPanelsLocal, InSurfaceBuffer, SortedList);
	});

	IDMXProtocolPtr Protocol = ProtocolName.GetProtocol();
	if (Protocol.IsValid())
	{
		// Sending only if there enough space at least for one pixel
		if (!FDMXPixelMappingUtils::CanFitCellIntoChannels(PixelFormat, StartAddress))
		{
			return;
		}

		// Prepare Universes for send
		TArray<uint8> SendBuffer;
		for (const FColor& Color : SortedList)
		{
			FColor ColorWithAppliedIntensity = Color;
			const float MaxValue = 255;
			ColorWithAppliedIntensity.R = static_cast<uint8>(FMath::Min(ColorWithAppliedIntensity.R * PixelIntensity, MaxValue));
			ColorWithAppliedIntensity.G = static_cast<uint8>(FMath::Min(ColorWithAppliedIntensity.G * PixelIntensity, MaxValue));
			ColorWithAppliedIntensity.B = static_cast<uint8>(FMath::Min(ColorWithAppliedIntensity.B * PixelIntensity, MaxValue));
			ColorWithAppliedIntensity.A = static_cast<uint8>(FMath::Min(ColorWithAppliedIntensity.A * AlphaIntensity, MaxValue));;
			AddColorToSendBuffer(ColorWithAppliedIntensity, SendBuffer);
		}

		// Start sending
		uint32 UniverseMaxChannels = FDMXPixelMappingUtils::GetUniverseMaxChannels(PixelFormat, StartAddress);
		uint32 SendDMXIndex = StartAddress;
		int32 UniverseToSend = RemoteUniverse;
		int32 SendBufferNum = SendBuffer.Num();
		IDMXFragmentMap DMXFragmentMapToSend;
		for (int32 FragmentMapIndex = 0; FragmentMapIndex < SendBufferNum; FragmentMapIndex++)
		{			
			// ready to send here
			if (SendDMXIndex > UniverseMaxChannels)
			{
				Protocol->SendDMXFragmentCreate(UniverseToSend, DMXFragmentMapToSend);

				// Now reset
				DMXFragmentMapToSend.Empty();
				SendDMXIndex = StartAddress;
				UniverseToSend++;
			}

			// should be channels from 1...UniverseMaxChannels
			DMXFragmentMapToSend.Add(SendDMXIndex, SendBuffer[FragmentMapIndex]);
			if (SendDMXIndex > UniverseMaxChannels || SendDMXIndex < 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("WrongIndex FragmentMapIndex %d, SendDMXIndex %d"), FragmentMapIndex, SendDMXIndex);
			}

			// send dmx if next iteration is the last one
			if ((SendBufferNum > FragmentMapIndex + 1) == false)
			{
				Protocol->SendDMXFragmentCreate(UniverseToSend, DMXFragmentMapToSend);
				break;
			}

			SendDMXIndex++;
		}
	}
}

void UDMXPixelMappingScreenComponent::Render()
{
	RendererOutputTexture();
}

void UDMXPixelMappingScreenComponent::RenderAndSendDMX()
{
	Render();
	SendDMX();
}

FVector2D UDMXPixelMappingScreenComponent::GetSize() const
{
	return FVector2D(SizeX, SizeY);
}

void UDMXPixelMappingScreenComponent::SetSize(const FVector2D& InSize)
{
	// SetSize from parent rounded the values for us
	Super::SetSize(InSize);

	SetSizeInternal(InSize);
}

FVector2D UDMXPixelMappingScreenComponent::GetPosition()
{
	return FVector2D(PositionX, PositionY);
}

void UDMXPixelMappingScreenComponent::SetPosition(const FVector2D& InPosition)
{
	// SetPosition from parent rounded the values for us
	Super::SetPosition(InPosition);

#if WITH_EDITOR
	if (Slot != nullptr)
	{
		Slot->Offset(FMargin(PositionX, PositionY, 0.f, 0.f));
	}
#endif // WITH_EDITOR
}

UTextureRenderTarget2D* UDMXPixelMappingScreenComponent::GetOutputTexture()
{
	if (OutputTarget == nullptr)
	{
		const FName TargetName = MakeUniqueObjectName(this, UTextureRenderTarget2D::StaticClass(), TEXT("DstTarget"));
		OutputTarget = NewObject<UTextureRenderTarget2D>(this, TargetName);
		OutputTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		OutputTarget->InitCustomFormat(10, 10, EPixelFormat::PF_B8G8R8A8, false);
	}

	return OutputTarget;
}

void UDMXPixelMappingScreenComponent::ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY)
{
	UTextureRenderTarget2D* Target = GetOutputTexture();

	if ((InSizeX > 0 && InSizeY > 0) && (Target->SizeX != InSizeX || Target->SizeY != InSizeY))
	{
		check(Target);

		Target->ResizeTarget(InSizeX, InSizeY);
		Target->UpdateResourceImmediate();
	}
}

void UDMXPixelMappingScreenComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
	{
		RendererComponent->RendererInputTexture();
	}

	RenderAndSendDMX();
}

void UDMXPixelMappingScreenComponent::SetSizeInternal(const FVector2D& InSize)
{
	if (InSize.X <= MixGridSize.X)
	{
		SizeX = MixGridSize.X;
	}
	else
	{
		SizeX = InSize.X;
	}

	if (InSize.Y <= MixGridSize.Y)
	{
		SizeY = MixGridSize.Y;
	}
	else
	{
		SizeY = InSize.Y;
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

void UDMXPixelMappingScreenComponent::RendererOutputTexture()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(Parent))
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
			const FVector2D UVCellSize(UVSize.X / NumXCells / 2.f, UVSize.Y / NumYCells / 2.f);

			const FIntPoint TargetSize(OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY());
			const FIntPoint TextureSize(1, 1);

			const bool bStaticCalculateUV = false;
			
			Renderer->DownsampleRender_GameThread(
				Texture->Resource,
				OutputTarget->Resource,
				OutputTarget->GameThread_GetRenderTargetResource(),
				FVector4(1.f, 1.f, 1.f, 1.f),
				FIntVector4(0),
				Position, // X, Y Position in screen pixels of the top left corner of the quad
				Size, // SizeX, SizeY	Size in screen pixels of the quad
				UV, // U, V	Position in texels of the top left corner of the quad's UV's
				UVSize, // SizeU, SizeV	Size in texels of the quad's UV's,
				UVCellSize,
				TargetSize, // TargetSizeX, TargetSizeY Size in screen pixels of the target surface
				TextureSize, // TextureSize Size in texels of the source texture
				CellBlendingQuality,
				bStaticCalculateUV,
				[=](TArray<FColor>& InSurfaceBuffer, FIntRect& InRect) { SetSurfaceBuffer(InSurfaceBuffer, InRect); }
			);
		}
	}
}

bool UDMXPixelMappingScreenComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRendererComponent>();
}

#undef LOCTEXT_NAMESPACE
