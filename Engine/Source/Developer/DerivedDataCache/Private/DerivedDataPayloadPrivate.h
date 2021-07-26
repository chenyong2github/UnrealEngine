// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Compression/OodleDataCompression.h"

namespace UE::DerivedData
{

constexpr FOodleDataCompression::ECompressor GDefaultCompressor = FOodleDataCompression::ECompressor::Mermaid;
constexpr FOodleDataCompression::ECompressionLevel GDefaultCompressionLevel = FOodleDataCompression::ECompressionLevel::VeryFast;

} // UE::DerivedData
