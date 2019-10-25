// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Channels/RemoteSessionImageChannel.h"
#include "RemoteSession.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"
#include "BackChannel/Protocol/OSC/BackChannelOSCMessage.h"
#include "Channels/RemoteSessionChannel.h"
#include "HAL/IConsoleManager.h"
#include "Async/Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"


DECLARE_CYCLE_STAT(TEXT("RSTextureUpdate"), STAT_TextureUpdate, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSNumTicks"), STAT_RSNumTicks, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSReadyFrameCount"), STAT_RSNumFrames, STATGROUP_Game);

static int32 QualityMasterSetting = 0;
static FAutoConsoleVariableRef CVarQualityOverride(
	TEXT("remote.quality"), QualityMasterSetting,
	TEXT("Sets quality (1-100)"),
	ECVF_Default);


FRemoteSessionImageChannel::FImageSender::FImageSender(TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection)
{
	Connection = InConnection;
	CompressQuality = 0;
	NumSentImages = 0;
}

void FRemoteSessionImageChannel::FImageSender::SetCompressQuality(int32 InQuality)
{
	CompressQuality = InQuality;
}

void FRemoteSessionImageChannel::FImageSender::SendRawImageToClients(int32 Width, int32 Height, const TArray<FColor>& ImageData)
{
	SendRawImageToClients(Width, Height, ImageData.GetData(), ImageData.GetAllocatedSize());
}

void FRemoteSessionImageChannel::FImageSender::SendRawImageToClients(int32 Width, int32 Height, const void* ImageData, int32 AllocatedImageDataSize)
{
	static bool SkipImages = FParse::Param(FCommandLine::Get(), TEXT("remote.noimage"));

	// Can be released on the main thread at anytime so hold onto it
	TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> LocalConnection = Connection.Pin();

	if (LocalConnection.IsValid() && SkipImages == false)
	{
		const double TimeNow = FPlatformTime::Seconds();

		// created on demand because there can be multiple SendImage requests in flight
		IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));
		if (ImageWrapperModule != nullptr)
		{
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

			ImageWrapper->SetRaw(ImageData, AllocatedImageDataSize, Width, Height, ERGBFormat::BGRA, 8);

			const int32 CurrentQuality = QualityMasterSetting > 0 ? QualityMasterSetting : CompressQuality.Load();
			const TArray<uint8>& JPGData = ImageWrapper->GetCompressed(CurrentQuality);

			FBackChannelOSCMessage Msg(TEXT("/Screen"));
			Msg.Write(Width);
			Msg.Write(Height);
			Msg.Write(JPGData);
			Msg.Write(++NumSentImages);
			LocalConnection->SendPacket(Msg);

			UE_LOG(LogRemoteSession, Verbose, TEXT("Sent image %d in %.02f ms"),
				NumSentImages, (FPlatformTime::Seconds() - TimeNow) * 1000.0);
		}
	}
}

FRemoteSessionImageChannel::FRemoteSessionImageChannel(ERemoteSessionChannelMode InRole, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection)
	: IRemoteSessionChannel(InRole, InConnection)
{
	Connection = InConnection;
	DecodedTextures[0] = nullptr;
	DecodedTextures[1] = nullptr;
	DecodedTextureIndex = MakeShared<TAtomic<int32>, ESPMode::ThreadSafe>(0);
	Role = InRole;
	LastDecodedImageIndex = 0;
	LastIncomingImageIndex = 0;

	BackgroundThread = nullptr;
	ScreenshotEvent = nullptr;
	ExitRequested = false;

	if (Role == ERemoteSessionChannelMode::Read)
	{
		auto Delegate = FBackChannelDispatchDelegate::FDelegate::CreateRaw(this, &FRemoteSessionImageChannel::ReceiveHostImage);
		MessageCallbackHandle = InConnection->AddMessageHandler(TEXT("/Screen"), Delegate);

		InConnection->SetMessageOptions(TEXT("/Screen"), 1);

		StartBackgroundThread();
	}
	else
	{
		ImageSender = MakeShared<FRemoteSessionImageChannel::FImageSender, ESPMode::ThreadSafe>(InConnection);
	}
}

FRemoteSessionImageChannel::~FRemoteSessionImageChannel()
{
	if (Role == ERemoteSessionChannelMode::Read)
	{
		TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> LocalConnection = Connection.Pin();
		if (LocalConnection.IsValid())
		{
			// Remove the callback so it doesn't call back on an invalid this
			LocalConnection->RemoveMessageHandler(TEXT("/Screen"), MessageCallbackHandle);
		}
		MessageCallbackHandle.Reset();

		ExitBackgroundThread();

		if (ScreenshotEvent)
		{
			//Cleanup the FEvent
			FGenericPlatformProcess::ReturnSynchEventToPool(ScreenshotEvent);
			ScreenshotEvent = nullptr;
		}

		if (BackgroundThread)
		{
			//Cleanup the worker thread
			delete BackgroundThread;
			BackgroundThread = nullptr;
		}
	}

	for (int32 i = 0; i < 2; i++)
	{
		if (DecodedTextures[i])
		{
			DecodedTextures[i]->RemoveFromRoot();
			DecodedTextures[i] = nullptr;
		}
	}
}

UTexture2D* FRemoteSessionImageChannel::GetHostScreen() const
{
	return DecodedTextures[DecodedTextureIndex->Load()];
}

void FRemoteSessionImageChannel::Tick(const float InDeltaTime)
{
	INC_DWORD_STAT(STAT_RSNumTicks);

	if (Role == ERemoteSessionChannelMode::Write)
	{
		if (ImageProvider)
		{
			ImageProvider->Tick(InDeltaTime);
		}
	}

	if (Role == ERemoteSessionChannelMode::Read)
	{
		SCOPE_CYCLE_COUNTER(STAT_TextureUpdate);

		TUniquePtr<FImageData> QueuedImage;

		{
			// Check to see if there are any queued images. We just care about the last
			FScopeLock ImageLock(&DecodedImageMutex);
			if (IncomingDecodedImages.Num())
			{
				INC_DWORD_STAT(STAT_RSNumFrames);
				QueuedImage = MoveTemp(IncomingDecodedImages.Last());
				LastDecodedImageIndex = QueuedImage->ImageIndex;

				UE_LOG(LogRemoteSession, Verbose, TEXT("GT: Image %d is ready, discarding %d earlier images"),
					QueuedImage->ImageIndex, IncomingDecodedImages.Num()-1);

				IncomingDecodedImages.Reset();
			}
		}

		// If an image was waiting...
		if (QueuedImage.IsValid())
		{
			int32 NextImage = DecodedTextureIndex->Load() == 0 ? 1 : 0;

			// create a texture if we don't have a suitable one
			if (DecodedTextures[NextImage] == nullptr || QueuedImage->Width != DecodedTextures[NextImage]->GetSizeX() || QueuedImage->Height != DecodedTextures[NextImage]->GetSizeY())
			{
				CreateTexture(NextImage, QueuedImage->Width, QueuedImage->Height);
			}

			// Update it on the render thread. There shouldn't (...) be any harm in GT code using it from this point
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, QueuedImage->Width, QueuedImage->Height);
			TArray<uint8>* TextureData = new TArray<uint8>(MoveTemp(QueuedImage->ImageData));
			TWeakPtr<TAtomic<int32>, ESPMode::ThreadSafe> DecodedTextureIndexWeaked = DecodedTextureIndex;

			// cleanup functions, gets executed on the render thread after UpdateTextureRegions
			TFunction<void(uint8*, const FUpdateTextureRegion2D*)> DataCleanupFunc = [DecodedTextureIndexWeaked, NextImage, TextureData](uint8* InTextureData, const FUpdateTextureRegion2D* InRegions)
			{
				if (TSharedPtr<TAtomic<int32>, ESPMode::ThreadSafe> DecodedTextureIndexPinned = DecodedTextureIndexWeaked.Pin())
				{
					DecodedTextureIndexPinned->Store(NextImage);
				}

				//this is executed later on the render thread, meanwhile TextureData might have changed
				delete TextureData;
				delete InRegions; 
			};

			DecodedTextures[NextImage]->UpdateTextureRegions(0, 1, Region, 4 * QueuedImage->Width, sizeof(FColor), TextureData->GetData(), DataCleanupFunc);

			UE_LOG(LogRemoteSession, Verbose, TEXT("GT: Uploaded image %d"),
				QueuedImage->ImageIndex);
		} //-V773
	}
}

void FRemoteSessionImageChannel::SetImageProvider(TSharedPtr<IRemoteSessionImageProvider> InImageProvider)
{
	if (Role == ERemoteSessionChannelMode::Write)
	{
		ImageProvider = InImageProvider;
	}
}

void FRemoteSessionImageChannel::SetCompressQuality(int32 InQuality)
{
	if (Role == ERemoteSessionChannelMode::Write)
	{
		ImageSender->SetCompressQuality(InQuality);
	}
}

void FRemoteSessionImageChannel::ReceiveHostImage(FBackChannelOSCMessage& Message, FBackChannelOSCDispatch& Dispatch)
{
	int32 ImageIndex(0);

	TUniquePtr<FImageData> ReceivedImage = MakeUnique<FImageData>();

	Message << ReceivedImage->Width;
	Message << ReceivedImage->Height;
	Message << ReceivedImage->ImageData;
	Message << ImageIndex;

	ReceivedImage->ImageIndex = ImageIndex;

	FScopeLock Lock(&IncomingImageMutex);
	if (LastIncomingImageIndex > 0 && IncomingEncodedImages.Num() > 1 && IncomingEncodedImages.Last()->ImageIndex > LastIncomingImageIndex)
	{
		IncomingEncodedImages.RemoveSingle(IncomingEncodedImages.Last());
	}

	IncomingEncodedImages.Add(MoveTemp(ReceivedImage));

	if (ScreenshotEvent)
	{
		// wake up the background thread.
		ScreenshotEvent->Trigger();
	}

	UE_LOG(LogRemoteSession, Verbose, TEXT("Received Image %d, %d pending"),
		ImageIndex, IncomingEncodedImages.Num());
}

void FRemoteSessionImageChannel::ProcessIncomingTextures()
{
	TUniquePtr<FImageData> Image;
	const double StartTime = FPlatformTime::Seconds();
	{
		// check if there's anything to do, if not pause the background thread
		FScopeLock TaskLock(&IncomingImageMutex);

		if (IncomingEncodedImages.Num() == 0)
		{
			return;
		}

		// take the last image we don't care about the rest
		Image = MoveTemp(IncomingEncodedImages.Last());
		LastIncomingImageIndex = Image->ImageIndex;

		UE_LOG(LogRemoteSession, Verbose, TEXT("Processing Image %d, discarding %d other pending images"), 
			Image->ImageIndex, IncomingEncodedImages.Num() - 1);

		IncomingEncodedImages.Reset();
	}

	IImageWrapperModule* ImageWrapperModule = FModuleManager::GetModulePtr<IImageWrapperModule>(FName("ImageWrapper"));

	if (ImageWrapperModule != nullptr)
	{
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper(EImageFormat::JPEG);

		ImageWrapper->SetCompressed(Image->ImageData.GetData(), Image->ImageData.Num());

		const TArray<uint8>* RawData = nullptr;

		if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
		{
			TUniquePtr<FImageData> QueuedImage = MakeUnique<FImageData>();
			QueuedImage->Width = Image->Width;
			QueuedImage->Height = Image->Height;
			QueuedImage->ImageData = MoveTemp(*((TArray<uint8>*)RawData));
			QueuedImage->ImageIndex = Image->ImageIndex;

			{
				FScopeLock ImageLock(&DecodedImageMutex);
				if (LastDecodedImageIndex > 0 && IncomingDecodedImages.Num() > 1 && IncomingDecodedImages.Last()->ImageIndex > LastDecodedImageIndex)
				{
					IncomingDecodedImages.RemoveSingle(IncomingDecodedImages.Last());
				}
				IncomingDecodedImages.Add(MoveTemp(QueuedImage));

				UE_LOG(LogRemoteSession, Verbose, TEXT("finished decompressing image %d in %.02f ms (%d in queue)"),
					Image->ImageIndex,
					(FPlatformTime::Seconds() - StartTime) * 1000.0,
					IncomingEncodedImages.Num());
			}
		}
	}
}

void FRemoteSessionImageChannel::CreateTexture(const int32 InSlot, const int32 InWidth, const int32 InHeight)
{
	if (DecodedTextures[InSlot])
	{
		DecodedTextures[InSlot]->RemoveFromRoot();
		DecodedTextures[InSlot] = nullptr;
	}

	DecodedTextures[InSlot] = UTexture2D::CreateTransient(InWidth, InHeight);

	DecodedTextures[InSlot]->AddToRoot();
	DecodedTextures[InSlot]->UpdateResource();

	UE_LOG(LogRemoteSession, Log, TEXT("Created texture in slot %d %dx%d for incoming image"), InSlot, InWidth, InHeight);
}

void FRemoteSessionImageChannel::StartBackgroundThread()
{
	check(BackgroundThread == nullptr);

	ExitRequested	= false;
	
	ScreenshotEvent = FGenericPlatformProcess::GetSynchEventFromPool(false);;

	BackgroundThread = FRunnableThread::Create(this, TEXT("RemoteSessionFrameBufferThread"), 1024 * 1024, TPri_AboveNormal);

}

void FRemoteSessionImageChannel::ExitBackgroundThread()
{
	ExitRequested = true;

	if (ScreenshotEvent)
	{
		ScreenshotEvent->Trigger();
	}

	if (BackgroundThread)
	{
		BackgroundThread->WaitForCompletion();
	}
}

uint32 FRemoteSessionImageChannel::Run()
{
	while(!ExitRequested)
	{
		// wait a maximum of 1 second or until triggered
		ScreenshotEvent->Wait(1000);

		ProcessIncomingTextures();
	}

	return 0;
}

TSharedPtr<IRemoteSessionChannel> FRemoteSessionImageChannelFactoryWorker::Construct(ERemoteSessionChannelMode InMode, TSharedPtr<FBackChannelOSCConnection, ESPMode::ThreadSafe> InConnection) const
{
	return MakeShared<FRemoteSessionImageChannel>(InMode, InConnection);
}
