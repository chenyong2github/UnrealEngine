// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGData.h"
#include "PCGElement.h"

#include "PCGSettings.generated.h"

class UPCGNode;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGSettingsChanged, UPCGSettings*);
#endif

/**
* Base class for settings-as-data in the PCG framework
*/
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSettings : public UPCGData
{
	GENERATED_BODY()

public:
	// TODO: check if we need this to be virtual, we don't really need if we're always caching
	/*virtual*/ FPCGElementPtr GetElement() const;
	virtual UPCGNode* CreateNode() const;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const { return NAME_None; }
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int Seed = 0xC35A9631; // random prime number

#if WITH_EDITOR
	FOnPCGSettingsChanged OnSettingsChangedDelegate;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const PURE_VIRTUAL(UPCGSettings::CreateElement, return nullptr;);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	mutable FPCGElementPtr CachedElement;
	mutable FCriticalSection CacheLock;
};

/** Trivial / Pass-through settings used for input/output nodes */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGTrivialSettings : public UPCGSettings
{
	GENERATED_BODY()

protected:
	//~UPCGSettings implementation
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGTrivialElement : public FSimplePCGElement
{
public:
	virtual bool Execute(FPCGContextPtr Context) const override;
};