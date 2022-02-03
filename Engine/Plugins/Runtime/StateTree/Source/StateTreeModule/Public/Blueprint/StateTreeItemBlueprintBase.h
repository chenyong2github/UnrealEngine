// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreeItemBlueprintBase.generated.h"

struct FStateTreeExecutionContext;

UENUM()
enum class EStateTreeBlueprintPropertyCategory : uint8
{
	NotSet,
	Input,	
	Parameter,
	Output,
	ExternalData,
};

/** Additional information for properties, captured from property metadata. */
USTRUCT()
struct STATETREEMODULE_API FStateTreeBlueprintPropertyInfo
{
	GENERATED_BODY()

	FStateTreeBlueprintPropertyInfo() = default;

	FStateTreeBlueprintPropertyInfo(const FName InPropertyName, const EStateTreeBlueprintPropertyCategory InCategory)
		: PropertyName(InPropertyName), Category(InCategory)
	{}
	
	UPROPERTY()
	FName PropertyName;

	UPROPERTY()
	EStateTreeBlueprintPropertyCategory Category = EStateTreeBlueprintPropertyCategory::NotSet;
};

/** Struct use to copy external data to the Blueprint item instance, resolved during StateTree linking. */
struct STATETREEMODULE_API FStateTreeBlueprintExternalDataHandle
{
	const FProperty* Property = nullptr;
	FStateTreeExternalDataHandle Handle;
};


UCLASS(Abstract)
class STATETREEMODULE_API UStateTreeItemBlueprintBase : public UObject
{
	GENERATED_BODY()

public:
	/** Called during link to collect external data handles. */
	void LinkExternalData(FStateTreeLinker& Linker, TArray<FStateTreeBlueprintExternalDataHandle>& OutExternalDataHandles) const;
	/** Called before call to logic functions to copy data from */
	void CopyExternalData(FStateTreeExecutionContext& Context, TConstArrayView<FStateTreeBlueprintExternalDataHandle> ExternalDataHandles);

protected:
	virtual UWorld* GetWorld() const override;
	AActor* GetOwnerActor(const FStateTreeExecutionContext& Context) const;

#if WITH_EDITOR
	virtual void PostCDOCompiled() override;
#endif

	/** Metadata for properties */
	UPROPERTY()
	TArray<FStateTreeBlueprintPropertyInfo> PropertyInfos;
};
