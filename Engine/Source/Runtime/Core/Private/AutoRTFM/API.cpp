// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM/AutoRTFM.h"
#include "AutoRTFM/AutoRTFMConstants.h"
#include "CallNest.h"
#include "Context.h"
#include "ContextInlines.h"
#include "ContextStatus.h"
#include "Debug.h"
#include "FunctionMapInlines.h"
#include "TransactionInlines.h"
#include "Utils.h"

// This is the implementation of the AutoRTFM.h API. Ideally, functions here should just delegate to some internal API.
// For now, I have these functions also perform some error checking.

namespace AutoRTFM
{

extern "C" bool autortfm_is_transactional()
{
    return FContext::Get()->GetStatus() == EContextStatus::OnTrack;
}

extern "C" bool autortfm_is_closed()
{
    return false;
}

// First Part - the API exposed outside transactions.
extern "C" autortfm_result autortfm_transact(void (*Work)(void* Arg), void* Arg)
{
    return static_cast<autortfm_result>(FContext::Get()->Transact(Work, Arg));
}

extern "C" void autortfm_commit(void (*Work)(void* Arg), void* Arg)
{
    autortfm_result Result = autortfm_transact(Work, Arg);
    if (Result != autortfm_committed)
    {
        fprintf(stderr, "FATAL: Unexpected transaction result: %u.\n", Result);
        abort();
    }
}

extern "C" void autortfm_abort()
{
    if (FContext::IsTransactional())
    {
        FContext::Get()->AbortByRequestAndThrow();
    }
    else
    {
        fprintf(stderr, "autortfm_abort called from outside a transaction.\n");
        abort();
    }
}

extern "C" bool autortfm_start_transaction()
{
	if (FContext::IsTransactional())
	{
		return FContext::Get()->StartTransaction();
	}
	else
	{
		fprintf(stderr, "autortfm_start_transaction called from outside a transact.\n");
		abort();
	}
}

extern "C" autortfm_result autortfm_commit_transaction()
{
	if (FContext::IsTransactional())
	{
		return static_cast<autortfm_result>(FContext::Get()->CommitTransaction());
	}
	else
	{
		fprintf(stderr, "autortfm_commit_transaction called from outside a transaction.\n");
		abort();
	}
}

extern "C" autortfm_result autortfm_abort_transaction()
{
	if (FContext::IsTransactional())
	{
		return static_cast<autortfm_result>(FContext::Get()->AbortTransaction(false));
	}
	else
	{
		fprintf(stderr, "autortfm_abort_transaction called from outside a transaction.\n");
		abort();
	}
}

extern "C" void autortfm_clear_transaction_status()
{
	ASSERT(FContext::Get()->IsAborting());
	FContext::Get()->ClearTransactionStatus();
}

extern "C" bool autortfm_is_aborting()
{
	return FContext::Get()->IsAborting();
}

extern "C" bool autortfm_current_nest_throw()
{
	FContext::Get()->Throw();
	return true;
}

extern "C" void autortfm_abort_if_transactional()
{
    if (FContext::IsTransactional())
    {
        fprintf(stderr, "autortfm_abort_if_transactional called from an open nest inside a transaction.\n");
        abort();
    }
}

extern "C" void autortfm_abort_if_closed()
{
}

extern "C" void autortfm_open(void (*Work)(void* Arg), void* Arg)
{
	Work(Arg);
}

extern "C" autortfm_status autortfm_close(void (*Work)(void* Arg), void* Arg)
{
	autortfm_status Result = autortfm_status_ontrack;

    if (FContext::IsTransactional())
    {
        FContext* Context = FContext::Get();
        void (*WorkClone)(void* Arg, FContext* Context) = FunctionMapLookup(Work, Context, "autortfm_close");
        if (WorkClone)
        {
			Result = static_cast<autortfm_status>(Context->CallClosedNest(WorkClone, Arg));
        }
    }
    else
    {
        fprintf(stderr, "autortfm_close called from outside a transaction.\n");
        abort();
    }

	return Result;
}

extern "C" void autortfm_record_open_write(void* Ptr, size_t Size)
{
	FContext::Get()->RecordWrite(Ptr, Size, false);
}

extern "C" void autortfm_register_open_function(void* OriginalFunction, void* NewFunction)
{
    constexpr bool bVerbose = false;
    if (bVerbose)
    {
        fprintf(GetLogFile(), "Registering open %p->%p\n", OriginalFunction, NewFunction);
    }
    FunctionMapAdd(OriginalFunction, NewFunction);
}

void DeferUntilCommit(std::function<void()>&& Work)
{
    if (FContext::IsTransactional())
    {
        FContext::Get()->GetCurrentTransaction()->DeferUntilCommit(std::move(Work));
    }
    else
    {
        Work();
    }
}

void DeferUntilAbort(std::function<void()>&& Work)
{
    if (FContext::IsTransactional())
    {
        FContext::Get()->GetCurrentTransaction()->DeferUntilAbort(std::move(Work));
    }
}

void OpenCommit(std::function<void()>&& Work)
{
    Work();
}

void OpenAbort(std::function<void()>&& Work)
{
}

extern "C" void autortfm_defer_until_commit(void (*Work)(void* Arg), void* Arg)
{
    DeferUntilCommit([Work, Arg] () { Work(Arg); });
}

extern "C" void autortfm_defer_until_abort(void (*Work)(void* arg), void* Arg)
{
    DeferUntilAbort([Work, Arg] () { Work(Arg); });
}

extern "C" void autortfm_open_commit(void (*Work)(void* Arg), void* Arg)
{
    Work(Arg);
}

extern "C" void autortfm_open_abort(void (*Work)(void* arg), void* Arg)
{
}

extern "C" void* autortfm_did_allocate(void* Ptr, size_t Size)
{
    return Ptr;
}

extern "C" void autortfm_will_deallocate(void* Ptr, size_t Size)
{
}

extern "C" void autortfm_check_consistency_assuming_no_races()
{
    if (FContext::IsTransactional())
    {
        AutoRTFM::Unreachable();
    }
}

extern "C" void autortfm_check_abi(void* const Ptr, const size_t Size)
{
    struct FConstants final
    {
        const size_t LogLineBytes = Constants::LogLineBytes;
        const size_t LineBytes = Constants::LineBytes;
        const size_t LineTableSize = Constants::LineTableSize;
        const size_t Offset_Context_CurrentTransaction = Constants::Offset_Context_CurrentTransaction;
        const size_t Offset_Context_LineTable = Constants::Offset_Context_LineTable;
        const size_t Offset_Context_Status = Constants::Offset_Context_Status;
        const size_t LogSize_LineEntry = Constants::LogSize_LineEntry;
        const size_t Size_LineEntry = Constants::Size_LineEntry;
        const size_t Offset_LineEntry_LogicalLine = Constants::Offset_LineEntry_LogicalLine;
        const size_t Offset_LineEntry_ActiveLine = Constants::Offset_LineEntry_ActiveLine;
        const size_t Offset_LineEntry_LoggingTransaction = Constants::Offset_LineEntry_LoggingTransaction;
        const size_t Offset_LineEntry_AccessMask = Constants::Offset_LineEntry_AccessMask;
        const uint32_t Context_Status_OnTrack = Constants::Context_Status_OnTrack;

		// This is messy - but we want to do comparisons but without comparing any padding bytes.
		// Before C++20 we cannot use a default created operator== and operator!=, so we use this
		// ugly trick to just compare the members.
	private:
		auto Tied() const
		{
			return std::tie(LogLineBytes, LineBytes, LineTableSize, Offset_Context_CurrentTransaction, Offset_Context_LineTable, Offset_Context_Status, LogSize_LineEntry, Size_LineEntry, Offset_LineEntry_LogicalLine, Offset_LineEntry_ActiveLine, Offset_LineEntry_LoggingTransaction, Offset_LineEntry_AccessMask, Context_Status_OnTrack);
		}

	public:
		bool operator==(const FConstants& Other) const
		{
			return Tied() == Other.Tied();
		}

		bool operator!=(const FConstants& Other) const
		{
			return !(*this == Other);
		}
    } RuntimeConstants;

    if (sizeof(FConstants) != Size)
    {
        fprintf(GetLogFile(), "Fatal: found ABI error between AutoRTFM compiler and runtime\n");
        abort();
    }

    const FConstants* const CompilerConstants = static_cast<FConstants*>(Ptr);

    if (RuntimeConstants != *CompilerConstants)
    {
        fprintf(GetLogFile(), "Fatal: found ABI error between AutoRTFM compiler and runtime\n");
        abort();
    }
}

// Second Part - the same API exposed inside transactions. Note that we don't expose all of the API
// to transactions! That's intentional. However, things like autortfm_defer_until_commit can be called
// from an open nest in a transaction.
bool STM_autortfm_is_transactional(FContext* Context)
{
    return true;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_is_transactional);

bool STM_autortfm_is_closed(FContext* Context)
{
    return true;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_is_closed);

autortfm_result STM_autortfm_transact(void (*Work)(void* Arg), void* Arg, FContext* Context)
{
    return static_cast<autortfm_result>(Context->Transact(Work, Arg));
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_transact);

void STM_autortfm_commit(void (*Work)(void* Arg), void* Arg, FContext*)
{
    autortfm_result Result = autortfm_transact(Work, Arg);
    if (Result != autortfm_committed)
    {
        fprintf(stderr, "FATAL: Unexpected transaction result: %u.\n", Result);
        abort();
    }
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_commit);

void STM_autortfm_abort(FContext* Context)
{
    Context->AbortByRequestAndThrow();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_abort);

void STM_autortfm_start_transaction(FContext* Context)
{
	fprintf(stderr, "autortfm_start_transaction called from closed code.\n");
	abort();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_start_transaction);

void STM_autortfm_commit_transaction(FContext* Context)
{
	fprintf(stderr, "autostm_committransaction called from closed code.\n");
	abort();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_commit_transaction);

autortfm_result STM_autortfm_abort_transaction(FContext* Context)
{
	if (FContext::IsTransactional())
	{
		return static_cast<autortfm_result>(Context->AbortTransaction(true));
	}
	else
	{
		fprintf(stderr, "autortfm_abort_transaction called from outside a transaction.\n");
		abort();
	}
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_abort_transaction);

void STM_autortfm_clear_transaction_status(FContext* Context)
{
	fprintf(stderr, "autortfm_clear_transaction_status called from closed code.\n");
	abort();
}

bool STM_autortfm_is_aborting(FContext* Context)
{
	return Context->IsAborting();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_is_aborting);

bool STM_autortfm_current_nest_throw(FContext* Context)
{
	Context->Throw();
	return true;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_current_nest_throw);

void STM_autortfm_abort_if_transactional(FContext* Context)
{
    if (FDebug::bVerbose)
    {
        fprintf(stderr, "autortfm_abort_if_transactional called from inside a transaction.\n");
    }
    Context->AbortByLanguageAndThrow();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_abort_if_transactional);

void STM_autortfm_abort_if_closed(FContext* Context)
{
    if (FDebug::bVerbose)
    {
        fprintf(stderr, "autortfm_abort_if_closed called from a closed nest inside a transaction.\n");
    }
    
    Context->AbortByLanguageAndThrow();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_abort_if_closed);

void STM_autortfm_open(void (*Work)(void* Arg), void* Arg, FContext* Context)
{
	// WARNING! DO NOT EDIT! Changes to this function will be elided due to special compiler optimizations
	Work(Arg);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_open);

autortfm_status STM_autortfm_close(void (*Work)(void* Arg), void* Arg, FContext* Context)
{
    void (*WorkClone)(void* Arg, FContext* Context) = FunctionMapLookup(Work, Context, "STM_autortfm_close");
    if (WorkClone)
    {
        WorkClone(Arg, Context);
    }

	return static_cast<autortfm_status>(FContext::Get()->GetStatus());
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_close);

extern "C" void STM_autortfm_record_open_write(void*, size_t, FContext*)
{
	fprintf(stderr, "FATAL: autortfm_record_open_write called from closed code.\n");
	abort();
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_record_open_write);

void STM_DeferUntilCommit(std::function<void()>&& Work, FContext* Context)
{
    ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
    Context->GetCurrentTransaction()->DeferUntilCommit(std::move(Work));
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(DeferUntilCommit);

void STM_DeferUntilAbort(std::function<void()>&& Work, FContext* Context)
{
    ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
    Context->GetCurrentTransaction()->DeferUntilAbort(std::move(Work));
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(DeferUntilAbort);

void STM_OpenCommit(std::function<void()>&& Work, FContext* Context)
{
    ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
    Context->GetCurrentTransaction()->DeferUntilCommit(std::move(Work));
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(OpenCommit);

void STM_OpenAbort(std::function<void()>&& Work, FContext* Context)
{
    ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
    Context->GetCurrentTransaction()->DeferUntilAbort(std::move(Work));
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(OpenAbort);

extern "C" void STM_autortfm_defer_until_commit(void (*Work)(void* Arg), void* Arg, FContext* Context)
{
    STM_DeferUntilCommit([Work, Arg] { Work(Arg); }, Context);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_defer_until_commit);

extern "C" void STM_autortfm_defer_until_abort(void (*Work)(void* arg), void* Arg, FContext* Context)
{
    STM_DeferUntilAbort([Work, Arg] { Work(Arg); }, Context);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_defer_until_abort);

extern "C" void STM_autortfm_open_commit(void (*Work)(void* Arg), void* Arg, FContext* Context)
{
    STM_OpenCommit([Work, Arg] { Work(Arg); }, Context);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_open_commit);

extern "C" void STM_autortfm_open_abort(void (*Work)(void* arg), void* Arg, FContext* Context)
{
    STM_OpenAbort([Work, Arg] { Work(Arg); }, Context);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_open_abort);

void* STM_autortfm_did_allocate(void* Ptr, size_t Size, FContext* Context)
{
    Context->DidAllocate(Ptr, Size);
    return Ptr;
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_did_allocate);

void STM_autortfm_will_deallocate(void* Ptr, size_t Size, FContext* Context)
{
    Context->WillDeallocate(Ptr, Size);
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_will_deallocate);

void STM_autortfm_check_consistency_assuming_no_races(FContext* Context)
{
}
UE_AUTORTFM_REGISTER_OPEN_FUNCTION(autortfm_check_consistency_assuming_no_races);

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
