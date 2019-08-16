// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DesignerExtension.h"

/**
 * Designer Extensibility Manager keep a series of Designer Extensions. See FDesignerExtension class for more information.
 */
class UMGEDITOR_API FDesignerExtensibilityManager
{
public:
	void AddDesignerExtension(const TSharedRef<FDesignerExtension>& Extension)
	{
		ExternalExtensions.AddUnique(Extension);
	}

	void RemoveDesignerExtension(const TSharedRef<FDesignerExtension>& Extension)
	{
		ExternalExtensions.Remove(Extension);
	}

	const TArray<TSharedRef<FDesignerExtension>>& GetExternalDesignerExtensions() const
	{
		return ExternalExtensions;
	}

private:
	TArray<TSharedRef<FDesignerExtension>> ExternalExtensions;
};

/** Indicates that a class has a designer that is extensible */
class IHasDesignerExtensibility
{
public:
	virtual TSharedPtr<FDesignerExtensibilityManager> GetDesignerExtensibilityManager() = 0;
};

