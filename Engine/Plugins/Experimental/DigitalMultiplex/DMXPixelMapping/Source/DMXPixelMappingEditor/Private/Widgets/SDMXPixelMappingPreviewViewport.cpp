// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingPreviewViewport.h"
#include "Viewports/DMXPixelMappingPreviewViewportClient.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "Viewports/DMXPixelMappingSceneViewport.h"

#include "Slate/SceneViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/TextureRenderTarget2D.h"

void SDMXPixelMappingPreviewViewport::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InViewport)
{
	bIsRenderingEnabled = true;
	ToolkitWeakPtr = InViewport;

	ChildSlot
		[
			SNew(SBox)
			.WidthOverride(this, &SDMXPixelMappingPreviewViewport::GetPreviewAreaWidth)
			.HeightOverride(this, &SDMXPixelMappingPreviewViewport::GetPreviewAreaHeight)
			[
				SAssignNew(ViewportWidget, SViewport)
					.EnableGammaCorrection(false)
					.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
					.ShowEffectWhenDisabled(false)
					.EnableBlending(true)
			]
		];

	ViewportClient = MakeShared<FDMXPixelMappingPreviewViewportClient>(InViewport, SharedThis(this));

	Viewport = MakeShared<FDMXPixelMappingSceneViewport>(ViewportClient.Get(), ViewportWidget);

	// The viewport widget needs an interface so it knows what should render
	ViewportWidget->SetViewportInterface(Viewport.ToSharedRef());
}

void SDMXPixelMappingPreviewViewport::EnableRendering()
{
	bIsRenderingEnabled = true;
}

void SDMXPixelMappingPreviewViewport::DisableRendering()
{
	bIsRenderingEnabled = false;
}

void SDMXPixelMappingPreviewViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bIsRenderingEnabled)
	{
		Viewport->Invalidate();
	}
}

FOptionalSize SDMXPixelMappingPreviewViewport::GetPreviewAreaWidth() const
{
	float Size = 1.f;

	for (UDMXPixelMappingBaseComponent* Component : GetActiveOutputComponents())
	{
		if (UDMXPixelMappingOutputDMXComponent* OutputDMXComponent = Cast<UDMXPixelMappingOutputDMXComponent>(Component))
		{
			return OutputDMXComponent->GetSize().X;
		}
		
	}

	for (const FTextureResource* Resource : GetOutputTextureResources())
	{
			Size = Resource->GetSizeX();
	}

	return Size;
}

FOptionalSize SDMXPixelMappingPreviewViewport::GetPreviewAreaHeight() const
{
	float Size = 1.f;

	for (UDMXPixelMappingBaseComponent* Component : GetActiveOutputComponents())
	{
		if (UDMXPixelMappingOutputDMXComponent* OutputDMXComponent = Cast<UDMXPixelMappingOutputDMXComponent>(Component))
		{
			return OutputDMXComponent->GetSize().Y;
		}

	}

	for (const FTextureResource* Resource : GetOutputTextureResources())
	{
		if (Resource->GetSizeY() > Size)
		{
			Size = Resource->GetSizeY();
		}
	}

	return Size;
}

TArray<UTexture*> SDMXPixelMappingPreviewViewport::GetOutputTextures() const
{
	TArray<UTexture*> OutputTextures;
	for (UDMXPixelMappingOutputComponent* OutputComponent : GetActiveOutputComponents())
	{
		OutputTextures.Add(OutputComponent->GetOutputTexture());
	}

	return OutputTextures;
}

TArray<const FTextureResource*> SDMXPixelMappingPreviewViewport::GetOutputTextureResources() const
{
	TArray<const FTextureResource*> OutputTextureResources;
	for (const UTexture* Texture : GetOutputTextures())
	{
		OutputTextureResources.Add(Texture->Resource);
	}

	return OutputTextureResources;
}

TArray<UDMXPixelMappingOutputComponent*> SDMXPixelMappingPreviewViewport::GetActiveOutputComponents() const
{
	if (TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin())
	{
		return Toolkit->GetActiveOutputComponents();
	}

	return TArray<UDMXPixelMappingOutputComponent*>();
}
