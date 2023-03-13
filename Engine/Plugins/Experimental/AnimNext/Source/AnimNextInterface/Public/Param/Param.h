// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/ScriptInterface.h"
#include "Param/ParamType.h"
#include "Param/ParamTypeHandle.h"

namespace UE::AnimNext
{
struct FContext;

struct FParamStorage;

// Parameter/result/state memory wrapper
// @TODO can we save memory here with some base/derived system for fundamental types vs struct types?
struct ANIMNEXTINTERFACE_API FParam
{
public:
	enum class EFlags : uint8
	{
		None		= 0,			// No flags
		Mutable		= 1 << 0,		// Parameter is mutable, so can be mutated at runtime
		Stored		= 1 << 1,		// Parameter has to be stored on context storage

		// --- Flags added for parameter storage prototype ---
		Value		= 1 << 2,		// Parameter will be stored as a Value
		Reference	= 1 << 3,		// Parameter will be stored as a Reference (pointer)
		Embedded	= 1 << 4,		// Parameter will be stored as a Value, but stored directly on the Data pointer
	};

	// Get the type handle of this param 
	FParamTypeHandle GetTypeHandle() const { return TypeHandle; }

	// Check whether the supplied param can be written to by this param
	// Verifies that types are compatible, destination is mutable and batching matches
	bool CanAssignTo(const FParam& InParam) const;

	// Helper functions for CanAssignTo
	bool CanAssignWith(const FParamTypeHandle& InTypeHandle, EFlags InFlags, FStringBuilderBase* OutReasonPtr = nullptr) const;

	// Check whether this parameter is able to be mutated
	bool IsMutable() const { return EnumHasAnyFlags(Flags, EFlags::Mutable); }

	// Get the internal flags
	EFlags GetFlags() const { return Flags; }

	// Get an immutable view of the parameter's data
	TConstArrayView<uint8> GetData() const { return TConstArrayView<uint8>(static_cast<uint8*>(Data), Size); }

	// Get an mutable view of the parameter's data, asserts if this parameter is immutable
	TArrayView<uint8> GetMutableData() const { check(IsMutable()); return TArrayView<uint8>(static_cast<uint8*>(Data), Size); }

protected:
	friend struct FContext;
	friend struct FState;
	friend struct FParamStorage;

	FParam(const FParam* InOtherParam);
	FParam(const FParamTypeHandle& InTypeHandle, TArrayView<uint8> InData, EFlags InFlags);
	FParam(const FParamTypeHandle& InTypeHandle, TConstArrayView<uint8> InData, EFlags InFlags);
	FParam(const FParamTypeHandle& InTypeHandle, EFlags InFlags);

	// Raw ptr to the data, or the data itself if we have EFlags::Embedded
	void* Data = nullptr;

	// The type of the param
	FParamTypeHandle TypeHandle;

	// Size of the data
	uint16 Size = 0;

	// Internal flags
	EFlags Flags = EFlags::None;

public:
	// Internal use, but required for default constructed elements on containers
	FParam() = default;

	/** Duplicate a parameter into the provided memory */
	static FParam DuplicateParam(const FParam& InSource, TArrayView<uint8> InTargetMemory);
};

ENUM_CLASS_FLAGS(FParam::EFlags);

namespace Private
{

template<typename ValueType>
static bool CheckParam(const FParam& InParam)
{
	return FParamTypeHandle::GetHandle<ValueType>() == InParam.GetTypeHandle();
}

}

namespace Private
{

// Concept used to decide whether to use container-based construction of wrapped results
struct CSizedContainerWithAccessibleDataAsRawPtr
{
	template<typename ContainerType>
	auto Requires(ContainerType& Container) -> decltype(
		Container.Num(),
		Container.GetData()
	);
};

}

// A typed result which wraps the type-erased underlying param
template<typename ValueType>
struct TParam : FParam
{
public:
	FORCEINLINE_DEBUGGABLE bool IsValid() const
	{
		return ((Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded))
			&& TypeHandle.IsValid());
	}

	FORCEINLINE_DEBUGGABLE const ValueType& GetDataChecked() const
	{
		check(Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded));

		const ValueType* ValueData = EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)
			? static_cast<const ValueType*>((void*)&Data)
			: static_cast<const ValueType*>(Data);

		return *ValueData;
	}

	FORCEINLINE_DEBUGGABLE ValueType& GetDataChecked()
	{
		check(Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded));

		ValueType* ValueData = EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)
			? static_cast<ValueType*>((void*)&Data)
			: static_cast<ValueType*>(Data);

		return *ValueData;
	}

	FORCEINLINE_DEBUGGABLE operator const ValueType& () const
	{
		check((Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)));

		const ValueType* ValueData = EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)
			? static_cast<const ValueType*>((void*)&Data)
			: static_cast<const ValueType*>(Data);

		return *ValueData;
	}

	FORCEINLINE_DEBUGGABLE operator ValueType& ()
	{
		check((Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)));

		ValueType* ValueData = EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)
			? static_cast<ValueType*>((void*)&Data)
			: static_cast<ValueType*>(Data);

		return *ValueData;
	}

	FORCEINLINE_DEBUGGABLE ValueType& operator*() const
	{
		check((Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)));

		ValueType* ValueData = EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)
			? static_cast<ValueType*>((void*)&Data)
			: static_cast<ValueType*>(Data);

		return *ValueData;
	}

	FORCEINLINE_DEBUGGABLE ValueType* operator->() const
	{
		check((Data != nullptr || EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)));

		ValueType* ValueData = EnumHasAnyFlags(Flags, FParam::EFlags::Embedded)
			? static_cast<ValueType*>((void*)&Data)
			: static_cast<ValueType*>(Data);

		return ValueData;
	}

protected:
	using TValueType = ValueType;
	
	friend struct FContext;
	friend struct FState;
	friend struct FParamStorage;

	TParam()
		: FParam(FParamTypeHandle::GetHandle<ValueType>(), GetTypeFlags(EFlags::None))
	{
	}
	
	TParam(EFlags InFlags)
		: FParam(FParamTypeHandle::GetHandle<ValueType>(), InFlags)
	{
	}

	TParam(TArrayView<uint8> InData, EFlags InFlags)
		: FParam(FParamTypeHandle::GetHandle<ValueType>(), InData, InFlags)
	{
	}

	TParam(TConstArrayView<uint8> InData, EFlags InFlags)
		: FParam(FParamTypeHandle::GetHandle<ValueType>(), InData, InFlags)
	{
	}

	TParam(FParam& InParam, EFlags InAdditionalFlags = EFlags::None)
		: FParam(&InParam)
	{
#if DO_CHECK
		TStringBuilder<64> Error;
		checkf(InParam.CanAssignWith(FParamTypeHandle::GetHandle<ValueType>(), GetTypeFlags(InAdditionalFlags), &Error),
			TEXT("Cannot assign type: %s"), Error.ToString());
#endif
	}

	TParam(const FParam& InParam, EFlags InAdditionalFlags = EFlags::None)
		: FParam(&InParam)
	{
#if DO_CHECK
		TStringBuilder<64> Error;
		checkf(InParam.CanAssignWith(FParamTypeHandle::GetHandle<ValueType>(), GetTypeFlags(InAdditionalFlags), &Error),
			TEXT("Cannot assign type: %s"), Error.ToString());
#endif
	}

protected:
	static EFlags GetTypeFlags(EFlags InAdditionalFlags)
	{
		EFlags Flags = InAdditionalFlags;

		// Add const flags or not
		if constexpr (!TIsConst<ValueType>::Value)
		{
			Flags |= FParam::EFlags::Mutable;
		}

		return Flags;
	}
};

// A typed param that wraps a user ptr
template<typename ValueType>
struct TWrapParam : TParam<ValueType>
{
public:
	TWrapParam(ValueType* InValuePtrToWrap)
		: TParam<ValueType>(GetDataForConstructor(InValuePtrToWrap), GetFlagsForConstructor(InValuePtrToWrap))
	{
	}

	TWrapParam(ValueType& InValueToWrap)
		: TParam<ValueType>(GetDataForConstructor(&InValueToWrap), GetFlagsForConstructor(&InValueToWrap))
	{
	}

protected:
	TArrayView<uint8> GetDataForConstructor(ValueType* InValuePtrToWrap) const
	{
		using MutableValueType = std::remove_const_t<ValueType>;

		return TArrayView<uint8>(reinterpret_cast<uint8*>(const_cast<MutableValueType*>(InValuePtrToWrap)), sizeof(ValueType));
	}

	FParam::EFlags GetFlagsForConstructor(const ValueType* InValuePtrToWrap) const
	{
		FParam::EFlags NewFlags = FParam::EFlags::None;

		// Add const flags or not
		if constexpr (!TIsConst<ValueType>::Value)
		{
			NewFlags |= FParam::EFlags::Mutable;
		}

		return NewFlags;
	}
};

// A typed param that owns it's own memory with size defined at compile time
template<typename ValueType>
struct TParamValue : TParam<ValueType>
{
public:
	TParamValue()
		: TParam<ValueType>(TArrayView<uint8>(reinterpret_cast<uint8*>(&Value), sizeof(ValueType)), GetFlagsForConstructor())
	{
	}

	TParamValue(FParam::EFlags InFlags)
		: TParam<ValueType>(TArrayView<uint8>(reinterpret_cast<uint8*>(&Value), sizeof(ValueType)), InFlags)
	{
	}

private:
	FParam::EFlags GetFlagsForConstructor() const
	{
		FParam::EFlags NewFlags = FParam::EFlags::None;

		// Add const flags or not
		if constexpr (!TIsConst<ValueType>::Value)
		{
			NewFlags |= FParam::EFlags::Mutable;
		}

		return NewFlags;
	}

	ValueType Value;
};

template<typename ValueType>
struct TContextStorageParam : TWrapParam<ValueType>
{
public:
	TContextStorageParam(ValueType* InValuePtrToWrap)
		: TWrapParam<ValueType>(InValuePtrToWrap)
	{
		FParam::Flags |= FParam::EFlags::Stored;
	}

	TContextStorageParam(ValueType& InValueToWrap)
		: TWrapParam<ValueType>(&InValueToWrap)
	{
		FParam::Flags |= FParam::EFlags::Stored;
	}
};

struct ANIMNEXTINTERFACE_API FParamHandle
{
	using FInternalHandle = int32;
	static constexpr FInternalHandle InvalidParamHandle = -1;

	FParamHandle() = default;
	~FParamHandle();

	FParamHandle(FParamHandle&& Other)
		: FParamHandle()
	{
		Swap(*this, Other);
	}

	FParamHandle& operator= (FParamHandle&& Other)
	{
		Swap(*this, Other);
		return *this;
	}

	FParamHandle(const FParamHandle& Other);

	FParamHandle& operator= (const FParamHandle& Other)
	{
		FParamHandle Temp(Other); // note that the ref count is incremented here

		Swap(*this, Temp);

		return *this;
	}

protected:
	friend struct FContext;
	friend struct FParamStorage;

	FParamHandle(FParamStorage* InOwnerStorage, FInternalHandle InInternalHandle);

	FParamStorage* OwnerStorage = nullptr;
	FInternalHandle ParamHandle = InvalidParamHandle;
};

} // end namespace UE::AnimNext
