// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"

#include "HAL/UnrealMemory.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace BufferOwnerPrivate
{

template <typename FOps>
inline TBufferOwnerPtr<FOps>::TBufferOwnerPtr(FBufferOwner* const InOwner)
	: Owner(InOwner)
{
	if (InOwner)
	{
		checkf(!FOps::HasRef(*InOwner), TEXT("FBufferOwner is referenced by another TBufferOwnerPtr. ")
			TEXT("Construct this from an existing pointer instead of a raw pointer."));
		FOps::AddRef(*InOwner);
	}
}

template <typename FOps>
inline void TBufferOwnerPtr<FOps>::Reset()
{
	FOps::Release(Owner);
	Owner = nullptr;
}

class FBufferOwnerHeap final : public FBufferOwner
{
public:
	inline explicit FBufferOwnerHeap(uint64 Size)
		: FBufferOwner(FMemory::Malloc(Size), Size)
	{
		SetIsMaterialized();
		SetIsOwned();
	}

protected:
	virtual void FreeBuffer() final
	{
		FMemory::Free(GetData());
	}
};

class FBufferOwnerView final : public FBufferOwner
{
public:
	inline FBufferOwnerView(void* Data, uint64 Size)
		: FBufferOwner(Data, Size)
	{
		SetIsMaterialized();
	}

protected:
	virtual void FreeBuffer() final
	{
	}
};

class FBufferOwnerOuterView final : public FBufferOwner
{
public:
	inline FBufferOwnerOuterView(void* Data, uint64 Size, FSharedBuffer InOuterBuffer)
		: FBufferOwner(Data, Size)
		, OuterBuffer(MoveTemp(InOuterBuffer))
	{
		check(OuterBuffer.GetView().Contains(MakeMemoryView(Data, Size)));
		SetIsMaterialized();
		if (OuterBuffer.IsOwned())
		{
			SetIsOwned();
		}
	}

protected:
	virtual void FreeBuffer() final
	{
		OuterBuffer.Reset();
	}

private:
	FSharedBuffer OuterBuffer;
};

} // BufferOwnerPrivate

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FUniqueBuffer FUniqueBuffer::Alloc(uint64 InSize)
{
	return FUniqueBuffer(new BufferOwnerPrivate::FBufferOwnerHeap(InSize));
}

FUniqueBuffer FUniqueBuffer::Clone(FMemoryView View)
{
	return Clone(View.GetData(), View.GetSize());
}

FUniqueBuffer FUniqueBuffer::Clone(const void* Data, uint64 Size)
{
	FUniqueBuffer Buffer = Alloc(Size);
	FMemory::Memcpy(Buffer.GetData(), Data, Size);
	return Buffer;
}

FUniqueBuffer FUniqueBuffer::MakeView(FMutableMemoryView View)
{
	return MakeView(View.GetData(), View.GetSize());
}

FUniqueBuffer FUniqueBuffer::MakeView(void* Data, uint64 Size)
{
	return FUniqueBuffer(new BufferOwnerPrivate::FBufferOwnerView(Data, Size));
}

FUniqueBuffer FUniqueBuffer::MakeUnique(FSharedBuffer Buffer)
{
	OwnerPtrType ExistingOwner = ToPrivateOwnerPtr(MoveTemp(Buffer));
	if (!ExistingOwner || (ExistingOwner->IsOwned() && ExistingOwner->GetTotalRefCount() == 1))
	{
		return FUniqueBuffer(MoveTemp(ExistingOwner));
	}
	else
	{
		return Clone(ExistingOwner->GetData(), ExistingOwner->GetSize());
	}
}

FUniqueBuffer::FUniqueBuffer(FBufferOwner* InOwner)
	: Owner(InOwner)
{
}

void FUniqueBuffer::Reset()
{
	Owner.Reset();
}

FUniqueBuffer FUniqueBuffer::MakeOwned() &&
{
	return IsOwned() ? MoveTemp(*this) : Clone(GetView());
}

void FUniqueBuffer::Materialize() const
{
	if (Owner)
	{
		Owner->Materialize();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FSharedBuffer FSharedBuffer::Clone(FMemoryView View)
{
	return FSharedBuffer(FUniqueBuffer::Clone(View));
}

FSharedBuffer FSharedBuffer::Clone(const void* Data, uint64 Size)
{
	return FSharedBuffer(FUniqueBuffer::Clone(Data, Size));
}

FSharedBuffer FSharedBuffer::MakeView(FMemoryView View)
{
	return MakeView(View.GetData(), View.GetSize());
}

FSharedBuffer FSharedBuffer::MakeView(FMemoryView View, FSharedBuffer OuterBuffer)
{
	if (OuterBuffer.IsNull())
	{
		return MakeView(View);
	}
	if (View == OuterBuffer.GetView())
	{
		return MoveTemp(OuterBuffer);
	}
	return FSharedBuffer(new BufferOwnerPrivate::FBufferOwnerOuterView(
		const_cast<void*>(View.GetData()), View.GetSize(), MoveTemp(OuterBuffer)));
}

FSharedBuffer FSharedBuffer::MakeView(const void* Data, uint64 Size)
{
	return FSharedBuffer(new BufferOwnerPrivate::FBufferOwnerView(const_cast<void*>(Data), Size));
}

FSharedBuffer FSharedBuffer::MakeView(const void* Data, uint64 Size, FSharedBuffer OuterBuffer)
{
	return MakeView(MakeMemoryView(Data, Size), MoveTemp(OuterBuffer));
}

FSharedBuffer::FSharedBuffer(FBufferOwner* InOwner)
	: Owner(InOwner)
{
}

FSharedBuffer::FSharedBuffer(const BufferOwnerPrivate::TBufferOwnerPtr<BufferOwnerPrivate::FWeakOps>& WeakOwner)
	: Owner(WeakOwner)
{
}

void FSharedBuffer::Reset()
{
	Owner.Reset();
}

FSharedBuffer FSharedBuffer::MakeOwned() const &
{
	return IsOwned() ? *this : Clone(GetView());
}

FSharedBuffer FSharedBuffer::MakeOwned() &&
{
	return IsOwned() ? MoveTemp(*this) : Clone(GetView());
}

void FSharedBuffer::Materialize() const
{
	if (Owner)
	{
		Owner->Materialize();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FWeakSharedBuffer::FWeakSharedBuffer(const FSharedBuffer& Buffer)
	: Owner(ToPrivateOwnerPtr(Buffer))
{
}

FWeakSharedBuffer& FWeakSharedBuffer::operator=(const FSharedBuffer& Buffer)
{
	Owner = ToPrivateOwnerPtr(Buffer);
	return *this;
}

void FWeakSharedBuffer::Reset()
{
	Owner.Reset();
}

FSharedBuffer FWeakSharedBuffer::Pin() const
{
	return FSharedBuffer(Owner);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
