// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Compression/CompressedBuffer.h"
#include "Compression/OodleDataCompression.h"

namespace UE::DerivedData
{

constexpr ECompressedBufferCompressor GDefaultCompressor = ECompressedBufferCompressor::Mermaid;
constexpr ECompressedBufferCompressionLevel GDefaultCompressionLevel = ECompressedBufferCompressionLevel::VeryFast;

} // UE::DerivedData
