// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EdGraph/EdGraphNode.h"
#include "NiagaraOverviewNode.generated.h"

class UNiagaraSystem;

UCLASS()
class NIAGARAEDITOR_API UNiagaraOverviewNode : public UEdGraphNode
{
	GENERATED_BODY()
public:
	UNiagaraOverviewNode();
	void Initialize(UNiagaraSystem* InOwningSystem);
	void Initialize(UNiagaraSystem* InOwningSystem, FGuid InEmitterHandleGuid);
	const FGuid GetEmitterHandleGuid() const;

	//~ Begin UEdGraphNode Interface
	/** Gets the name of this node, shown in title bar */
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	virtual FLinearColor GetNodeTitleColor() const override;

	/** Whether or not this node can be deleted by user action */
	virtual bool CanUserDeleteNode() const override;

	/** Whether or not this node can be safely duplicated (via copy/paste, etc...) in the graph */
	virtual bool CanDuplicateNode() const override;

	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	//~ End UEdGraphNode Interface

	UNiagaraSystem* GetOwningSystem();

private:
	UPROPERTY()
	UNiagaraSystem* OwningSystem;

	UPROPERTY()
	FGuid EmitterHandleGuid;

	static bool bColorsAreInitialized;
	static FLinearColor SystemColor;
	static FLinearColor EmitterColor;
};
