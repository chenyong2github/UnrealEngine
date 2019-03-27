// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EncoderDevice.h"

#if PLATFORM_WINDOWS

FEncoderDevice::FEncoderDevice()
{
	if (GDynamicRHI)
	{
		ID3D11Device* D3D11Device = GetUE4DxDevice();

		IDXGIDevice* DXGIDevice = nullptr;
		CHECK_HR_VOID(D3D11Device->QueryInterface(__uuidof(IDXGIDevice), (LPVOID*)&DXGIDevice));

		IDXGIAdapter* Adapter;
		CHECK_HR_VOID(DXGIDevice->GetAdapter(&Adapter));

		D3D_DRIVER_TYPE DriverType = D3D_DRIVER_TYPE_UNKNOWN;
		uint32 DeviceFlags = D3D11Device->GetCreationFlags();
		D3D_FEATURE_LEVEL FeatureLevel = D3D11Device->GetFeatureLevel();
		D3D_FEATURE_LEVEL ActualFeatureLevel;

		CHECK_HR_VOID(D3D11CreateDevice(
			Adapter,
			DriverType,
			NULL,
			DeviceFlags,
			&FeatureLevel,
			1,
			D3D11_SDK_VERSION,
			Device.GetInitReference(),
			&ActualFeatureLevel,
			DeviceContext.GetInitReference()
		));

		DXGIDevice->Release();
	}
	else
	{
		UE_LOG(GameplayMediaEncoder, Error, TEXT("Attempting to create Encoder Device without existing RHI"));
	}
}

#endif

