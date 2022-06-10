// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace UE::RenderPages::Private
{
	/**
	 * This struct contains the IDs as well as the localized text of each of the render page editor application modes.
	 */
	struct FRenderPagesApplicationModes
	{
	public:
		/** Constant for the listing mode. */
		static const FName ListingMode;

		/** Constant for the logic mode. */
		static const FName LogicMode;

	public:
		/** Returns the localized text for the given render page editor application mode. */
		static FText GetLocalizedMode(const FName InMode);

	private:
		FRenderPagesApplicationModes() = default;
	};
}
