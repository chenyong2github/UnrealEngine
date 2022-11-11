// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UPCGMetadata;

namespace PCGMetadataHelpers
{
	bool HasSameRoot(const UPCGMetadata* Metadata1, const UPCGMetadata* Metadata2);
	const UPCGMetadata* GetParentMetadata(const UPCGMetadata* Metadata);
}