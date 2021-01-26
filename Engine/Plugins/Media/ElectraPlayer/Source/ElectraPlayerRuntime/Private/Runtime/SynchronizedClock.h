// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "PlayerTime.h"

namespace Electra
{

	/**
	 * This implements a UTC clock synchronized to an external time source.
	 */
	class ISynchronizedUTCTime
	{
	public:
		static ISynchronizedUTCTime* Create();
		virtual ~ISynchronizedUTCTime() = default;

		virtual void SetTime(const FTimeValue& TimeNow) = 0;

		virtual FTimeValue GetTime() = 0;

	};


} // namespace Electra


