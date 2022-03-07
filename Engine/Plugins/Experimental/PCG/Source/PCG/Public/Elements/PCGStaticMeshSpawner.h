// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGStaticMeshSpawner.generated.h"

class UStaticMesh;

USTRUCT(BlueprintType)
struct FPCGStaticMeshSpawnerEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0"))
	int Weight = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UStaticMesh> Mesh;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPCGStaticMeshSpawnerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	// ~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("StaticMeshSpawnerNode")); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGStaticMeshSpawnerEntry> Meshes;
};

class FPCGStaticMeshSpawnerElement : public FSimpleTypedPCGElement<UPCGStaticMeshSpawnerSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};