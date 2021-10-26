// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "Elements/Framework/TypedElementHandle.h"

#include "EditorElementSubsystem.generated.h"

UCLASS(Transient)
class EDITORFRAMEWORK_API UEditorElementSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Sets the world transform of the given element handle, if possible.
	 * @returns false if the world transform could not be set.
	 */
	bool SetElementTransform(FTypedElementHandle InElementHandle, const FTransform& InWorldTransform);
};