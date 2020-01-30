// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#define VIVOXVOICECHAT_ERROR(...) FVoiceChatResult::CreateError(TEXT("errors.com.epicgames.voicechat.vivox"), __VA_ARGS__)

namespace VivoxVoiceChat
{
	namespace Errors
	{
		inline FVoiceChatResult AlreadyInitialized() { return VIVOXVOICECHAT_ERROR(EVoiceChatResult::InvalidState, TEXT("already_initialized")); }
		inline FVoiceChatResult AlreadyLoggedIn() { return VIVOXVOICECHAT_ERROR(EVoiceChatResult::InvalidState, TEXT("already_logged_in")); }
		inline FVoiceChatResult KickedFromChannel() { return VIVOXVOICECHAT_ERROR(EVoiceChatResult::ImplementationError, TEXT("kicked_from_channel")); }
		inline FVoiceChatResult NoExist() { return VIVOXVOICECHAT_ERROR(EVoiceChatResult::ImplementationError, TEXT("no_exist")); }
		inline FVoiceChatResult MaximumNumberOfCallsExceeded() { return VIVOXVOICECHAT_ERROR(EVoiceChatResult::ImplementationError, TEXT("maximum_number_of_calls_exceeded")); }
	}
}