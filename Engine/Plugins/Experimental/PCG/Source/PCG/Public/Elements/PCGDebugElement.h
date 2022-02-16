// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGDebugElement.generated.h"

namespace PCGDebugElement
{
	void ExecuteDebugDisplay(FPCGContextPtr Context);
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGDebugSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	// ~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("DebugNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface
};

class FPCGDebugElement : public FSimpleTypedPCGElement<UPCGDebugSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContextPtr Contexxt) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};