// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGNode.generated.h"

class UPCGSettings;
class UPCGGraph;
class IPCGElement;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGNodeSettingsChanged, UPCGNode*);
#endif

UCLASS(ClassGroup = (Procedural))
class UPCGNode : public UObject
{
	GENERATED_BODY()

	friend class UPCGGraph;
	friend class FPCGGraphCompiler;

public:
	UPCGNode(const FObjectInitializer& ObjectInitializer);
	
	/** ~Begin UObject interface */
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** ~End UObject interface */

	/** Returns the owning graph */
	UFUNCTION(BlueprintCallable, Category = Node)
	UPCGGraph* GetGraph() const;

	/** Adds an edge in the owning graph to the given "To" node. */
	UFUNCTION(BlueprintCallable, Category = Node)
	UPCGNode* AddEdgeTo(UPCGNode* To);

	/** Changes the default settings in the node */
	void SetDefaultSettings(TObjectPtr<UPCGSettings> InSettings);

	const TArray<TObjectPtr<UPCGNode>>& GetOutboundNodes() const { return OutboundNodes; }

	/** Note: do not set this property directly from code, use SetDefaultSettings instead */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Node, meta=(EditInline))
	TObjectPtr<UPCGSettings> DefaultSettings;

#if WITH_EDITOR
	FOnPCGNodeSettingsChanged OnNodeSettingsChangedDelegate;
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	int32 PositionX;

	UPROPERTY()
	int32 PositionY;
#endif // WITH_EDITORONLY_DATA

protected:
	void ConnectTo(UPCGNode* InSuccessor);
	void ConnectFrom(UPCGNode* InPredecessor);

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnSettingsChanged(UPCGSettings* InSettings);
#endif

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Node)
	TArray<TObjectPtr<UPCGNode>> InboundNodes;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Node)
	TArray<TObjectPtr<UPCGNode>> OutboundNodes;

	// TODO: add this information:
	// - Ability to run on non-game threads (here or element)
	// - Ability to be multithreaded (here or element)
	// - Generates artifacts (here or element)
	// - Priority
};