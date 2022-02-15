// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnsyncCore.h"

namespace unsync {

struct FIOReaderStream;
struct FVectorStreamOut;

bool LoadBlocks(FGenericBlockArray& OutBlocks, uint32& OutBlockSize, const fs::path& Filename);
bool SaveBlocks(const std::vector<FBlock128>& Blocks, uint32 BlockSize, const fs::path& Filename);

bool LoadDirectoryManifest(FDirectoryManifest& OutManifest, const fs::path& Root, FIOReaderStream& Stream);
bool LoadDirectoryManifest(FDirectoryManifest& OutManifest, const fs::path& Root, const fs::path& Filename);

bool SaveDirectoryManifest(const FDirectoryManifest& Manifest, FVectorStreamOut& Stream);
bool SaveDirectoryManifest(const FDirectoryManifest& Manifest, const fs::path& Filename);

}  // namespace unsync
