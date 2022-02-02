// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IVirtualizationBackend.h"

namespace UE::Utility { struct FRequestPool; }
namespace UE::Utility { struct FAccessToken; }

namespace UE::Virtualization
{
/**
* NOTE: Although this backend can be used to store data directly in Horde storage, it is much better to use 
* UE::Virtualization::FDDCBackend with a Zen enabled DDC instead. Due to this reason FHttpBackend will most
* likely be deprecated in UE 5.1 and is only provided for experimentation purposes.
* 
* This backend allows data to be stored in and retrieved from the Horde storage service.
*
* Ini file setup:
* 'Name'=(Type=HordeStorage, Host="", Namespace="", ChunkSize=, OAuthProvider="", OAuthClientId="", OAuthSecret="")
* Host:				The URL of the service, use http://localhost if hosted locally.
* Namespace:		Horde storage is divided into a number of namespaces allowing projects to keep their data separate
*					while using the same service. This value controls which name space will be used.
* ChunkSize:		Each payload can be divided into a number of chunks when being uploaded to Horde to improve upload
*					performance, this value sets the max size (in bytes) of each chunk. To disable and attempt to upload
*					each payload as a single data blob, set this to -1.
* OAuthProvider:	Url of the OAuth authorization server.
* OAuthClientId:	Public identifier for use with the OAuth authorization server.
* OAuthSecret:		Password for the OAuthClientId
* (Note that the OAuth entries are not required if hosting locally)
*/
class FHttpBackend final : public IVirtualizationBackend
{
public:
	explicit FHttpBackend(FStringView ConfigName, FStringView DebugName);
	virtual ~FHttpBackend() = default;

private:
	/* IVirtualizationBackend implementation */

	virtual bool Initialize(const FString& ConfigEntry) override;

	virtual EPushResult PushData(const FIoHash& Id, const FCompressedBuffer& CompressedPayload, const FString& PackageContext) override;

	virtual FCompressedBuffer PullData(const FIoHash& Id) override;

	virtual bool DoesPayloadExist(const FIoHash& Id) override;

private:

	bool IsUsingLocalHost() const;
	bool IsServiceReady() const;
	bool AcquireAccessToken();

	/**
	 * Request the status of the service that we are connected to and make sure that it supports the
	 * feature set we need and meets our minimum version requirements.
	 */
	bool ValidateServiceVersion();

	bool ShouldRetryOnError(int64 ResponseCode);

	bool PostChunk(const TArrayView<const uint8>& ChunkData, const FIoHash& PayloadId, FString& OutHashAsString);
	bool PullChunk(const FString& Hash, const FIoHash& PayloadId, uint8* DataPtr, int64 BufferSize);
	bool DoesChunkExist(const FString& Hash);

	/** Address of the service*/
	FString HostAddress;
	/** Namespace to connect to */
	FString Namespace;
	/** Europa allows us to organize the payloads by bucket. Currently this is not exposed and just set to 'default' */
	FString Bucket;

	/** The max size of each payload chunk */
	uint64 ChunkSize;

	/** Url of the OAuth authorization server */
	FString OAuthProvider;
	/**  Public identifier for use with the OAuth authorization server */
	FString OAuthClientId;
	/** Password for the OAuthClientId */
	FString OAuthSecret;

	/** The pool of FRequest objects that can be recycled */
	TUniquePtr<Utility::FRequestPool> RequestPool;

	/** Critical section used to protect the creation of new access tokens */
	FCriticalSection AccessCs;
	/** The access token used with service authorization */
	TUniquePtr<Utility::FAccessToken> AccessToken;
	/** Count how many times a login has failed since the last successful login */
	uint32 FailedLoginAttempts;
};

} // namespace UE::Virtualization
