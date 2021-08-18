// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareCore.h"
#include "TextureShareCoreLog.h"
#include "IPC/SharedResource.h"
#include "Core/TextureShareItemBase.h"
#include "DirectX/TextureShareItemD3D11.h"
#include "DirectX/TextureShareItemD3D12.h"
#include "Misc/ScopeLock.h"

FTextureShareCoreModule::FTextureShareCoreModule()
{
}

FTextureShareCoreModule::~FTextureShareCoreModule()
{
	ShutdownModule();
}

void FTextureShareCoreModule::StartupModule()
{
	InitializeProcessMemory();
}

void FTextureShareCoreModule::ShutdownModule()
{
	ReleaseLib();
	ReleaseProcessMemory();
}

void FTextureShareCoreModule::InitializeProcessMemory()
{
	if (!bIsValidSharedResource)
	{
		bIsValidSharedResource = TextureShareItem::FSharedResource::InitializeProcessMemory();
		if (!bIsValidSharedResource)
		{
			UE_LOG(LogTextureShareCore, Error, TEXT("Failed InitializeProcessMemory"));
		}
	}
}

void FTextureShareCoreModule::ReleaseProcessMemory()
{
	if (bIsValidSharedResource)
	{
		bIsValidSharedResource = false;
		TextureShareItem::FSharedResource::ReleaseProcessMemory();
	}
}

void FTextureShareCoreModule::ReleaseLib()
{
	FScopeLock ScopeLock(&DataGuard);
	for (auto& It : TextureShares)
	{
		It.Value->Release();
		It.Value.Reset();
	}
	TextureShares.Empty();
}

static bool IsShareNameEmpty(const FString& ShareName)
{
	if (ShareName.IsEmpty())
	{
		UE_LOG(LogTextureShareCore, Error, TEXT("TextureShare: Share name is empty"));
		return true;
	}
	return false;
}

bool FTextureShareCoreModule::GetTextureShareItem(const FString& InShareName, TSharedPtr<ITextureShareItem>& OutShareObject) const
{
	if (IsShareNameEmpty(InShareName))
	{
		return false;
	}

	if (bIsValidSharedResource)
	{
		FScopeLock ScopeLock(&DataGuard);

		const TSharedPtr<ITextureShareItem>* ShareItem = TextureShares.Find(InShareName.ToLower());
		if (ShareItem)
		{
			OutShareObject = *ShareItem;
			return OutShareObject.IsValid();
		}
	}

	UE_LOG(LogTextureShareCore, Error, TEXT("Texture share '%s' not exist"), *InShareName);
	return false;
}

// Create shared resource object
bool FTextureShareCoreModule::CreateTextureShareItem(const FString& InShareName, ETextureShareProcess Process, FTextureShareSyncPolicy SyncMode, ETextureShareDevice DeviceType, TSharedPtr<ITextureShareItem>& OutShareObject, float SyncWaitTime)
{
	if (IsShareNameEmpty(InShareName))
	{
		return false;
	}

	FScopeLock ScopeLock(&DataGuard);

	FString LowerShareName = InShareName.ToLower();
	if (TextureShares.Contains(LowerShareName))
	{
		UE_LOG(LogTextureShareCore, Error, TEXT("CreateTextureShare: Texture share '%s' already exist"), *InShareName);
		return false;
	}

	if (bIsValidSharedResource)
	{
		TextureShareItem::FTextureShareItemBase* Resource = nullptr;
		switch (DeviceType)
		{

#if TEXTURESHARELIB_USE_D3D11
		case ETextureShareDevice::D3D11:
			Resource = new TextureShareItem::FTextureShareItemD3D11(LowerShareName, SyncMode, Process);
			break;
#endif /*TEXTURESHARELIB_USE_D3D11*/

#if TEXTURESHARELIB_USE_D3D12
		case ETextureShareDevice::D3D12:
			Resource = new TextureShareItem::FTextureShareItemD3D12(LowerShareName, SyncMode, Process);
			break;
#endif /*TEXTURESHARELIB_USE_D3D12*/

		case ETextureShareDevice::Vulkan: //@todo: NOT_SUPPORTED
		case ETextureShareDevice::Memory:  //@todo: NOT_SUPPORTED
		default:
			break;
		}

		// Is created resource valid (out of max nums, unsupported devices, etc)
		if (!Resource || !Resource->IsValid())
		{
			if (Resource)
			{
				delete Resource;
			}
			UE_LOG(LogTextureShareCore, Error, TEXT("CreateTextureShare: Failed initialize Texture share '%s'"), *InShareName);
			return false;
		}

		// set sync wait time
		Resource->SetSyncWaitTime(SyncWaitTime);

		// Save created object ptr
		TSharedPtr<ITextureShareItem> NewShareObject = MakeShareable(Resource);
		TextureShares.Add(LowerShareName, NewShareObject);

		OutShareObject = NewShareObject;
		UE_LOG(LogTextureShareCore, Log, TEXT("Created TextureShare '%s'"), *InShareName);
		return true;
	}

	UE_LOG(LogTextureShareCore, Error, TEXT("CreateTextureShare: Failed. Shared memory not connected for share '%s'"), *InShareName);
	return false;
}

bool FTextureShareCoreModule::ReleaseTextureShareItem(const FString& InShareName)
{
	if (IsShareNameEmpty(InShareName))
	{
		return false;
	}

	FScopeLock ScopeLock(&DataGuard);

	FString LowerShareName = InShareName.ToLower();
	if (bIsValidSharedResource && TextureShares.Contains(LowerShareName))
	{
		TSharedPtr<ITextureShareItem> Resource = TextureShares[LowerShareName];

		if (Resource)
		{
			Resource->Release();
			Resource.Reset();
			TextureShares.Remove(LowerShareName);

			UE_LOG(LogTextureShareCore, Log, TEXT("Released TextureShare '%s'"), *InShareName);
			return true;
		}
	}

	return false;
}

FTextureShareSyncPolicySettings FTextureShareCoreModule::GetSyncPolicySettings(ETextureShareProcess Process) const
{
	return TextureShareItem::FTextureShareItemBase::GetSyncPolicySettings(Process);
}

void FTextureShareCoreModule::SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings)
{
	TextureShareItem::FTextureShareItemBase::SetSyncPolicySettings(Process, InSyncPolicySettings);
}

bool FTextureShareCoreModule::BeginSyncFrame()
{
	// NOT IMPLEMENTED
	return false;
}

bool FTextureShareCoreModule::EndSyncFrame()
{
	// NOT IMPLEMENTED
	return false;
}

IMPLEMENT_MODULE(FTextureShareCoreModule, TextureShareCore);
