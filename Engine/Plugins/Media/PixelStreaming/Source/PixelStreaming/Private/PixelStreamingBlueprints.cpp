// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingBlueprints.h"
#include "PixelStreamingPrivate.h"
#include "Misc/FileHelper.h"
#include "PixelStreamerInputComponent.h"
#include "PixelStreamingAudioComponent.h"

void UPixelStreamingBlueprints::SendFileAsByteArray(TArray<uint8> ByteArray, FString MimeType, FString FileExtension)
{
	IPixelStreamingModule* Module = FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->SendFileData(ByteArray, MimeType, FileExtension);
	}
}

void UPixelStreamingBlueprints::SendFile(FString FilePath, FString MimeType, FString FileExtension)
{
	IPixelStreamingModule* Module = FPixelStreamingModule::GetModule();
	if (Module)
	{
		TArray<uint8> ByteData;
		bool bSuccess = FFileHelper::LoadFileToArray(ByteData, *FilePath);
		if (bSuccess)
		{
			Module->SendFileData(ByteData, MimeType, FileExtension);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("FileHelper failed to load file data"));
		}
	}
}

void UPixelStreamingBlueprints::FreezeFrame(UTexture2D* Texture)
{
	IPixelStreamingModule* Module = FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->FreezeFrame(Texture);
	}
}

void UPixelStreamingBlueprints::UnfreezeFrame()
{
	IPixelStreamingModule* Module = FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->UnfreezeFrame();
	}
}

UPixelStreamerDelegates* UPixelStreamingBlueprints::GetPixelStreamerDelegates()
{
	return UPixelStreamerDelegates::GetPixelStreamerDelegates();
}