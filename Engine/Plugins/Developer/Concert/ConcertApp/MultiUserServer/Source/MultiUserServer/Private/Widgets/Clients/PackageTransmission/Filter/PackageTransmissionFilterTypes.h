// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::MultiUserServer
{
	struct FPackageTransmissionEntry;
	template<typename TFilterType> class TConcertFilter;
	template<typename TFilterType> class TConcertFrontendFilter;

	using FPackageTransmissionFilter = TConcertFilter<const FPackageTransmissionEntry&>;
	using FFrontendPackageTransmissionFilter = TConcertFrontendFilter<const FPackageTransmissionEntry&>;
}


