// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class URCController;

namespace UE::RCUIHelpers
{
	/** GetFieldClassTypeColor
	 * Fetches the editor color associated with a given Unreal Type (FProperty)
	 * Used to provide color coding in the Remote Control Logic Actions panel 
	 */
	FLinearColor GetFieldClassTypeColor(const FProperty* InProperty);

	/** GetFieldClassDisplayName
	 * Fetches the display name associated with a given Unreal Type (FProperty)
	 */
	FName GetFieldClassDisplayName(const FProperty* InProperty);
}