// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

using namespace Windows::Storage::Streams;

template <typename T = byte>
T* GetDataFromIBuffer(Windows::Storage::Streams::IBuffer^ InBuffer)
{
	if (InBuffer == nullptr)
	{
		return nullptr;
	}

	ComPtr<IUnknown> Unknown = reinterpret_cast<IUnknown*>(InBuffer);
	ComPtr<IBufferByteAccess> ByteAccess;
	HRESULT hr = Unknown.As(&ByteAccess);
	if (FAILED(hr))
	{
		return nullptr;
	}

	byte* RawData = nullptr;
	hr = ByteAccess->Buffer(&RawData);
	if (FAILED(hr))
	{
		return nullptr;
	}

	return reinterpret_cast<T*>(RawData);
}
