// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingScreenComponent.h"

#include "DMXPixelMappingRuntimeCommon.h"
#include "DMXPixelMappingTypes.h"
#include "DMXPixelMappingUtils.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXPortManager.h"

#include "Engine/Texture.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_EDITOR
#include "SDMXPixelMappingEditorWidgets.h"
#endif // WITH_EDITOR


DECLARE_CYCLE_STAT(TEXT("Send Screen"), STAT_DMXPixelMaping_SendScreen, STATGROUP_DMXPIXELMAPPING);

#define LOCTEXT_NAMESPACE "DMXPixelMappingScreenComponent"

const FVector2D UDMXPixelMappingScreenComponent::MinGridSize = FVector2D(1.f);

#if WITH_EDITORONLY_DATA
const uint32 UDMXPixelMappingScreenComponent::MaxGridUICells = 40 * 40;
#endif

UDMXPixelMappingScreenComponent::UDMXPixelMappingScreenComponent()
	: bSendToAllOutputPorts(true)
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
	else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, OutputPortReferences))
	{
		// Rebuild the set of ports
		OutputPorts.Reset();
		for (const FDMXOutputPortReference& OutputPortReference : OutputPortReferences)
		{
			const FDMXOutputPortSharedRef* OutputPortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([&OutputPortReference](const FDMXOutputPortSharedRef& OutputPort) {
				return OutputPort->GetPortGuid() == OutputPortReference.GetPortGuid();
				});

			if (OutputPortPtr)
			{
				OutputPorts.Add(*OutputPortPtr);
			}
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

UDMXPixelMappingRendererComponent* UDMXPixelMappingScreenComponent::GetRendererComponent() const
{
	return Cast<UDMXPixelMappingRendererComponent>(Parent);
}

void UDMXPixelMappingScreenComponent::ResetDMX()
{
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(RendererComponent))
	{
		return;
	}
	
	RendererComponent->ResetColorDownsampleBufferPixels(PixelDownsamplePositionRange.Key, PixelDownsamplePositionRange.Value);
	SendDMX();
}

void UDMXPixelMappingScreenComponent::SendDMX()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXPixelMaping_SendScreen);

	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(RendererComponent))
	{
		return;
	}

	if (RemoteUniverse < 0)
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("RemoteUniverse < 0"));
		return;
	}

	// Helper to send to the correct ports, depending on bSendToAllOutputPorts
	auto SendDMXToPorts = [this](int32 InUniverseID, const TMap<int32, uint8>& InChannelToValueMap)
	{
		if (bSendToAllOutputPorts)
		{
			for (const FDMXOutputPortSharedRef& OutputPort : FDMXPortManager::Get().GetOutputPorts())
			{
				OutputPort->SendDMX(InUniverseID, InChannelToValueMap);
			}
		}
		else
		{
			for (const FDMXOutputPortSharedRef& OutputPort : OutputPorts)
			{
				OutputPort->SendDMX(InUniverseID, InChannelToValueMap);
			}
		}
	};

	TArray<FColor> UnsortedList = RendererComponent->GetDownsampleBufferPixels(PixelDownsamplePositionRange.Key, PixelDownsamplePositionRange.Value);
	TArray<FColor> SortedList;
	SortedList.Reserve(UnsortedList.Num());
	FDMXPixelMappingUtils::TextureDistributionSort<FColor>(Distribution, NumXCells, NumYCells, UnsortedList, SortedList);

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
	const uint32 UniverseMaxChannels = FDMXPixelMappingUtils::GetUniverseMaxChannels(PixelFormat, StartAddress);
	uint32 SendDMXIndex = StartAddress;
	int32 UniverseToSend = RemoteUniverse;
	const int32 SendBufferNum = SendBuffer.Num();
	TMap<int32, uint8> ChannelToValueMap;
	for (int32 FragmentMapIndex = 0; FragmentMapIndex < SendBufferNum; FragmentMapIndex++)
	{			
		// ready to send here
		if (SendDMXIndex > UniverseMaxChannels)
		{
			SendDMXToPorts(UniverseToSend, ChannelToValueMap);

			// Now reset
			ChannelToValueMap.Empty();
			SendDMXIndex = StartAddress;
			UniverseToSend++;
		}

		// should be channels from 1...UniverseMaxChannels
		ChannelToValueMap.Add(SendDMXIndex, SendBuffer[FragmentMapIndex]);
		if (SendDMXIndex > UniverseMaxChannels || SendDMXIndex < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("WrongIndex FragmentMapIndex %d, SendDMXIndex %d"), FragmentMapIndex, SendDMXIndex);
		}

		// send dmx if next iteration is the last one
		if ((SendBufferNum > FragmentMapIndex + 1) == false)
		{
			SendDMXToPorts(UniverseToSend, ChannelToValueMap);
			break;
		}

		SendDMXIndex++;
	}
}

void UDMXPixelMappingScreenComponent::QueueDownsample()
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

	const uint32 TextureSizeX = InputTexture->Resource->GetSizeX();
	const uint32 TextureSizeY = InputTexture->Resource->GetSizeY();
	check(TextureSizeX > 0 && TextureSizeY > 0);
	
	constexpr bool bStaticCalculateUV = true;
	const FVector2D SizePixel(SizeX / NumXCells, SizeY / NumYCells);
	const FVector2D UVSize(SizePixel.X / TextureSizeX, SizePixel.Y / TextureSizeY);
	const FVector2D UVCellSize = UVSize / 2.f;
	const int32 PixelNum = NumXCells * NumYCells;
	const FVector4 PixelFactor(1.f, 1.f, 1.f, 1.f);
	const FIntVector4 InvertPixel(0);

	// Start of downsample index
	PixelDownsamplePositionRange.Key = RendererComponent->GetDownsamplePixelNum();

	int32 IterationCount = 0;
	ForEachPixel([&](const int32 InXYIndex, const int32 XIndex, const int32 YIndex)
        {
            const FIntPoint PixelPosition = RendererComponent->GetPixelPosition(InXYIndex + PixelDownsamplePositionRange.Key);
            const FVector2D UV = FVector2D((PositionX + SizePixel.X * XIndex) / TextureSizeX, (PositionY + SizePixel.Y * YIndex) / TextureSizeY);

            FDMXPixelMappingDownsamplePixelParam DownsamplePixelParam
            {
                PixelFactor,
                InvertPixel,
                PixelPosition,
                UV,
                UVSize,
                UVCellSize,
                CellBlendingQuality,
                bStaticCalculateUV
            };

            RendererComponent->AddPixelToDownsampleSet(MoveTemp(DownsamplePixelParam));

            IterationCount = InXYIndex;
        });

	// End of downsample index
	PixelDownsamplePositionRange.Value = PixelDownsamplePositionRange.Key + IterationCount;
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
	if (InSize.X <= MinGridSize.X)
	{
		SizeX = MinGridSize.X;
	}
	else
	{
		SizeX = InSize.X;
	}

	if (InSize.Y <= MinGridSize.Y)
	{
		SizeY = MinGridSize.Y;
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

bool UDMXPixelMappingScreenComponent::CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
{
	return Component && Component->IsA<UDMXPixelMappingRendererComponent>();
}

const FVector2D UDMXPixelMappingScreenComponent::GetScreenPixelSize() const
{
	return FVector2D(SizeX / NumXCells, SizeY / NumYCells);
}

void UDMXPixelMappingScreenComponent::ForEachPixel(ForEachPixelCallback InCallback)
{
	int32 IndexXY = 0;
	for (int32 NumXIndex = 0; NumXIndex < NumXCells; ++NumXIndex)
	{
		for (int32 NumYIndex = 0; NumYIndex < NumYCells; ++NumYIndex)
		{
			InCallback(IndexXY, NumXIndex, NumYIndex);
			IndexXY++;
		}
	}
}

#undef LOCTEXT_NAMESPACE
