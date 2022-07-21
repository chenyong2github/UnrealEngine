// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "InstancePackers/PCGInstancePackerBase.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"

#include "Engine/CollisionProfile.h"
#include "Templates/SubclassOf.h"

#include "PCGStaticMeshSpawner.generated.h"

class UStaticMesh;

USTRUCT(BlueprintType, meta=(Deprecated = "5.0", DeprecationMessage="Use MeshSelectorWeighted instead."))
struct PCG_API FPCGStaticMeshSpawnerEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0"))
	int Weight = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bOverrideCollisionProfile = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FCollisionProfileName CollisionProfile;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGStaticMeshSpawnerSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGStaticMeshSpawnerSettings(const FObjectInitializer &ObjectInitializer);

#if WITH_EDITOR
	// ~Begin UPCGSettings interface
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("StaticMeshSpawnerNode")); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	// ~Begin UObject interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// ~End UObject interface
#endif

	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetMeshSelectorType(TSubclassOf<UPCGMeshSelectorBase> InMeshSelectorType);

	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetInstancePackerType(TSubclassOf<UPCGInstancePackerBase> InInstancePackerType);

public:
	/** Defines the method of mesh selection per input data */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, NoClear, Category = Settings)
	TSubclassOf<UPCGMeshSelectorBase> MeshSelectorType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGMeshSelectorBase> MeshSelectorInstance;

	/** Defines the method of custom data packing for spawned (H)ISMCs */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings)
	TSubclassOf<UPCGInstancePackerBase> InstancePackerType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGInstancePackerBase> InstancePackerInstance = nullptr;

	/** Attribute name to store mesh SoftObjectPaths inside if the output pin is connected. Note: Will overwrite existing data if the attribute name already exists. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName OutAttributeName = NAME_None;

	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use MeshSelectorType and MeshSelectorInstance instead."))
	TArray<FPCGStaticMeshSpawnerEntry> Meshes_DEPRECATED;

	bool bForceConnectOutput = false;

protected:
	void RefreshMeshSelector();
	void RefreshInstancePacker();
};

class FPCGStaticMeshSpawnerElement : public FSimplePCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(const UPCGSettings* InSettings) const { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;	
	void SpawnStaticMeshInstances(FPCGContext* Context, const FPCGMeshInstanceList& InstanceList, AActor* TargetActor, const FPCGPackedCustomData& PackedCustomData) const;
};

