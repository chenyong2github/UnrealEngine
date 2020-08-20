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

	NumXPanels = 10;
	NumYPanels = 10;

	PixelFormat = EDMXPixelFormat::PF_RGB;
	bIngoneAlfaChannel = true;

	RemoteUniverse = 1;
	StartAddress = 1;
	PixelIntensity = 1;
	AlphaIntensity = 1;
	Distribution = EDMXPixelsDistribution::TopLeftToRight;

#if WITH_EDITOR
	bIsUpdateWidgetRequested = false;
	Slot = nullptr;
#endif // WITH_EDITOR
}

void UDMXPixelMappingScreenComponent::PostParentAssigned()
{
	Super::PostParentAssigned();

	ResizeOutputTarget(NumXPanels, NumYPanels);
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

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumXPanels) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumYPanels))
	{
		ResizeOutputTarget(NumXPanels, NumYPanels);
	}

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PositionX) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PositionY))
	{
		Slot->Position(FVector2D(PositionX, PositionY));
	}

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, SizeX) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, SizeY))
	{
		Slot->Size(FVector2D(SizeX, SizeY));
	}

	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumXPanels) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumYPanels) ||
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
}

const FText UDMXPixelMappingScreenComponent::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}

TSharedRef<SWidget> UDMXPixelMappingScreenComponent::BuildSlot(TSharedRef<SCanvas> InCanvas)
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

void UDMXPixelMappingScreenComponent::ToggleHighlightSelection(bool bIsSelected)
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

TSharedRef<SWidget> UDMXPixelMappingScreenComponent::ConstructGrid()
{
	if ((NumXPanels * NumYPanels) > MaxGridUICells)
	{
		return SNew(SOverlay)
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
				SNew(SDMXPixelMappingSimpleScreenLayout)
				.NumXPanels(NumXPanels)
				.NumYPanels(NumYPanels)
				.Brush(&Brush)
				.RemoteUniverse(RemoteUniverse)
				.StartAddress(StartAddress)
			];
	}
	else
	{
		return SNew(SOverlay)
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
				SNew(SDMXPixelMappingScreenLayout)
				.NumXPanels(NumXPanels)
				.NumYPanels(NumYPanels)
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

	ResizeOutputTarget(NumXPanels, NumYPanels);
}

void UDMXPixelMappingScreenComponent::AddColorToSendBuffer(const FColor& InColor, TArray<uint8>& OutDMXSendBuffer)
{
	if (PixelFormat == EDMXPixelFormat::PF_R)
	{
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_G)
	{
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_B)
	{
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_RG)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_RB)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_GB)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_GR)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_BR)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_BG)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_RGB)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_BRG)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_GRB)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.B);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_GBR)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_RGBA)
	{
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(bIngoneAlfaChannel ? 0 : InColor.A);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_GBRA)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(bIngoneAlfaChannel ? 0 : InColor.A);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_BRGA)
	{
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(bIngoneAlfaChannel ? 0 : InColor.A);
	}
	else if (PixelFormat == EDMXPixelFormat::PF_GRBA)
	{
		OutDMXSendBuffer.Add(InColor.G);
		OutDMXSendBuffer.Add(InColor.R);
		OutDMXSendBuffer.Add(InColor.B);
		OutDMXSendBuffer.Add(bIngoneAlfaChannel ? 0 : InColor.A);
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
		if (!FDMXPixelMappingUtils::CanFitPixelIntoChannels(PixelFormat, StartAddress))
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
			if (SendDMXIndex > UniverseMaxChannels ||
				(SendBufferNum > FragmentMapIndex + 1) == false // send dmx if next iteration is the last one
				)
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

FVector2D UDMXPixelMappingScreenComponent::GetSize()
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
		Slot->Position(FVector2D(PositionX, PositionY));
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
		Slot->Size(FVector2D(SizeX, SizeY));
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
			uint32 TexureSizeX = Texture->Resource->GetSizeX();
			uint32 TexureSizeY = Texture->Resource->GetSizeY();

			GetOutputTexture();

			Renderer->DownsampleRender_GameThread(
				Texture->Resource,
				OutputTarget->Resource,
				OutputTarget->GameThread_GetRenderTargetResource(),
				FVector4(1.f, 1.f, 1.f, 1.f),
				FIntVector4(0),
				0, 0, // X, Y Position in screen pixels of the top left corner of the quad
				OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY(), // SizeX, SizeY	Size in screen pixels of the quad
				PositionX / TexureSizeX, PositionY / TexureSizeY, // U, V	Position in texels of the top left corner of the quad's UV's
				SizeX / TexureSizeX, SizeY / TexureSizeY, // SizeU, SizeV	Size in texels of the quad's UV's
				FIntPoint(OutputTarget->Resource->GetSizeX(), OutputTarget->Resource->GetSizeY()), // TargetSizeX, TargetSizeY Size in screen pixels of the target surface
				FIntPoint(1, 1), // TextureSize Size in texels of the source texture
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
