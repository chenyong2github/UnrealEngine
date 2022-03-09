// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "IVirtualizationBackend.h"

#include "Containers/StringView.h"

class ISourceControlProvider;

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
 * 'Name'=(Type=SourceControl, DepotRoot="//XXX/", UsePartitionedClient=X, SubmitFromTempDir=X)
 * Where 'Name' is the backend name in the hierarchy and 'XXX' is the path in the source control
 * depot where the payload files are being stored.
 * 
 * Optional Values:
 * UsePartitionedClient [bool]:	When true the temporary workspace client created to submit payloads 
 *								from will be created as a partitioned workspace which is less overhead
 *								on the source control server. If your server does not support this then
 *								use false. [Default=True]
 * SubmitFromTempDir [bool]:	When set to true, payloads will be submitted from the temp directory of
 *								the current machine and when false the files will be submitted from the
 *								Save directory of the current project. [Default=false]
 * 
 * Environment Variables:
 * UE-VirtualizationWorkingDir [string]:	This can be set to a valid directory path that the backend
 *											should use as the root location to submit payloads from.
 *											If the users machine has this set then 'SubmitFromTempDir' 
 *											will be ignored. 
 */
class FSourceControlBackend final : public IVirtualizationBackend
{
public:
	explicit FSourceControlBackend(FStringView ProjectName, FStringView ConfigName, FStringView InDebugName);
	virtual ~FSourceControlBackend() = default;
	
private:
	/* IVirtualizationBackend implementation */

	virtual bool Initialize(const FString& ConfigEntry) override;
	
	virtual EPushResult PushData(const FIoHash& Id, const FCompressedBuffer& Payload, const FString& Context) override;
	virtual bool PushData(TArrayView<FPushRequest> Requests) override;

	virtual FCompressedBuffer PullData(const FIoHash& Id) override;

	virtual bool DoesPayloadExist(const FIoHash& Id) override;
	
	virtual bool DoPayloadsExist(TArrayView<const FIoHash> PayloadIds, TArray<bool>& OutResults) override;

private:

	void CreateDepotPath(const FIoHash& PayloadId, FStringBuilderBase& OutPath);

	bool FindSubmissionWorkingDir(const FString& ConfigEntry);

	/** Will display a FMessage notification to the user on the next valid engine tick to try and keep them aware of connection failures */
	void OnConnectionError();

	/** A source control connection owned by the backend*/
	TUniquePtr<ISourceControlProvider> SCCProvider;
	
	/** The name of the current project */
	FString ProjectName;

	/** The root where the virtualized payloads are stored in source control */
	FString DepotRoot;

	/** The root directory from which payloads are submitted. */
	FString SubmissionRootDir;

	/** Should we try to make the temp client partitioned or not? */
	bool bUsePartitionedClient = true;

};

} // namespace UE::Virtualization
