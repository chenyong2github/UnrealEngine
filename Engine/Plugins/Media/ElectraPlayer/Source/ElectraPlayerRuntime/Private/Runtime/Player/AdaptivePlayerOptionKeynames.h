// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"

namespace Electra
{
	const TCHAR* const OptionKeyInitialBitrate = TEXT("initial_bitrate");							//!< (int64) value indicating the bitrate to start with.
	const TCHAR* const OptionKeyLiveSeekableEndOffset = TEXT("seekable_range_live_end_offset");		//!< (FTimeValue) value specifying how many seconds away from the Live media timeline the seekable range should end.

} // namespace Electra


