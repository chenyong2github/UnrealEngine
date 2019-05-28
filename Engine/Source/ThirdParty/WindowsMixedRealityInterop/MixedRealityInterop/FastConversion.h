// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include <DirectXMath.h>
#include <windows.graphics.directx.h>

using namespace DirectX;

static inline XMFLOAT3 ToUE4Scale(XMVECTOR InScale)
{
	InScale = XMVectorSwizzle(InScale, 2, 0, 1, 3);

	XMFLOAT3 OutScale;
	XMStoreFloat3(&OutScale, InScale);
	return OutScale;
}

static inline XMFLOAT3 ToUE4Scale(XMFLOAT4 InValue)
{
	XMVECTOR VecInValue = XMLoadFloat4(&InValue);
	return ToUE4Scale(VecInValue);
}

static inline XMFLOAT4 ToUE4Quat(XMVECTOR InQuat)
{
	static const XMFLOAT4 NegateZW(1.f, 1.f, -1.f, -1.f);
	static const XMVECTOR VecNegateZW = XMLoadFloat4(&NegateZW);

	InQuat = XMVectorMultiply(InQuat, VecNegateZW);
	InQuat = XMVectorSwizzle(InQuat, 2, 0, 1, 3);

	XMFLOAT4 OutValue;
	XMStoreFloat4(&OutValue, InQuat);
	return OutValue;
}

static inline XMFLOAT4 ToUE4Quat(XMFLOAT4 InQuat)
{
	XMVECTOR VecInQuat = XMLoadFloat4(&InQuat);
	return ToUE4Quat(VecInQuat);
}

static inline XMFLOAT3 ToUE4Translation(XMVECTOR InValue)
{
	// UE4 is in centimeters so include a multiply by 100 everywhere, as well as flip z
	static const XMFLOAT4 ScaleAndNegateZ(100.f, 100.f, -100.f, 100.f);
	static const XMVECTOR VecNegateZ = XMLoadFloat4(&ScaleAndNegateZ);

	InValue = XMVectorMultiply(InValue, VecNegateZ);
	InValue = XMVectorSwizzle(InValue, 2, 0, 1, 3);

	XMFLOAT3 OutValue;
	XMStoreFloat3(&OutValue, InValue);
	return OutValue;
}

static inline XMFLOAT3 ToUE4Translation(XMFLOAT4 InValue)
{
	XMVECTOR VecInValue = XMLoadFloat4(&InValue);
	return ToUE4Translation(VecInValue);
}

