// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingScreenComponent.h"

#include "DMXPixelMappingRuntimeCommon.h"
#include "DMXPixelMappingTypes.h"
#include "DMXPixelMappingUtils.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXPortManager.h"

#if WITH_EDITOR
#include "DMXPixelMappingComponentWidget.h"
#include "SDMXPixelMappingScreenComponentBox.h"
#endif // WITH_EDITOR

#include "Engine/Texture.h"


DECLARE_CYCLE_STAT(TEXT("Send Screen"), STAT_DMXPixelMaping_SendScreen, STATGROUP_DMXPIXELMAPPING);

#define LOCTEXT_NAMESPACE "DMXPixelMappingScreenComponent"

const FVector2D UDMXPixelMappingScreenComponent::MinGridSize = FVector2D(1.f);

UDMXPixelMappingScreenComponent::UDMXPixelMappingScreenComponent()
	: bSendToAllOutputPorts(true)
{
	SizeX = 500.f; 
	SizeY = 500.f; 

	NumXCells = 10;
	NumYCells = 10;

	PixelFormat = EDMXCellFormat::PF_RGB;
	bIgnoreAlphaChannel = true;

	LocalUniverse = 1;
	StartAddress = 1;
	PixelIntensity = 1;
	AlphaIntensity = 1;
	Distribution = EDMXPixelMappingDistribution::TopLeftToRight;
}

#if WITH_EDITOR
void UDMXPixelMappingScreenComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent)
{
	// Call the parent at the first place
	Super::PostEditChangeChainProperty(PropertyChangedChainEvent);
	
	if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumXCells) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, NumYCells) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, LocalUniverse) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, StartAddress) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, Distribution) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PixelFormat) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, bShowAddresses) ||
		PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, bShowUniverse))
	{
		if (ScreenComponentBox.IsValid())
		{
			FDMXPixelMappingScreenComponentGridParams GridParams;
			GridParams.bShowAddresses = bShowAddresses;
			GridParams.bShowUniverse = bShowUniverse;
			GridParams.Distribution = Distribution;
			GridParams.NumXCells = NumXCells;
			GridParams.NumYCells = NumYCells;
			GridParams.PixelFormat = PixelFormat;
			GridParams.LocalUniverse = LocalUniverse;
			GridParams.StartAddress = StartAddress;

			ScreenComponentBox->RebuildGrid(GridParams);
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
	else if (PropertyChangedChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PositionX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, PositionY))
		{
			if (ComponentWidget.IsValid())
			{
				ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
			}
		}
		else if (PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, SizeX) ||
			PropertyChangedChainEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingScreenComponent, SizeY))
		{
			if (ComponentWidget.IsValid())
			{
				ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
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
TSharedRef<FDMXPixelMappingComponentWidget> UDMXPixelMappingScreenComponent::BuildSlot(TSharedRef<SConstraintCanvas> InCanvas)
{
	if (!ComponentWidget.IsValid())
	{
		ScreenComponentBox = 
			SNew(SDMXPixelMappingScreenComponentBox)
			.NumXCells(NumXCells)
			.NumYCells(NumYCells)
			.Distribution(Distribution)
			.PixelFormat(PixelFormat)
			.LocalUniverse(LocalUniverse)
			.StartAddress(StartAddress)
			.bShowAddresses(bShowAddresses)
			.bShowUniverse(bShowUniverse);

		ComponentWidget = MakeShared<FDMXPixelMappingComponentWidget>(ScreenComponentBox, nullptr);

		ComponentWidget->AddToCanvas(InCanvas, ZOrder);
		ComponentWidget->SetPosition(GetPosition());
		ComponentWidget->SetSize(GetSize());
		ComponentWidget->SetColor(GetEditorColor());
		ComponentWidget->SetLabelText(FText::FromString(GetUserFriendlyName()));
	}
	
	return ComponentWidget.ToSharedRef();
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
	return Cast<UDMXPixelMappingRendererComponent>(GetParent());
}

void UDMXPixelMappingScreenComponent::ResetDMX()
{
	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (ensure(RendererComponent))
	{
		RendererComponent->ResetColorDownsampleBufferPixels(PixelDownsamplePositionRange.Key, PixelDownsamplePositionRange.Value);
		SendDMX();
	}
}

void UDMXPixelMappingScreenComponent::SendDMX()
{
	SCOPE_CYCLE_COUNTER(STAT_DMXPixelMaping_SendScreen);

	UDMXPixelMappingRendererComponent* RendererComponent = GetRendererComponent();
	if (!ensure(RendererComponent))
	{
		return;
	}

	if (LocalUniverse < 0)
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("LocalUniverse < 0"));
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

	TArray<FLinearColor> UnsortedList; 
	if (RendererComponent->GetDownsampleBufferPixels(PixelDownsamplePositionRange.Key, PixelDownsamplePositionRange.Value, UnsortedList))
	{
		TArray<FLinearColor> SortedList;
		SortedList.Reserve(UnsortedList.Num());
		FDMXPixelMappingUtils::TextureDistributionSort<FLinearColor>(Distribution, NumXCells, NumYCells, UnsortedList, SortedList);

		// Sending only if there enough space at least for one pixel
		if (!FDMXPixelMappingUtils::CanFitCellIntoChannels(PixelFormat, StartAddress))
		{
			return;
		}

		// Prepare Universes for send
		TArray<uint8> SendBuffer;
		for (const FLinearColor& LinearColor : SortedList)
		{
			constexpr bool bUseSRGB = true;
			FColor Color = LinearColor.ToFColor(bUseSRGB);
		
			const float MaxValue = 255.f;
			Color.R = static_cast<uint8>(FMath::Min(Color.R * PixelIntensity, MaxValue));
			Color.G = static_cast<uint8>(FMath::Min(Color.G * PixelIntensity, MaxValue));
			Color.B = static_cast<uint8>(FMath::Min(Color.B * PixelIntensity, MaxValue));
			Color.A = static_cast<uint8>(FMath::Min(Color.A * AlphaIntensity, MaxValue));;
			AddColorToSendBuffer(Color, SendBuffer);
		}

		// Start sending
		const uint32 UniverseMaxChannels = FDMXPixelMappingUtils::GetUniverseMaxChannels(PixelFormat, StartAddress);
		uint32 SendDMXIndex = StartAddress;
		int32 UniverseToSend = LocalUniverse;
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

void UDMXPixelMappingScreenComponent::RenderWithInputAndSendDMX()
{
	if (UDMXPixelMappingRendererComponent* RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this))
	{
		RendererComponent->RendererInputTexture();
	}

	RenderAndSendDMX();
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
	for (int32 NumYIndex = 0; NumYIndex < NumYCells; ++NumYIndex)
	{
		for (int32 NumXIndex = 0; NumXIndex < NumXCells; ++NumXIndex)
		{
			InCallback(IndexXY, NumXIndex, NumYIndex);
			IndexXY++;
		}
	}
}

void UDMXPixelMappingScreenComponent::SetPosition(const FVector2D& NewPosition)
{
	PositionX = NewPosition.X;
	PositionY = NewPosition.Y;

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetPosition(FVector2D(PositionX, PositionY));
	}
#endif
}

void UDMXPixelMappingScreenComponent::SetSize(const FVector2D& NewSize)
{
	SizeX = FMath::Max(NewSize.X, 1.f);
	SizeY = FMath::Max(NewSize.Y, 1.f);

	SizeX = FMath::Max(SizeX, 1.f);
	SizeY = FMath::Max(SizeY, 1.f);

#if WITH_EDITOR
	if (ComponentWidget.IsValid())
	{
		ComponentWidget->SetSize(FVector2D(SizeX, SizeY));
	}
#endif
}

#undef LOCTEXT_NAMESPACE
