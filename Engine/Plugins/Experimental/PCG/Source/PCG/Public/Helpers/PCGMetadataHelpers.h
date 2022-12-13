// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UPCGMetadata;

namespace PCGMetadataHelpers
{
	PCG_API bool HasSameRoot(const UPCGMetadata* Metadata1, const UPCGMetadata* Metadata2);
	PCG_API const UPCGMetadata* GetParentMetadata(const UPCGMetadata* Metadata);
}