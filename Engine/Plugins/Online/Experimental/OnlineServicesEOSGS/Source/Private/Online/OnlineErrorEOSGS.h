// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineError.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_common.h"

namespace UE::Online {

namespace Errors {

UE_ONLINE_ERROR_CATEGORY(EOS, Engine, 0x3, "Online Services EOS")

} /* namespace Errors */

class FOnlineErrorDetailsEOS : public IOnlineErrorDetails
{
public:
	static const TSharedRef<const IOnlineErrorDetails, ESPMode::ThreadSafe>& Get();

	virtual FText GetText(const FOnlineError& OnlineError) const override;
	virtual FString GetLogString(const FOnlineError& OnlineError) const override;
};

bool operator==(const FOnlineError& OnlineError, EOS_EResult EosResult);
inline bool operator==(EOS_EResult EosResult, const FOnlineError& OnlineError) { return OnlineError == EosResult; }
inline bool operator!=(const FOnlineError& OnlineError, EOS_EResult EosResult) { return !(OnlineError == EosResult); }
inline bool operator!=(EOS_EResult EosResult, const FOnlineError& OnlineError) { return !(OnlineError == EosResult); }

ErrorCodeType CreateErrorCode(EOS_EResult EosResult);
FOnlineError CreateOnlineError(EOS_EResult EosResult);

} /* namespace UE::Online */