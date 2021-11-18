// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/ExternalUICommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_ui_types.h"

namespace UE::Online {

class FOnlineServicesEOS;

class ONLINESERVICESEOS_API FExternalUIEOS : public FExternalUICommon
{
public:
	using Super = FExternalUICommon;

	FExternalUIEOS(FOnlineServicesEOS& InOwningSubsystem);
	virtual void Initialize() override;
	virtual void PreShutdown() override;
	virtual TOnlineAsyncOpHandle<FExternalUIShowFriendsUI> ShowFriendsUI(FExternalUIShowFriendsUI::Params&& Params) override;

protected:
	EOS_HUI UIHandle;
};

/* UE::Online */ }
