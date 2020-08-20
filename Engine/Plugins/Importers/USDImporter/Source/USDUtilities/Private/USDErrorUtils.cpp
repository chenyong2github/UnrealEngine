// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDErrorUtils.h"

#include "IMessageLogListing.h"
#include "Math/NumericLimits.h"
#include "MessageLogModule.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

#include "USDLog.h"

#if USE_USD_SDK

#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "USDIncludesStart.h"
#include "pxr/pxr.h"
#include "pxr/base/tf/errorMark.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDErrorUtils"

#if USE_USD_SDK

namespace UsdUtils
{
	// We need an extra level of indirection because TfErrorMark is noncopyable
	using MarkRef = TUsdStore<TSharedRef<pxr::TfErrorMark>>;

	static TArray<MarkRef> ErrorMarkStack;

	void StartMonitoringErrors()
	{
		FScopedUsdAllocs Allocs;

		TSharedRef<pxr::TfErrorMark> Mark = MakeShared<pxr::TfErrorMark>();
		Mark->SetMark();

		ErrorMarkStack.Emplace(Mark);
	}

	TArray<FString> GetErrorsAndStopMonitoring()
	{
		if (ErrorMarkStack.Num() == 0)
		{
			return {};
		}

		MarkRef Store = ErrorMarkStack.Pop();
		pxr::TfErrorMark& Mark = Store.Get().Get();

		if (Mark.IsClean())
		{
			return {};
		}

		TArray<FString> Errors;

		for (pxr::TfErrorMark::Iterator ErrorIter = Mark.GetBegin();
			 ErrorIter != Mark.GetEnd();
			 ++ErrorIter)
		{
			std::string ErrorStr = ErrorIter->GetErrorCodeAsString();
			ErrorStr += ": ";
			ErrorStr += ErrorIter->GetCommentary();

			// Add unique here as for some errors (e.g. parsing errors) USD can emit the exact same
			// error message 5+ times in a row
			Errors.AddUnique(UsdToUnreal::ConvertString(ErrorStr));
		}

		Mark.Clear();

		return Errors;
	}

	bool ShowErrorsAndStopMonitoring(const FText& ToastMessage)
	{
		TArray<FString> Errors = GetErrorsAndStopMonitoring();
		bool bHadErrors = Errors.Num() > 0;

		if (bHadErrors)
		{
			FNotificationInfo ErrorToast(!ToastMessage.IsEmpty() ? ToastMessage : LOCTEXT("USDErrorsToast", "Encountered USD errors!\nCheck the Output Log for details."));

			ErrorToast.ExpireDuration = 5.0f;
			ErrorToast.bFireAndForget = true;
			ErrorToast.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
			FSlateNotificationManager::Get().AddNotification(ErrorToast);
		}

		for (const FString& Error : Errors)
		{
			FUsdLogManager::LogMessage( FTokenizedMessage::Create( EMessageSeverity::Error, FText::FromString( Error ) ) );
		}

		return Errors.Num() > 0;
	}
}; // namespace UsdUtils

#else // #if USE_USD_SDK

namespace UsdUtils
{
	void StartMonitoringErrors()
	{
	}
	TArray<FString> GetErrorsAndStopMonitoring()
	{
		return {TEXT("USD SDK is not available!")};
	}
	bool ShowErrorsAndStopMonitoring(const FText& ToastMessage)
	{
		return false;
	}
}

#endif // #if USE_USD_SDK

namespace UE
{
	namespace Internal
	{
		FUsdMessageLog::~FUsdMessageLog()
		{
			Dump();
		}

		void FUsdMessageLog::Push( const TSharedRef< FTokenizedMessage >& Message )
		{
			TokenizedMessages.Add( Message );
		}

		void FUsdMessageLog::Dump()
		{
			FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked< FMessageLogModule >( "MessageLog" );
			TSharedRef< IMessageLogListing > LogListing = MessageLogModule.GetLogListing( TEXT("USD") );

			if ( TokenizedMessages.Num() > 0 )
			{
				LogListing->AddMessages( TokenizedMessages );
				LogListing->NotifyIfAnyMessages( LOCTEXT("Log", "There were some issues loading the USD Stage."), EMessageSeverity::Info );
				TokenizedMessages.Empty();
			}
		}
	}
}

TOptional< UE::Internal::FUsdMessageLog > FUsdLogManager::MessageLog;
int32 FUsdLogManager::MessageLogRefCount;
FCriticalSection FUsdLogManager::MessageLogLock;

void FUsdLogManager::LogMessage( EMessageSeverity::Type Severity, const FText& Message )
{
	LogMessage( FTokenizedMessage::Create( Severity, Message ) );
}

void FUsdLogManager::LogMessage( const TSharedRef< FTokenizedMessage >& Message )
{
	bool bMessageProcessed = false;

	if ( MessageLog )
	{
		FScopeLock Lock( &MessageLogLock );
		if ( MessageLog ) // Make sure it's still valid
		{
			MessageLog->Push( Message );
			bMessageProcessed = true;
		}
	}

	if ( !bMessageProcessed )
	{
		if ( Message->GetSeverity() == EMessageSeverity::CriticalError || Message->GetSeverity() == EMessageSeverity::Error )
		{
			UE_LOG( LogUsd, Error, TEXT("%s"), *(Message->ToText().ToString()) );
		}
		else if ( Message->GetSeverity() == EMessageSeverity::Warning || Message->GetSeverity() == EMessageSeverity::PerformanceWarning )
		{
			UE_LOG( LogUsd, Warning, TEXT("%s"), *(Message->ToText().ToString()) );
		}
		else
		{
			UE_LOG( LogUsd, Log, TEXT("%s"), *(Message->ToText().ToString()) );
		}
	}
}

void FUsdLogManager::EnableMessageLog()
{
	FScopeLock Lock( &MessageLogLock );

	if ( ++MessageLogRefCount == 1 )
	{
		MessageLog.Emplace();
	}

	check( MessageLogRefCount < MAX_int32 );
}

void FUsdLogManager::DisableMessageLog()
{
	FScopeLock Lock( &MessageLogLock );

	if ( --MessageLogRefCount == 0 )
	{
		MessageLog.Reset();
	}
}

FScopedUsdMessageLog::FScopedUsdMessageLog()
{
	FUsdLogManager::EnableMessageLog();
}

FScopedUsdMessageLog::~FScopedUsdMessageLog()
{
	FUsdLogManager::DisableMessageLog();
}

#undef LOCTEXT_NAMESPACE
