// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FConcertLogEntry;

namespace UE::MultiUserServer
{
	template<typename TFilterType> class TConcertFilter;
	template<typename TFilterType> class TConcertFrontendFilter;

	using FConcertLogFilter = TConcertFilter<const FConcertLogEntry&>;
	using FConcertFrontendLogFilter = TConcertFrontendFilter<const FConcertLogEntry&>;
}


