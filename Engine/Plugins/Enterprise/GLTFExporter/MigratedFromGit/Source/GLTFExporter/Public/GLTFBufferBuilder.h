// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "Engine.h"

struct FGLTFContainerBuilder;

struct GLTFEXPORTER_API FGLTFBufferBuilder
{
	FGLTFJsonBufferIndex BufferIndex;
	TArray<uint8> BufferData;

	FGLTFBufferBuilder(FGLTFJsonBufferIndex BufferIndex);

	FGLTFJsonBufferViewIndex AddBufferView(FGLTFContainerBuilder& Container, const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget);

	void UpdateBuffer(FGLTFJsonBuffer& JsonBuffer);
};
