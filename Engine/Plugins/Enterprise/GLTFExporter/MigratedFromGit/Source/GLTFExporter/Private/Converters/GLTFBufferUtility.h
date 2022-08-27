// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLTFBufferUtility
{
	static bool GetAllowCPUAccess(const FRawStaticIndexBuffer* IndexBuffer);
	static bool GetAllowCPUAccess(const FPositionVertexBuffer* VertexBuffer);
	static bool GetAllowCPUAccess(const FColorVertexBuffer* VertexBuffer);

	static const void* GetBufferData(const FSkinWeightDataVertexBuffer* VertexBuffer);
	static const void* GetBufferData(const FSkinWeightLookupVertexBuffer* VertexBuffer);

	static void ReadRHIBuffer(FRHIIndexBuffer* SourceBuffer, TArray<uint8>& OutData);
	static void ReadRHIBuffer(FRHIVertexBuffer* SourceBuffer, TArray<uint8>& OutData);
};
