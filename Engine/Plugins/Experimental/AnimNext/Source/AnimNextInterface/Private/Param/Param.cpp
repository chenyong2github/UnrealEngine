// Copyright Epic Games, Inc. All Rights Reserved.

#include "Param/Param.h"
#include "Param/ParamStorage.h"
#include "Param/ParamHelpers.h"

namespace UE::AnimNext
{

FParam::FParam(const FParam* InOtherParam)
	: Data(EnumHasAnyFlags(InOtherParam->Flags, EFlags::Embedded) ? (void*)&InOtherParam->Data : InOtherParam->Data)
	, TypeHandle(InOtherParam->TypeHandle)
	, Size(InOtherParam->Size)
	, Flags(InOtherParam->Flags)
{
	EnumRemoveFlags(Flags, EFlags::Embedded); // For compatibility reasons, I can not keep the embedded value if we create a copy of the FParam

	check(TypeHandle.IsValid());
	check(Data);
	check(Size);
}

FParam::FParam(const FParamTypeHandle& InTypeHandle, TArrayView<uint8> InData, EFlags InFlags)
	: Data(InData.GetData())
	, TypeHandle(InTypeHandle)
	, Size(InData.Num())
	, Flags(InFlags)
{
	check(TypeHandle.IsValid());
	check(Data);
	check(InData.Num() > 0 && InData.Num() < 0xffff);
}

FParam::FParam(const FParamTypeHandle& InTypeHandle, TConstArrayView<uint8> InData, EFlags InFlags)
	: Data(const_cast<uint8*>(InData.GetData()))
	, TypeHandle(InTypeHandle)
	, Size(InData.Num())
	, Flags(InFlags)
{
	check(TypeHandle.IsValid());
	check(Data);
	check(InData.Num() > 0 && InData.Num() < 0xffff);
}

FParam::FParam(const FParamTypeHandle& InTypeHandle, EFlags InFlags)
	: Data(nullptr)
	, TypeHandle(InTypeHandle)
	, Size(0)
	, Flags(InFlags)
{
	check(TypeHandle.IsValid());
}

bool FParam::CanAssignTo(const FParam& InParam) const
{
	return CanAssignWith(InParam.GetTypeHandle(), InParam.Flags);
}

bool FParam::CanAssignWith(const FParamTypeHandle& InTypeHandle, EFlags InFlags, FStringBuilderBase* OutReasonPtr) const
{
	// Check type
	if(TypeHandle != InTypeHandle)
	{
		if(OutReasonPtr)
		{
			OutReasonPtr->Appendf(TEXT("Types do not match: %s and %s"), *TypeHandle.ToString(), *InTypeHandle.ToString());
		}
		return false;
	}
	
	// Check mutability - we cannot return a mutable version of an immutable param
	if(!EnumHasAnyFlags(Flags, EFlags::Mutable) && EnumHasAnyFlags(InFlags, EFlags::Mutable))
	{
		if(OutReasonPtr)
		{
			OutReasonPtr->Append(TEXT("Cannot assign to an immutable parameter"));
		}
		return false;
	}

	return true;
}

FParam FParam::DuplicateParam(const FParam& InSource, TArrayView<uint8> InTargetMemory)
{
	const FParamTypeHandle ParamType = InSource.GetTypeHandle();

	FParamHelpers::Copy(ParamType, ParamType, InSource.GetMutableData(), InTargetMemory);

	return FParam(ParamType, InTargetMemory, InSource.GetFlags());
}

FParamHandle::FParamHandle(FParamStorage* InOwnerStorage, FInternalHandle InParamHandle)
	: OwnerStorage(InOwnerStorage)
	, ParamHandle(InParamHandle)
{
}

FParamHandle::FParamHandle(const FParamHandle& Other)
	: OwnerStorage(Other.OwnerStorage)
	, ParamHandle(Other.ParamHandle)
{
	if (ParamHandle != InvalidParamHandle)
	{
		OwnerStorage->IncRefCount(ParamHandle);
	}
}

FParamHandle::~FParamHandle()
{
	if (ParamHandle != InvalidParamHandle)
	{
		check(OwnerStorage != nullptr);
		OwnerStorage->DecRefCount(ParamHandle);
	}
}

} // end namespace UE::AnimNext
