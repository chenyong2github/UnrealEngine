// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizationManager.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogVirtualization);

FVirtualizationManager& FVirtualizationManager::Get()
{
	// TODO: Do we really need to make this a singleton? Easier for prototyping.
	static FVirtualizationManager Singleton;
	return Singleton;
}

FVirtualizationManager::FVirtualizationManager()
	: bEnablePayloadPushing(true)
	, MinPayloadLength(0)
{
	LoadSettingsFromConfigFiles();
}

bool FVirtualizationManager::PushData(const FSharedBuffer& Payload, const FGuid& Guid)
{
	checkf(Guid.IsValid(), TEXT("VirtualizationManager::PushData called with an invalid guid (%s)."), *Guid.ToString());

	// Early out if the pushing of payloads is disabled
	if (bEnablePayloadPushing == false)
	{
		return false;
	}

	// Early out if we have no payload
	if (Payload.GetSize() == 0)
	{
		return false;
	}

	// Early out if the payload length is below our minimum required length
	if ((int64)Payload.GetSize() < MinPayloadLength)
	{
		return false;
	}

	// TODO: At this point we should be able to consider which back end actually gets used, the following code is 
	//		 more of a reference implementation!
	{
		const FString FilePath = CreateFilePath(Guid);

		TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileWriter(*FilePath));
		check(FileAr);

		uint64 PayloadLength = Payload.GetSize();
		// Cast away the const because FArchive requires a non-const pointer even if we are only reading from it (saving)
		void* PayloadPtr = const_cast<void*>(Payload.GetData());

		*FileAr << PayloadLength;
		FileAr->Serialize(PayloadPtr, PayloadLength);
	}

	return true;
}

FSharedBuffer FVirtualizationManager::PullData(const FGuid& Guid)
{
	checkf(Guid.IsValid(), TEXT("VirtualizationManager::PullData called with an invalid guid (%s)."), *Guid.ToString());

	// TODO: At this point we should be able to consider which back end actually gets used, the following code is 
	//		 more of a reference implementation!
	
	const FString FilePath = CreateFilePath(Guid);

	TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileReader(*FilePath));
	checkf(FileAr != nullptr, TEXT("Unable to open %s, virtualized data is inaccessible!"), *FilePath);

	int64 DataLength = 0;
	*FileAr << DataLength;

	FUniqueBuffer Buffer = FUniqueBuffer::Alloc(DataLength);
	FileAr->Serialize(Buffer.GetData(), DataLength);
	return FSharedBuffer(MoveTemp(Buffer));
}

void FVirtualizationManager::LoadSettingsFromConfigFiles()
{
	FConfigFile PlatformEngineIni;
	if (FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true))
	{
		bool bEnablePayloadPushingFromIni = false;
		if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualization"), TEXT("EnablePushToBackend"), bEnablePayloadPushingFromIni))
		{
			bEnablePayloadPushing = bEnablePayloadPushingFromIni;
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].EnablePushToBackend from ini file!"));
		}

		int64 MinPayloadLengthFromIni = 0;
		if (PlatformEngineIni.GetInt64(TEXT("Core.ContentVirtualization"), TEXT("MinPayloadLength"), MinPayloadLengthFromIni))
		{
			MinPayloadLength = MinPayloadLengthFromIni;
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].MinPayloadLength from ini file!"));;
		}
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load 'Core.ContentVirtualization' ini file settings!"));
	}
}

FString FVirtualizationManager::CreateFilePath(const FGuid& Guid)
{
	return FString::Printf(TEXT("%sVirtualizedPayloads/%s.payload"), *FPaths::ProjectSavedDir(), *Guid.ToString());
}
