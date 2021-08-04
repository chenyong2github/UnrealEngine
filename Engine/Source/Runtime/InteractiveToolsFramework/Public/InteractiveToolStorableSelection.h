// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolStorableSelection.generated.h"

/**
 * Interface class to allow an object to be storable in a UInteractiveToolSelectionStoreSubsystem.
 *
 * Note for inheriting classes: these objects can end up in the undo stack, which will
 * prevent them from being garbage collected until the undo stack is emptied. So, they
 * should probably not be huge, and they shouldn't be modified after submitting them to
 * UInteractiveToolSelectionStoreSubsystem.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolStorableSelection : public UObject
{
	GENERATED_BODY()
};