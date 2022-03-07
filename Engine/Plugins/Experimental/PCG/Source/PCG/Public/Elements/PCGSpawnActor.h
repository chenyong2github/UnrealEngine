// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "PCGSubgraph.h"
#include "Templates/SubclassOf.h"

#include "PCGSpawnActor.generated.h"

class AActor;

UENUM()
enum class EPCGSpawnActorOption : uint8
{
	CollapseActors,
	MergePCGOnly,
	NoMerging
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSpawnActorSettings : public UPCGBaseSubgraphSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSubclassOf<AActor> TemplateActorClass = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGSpawnActorOption Option = EPCGSpawnActorOption::CollapseActors;

	//~Begin UCPGSettings interface
	virtual UPCGNode* CreateNode() const override;

#if WITH_EDITOR	
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SpawnActorNode")); }
#endif
protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	//~Begin UPCGBaseSubgraphSettings interface
	virtual UPCGGraph* GetSubgraph() const override;

protected:
#if WITH_EDITOR
	virtual bool IsStructuralProperty(const FName& InPropertyName) const override;
#endif
	//~End UPCGBaseSubgraphSettings interface
};

UCLASS(ClassGroup = (Procedural))
class PCG_API UPCGSpawnActorNode : public UPCGBaseSubgraphNode
{
	GENERATED_BODY()
public:
	/** ~Begin UPCGBaseSubgraphNode interface */
	virtual TObjectPtr<UPCGGraph> GetSubgraph() const override;
	/** ~End UPCGBaseSubgraphNode interface */
};

class FPCGSpawnActorElement : public FSimpleTypedPCGElement<UPCGSpawnActorSettings>
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};
