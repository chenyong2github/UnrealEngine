// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Virtualization/IVirtualizationBackend.h"

namespace UE::Virtualization
{
/**
 * A basic backend based on the file system. This can be used to access/store virtualization
 * data either on a local disk or a network share. It is intended to be used as a caching system
 * to speed up operations (running a local cache or a shared cache for a site) rather than as the
 * proper backend solution.
 *
 * Ini file setup:
 * 'Name'=(Type=FileSystem, Path="XXX")
 * Where 'Name' is the backend name in the hierarchy and 'XXX' if the path to the directory where
 * you want to files to be stored.
 */
class FFileSystemBackend : public IVirtualizationBackend
{
public:
	FFileSystemBackend(FStringView ConfigName);
	virtual ~FFileSystemBackend() = default;

protected:

	virtual bool Initialize(const FString& ConfigEntry) override;

	virtual EPushResult PushData(const FPayloadId& Id, const FCompressedBuffer& Payload) override;

	virtual FCompressedBuffer PullData(const FPayloadId& Id) override;

	virtual FString GetDebugString() const override;

	bool DoesExist(const FPayloadId& Id);

	void CreateFilePath(const FPayloadId& PayloadId, FStringBuilderBase& OutPath);

	FString Name;
	FString RootDirectory;
};

} // namespace UE::Virtualization