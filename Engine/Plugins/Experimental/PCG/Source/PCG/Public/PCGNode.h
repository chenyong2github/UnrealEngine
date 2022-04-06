// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGNode.generated.h"

class UPCGSettings;
class UPCGGraph;
class UPCGEdge;
class IPCGElement;

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGNodeSettingsChanged, UPCGNode*);
#endif

UCLASS(ClassGroup = (Procedural))
class PCG_API UPCGNode : public UObject
{
	GENERATED_BODY()

	friend class UPCGGraph;
	friend class UPCGEdge;
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
	UPCGNode* AddEdgeTo(FName InboundName, UPCGNode* To, FName OutboundName);

	/** Returns true if one of the input pins matches with the given name */
	bool HasInLabel(const FName& Label) const;

	/** Returns true if one of the output pins matches with the given name */
	bool HasOutLabel(const FName& Label) const;

	/** Returns all the input labels */
	TArray<FName> InLabels() const;

	/** Returns all the output labels */
	TArray<FName> OutLabels() const;

	/** Returns true if the input pin is connected */
	bool IsInputPinConnected(const FName& Label) const;

	/** Returns true if the output pin is connected */
	bool IsOutputPinConnected(const FName& Label) const;

	/** Changes the default settings in the node */
	void SetDefaultSettings(TObjectPtr<UPCGSettings> InSettings);

	const TArray<TObjectPtr<UPCGEdge>>& GetOutboundEdges() const { return OutboundEdges; }

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
	bool HasEdgeTo(UPCGNode* InNode) const;

	void RemoveInboundEdge(UPCGEdge* InEdge);
	void RemoveOutboundEdge(UPCGEdge* InEdge);
	UPCGEdge* GetOutboundEdge(const FName& FromLabel, UPCGNode* To, const FName& ToLabel);

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnSettingsChanged(UPCGSettings* InSettings);
#endif

	UPROPERTY()
	TArray<TObjectPtr<UPCGNode>> OutboundNodes_DEPRECATED;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Node)
	TArray<TObjectPtr<UPCGEdge>> InboundEdges;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Node)
	TArray<TObjectPtr<UPCGEdge>> OutboundEdges;

	// TODO: add this information:
	// - Ability to run on non-game threads (here or element)
	// - Ability to be multithreaded (here or element)
	// - Generates artifacts (here or element)
	// - Priority
};
