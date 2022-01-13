// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGStaticMeshSpawner.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UPCGComponent;
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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TArray<FPCGStaticMeshSpawnerEntry> Meshes;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGStaticMeshSpawnerElement : public FSimpleTypedPCGElement<UPCGStaticMeshSpawnerSettings>
{
public:
	virtual bool Execute(FPCGContextPtr Context) const override;

private:
	UInstancedStaticMeshComponent* GetOrCreateISMC(AActor* InActor, const UPCGComponent* SourceComponent, UStaticMesh* InMesh) const;
};