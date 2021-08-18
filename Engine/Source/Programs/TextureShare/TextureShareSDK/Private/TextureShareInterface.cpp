// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareInterface.h"
#include "ITextureShareCore.h"
#include "ITextureShareItem.h"
#include "ITextureShareItemD3D11.h"
#include "ITextureShareItemD3D12.h"

// Data Helpers
template <typename SrcType, typename DstType>
void CopyMatrix(SrcType& Src, DstType& Dst)
{
	for (int x = 0; x < 4; x++)
	{
		for (int y = 0; y < 4; y++)
		{
			Dst.M[x][y] = Src.M[x][y];
		}
	}
}

template <typename SrcType, typename DstType>
void CopyVector(SrcType& Src, DstType& Dst)
{
	Dst.X = Src.X;
	Dst.Y = Src.Y;
	Dst.Z = Src.Z;
}

template <typename SrcType, typename DstType>
void CopyRotator(SrcType& Src, DstType& Dst)
{
	Dst.Pitch = Src.Pitch;
	Dst.Yaw   = Src.Yaw;
	Dst.Roll  = Src.Roll;
}

template <typename SrcType, typename DstType>
void CopyAdditionalData(SrcType& Src, DstType& Dst)
{
	// Frame info
	Dst.FrameNumber = Src.FrameNumber;

	// Projection matrix
	CopyMatrix(Src.PrjMatrix,     Dst.PrjMatrix);

	// View info
	CopyMatrix(Src.ViewMatrix,    Dst.ViewMatrix);

	CopyVector(Src.ViewLocation,  Dst.ViewLocation);
	CopyRotator(Src.ViewRotation, Dst.ViewRotation);
	CopyVector(Src.ViewScale,     Dst.ViewScale);

	//@todo: add more frame data
}


template <typename SrcType, typename DstType>
void CopyCustomProjectionData(SrcType& Src, DstType& Dst)
{
	// Projection matrix
	CopyMatrix(Src.PrjMatrix, Dst.PrjMatrix);

	CopyVector(Src.ViewLocation, Dst.ViewLocation);
	CopyRotator(Src.ViewRotation, Dst.ViewRotation);
	CopyVector(Src.ViewScale, Dst.ViewScale);
}

ITextureShareCore& ShareCoreAPI()
{
	static ITextureShareCore* Singleton = &ITextureShareCore::Get();
	return *Singleton;
}

bool FTextureShareInterface::BeginSyncFrame()
{
	return ShareCoreAPI().BeginSyncFrame();
}

bool FTextureShareInterface::EndSyncFrame()
{
	return ShareCoreAPI().EndSyncFrame();
}

bool FTextureShareInterface::SetCustomProjectionData(const TCHAR* ShareName, const FTextureShareSDKCustomProjectionData& InData)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem))
	{
		// Convert to UE
		FTextureShareCustomProjectionData Data;
		CopyCustomProjectionData(InData, Data);
		return ShareItem->SetCustomProjectionData(Data);
	}

	return false;
}

bool FTextureShareInterface::SetLocalAdditionalData(const TCHAR* ShareName, const FTextureShareSDKAdditionalData& InData)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem))
	{
		// Convert to UE
		FTextureShareAdditionalData Data;
		CopyAdditionalData(InData, Data);
		return ShareItem->SetLocalAdditionalData(Data);
	}
	return false;
}

bool FTextureShareInterface::GetRemoteAdditionalData(const TCHAR* ShareName, FTextureShareSDKAdditionalData& Data)
{
	FTextureShareAdditionalData InData;
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->GetRemoteAdditionalData(InData))
	{
		// Convert from UE to SDK
		CopyAdditionalData(InData, Data);
		return true;
	}
	return false;
}

FTextureShareSyncPolicySettings FTextureShareInterface::GetSyncPolicySettings()
{
	return ShareCoreAPI().GetSyncPolicySettings(ETextureShareProcess::Client);
}

void FTextureShareInterface::SetSyncPolicySettings(const FTextureShareSyncPolicySettings& InSyncPolicySettings)
{
	ShareCoreAPI().SetSyncPolicySettings(ETextureShareProcess::Client, InSyncPolicySettings);
}
bool FTextureShareInterface::CreateTextureShare(const TCHAR* ShareName, ETextureShareProcess Process, FTextureShareSyncPolicy SyncMode, ETextureShareDevice DeviceType, float SyncWaitTime)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().CreateTextureShareItem(FString(ShareName), Process, SyncMode, DeviceType, ShareItem, SyncWaitTime);
}

bool FTextureShareInterface::ReleaseTextureShare(const TCHAR* ShareName)
{
	return ShareCoreAPI().ReleaseTextureShareItem(FString(ShareName));
}

bool FTextureShareInterface::IsValid(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->IsValid();
}

bool FTextureShareInterface::IsSessionValid(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->IsSessionValid();
}

ETextureShareDevice FTextureShareInterface::GetDeviceType(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) ? ShareItem->GetDeviceType() : ETextureShareDevice::Undefined;
}

bool FTextureShareInterface::RegisterTexture(const TCHAR* ShareName, const TCHAR* TextureName, int Width, int Height, ETextureShareFormat InFormat, uint32 InFormatValue, ETextureShareSurfaceOp OperationType)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->RegisterTexture(TextureName, FIntPoint(Width, Height), InFormat, InFormatValue, OperationType);
}

bool FTextureShareInterface::SetTextureGPUIndex(const TCHAR* ShareName, const TCHAR* TextureName, uint32 GPUIndex)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(ShareName, ShareItem) && ShareItem->SetTextureGPUIndex(TextureName, GPUIndex);
}

bool FTextureShareInterface::SetDefaultGPUIndex(const TCHAR* ShareName, uint32 GPUIndex)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(ShareName, ShareItem) && ShareItem->SetDefaultGPUIndex(GPUIndex);
}

bool FTextureShareInterface::IsRemoteTextureUsed(const TCHAR* ShareName, const TCHAR* TextureName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->IsRemoteTextureUsed(FString(TextureName));
}

bool FTextureShareInterface::BeginSession(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->BeginSession();
}

bool FTextureShareInterface::EndSession(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem))
	{
		ShareItem->EndSession();
		return true;
	}

	return false;
}

bool FTextureShareInterface::BeginFrame_RenderThread(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->BeginFrame_RenderThread();
}

bool FTextureShareInterface::EndFrame_RenderThread(const TCHAR* ShareName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->EndFrame_RenderThread();
}

bool FTextureShareInterface::UnlockTexture_RenderThread(const TCHAR* ShareName, const TCHAR* TextureName)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	return ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->UnlockTexture_RenderThread(FString(TextureName));
}

bool FTextureShareInterface::LockTextureD3D11_RenderThread(ID3D11Device* pD3D11Device, const TCHAR* ShareName, const TCHAR* TextureName, ID3D11Texture2D* &OutD3D11Texture)
{
#if TEXTURESHARELIB_USE_D3D11
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->GetD3D11())
	{
		OutD3D11Texture = ShareItem->GetD3D11()->LockTexture_RenderThread(pD3D11Device, FString(TextureName));
		return OutD3D11Texture != nullptr;
	}
#endif
	return false;
}

bool FTextureShareInterface::LockTextureD3D12_RenderThread(ID3D12Device* pD3D12Device, const TCHAR* ShareName, const TCHAR* TextureName, ID3D12Resource*& OutD3D12Resource)
{
#if TEXTURESHARELIB_USE_D3D12
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI().GetTextureShareItem(FString(ShareName), ShareItem) && ShareItem->GetD3D12())
	{
		OutD3D12Resource = ShareItem->GetD3D12()->LockTexture_RenderThread(pD3D12Device, FString(TextureName));
		return OutD3D12Resource != nullptr;
	}
#endif
	return false;
}
