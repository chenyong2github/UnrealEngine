// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "CppNewSupport.h"
#include "ContextInlines.h"
#include "Debug.h"
#include "FunctionMap.h"
#include "TransactionInlines.h"
#include "Utils.h"

namespace AutoRTFM
{
#ifdef _WIN32
	using FCppNew = void* (*)(size_t);
	using FCppDeleteWithSize = void(*)(void*, size_t);
	using FCppDelete = void(*)(void*);

	FCppNew CppNew;
	FCppNew CppNewWithSize;
	FCppDeleteWithSize CppDeleteWithSize;
	FCppDelete CppDelete;
#endif

// These are also exposed as compiler ABI, currently just for the benefit of Windows.
extern "C" void* autortfm_cpp_new(size_t Size, FContext* Context)
{
    FDebug Debug(Context, nullptr, nullptr, Size, 0, __FUNCTION__);
    void* Result = CppNew(Size);

    Context->GetCurrentTransaction()->DeferUntilAbort([Result]
    {
        CppDelete(Result);
    });
    Context->DidAllocate(Result, Size);
    return Result;
}

extern "C" void autortfm_cpp_delete(void* Ptr, FContext* Context)
{
    FDebug Debug(Context, Ptr, nullptr, 0, 0, __FUNCTION__);
    if (Ptr)
    {
        Context->GetCurrentTransaction()->DeferUntilCommit([Ptr]
        {
            CppDelete(Ptr);
        });
    }
}
extern "C" void autortfm_cpp_delete_with_size(void* Ptr, size_t Size, FContext* Context)
{
    autortfm_cpp_delete(Ptr, Context);
}

#ifdef _WIN32
#endif // _WIN32

struct FRegisterCppNewDelete
{
    FRegisterCppNewDelete()
    {
#ifdef _WIN32
		CppNew = &operator new;
		CppNewWithSize = &operator new[];
		CppDeleteWithSize = &operator delete[];
		CppDelete = &operator delete;

        FunctionMapAdd(reinterpret_cast<void*>(CppNew), reinterpret_cast<void*>(autortfm_cpp_new));
        FunctionMapAdd(reinterpret_cast<void*>(CppNewWithSize), reinterpret_cast<void*>(autortfm_cpp_new));
        FunctionMapAdd(reinterpret_cast<void*>(CppDeleteWithSize), reinterpret_cast<void*>(autortfm_cpp_delete_with_size));
        FunctionMapAdd(reinterpret_cast<void*>(CppDelete), reinterpret_cast<void*>(autortfm_cpp_delete));
#else
        FunctionMapAdd(reinterpret_cast<void*>(_Znwm), reinterpret_cast<void*>(autortfm_cpp_new));
        FunctionMapAdd(reinterpret_cast<void*>(_Znam), reinterpret_cast<void*>(autortfm_cpp_new));
        FunctionMapAdd(reinterpret_cast<void*>(_ZdlPv), reinterpret_cast<void*>(autortfm_cpp_delete));
#endif
    }
};
const FRegisterCppNewDelete RegisterCppNewDelete;

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
