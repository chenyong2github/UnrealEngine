// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

namespace UE
{
	namespace Interchange
	{
		class FAssetImportResult;

		using FAssetImportResultRef = TSharedRef< FAssetImportResult, ESPMode::ThreadSafe >;
	}
}
