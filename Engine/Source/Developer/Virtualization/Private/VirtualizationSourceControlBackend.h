// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "IVirtualizationBackend.h"

#include "Containers/StringView.h"

namespace UE::Virtualization
{

/**
 * This backend can be used to access payloads stored in source control.
 * The backend doesn't 'check out' a payload file but instead will just download the payload as
 * a binary blob.
 * It is assumed that the files are stored with the same path convention as the file system
 * backend, found in Utils::PayloadIdToPath.
 *
 * Ini file setup:
 * 'Name'=(Type=SourceControl, DepotRoot="//XXX/")
 * Where 'Name' is the backend name in the hierarchy and 'XXX' is the path in the source control
 * depot where the payload files are being stored.
 */
class FSourceControlBackend final : public IVirtualizationBackend
{
public:
	explicit FSourceControlBackend(FStringView ConfigName, FStringView InDebugName);
	virtual ~FSourceControlBackend() = default;
	
private:
	/* IVirtualizationBackend implementation */

	virtual bool Initialize(const FString& ConfigEntry) override;
	
	virtual EPushResult PushData(const FPayloadId& Id, const FCompressedBuffer& Payload, const FPackagePath& PackageContext) override;
	
	virtual FCompressedBuffer PullData(const FPayloadId& Id) override;

	virtual bool DoesPayloadExist(const FPayloadId& Id) override;
	
	virtual bool DoPayloadsExist(TArrayView<const FPayloadId> PayloadIds, TArray<bool>& OutResults) override;

private:

	void CreateDepotPath(const FPayloadId& PayloadId, FStringBuilderBase& OutPath);

	/** The root where the virtualized payloads are stored in source control */
	FString DepotRoot;
};

} // namespace UE::Virtualization
