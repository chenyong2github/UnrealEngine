// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDErrorUtils.h"

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

#define LOCTEXT_NAMESPACE "USDErrorUtils"

DEFINE_LOG_CATEGORY(LogUsdErrorUtility);

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
			ErrorStr += " ";
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
			UE_LOG(LogUsdErrorUtility, Error, TEXT("%s"), *Error);
		}

		return Errors.Num() > 0;
	}
}; // namespace UsdUtils

#undef LOCTEXT_NAMESPACE

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

