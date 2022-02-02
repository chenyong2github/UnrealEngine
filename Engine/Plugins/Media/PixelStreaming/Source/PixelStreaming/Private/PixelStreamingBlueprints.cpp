// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingBlueprints.h"
#include "PixelStreamingPrivate.h"
#include "Misc/FileHelper.h"
#include "PixelStreamingInputComponent.h"
#include "PixelStreamingAudioComponent.h"
#include "PixelStreamingPlayerId.h"

void UPixelStreamingBlueprints::SendFileAsByteArray(TArray<uint8> ByteArray, FString MimeType, FString FileExtension)
{
	IPixelStreamingModule* Module = UE::PixelStreaming::FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->SendFileData(ByteArray, MimeType, FileExtension);
	}
}

void UPixelStreamingBlueprints::SendFile(FString FilePath, FString MimeType, FString FileExtension)
{
	IPixelStreamingModule* Module = UE::PixelStreaming::FPixelStreamingModule::GetModule();
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
			UE_LOG(LogPixelStreaming, Error, TEXT("FileHelper failed to load file data"));
		}
	}
}

void UPixelStreamingBlueprints::FreezeFrame(UTexture2D* Texture)
{
	IPixelStreamingModule* Module = UE::PixelStreaming::FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->FreezeFrame(Texture);
	}
}

void UPixelStreamingBlueprints::UnfreezeFrame()
{
	IPixelStreamingModule* Module = UE::PixelStreaming::FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->UnfreezeFrame();
	}
}

void UPixelStreamingBlueprints::KickPlayer(FString PlayerId)
{
	IPixelStreamingModule* Module = UE::PixelStreaming::FPixelStreamingModule::GetModule();
	if (Module)
	{
		Module->KickPlayer(ToPlayerId(PlayerId));
	}
}

UPixelStreamingDelegates* UPixelStreamingBlueprints::GetPixelStreamingDelegates()
{
	return UPixelStreamingDelegates::GetPixelStreamingDelegates();
}