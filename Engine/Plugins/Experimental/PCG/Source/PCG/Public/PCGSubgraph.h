// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSettings.h"

class UPCGGraph;

#include "PCGSubgraph.generated.h"

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGStructuralSettingsChanged, UPCGSettings*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGNodeStructuralSettingsChanged, UPCGNode*);
#endif

UCLASS(BlueprintType, ClassGroup=(Procedural))
class UPCGSubgraphSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~UPCGSettings interface implementation
	virtual UPCGNode* CreateNode() const override;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("SubgraphNode")); }
	virtual TArray<FName> GetTrackedActorTags() const override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface implementation

protected:
	//~Begin UObject interface implementation
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~End UObject interface implementation

	void OnSubgraphChanged(UPCGGraph* InGraph, bool bIsStructural);
#endif

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TObjectPtr<UPCGGraph> Subgraph;

#if WITH_EDITOR
	FOnPCGStructuralSettingsChanged OnStructuralSettingsChangedDelegate;
#endif
};

UCLASS(ClassGroup = (Procedural))
class UPCGSubgraphNode : public UPCGNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bAllowDynamicGraph = false;

#if WITH_EDITOR
	FOnPCGNodeStructuralSettingsChanged OnNodeStructuralSettingsChangedDelegate;
#endif

	TObjectPtr<UPCGGraph> GetGraph() const;

protected:	
	/** ~Begin UObject interface */
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** ~End UObject interface */

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnStructuralSettingsChanged(UPCGSettings* InSettings);
#endif
};