// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace Chaos
{
template <typename T>
class TSerializablePtr
{
public:
	TSerializablePtr() : Ptr(nullptr) {}
	explicit TSerializablePtr(const TUniquePtr<T>& Unique) : Ptr(Unique.Get()) {}
	explicit TSerializablePtr(TUniquePtr<T>&& Unique) = delete;
	
	template<ESPMode TESPMode>
	explicit TSerializablePtr(const TSharedPtr<T, TESPMode>& Shared) : Ptr(Shared.Get()) {}
	
	const T* operator->() const { return Ptr; }
	const T* Get() const { return Ptr; }
	const T& operator*() const { return *Ptr; }
	void Reset() { Ptr = nullptr; }
	bool operator!() const { return Ptr == nullptr; }
	bool operator==(const TSerializablePtr<T>& Serializable) const { return Ptr == Serializable.Ptr; }
	operator bool() const { return Ptr != nullptr; }

	template <typename R>
	operator TSerializablePtr<R>() const
	{
		const R* RCast = Ptr;
		TSerializablePtr<R> Ret;
		Ret.SetFromRawLowLevel(RCast);
		return Ret;
	}

	//NOTE: this is needed for serialization. This should NOT be used directly
	void SetFromRawLowLevel(const T* InPtr)
	{
		Ptr = InPtr;
	}

private:
	TSerializablePtr(T* InPtr) : Ptr(InPtr) {}
	const T* Ptr;
};

template <typename T>
inline uint32 GetTypeHash(const TSerializablePtr<T>& Ptr) { return ::GetTypeHash(Ptr.Get()); }

template <typename T>
TSerializablePtr<T> MakeSerializable(const TUniquePtr<T>& Unique)
{
	return TSerializablePtr<T>(Unique);
}

template <typename T>
TSerializablePtr<T> MakeSerializable(const TUniquePtr<T>&& Unique) = delete;

template<typename T, ESPMode TESPMode>
TSerializablePtr<T> MakeSerializable(const TSharedPtr<T, TESPMode>& Shared)
{
	return TSerializablePtr<T>(Shared);
}

class FChaosArchive;

}