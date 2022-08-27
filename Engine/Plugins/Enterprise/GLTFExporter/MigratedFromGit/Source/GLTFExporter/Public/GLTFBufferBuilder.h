// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFBufferBuilder
{
	FGLTFJsonRoot& JsonRoot;

	FGLTFJsonBufferIndex MergedBufferIndex;
	TArray<uint8> MergedBufferData;

	FGLTFBufferBuilder(FGLTFJsonRoot& JsonRoot);

	FGLTFJsonBufferViewIndex AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget);

	void UpdateMergedBuffer();
};
