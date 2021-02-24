// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/AsyncFileHandle.h"
#include "DerivedDataCache.h"
#include "EditorDomain/EditorDomain.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "Misc/PackagePath.h"
#include "Serialization/Archive.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"
#include "UObject/PackageResourceManager.h"

class FAssetPackageData;

/**
 * An Archive that asynchronously waits for the cache request to complete, and reads either from the returned cache bytes
 * or from the fallback WorkspaceDomain Archive for the given PackagePath.
 *
 * This class is a serialization archive rather than a full archive; it overrides the serialization functions used by
 * LinkerLoad and BulkData but does not override all of the functions used by general Archive use as FArchiveProxy does.
 */
class FEditorDomainReadArchive : public FArchive
{
public:
	FEditorDomainReadArchive(const TRefCountPtr<FEditorDomain::FLocks>& InLocks,
		const FPackagePath& InPackagePath, const TRefCountPtr<FEditorDomain::FPackageSource>& InPackageSource);
	~FEditorDomainReadArchive();

	/** Set the CacheRequest handle that will feed this archive. */
	void SetRequest(const UE::DerivedData::FRequest& InRequest);
	/** Callback from the CacheRequest; set whether we're reading from EditorDomain bytes or WorkspaceDomain archive. */
	void OnCacheRequestComplete(UE::DerivedData::FCacheGetCompleteParams&& Params);
	/** Get the PackageFormat, which depends on the domain the data is read from. */
	EPackageFormat GetPackageFormat() const;

	// FArchive interface
	virtual void Seek(int64 InPos) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual bool Close() override;
	virtual void Serialize(void* V, int64 Length) override;
	virtual FString GetArchiveName() const override;
	virtual void Flush() override;
	virtual void FlushCache() override;
	virtual bool Precache(int64 PrecacheOffset, int64 PrecacheSize) override;

private:
	enum class ESource : uint8
	{
		Uninitialized,
		Bytes,
		Archive,
		Closed
	};

	/** Wait for the handle to call OnCacheRequestComplete and make the size and bytes available. */
	void WaitForReady() const;

	/** Lock to synchronize the CacheCompletion and the public interface thread. */
	mutable FCriticalSection Lock;

	// Data in this section is either read-only, or is read and written only on the public interface thread.
	TRefCountPtr<FEditorDomain::FLocks> EditorDomainLocks; // Pointer is read-only, *pointer has internal lock
	UE::DerivedData::FRequest Request; // Read-only
	FPackagePath PackagePath; // Read-only
	int64 Pos = 0; // Interface-thread-only
	mutable ESource Source = ESource::Uninitialized; // Interface-thread-only
	TRefCountPtr<FEditorDomain::FPackageSource> PackageSource; // Read-only, *pointer requires EditorDomainLocks.Lock

	// Data in this section is written on the callback thread.
	// It can not be read until after WaitForReady or Cancel.
	TUniquePtr<FArchive> InnerArchive;
	FSharedBuffer Bytes;
	int64 Size = 0;
	ESource AsyncSource = ESource::Uninitialized;
	EPackageFormat PackageFormat = EPackageFormat::Binary;
};

/**
 * An IAsyncReadFileHandle that asynchronously waits for the cache request to complete, and reads either from the
 * returned cache bytes or from the fallback WorkspaceDomain Archive for the given PackagePath.
 */
class FEditorDomainAsyncReadFileHandle : public IAsyncReadFileHandle
{
public:
	FEditorDomainAsyncReadFileHandle(const TRefCountPtr<FEditorDomain::FLocks>& InLocks,
		const FPackagePath& InPackagePath, const TRefCountPtr<FEditorDomain::FPackageSource>& InPackageSource);
	~FEditorDomainAsyncReadFileHandle();

	/** Set the CacheRequest handle that will feed this archive. */
	void SetRequest(const UE::DerivedData::FRequest& InRequest);
	/** Callback from the CacheRequest; set whether we're reading from EditorDomain bytes or WorkspaceDomain archive. */
	void OnCacheRequestComplete(UE::DerivedData::FCacheGetCompleteParams&& Params);

	// IAsyncReadFileHandle interface
	virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* CompleteCallback = nullptr) override;
	virtual IAsyncReadRequest* ReadRequest(int64 Offset, int64 BytesToRead,
		EAsyncIOPriorityAndFlags PriorityAndFlags = AIOP_Normal, FAsyncFileCallBack* CompleteCallback = nullptr,
		uint8* UserSuppliedMemory = nullptr) override;
	virtual bool UsesCache();

private:
	enum class ESource : uint8
	{
		Uninitialized,
		Bytes,
		Archive,
		Closed
	};

	/** Wait for the handle to call OnCacheRequestComplete and make the size and bytes available. */
	void WaitForReady() const;

	/** Lock to synchronize the CacheCompletion and the public interface thread. */
	mutable FCriticalSection Lock;

	// Data in this section is either read-only, or is read and written only on the public interface thread.
	TRefCountPtr<FEditorDomain::FLocks> EditorDomainLocks; // Pointer is read-only, *pointer has internal lock
	UE::DerivedData::FRequest Request; // Read-only
	FPackagePath PackagePath; // Read-only
	mutable ESource Source = ESource::Uninitialized; // Interface-thread-only
	TRefCountPtr<FEditorDomain::FPackageSource> PackageSource; // Read-only, *pointer requires EditorDomainLocks.Lock

	// Data in this section is written on the callback thread.
	// It can not be read until after WaitForReady or Cancel.
	TUniquePtr<IAsyncReadFileHandle> InnerArchive;
	FSharedBuffer Bytes;
	ESource AsyncSource = ESource::Uninitialized;
};