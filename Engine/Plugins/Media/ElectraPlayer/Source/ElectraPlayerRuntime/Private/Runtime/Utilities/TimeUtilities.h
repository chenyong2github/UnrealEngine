// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"


namespace Electra
{

	namespace ISO8601
	{
		UEMediaError ParseDateTime(FTimeValue& OutTimeValue, const FString& DateTime);
	}


	namespace RFC7231
	{
		UEMediaError ParseDateTime(FTimeValue& OutTimeValue, const FString& DateTime);
	}

} // namespace Electra

