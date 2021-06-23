// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#define EOSVOICECHAT_ERROR(...) FVoiceChatResult::CreateError(TEXT("errors.com.epicgames.voicechat.eos"), __VA_ARGS__)

namespace EOSVoiceChat
{
	namespace Errors
	{
		// None, EOS specific errors use the EOS_Code enum string
	}
}