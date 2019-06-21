// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "Misc/Guid.h"
#include "NiagaraSystem.h"
#include "NiagaraOverviewNodeStackItem.generated.h"

class UEdGraphPin;
class INiagaraCompiler;
struct FNiagaraGraphFunctionAliasContext;

#if WITH_EDITORONLY_DATA
UCLASS()
class UNiagaraOverviewNodeStackItem : public UEdGraphNode
{
	GENERATED_BODY()
public:
	UNiagaraOverviewNodeStackItem();
	void Initialize(const UNiagaraSystem* InOwningSystem);
	void Initialize(const UNiagaraSystem* InOwningSystem, FGuid InEmitterHandleGuid);
	const FGuid GetEmitterHandleGuid() const;

	//~ Begin UEdGraphNode Interface
	/** Gets the name of this node, shown in title bar */
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	//~ End UEdGraphNode Interface

private:
	UPROPERTY()
	UNiagaraSystem* OwningSystem;

	UPROPERTY()
	FGuid EmitterHandleGuid;
};
#endif
