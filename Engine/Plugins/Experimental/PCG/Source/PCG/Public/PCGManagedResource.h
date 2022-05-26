// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGManagedResource.generated.h"

class UActorComponent;
class UInstancedStaticMeshComponent;

/** 
* This class is used to hold resources and their mechanism to delete them on demand.
* In order to allow for some reuse (e.g. components), the Release call supports a "soft"
* release that should empty any data but might retain some data.
* At the end of the generate, a call to ReleaseIfUnused will serve to finally cleanup
* what is not needed.
*/
UCLASS(BlueprintType)
class PCG_API UPCGManagedResource : public UObject
{
	GENERATED_BODY()
public:
	/** Releases/Resets the resource depending on the bHardRelease flag. Returns true if resource can be removed from the PCG component */
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) { return true; }
	/** Releases resource if empty or unused. Returns true if the resource can be removed from the PCG component */
	virtual bool ReleaseIfUnused() { return true; }
};

UCLASS(BlueprintType)
class PCG_API UPCGManagedActors : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedResource interface
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	virtual bool ReleaseIfUnused() override;
	//~End UPCGManagedResource interface

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = GeneratedData)
	TSet<TSoftObjectPtr<AActor>> GeneratedActors;
};

UCLASS(BlueprintType)
class PCG_API UPCGManagedComponent : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedResource interface
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;
	virtual bool ReleaseIfUnused() override;
	//~End UPCGManagedResource interface

	virtual void ResetComponent() { check(0); }
	virtual bool SupportsComponentReset() const { return false; }

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = GeneratedData)
	TSoftObjectPtr<UActorComponent> GeneratedComponent;
};

UCLASS(BlueprintType)
class PCG_API UPCGManagedISMComponent : public UPCGManagedComponent
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedResource interface
	virtual bool ReleaseIfUnused() override;
	//~End UPCGManagedResource interface

	//~Begin UPCGManagedComponents interface
	virtual void ResetComponent() override;
	virtual bool SupportsComponentReset() const { return true; }
	//~End UPCGManagedComponents interface

	UInstancedStaticMeshComponent* GetComponent() const;
};