// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)
#define UE_AUTORTFM 1
#else
#define UE_AUTORTFM 0
#endif

#if UE_AUTORTFM
	#define UE_PRAGMA_AUTORTFM _Pragma("autortfm")
#else
	#define UE_PRAGMA_AUTORTFM
#endif

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The C API exists for two reasons:
//
// - It makes linking easy. AutoRTFM has to deal with a weird kind of linking
//   where the compiler directly emits calls to functions with a given name.
//   It's easiest to do that in llvm if the functions have C linkage and C ABI.
//
// - It makes testing easy. Even seemingly simple C++ code introduces pitfalls
//   for AutoRTFM. So very focused tests work best when written in C.
//
// We use snake_case for C API surface area to make it easy to distinguish.

// This must match AutoRTFM::ETransactionResult.
typedef enum
{
    autortfm_aborted_by_request,
    autortfm_aborted_by_language,
    autortfm_committed
} autortfm_result;

// This must match AutoRTFM::EContextStatus.
typedef enum
{
	autortfm_status_idle,
	autortfm_status_ontrack,
	autortfm_status_aborted_by_failed_lock_aquisition,
	autortfm_status_aborted_by_language,
	autortfm_status_aborted_by_request
} autortfm_status;

// Tells if we are currently running in a transaction. This will return true in an
// open nest (see autortfm_open).
#if UE_AUTORTFM
bool autortfm_is_transactional(void);
#else
inline bool autortfm_is_transactional(void)
{
    return false;
}
#endif

// Tells if we are currently running in the closed nest of a transaction. By
// default, transactional code is in a closed nest; the only way to be in an open
// nest is to request it via autortfm_open.
//
// The advantages of this function over autortfm_is_transactional are:
//
// - It's faster. Once all of the optimizations are implemented, the compiler will
//   constant-fold this.
//
// - Usually, if you are doing special things for transactions, it's to work around
//   the transactional openation in a closed nest. So, it's often more correct
//   to test is_closed than is_transactional.
#if UE_AUTORTFM
bool autortfm_is_closed(void);
#else
inline bool autortfm_is_closed(void)
{
    return false;
}
#endif

// If AutoRTFM is enabled, run the callback in a transaction (otherwise just run the callback).
// Writes and other effects get instrumented and will be reversed if the transaction aborts.
// If this begins a nested transaction, the instrumented effects are logged onto the root
// transaction, so the effects can be reversed later if the root transaction aborts, even
// if this nested transaction succeeds.
#if UE_AUTORTFM
autortfm_result autortfm_transact(void (*work)(void* arg), void* arg);
#else
inline autortfm_result autortfm_transact(void (*work)(void* arg), void* arg)
{
	work(arg);
    return autortfm_committed;
}
#endif

// Run the callback in a transaction like autortfm_transact, but abort program
// execution if the result is anything other than autortfm_committed. Useful for
// testing.
#if UE_AUTORTFM
void autortfm_commit(void (*work)(void* arg), void* arg);
#else
inline void autortfm_commit(void (*work)(void* arg), void* arg)
{
    abort();
}
#endif

// Create a new transaction in the open.
#if UE_AUTORTFM
bool autortfm_start_transaction();
#else
inline bool autortfm_start_transaction()
{
	return false;
}
#endif

// End a transaction and commit the changes to be visible to all
#if UE_AUTORTFM
autortfm_result autortfm_commit_transaction();
#else
inline autortfm_result autortfm_commit_transaction()
{
	return autortfm_aborted_by_language;
}
#endif

// End a transaction and discard all changes
#if UE_AUTORTFM
autortfm_result autortfm_abort_transaction();
#else
inline autortfm_result autortfm_abort_transaction()
{
	return autortfm_aborted_by_request;
}
#endif

// Clear the status of a transaction that was aborted in the open
#if UE_AUTORTFM
void autortfm_clear_transaction_status();
#else
inline void autortfm_clear_transaction_status()
{
}
#endif

#if UE_AUTORTFM
bool autortfm_is_aborting();
#else
inline bool autortfm_is_aborting()
{
	return false;
}
#endif

#if UE_AUTORTFM
bool autortfm_current_nest_throw();
#else
inline bool autortfm_current_nest_throw()
{
	return false;
}
#endif

// Abort if running in a transaction. If called from closed code, this does
// an abort_by_language, which will either abort the program or the transaction,
// depending on configuration. If called from open code that is inside a
// transaction, this aborts the program.
#if UE_AUTORTFM
void autortfm_abort_if_transactional(void);
#else
inline void autortfm_abort_if_transactional(void) { }
#endif

// Abort if running in closed code. This does an abort_by_language, which will
// either abort the program or the transaction, depending on configuration.
#if UE_AUTORTFM
void autortfm_abort_if_closed(void);
#else
inline void autortfm_abort_if_closed(void) { }
#endif

// Executes the given code non-transactionally regardless of whether we are in
// a transaction or not.
#if UE_AUTORTFM
void autortfm_open(void (*work)(void* arg), void* arg);
#else
inline void autortfm_open(void (*work)(void* arg), void* arg)
{
    work(arg);
}
#endif

// Executes the given code transactionally while inside a autortfm_open. Also
// executes the given code transactionally if called from transactional code.
//
// Guaranteed to crash if called outside a transaction.
#if UE_AUTORTFM
[[nodiscard]] autortfm_status autortfm_close(void (*work)(void* arg), void* arg);
#else
inline autortfm_status autortfm_close(void (*work)(void* arg), void* arg)
{
    abort();
	return autortfm_status_aborted_by_language;
}
#endif

// Records the pointer and size from the open into the current transaction
//
// Guaranteed to assert if called outside a transaction.
// Guaranteed to assert if called from closed code.
#if UE_AUTORTFM
void autortfm_record_open_write(void* Ptr, size_t Size);
#else
inline void autortfm_record_open_write(void* Ptr, size_t Size)
{
}
#endif

// Register a transactional version of a function that wasn't compiled by the
// autortfm compiler. Normally, code is transactionalized by the compiler by
// emitting a clone that has transactional openation, with some magic to
// redirect all function calls within a transaction to the transactional clone.
// This allows you to hook in your own transactionalized implementations of
// functions that the compiler did not see.
//
// Use with great caution!
//
// This results in calls to new_function to happen in open mode. We will call
// new_function's nontransactional version within the transaction. This happens
// with the additional caveat that if the original_function signature is:
//
//     foo (*original_function)(bar, baz)
//
// then the new_function signature is expected to be:
//
//     foo (*original_function)(bar, baz, void*)
//
// In general, a void* argument -- which you cannot introspect -- is added as
// the last parameter.
//
// Note that the string describing the function may be any non-NULL string. It
// does not have to be unique.
#if UE_AUTORTFM
void autortfm_register_open_function(void* original_function, void* new_function);
#else
inline void autortfm_register_open_function(void* original_function, void* new_function) { }
#endif

// Have some work happen when this transaction commits. For nested transactions,
// this just adds the work to the work deferred until the outer nest's commit.
// If this is called outside a transaction then the work happens immediately. An
// open nest within a transaction will be deferred until commit too.
#if UE_AUTORTFM
void autortfm_defer_until_commit(void (*work)(void* arg), void* arg);
#else
inline void autortfm_defer_until_commit(void (*work)(void* arg), void* arg)
{
    work(arg);
}
#endif

// Have some work happen when this transaction aborts. For nested transactions,
// this work will be processed only when the outer nest actually commits or
// aborts. If this is called outside a transaction then the work is ignored. An
// open nest within a transaction will be deferred until abort too.
#if UE_AUTORTFM
void autortfm_defer_until_abort(void (*work)(void* arg), void* arg);
#else
inline void autortfm_defer_until_abort(void (*work)(void* arg), void* arg) { }
#endif

// Have some work happen when this transaction commits. For nested transactions,
// this just adds the work to the work deferred until the outer nest's commit.
// If this is called outside a transaction or from an open nest then the work
// happens immediately.
#if UE_AUTORTFM
void autortfm_open_commit(void (*work)(void* arg), void* arg);
#else
inline void autortfm_open_commit(void (*work)(void* arg), void* arg)
{
    work(arg);
}
#endif

// Have some work happen when this transaction aborts. If this is called
// outside a transaction or from an open nest then the work is ignored.
#if UE_AUTORTFM
void autortfm_open_abort(void (*work)(void* arg), void* arg);
#else
inline void autortfm_open_abort(void (*work)(void* arg), void* arg) { }
#endif

// Inform the runtime that we have performed a new object allocation. It's only
// necessary to call this inside of custom malloc implementations. As an
// optimization, you can choose to then only have your malloc return the pointer
// returned by this function. It's guaranteed to be equal to the pointer you
// passed, but it's blessed specially from the compiler's perspective, leading
// to some nice optimizations. This does nothing when called from open code.
#if UE_AUTORTFM
void* autortfm_did_allocate(void* ptr, size_t size);
#else
inline void* autortfm_did_allocate(void* ptr, size_t size)
{
    return ptr;
}
#endif

// If running in a transaction, then perform a consistency check of the
// transaction's read-write set. If possible, this compares the read-write set's
// expected values with the actual values in global memory. Does nothing when
// called outside of a transaction. May do nothing if debugging features aren't
// enabled in the autortfm runtime.
#if UE_AUTORTFM
void autortfm_check_consistency_assuming_no_races(void);
#else
inline void autortfm_check_consistency_assuming_no_races(void) { }
#endif

// If running with AutoRTFM enabled, then perform an ABI check between the
// AutoRTFM compiler and the AutoRTFM runtime, to ensure that memory is being
// laid out in an identical manner between the AutoRTFM runtime and the AutoRTFM
// compiler pass. Should not be called manually by the user, a call to this will
// be injected by the compiler into a global constructor in the AutoRTFM compiled
// code.
#if UE_AUTORTFM
void autortfm_check_abi(void* ptr, size_t size);
#else
inline void autortfm_check_abi(void* ptr, size_t size) { }
#endif

#ifdef __cplusplus
}

namespace AutoRTFM
{

// This must match autortfm_result.
enum class ETransactionResult
{
    AbortedByRequest = autortfm_aborted_by_request,
    AbortedByLanguage = autortfm_aborted_by_language,
    Committed = autortfm_committed
};

enum class EContextStatus
{
	Idle = autortfm_status_idle,
	OnTrack = autortfm_status_ontrack,
	AbortedByFailedLockAcquisition = autortfm_status_aborted_by_failed_lock_aquisition,
	AbortedByLanguage = autortfm_status_aborted_by_language,
	AbortedByRequest = autortfm_status_aborted_by_request
};

inline bool IsTransactional() { return autortfm_is_transactional(); }
inline bool IsClosed() { return autortfm_is_closed(); }

template<typename TFunctor>
ETransactionResult Transact(const TFunctor& Functor)
{
    return static_cast<ETransactionResult>(autortfm_transact(
        [] (void* Arg) { (*static_cast<const TFunctor*>(Arg))(); },
        const_cast<void*>(static_cast<const void*>(&Functor))));
}

template<typename TFunctor>
void Commit(const TFunctor& Functor)
{
    autortfm_commit(
        [] (void* Arg) { (*static_cast<const TFunctor*>(Arg))(); },
        const_cast<void*>(static_cast<const void*>(&Functor)));
}

inline bool StartTransaction()
{
	return autortfm_start_transaction();
}

inline ETransactionResult CommitTransaction()
{
	return static_cast<ETransactionResult>(autortfm_commit_transaction());
}

inline ETransactionResult AbortTransaction()
{
	return static_cast<ETransactionResult>(autortfm_abort_transaction());
}

inline void ClearTransactionStatus()
{
	autortfm_clear_transaction_status();
}

inline bool IsAborting()
{
	return autortfm_is_aborting();
}

inline bool CurrentNestThrow()
{
	return autortfm_current_nest_throw();
}

// RecordOpenWrite records the memory span into the current transaction as written.
//  If this memory is previously unknown to the transaction, the original value is saved.
inline void RecordOpenWrite(void* Ptr, size_t Size)
{
	autortfm_record_open_write(Ptr, Size);
}

template<typename TTYPE>
inline void RecordOpenWrite(TTYPE* Ptr)
{
	autortfm_record_open_write(Ptr, sizeof(TTYPE));
}

// RecordOpenRead does nothing right now, but it is intended as a support stub for the day when we move to full AutoSTM
inline void RecordOpenRead(void const* Ptr, size_t Size)
{
}

template<typename TTYPE>
inline void RecordOpenRead(TTYPE* Ptr)
{
	RecordOpenRead(Ptr, sizeof(TTYPE));
}

// WriteMemory first records the memory span as written (see RecordOpenWrite) and then copies the specified value into it.
inline void WriteMemory(void* DestPtr, void const* SrcPtr, size_t Size)
{
	RecordOpenWrite(DestPtr, Size);
	memcpy(DestPtr, SrcPtr, Size);
}

template<typename TTYPE>
inline void WriteMemoryTrivial(TTYPE* DestPtr, TTYPE const* SrcPtr)
{
	RecordOpenWrite(DestPtr, sizeof(TTYPE));
	*DestPtr = *SrcPtr;
}

template<typename TTYPE>
inline void WriteMemory(TTYPE* DestPtr, TTYPE const* SrcPtr)
{
	if constexpr (std::is_trivially_copyable<TTYPE>::value)
	{
		WriteMemoryTrivial<TTYPE>(DestPtr, SrcPtr);
	}
	else
	{
		WriteMemory(DestPtr, SrcPtr, sizeof(TTYPE));
	}
}

template<typename TTYPE>
inline void WriteMemory(TTYPE* DestPtr, TTYPE const SrcValue)
{
	if constexpr (std::is_trivially_copyable<TTYPE>::value)
	{
		WriteMemoryTrivial<TTYPE>(DestPtr, &SrcValue);
	}
	else
	{
		WriteMemory(DestPtr, &SrcValue, sizeof(TTYPE));
	}
}

inline void AbortIfTransactional()
{
    autortfm_abort_if_transactional();
}

inline void AbortIfClosed()
{
    autortfm_abort_if_closed();
}

template<typename TFunctor>
void Open(const TFunctor& Functor)
{
    autortfm_open(
        [] (void* Arg) { (*static_cast<const TFunctor*>(Arg))(); },
        const_cast<void*>(static_cast<const void*>(&Functor)));
}

template<typename TFunctor>
[[nodiscard]] EContextStatus Close(const TFunctor& Functor)
{
    return static_cast<EContextStatus>(autortfm_close(
        [] (void* Arg) { (*static_cast<const TFunctor*>(Arg))(); },
        const_cast<void*>(static_cast<const void*>(&Functor))));
}

inline void RegisterOpenFunction(void* OriginalFunction, void* NewFunction)
{
    autortfm_register_open_function(OriginalFunction, NewFunction);
}

#if UE_AUTORTFM
void DeferUntilCommit(TFunction<void()>&& Work);
void DeferUntilAbort(TFunction<void()>&& Work);
void OpenCommit(TFunction<void()>&& Work);
void OpenAbort(TFunction<void()>&& Work);
#else
template<typename TFunctor>
void DeferUntilCommit(const TFunctor& Work) { Work(); }
template<typename TFunctor>
void DeferUntilAbort(const TFunctor& Work) { }
template<typename TFunctor>
void OpenCommit(const TFunctor& Work) { Work(); }
template<typename TFunctor>
void OpenAbort(const TFunctor& Work) { }
#endif

inline void* DidAllocate(void* Ptr, size_t Size)
{
    return autortfm_did_allocate(Ptr, Size);
}

inline void CheckConsistencyAssumingNoRaces()
{
    autortfm_check_consistency_assuming_no_races();
}

struct FRegisterOpenFunction
{
    FRegisterOpenFunction(void* OriginalFunction, void* NewFunction)
    {
        RegisterOpenFunction(OriginalFunction, NewFunction);
    }
};

#define UE_AUTORTFM_CONCAT_IMPL(A, B) A ## B
#define UE_AUTORTFM_CONCAT(A, B) UE_AUTORTFM_CONCAT_IMPL(A, B)

#if UE_AUTORTFM
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OriginalFunction, NewFunction) static const FRegisterOpenFunction UE_AUTORTFM_CONCAT(AutoRTFMFunctionRegistration, __COUNTER__)(reinterpret_cast<void*>(OriginalFunction), reinterpret_cast<void*>(NewFunction))
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION(OriginalFunction) UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OriginalFunction, STM_ ## OriginalFunction)
#define UE_AUTORTFM_REGISTER_SELF_FUNCTION(OriginalFunction) UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OriginalFunction, OriginalFunction)
#else
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION_EXPLICIT(OriginalFunction, NewFunction)
#define UE_AUTORTFM_REGISTER_OPEN_FUNCTION(OriginalFunction)
#define UE_AUTORTFM_REGISTER_SELF_FUNCTION(OriginalFunction)
#endif

} // namespace AutoRTFM
#endif // __cplusplus
