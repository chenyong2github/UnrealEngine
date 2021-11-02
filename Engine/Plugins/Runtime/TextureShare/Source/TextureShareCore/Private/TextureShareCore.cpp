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

bool FTextureShareCoreModule::ImplCheckTextureShareItem(const FString& InShareName) const
{
	if (!bIsValidSharedResource)
	{
		UE_LOG(LogTextureShareCore, Error, TEXT("CreateTextureShare: Failed. Shared memory not connected for share '%s'"), *InShareName);
		return false;
	}

	if (InShareName.IsEmpty())
	{
		UE_LOG(LogTextureShareCore, Error, TEXT("TextureShare: Share name is empty"));
		return false;
	}

	return true;
}

bool FTextureShareCoreModule::GetTextureShareItem(const FString& InShareName, TSharedPtr<ITextureShareItem>& OutShareObject) const
{
	if (ImplCheckTextureShareItem(InShareName))
	{
		FScopeLock ScopeLock(&DataGuard);

		const TSharedPtr<ITextureShareItem>* ShareItem = TextureShares.Find(InShareName.ToLower());
		if (ShareItem && ShareItem->IsValid())
		{
			OutShareObject = *ShareItem;

			return true;
		}

		UE_LOG(LogTextureShareCore, Error, TEXT("Texture share '%s' not exist"), *InShareName);
	}

	return false;
}

// Create shared resource object
bool FTextureShareCoreModule::CreateTextureShareItem(const FString& InShareName, ETextureShareProcess Process, FTextureShareSyncPolicy SyncMode, ETextureShareDevice DeviceType, TSharedPtr<ITextureShareItem>& OutShareObject, float SyncWaitTime)
{
	if (ImplCheckTextureShareItem(InShareName))
	{
		FScopeLock ScopeLock(&DataGuard);

		FString LowerShareName = InShareName.ToLower();
		if (TextureShares.Contains(LowerShareName))
		{
			UE_LOG(LogTextureShareCore, Error, TEXT("CreateTextureShare: Texture share '%s' already exist"), *InShareName);
			return false;
		}

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

		// Save created object ptr
		TSharedPtr<ITextureShareItem> NewShareObject = MakeShareable(Resource);

		TextureShares.Add(LowerShareName, NewShareObject);
		OutShareObject = NewShareObject;

		UE_LOG(LogTextureShareCore, Log, TEXT("Created TextureShare '%s'"), *InShareName);
		return true;
	}

	return false;
}

bool FTextureShareCoreModule::ReleaseTextureShareItem(const FString& InShareName)
{
	if (ImplCheckTextureShareItem(InShareName))
	{
		FScopeLock ScopeLock(&DataGuard);

		FString LowerShareName = InShareName.ToLower();
		if (TextureShares.Contains(LowerShareName))
		{
			// Reset share ptr
			TextureShares[LowerShareName].Reset();

			// Remove key
			TextureShares.Remove(LowerShareName);

			UE_LOG(LogTextureShareCore, Log, TEXT("Released TextureShare '%s'"), *InShareName);
			return true;
		}
	}

	return false;
}

FTextureShareSyncPolicySettings FTextureShareCoreModule::GetSyncPolicySettings(ETextureShareProcess Process) const
{
	check(IsInGameThread());

	return TextureShareItem::FTextureShareItemBase::GetSyncPolicySettings(Process);
}

void FTextureShareCoreModule::SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings)
{
	check(IsInGameThread());

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
