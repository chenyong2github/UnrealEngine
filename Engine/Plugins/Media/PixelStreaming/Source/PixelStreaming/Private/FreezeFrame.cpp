// Copyright Epic Games, Inc. All Rights Reserved.

#include "FreezeFrame.h"
#include "IPixelStreamingModule.h"

UFreezeFrame* UFreezeFrame::Singleton = nullptr;

void UFreezeFrame::FreezeFrame(UTexture2D* Texture)
{
	if(Singleton)
	{
		Singleton->PixelStreamingModule->FreezeFrame(Texture);
	}
}

void UFreezeFrame::UnfreezeFrame()
{
	if(Singleton)
	{
		Singleton->PixelStreamingModule->UnfreezeFrame();
	}
}

void UFreezeFrame::CreateInstance()
{
	Singleton = NewObject<UFreezeFrame>();
	Singleton->AddToRoot();
	Singleton->PixelStreamingModule = &FModuleManager::Get().GetModuleChecked<IPixelStreamingModule>("PixelStreaming");
}
