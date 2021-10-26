// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamerDelegates.h"

UPixelStreamerDelegates* UPixelStreamerDelegates::Singleton = nullptr;

UPixelStreamerDelegates* UPixelStreamerDelegates::CreateInstance()
{
	if(Singleton == nullptr)
	{
		Singleton = NewObject<UPixelStreamerDelegates>();
		Singleton->AddToRoot();
		return Singleton;
	}
	return Singleton;
}
