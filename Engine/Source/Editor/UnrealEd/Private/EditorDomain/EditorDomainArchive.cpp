// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDomain/EditorDomainArchive.h"

#include "AssetRegistry/AssetData.h"
#include "Async/AsyncFileHandleNull.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheRecord.h"
#include "EditorDomain/EditorDomainSave.h"
#include "HAL/UnrealMemory.h"
#include "Memory/SharedBuffer.h"
#include "Misc/AssertionMacros.h"
#include "Misc/PackageSegment.h"
#include "Misc/ScopeLock.h"
#include "Serialization/CompactBinary.h"

FEditorDomainReadArchive::FEditorDomainReadArchive(const TRefCountPtr<FEditorDomain::FLocks>& InLocks,
	const FPackagePath& InPackagePath, const TRefCountPtr<FEditorDomain::FPackageSource>& InPackageSource)
	: EditorDomainLocks(InLocks)
	, PackagePath(InPackagePath)
	, PackageSource(InPackageSource)
{
	this->SetIsLoading(true);
	this->SetIsPersistent(true);
}

FEditorDomainReadArchive::~FEditorDomainReadArchive()
{
	Close();
}

void FEditorDomainReadArchive::SetRequest(const UE::DerivedData::FRequest& InRequest)
{
	Request = InRequest;
}

void FEditorDomainReadArchive::Seek(int64 InPos)
{
	switch (Source)
	{
	case ESource::Archive:
		InnerArchive->Seek(InPos);
		break;
	default:
		Pos = InPos;
		break;
	}
}

int64 FEditorDomainReadArchive::Tell()
{
	switch (Source)
	{
	case ESource::Archive:
		return InnerArchive->Tell();
	default:
		return Pos;
	}
}

int64 FEditorDomainReadArchive::TotalSize()
{
	WaitForReady();
	return Size;
}

bool FEditorDomainReadArchive::Close()
{
	{
		FScopeLock ScopeLock(&Lock);
		if (AsyncSource == ESource::Uninitialized)
		{
			AsyncSource = ESource::Closed;
		}
	}
	Request.Cancel();
	InnerArchive.Reset();
	Bytes.Reset();
	Source = ESource::Closed;
	return true;
}

void FEditorDomainReadArchive::Serialize(void* V, int64 Length)
{
	switch (Source)
	{
	case ESource::Uninitialized:
		WaitForReady();
		check(Source != ESource::Uninitialized);
		Serialize(V, Length);
		break;
	case ESource::Bytes:
		if (Pos + Length > Size)
		{
			SetError();
			UE_LOG(LogEditorDomain, Error, TEXT("Requested read of %d bytes when %d bytes remain (file=%s, size=%d)"),
				Length, Size - Pos, *PackagePath.GetDebugName(), Size);
			return;
		}
		FMemory::Memcpy(V, static_cast<const uint8*>(Bytes.GetData()) + Pos, Length);
		Pos += Length;
		break;
	case ESource::Archive:
		InnerArchive->Serialize(V, Length);
		break;
	case ESource::Closed:
		UE_LOG(LogEditorDomain, Error, TEXT("Requested read after close (file=%s)"), *PackagePath.GetDebugName());
		break;
	default:
		checkNoEntry();
		break;
	}
}

FString FEditorDomainReadArchive::GetArchiveName() const
{
	return PackagePath.GetDebugName();
}

void FEditorDomainReadArchive::Flush()
{
	switch (Source)
	{
	case ESource::Uninitialized:
		WaitForReady();
		check(Source != ESource::Uninitialized);
		Flush();
		break;
	case ESource::Archive:
		InnerArchive->Flush();
		break;
	default:
		break;
	}
}

void FEditorDomainReadArchive::FlushCache()
{
	switch (Source)
	{
	case ESource::Uninitialized:
		WaitForReady();
		check(Source != ESource::Uninitialized);
		FlushCache();
		break;
	case ESource::Archive:
		InnerArchive->FlushCache();
		break;
	default:
		break;
	}
}

bool FEditorDomainReadArchive::Precache(int64 PrecacheOffset, int64 PrecacheSize)
{
	switch (Source)
	{
	case ESource::Uninitialized:
		WaitForReady();
		check(Source != ESource::Uninitialized);
		return Precache(PrecacheOffset, PrecacheSize);
	case ESource::Archive:
		return InnerArchive->Precache(PrecacheOffset, PrecacheSize);
	default:
		return true;
	}
}

EPackageFormat FEditorDomainReadArchive::GetPackageFormat() const
{
	WaitForReady();
	return PackageFormat;
}


void FEditorDomainReadArchive::OnCacheRequestComplete(UE::DerivedData::FCacheGetCompleteParams&& Params)
{
	FScopeLock ScopeLock(&Lock);
	if (AsyncSource == ESource::Closed)
	{
		return;
	}

	check(AsyncSource == ESource::Uninitialized);
	FScopeLock DomainScopeLock(&EditorDomainLocks->Lock);
	if ((PackageSource->Source == FEditorDomain::EPackageSource::Undecided || PackageSource->Source == FEditorDomain::EPackageSource::Editor) &&
		Params.Status == UE::DerivedData::EStatus::Ok)
	{
		const FCbObject& MetaData = Params.Record.GetMeta();
		int64 FileSize = MetaData["FileSize"].AsInt64(-1);
		FSharedBuffer LocalBytes = Params.Record.GetValue();
		if (static_cast<int64>(LocalBytes.GetSize()) != FileSize)
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("Package %s received invalid record from EditorDomainPackage table ")
				TEXT("with blob size %" INT64_FMT " not equal to FileSize in metadata %" INT64_FMT)
				TEXT(". Reading from workspace domain instead."),
				*PackagePath.GetDebugName(), static_cast<int64>(LocalBytes.GetSize()), FileSize);
		}
		else
		{
			AsyncSource = ESource::Bytes;
			Size = FileSize;
			LocalBytes.MakeOwned();
			Bytes = MoveTemp(LocalBytes);
			PackageFormat = EPackageFormat::Binary;
			PackageSource->Source = FEditorDomain::EPackageSource::Editor;
		}
	}
	if (AsyncSource == ESource::Uninitialized)
	{
		checkf(PackageSource->Source == FEditorDomain::EPackageSource::Undecided || PackageSource->Source == FEditorDomain::EPackageSource::Workspace,
			TEXT("%s was previously loaded from the EditorDomain but now is unavailable."), *PackagePath.GetDebugName());
		if (EditorDomainLocks->Owner)
		{
			FEditorDomain& EditorDomain(*EditorDomainLocks->Owner);
			IPackageResourceManager& Workspace = *EditorDomain.Workspace;
			EditorDomain.SaveClient->RequestSave(PackagePath);
			FOpenPackageResult Result = Workspace.OpenReadPackage(PackagePath, EPackageSegment::Header);
			if (Result.Archive)
			{
				PackageSource->Source = FEditorDomain::EPackageSource::Workspace;
				InnerArchive = MoveTemp(Result.Archive);
				AsyncSource = ESource::Archive;
				Size = InnerArchive->TotalSize();
				PackageFormat = Result.Format;
			}
			else
			{
				UE_LOG(LogEditorDomain, Warning, TEXT("%s could not be read from WorkspaceDomain. Archive Set to Error."),
					*PackagePath.GetDebugName());
				AsyncSource = ESource::Bytes;
				Size = 0;
				PackageFormat = EPackageFormat::Binary;
				SetError();
			}
		}
		else
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("%s read after EditorDomain shutdown. Archive Set to Error."),
				*PackagePath.GetDebugName());
			AsyncSource = ESource::Bytes;
			Size = 0;
			PackageFormat = EPackageFormat::Binary;
			SetError();
		}
	}
}

void FEditorDomainReadArchive::WaitForReady() const
{
	if (Source != ESource::Uninitialized)
	{
		return;
	}
	Request.Wait();
	{
		// Even though we know that the asynchronous task has left the critical section,
		// we still need to synchronize the memory order.
		// Entering the critical section will activate the equivalent of std::memory_order::acquire that we need.
		FScopeLock ReceiverScopeLock(&Lock);
		Source = AsyncSource;
	}

	switch (Source)
	{
	case ESource::Archive:
		// Copy local variables over to InnerArchive
		if (Pos != 0)
		{
			InnerArchive->Seek(Pos);
		}
		break;
	case ESource::Bytes:
		break;
	default:
		check(false);
		break;
	}
}

/** An IAsyncReadRequest SizeRequest that returns a value known at construction time. */
class FAsyncSizeRequestConstant : public IAsyncReadRequest
{
public:
	FAsyncSizeRequestConstant(int64 InSize, FAsyncFileCallBack* InCallback)
		: IAsyncReadRequest(InCallback, true /* bInSizeRequest */, nullptr /* UserSuppliedMemory */)
	{
		Size = InSize;
		SetComplete();
	}

protected:
	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
	}

	virtual void CancelImpl() override
	{
	}
};

/** An IAsyncReadRequest that reads from a SharedBuffer that was already populated at construction time. */
class FAsyncReadRequestConstant : public IAsyncReadRequest
{
public:
	FAsyncReadRequestConstant(const FSharedBuffer& Bytes, FAsyncFileCallBack* InCallback, int64 Offset,
		int64 BytesToRead, uint8* UserSuppliedMemory, const FPackagePath& PackagePath)
		: IAsyncReadRequest(InCallback, false /* bInSizeRequest */, UserSuppliedMemory)
	{
		if (Offset < 0 || BytesToRead < 0 || Offset + BytesToRead > static_cast<int64>(Bytes.GetSize()))
		{
			UE_LOG(LogEditorDomain, Fatal, TEXT("FAsyncReadRequestConstant bogus request Offset = %" INT64_FMT)
				TEXT(" BytesToRead = %" INT64_FMT " Bytes.GetSize() == %" UINT64_FMT " File = %s"),
				Offset, BytesToRead, Bytes.GetSize(), *PackagePath.GetDebugName());
		}
		if (!UserSuppliedMemory)
		{
			Memory = (uint8*)FMemory::Malloc(BytesToRead);
		}
		check(Memory);
		FMemory::Memcpy(Memory, reinterpret_cast<const uint8*>(Bytes.GetData()) + Offset, BytesToRead);
		SetComplete();
	}

protected:
	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
	}

	virtual void CancelImpl() override
	{
	}
};


FEditorDomainAsyncReadFileHandle::FEditorDomainAsyncReadFileHandle(const TRefCountPtr<FEditorDomain::FLocks>& InLocks,
	const FPackagePath& InPackagePath, const TRefCountPtr<FEditorDomain::FPackageSource>& InPackageSource)
	: EditorDomainLocks(InLocks)
	, PackagePath(InPackagePath)
	, PackageSource(InPackageSource)
{
}

FEditorDomainAsyncReadFileHandle::~FEditorDomainAsyncReadFileHandle()
{
	{
		FScopeLock ScopeLock(&Lock);
		if (AsyncSource == ESource::Uninitialized)
		{
			AsyncSource = ESource::Closed;
		}
	}
	Request.Cancel();
	Source = ESource::Closed;
}

void FEditorDomainAsyncReadFileHandle::SetRequest(const UE::DerivedData::FRequest& InRequest)
{
	Request = InRequest;
}

IAsyncReadRequest* FEditorDomainAsyncReadFileHandle::SizeRequest(FAsyncFileCallBack* CompleteCallback)
{
	switch (Source)
	{
	case ESource::Uninitialized:
		WaitForReady();
		check(Source != ESource::Uninitialized);
		return SizeRequest(CompleteCallback);
	case ESource::Bytes:
		return new FAsyncSizeRequestConstant(Bytes.GetSize(), CompleteCallback);
	case ESource::Archive:
		return InnerArchive->SizeRequest(CompleteCallback);
	default:
		checkNoEntry();
		return nullptr;
	}
}

IAsyncReadRequest* FEditorDomainAsyncReadFileHandle::ReadRequest(int64 Offset, int64 BytesToRead,
	EAsyncIOPriorityAndFlags PriorityAndFlags, FAsyncFileCallBack* CompleteCallback,
	uint8* UserSuppliedMemory)
{
	switch (Source)
	{
	case ESource::Uninitialized:
		WaitForReady();
		check(Source != ESource::Uninitialized);
		return ReadRequest(Offset, BytesToRead, PriorityAndFlags, CompleteCallback, UserSuppliedMemory);
	case ESource::Bytes:
		return new FAsyncReadRequestConstant(Bytes, CompleteCallback, Offset, BytesToRead, UserSuppliedMemory, PackagePath);
	case ESource::Archive:
		return InnerArchive->ReadRequest(Offset, BytesToRead, PriorityAndFlags, CompleteCallback, UserSuppliedMemory);
	default:
		checkNoEntry();
		return nullptr;
	}
}

bool FEditorDomainAsyncReadFileHandle::UsesCache()
{
	return false;
}

void FEditorDomainAsyncReadFileHandle::OnCacheRequestComplete(UE::DerivedData::FCacheGetCompleteParams&& Params)
{
	FScopeLock ScopeLock(&Lock);
	if (AsyncSource == ESource::Closed)
	{
		return;
	}

	check(AsyncSource == ESource::Uninitialized);
	FScopeLock DomainScopeLock(&EditorDomainLocks->Lock);
	if ((PackageSource->Source == FEditorDomain::EPackageSource::Undecided || PackageSource->Source == FEditorDomain::EPackageSource::Editor) &&
		Params.Status == UE::DerivedData::EStatus::Ok)
	{
		const FCbObject& MetaData = Params.Record.GetMeta();
		int64 FileSize = MetaData["FileSize"].AsInt64(-1);
		FSharedBuffer LocalBytes = Params.Record.GetValue();
		if (static_cast<int64>(LocalBytes.GetSize()) != FileSize)
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("Package %s received invalid record from EditorDomainPackage ")
				TEXT("table with blob size %" INT64_FMT "not equal to FileSize in metadata %" INT64_FMT)
				TEXT(". Reading from workspace domain instead."),
				*PackagePath.GetDebugName(), static_cast<int64>(LocalBytes.GetSize()), FileSize);
		}
		else
		{
			AsyncSource = ESource::Bytes;
			LocalBytes.MakeOwned();
			Bytes = MoveTemp(LocalBytes);
			PackageSource->Source = FEditorDomain::EPackageSource::Editor;
		}
	}
	if (AsyncSource == ESource::Uninitialized)
	{
		checkf(PackageSource->Source == FEditorDomain::EPackageSource::Undecided || PackageSource->Source == FEditorDomain::EPackageSource::Workspace,
			TEXT("%s was previously loaded from the EditorDomain but now is unavailable."), *PackagePath.GetDebugName());
		if (EditorDomainLocks->Owner)
		{
			FEditorDomain& EditorDomain(*EditorDomainLocks->Owner);
			IPackageResourceManager& Workspace = *EditorDomain.Workspace;
			EditorDomain.SaveClient->RequestSave(PackagePath);
			IAsyncReadFileHandle* Result = Workspace.OpenAsyncReadPackage(PackagePath, EPackageSegment::Header);
			check(Result);
			AsyncSource = ESource::Archive;
			InnerArchive.Reset(Result);
			PackageSource->Source = FEditorDomain::EPackageSource::Workspace;
		}
		else
		{
			UE_LOG(LogEditorDomain, Warning, TEXT("%s read after EditorDomain shutdown. Returning null archive"),
				*PackagePath.GetDebugName());
			AsyncSource = ESource::Archive;
			InnerArchive.Reset(new FAsyncReadFileHandleNull());
		}
	}
}

void FEditorDomainAsyncReadFileHandle::WaitForReady() const
{
	if (Source != ESource::Uninitialized)
	{
		return;
	}
	Request.Wait();
	{
		// Even though we know that the asynchronous task has left the critical section, we still need to synchronize the memory order
		// Entering the critical section will activate the equivalent of std::memory_order::acquire that we need
		FScopeLock ReceiverScopeLock(&Lock);
		Source = AsyncSource;
	}
}
