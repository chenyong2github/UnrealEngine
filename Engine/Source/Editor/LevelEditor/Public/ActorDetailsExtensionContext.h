// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorDetailsExtensionContext.generated.h"

UCLASS()
class LEVELEDITOR_API UActorDetailsExtensionContext : public UObject
{
	GENERATED_BODY()

public:
	const TArray<TWeakObjectPtr<UObject>>& GetSelectedObjects() const;

private:
	friend class SActorDetails;

	DECLARE_DELEGATE_RetVal(const TArray<TWeakObjectPtr<UObject>>&, FGetSelectedObjects);
	FGetSelectedObjects GetSelectedObjectsDelegate;
};
