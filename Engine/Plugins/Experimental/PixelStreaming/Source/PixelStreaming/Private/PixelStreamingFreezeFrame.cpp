// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingFreezeFrame.h"
#include "IPixelStreamingPlugin.h"

UPixelStreamingFreezeFrame* UPixelStreamingFreezeFrame::Singleton = nullptr;

void UPixelStreamingFreezeFrame::FreezeFrame(UTexture2D* Texture)
{
	Singleton->PixelStreamingPlugin->FreezeFrame(Texture);
}

void UPixelStreamingFreezeFrame::UnfreezeFrame()
{
	Singleton->PixelStreamingPlugin->UnfreezeFrame();
}

void UPixelStreamingFreezeFrame::CreateInstance()
{
	Singleton = NewObject<UPixelStreamingFreezeFrame>();
	Singleton->AddToRoot();
	Singleton->PixelStreamingPlugin = &FModuleManager::Get().GetModuleChecked<IPixelStreamingPlugin>("PixelStreaming");
}
