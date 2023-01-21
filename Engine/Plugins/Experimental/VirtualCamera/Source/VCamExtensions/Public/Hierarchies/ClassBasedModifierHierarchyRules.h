// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseModifierGroup.h"
#include "ModifierHierarchyRules.h"
#include "Modifier/VCamModifier.h"
#include "Templates/Function.h"
#include "Templates/SubclassOf.h"
#include "ClassBasedModifierHierarchyRules.generated.h"

class UVCamComponent;

UCLASS(EditInlineNew)
class VCAMEXTENSIONS_API UClassBasedModifierGroup : public UBaseModifierGroup
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere, Category = "Virtual Camera|Hierarchies")
	TSet<TSubclassOf<UVCamModifier>> ModifierClasses;

	UPROPERTY(EditAnywhere, Instanced, Category = "Virtual Camera|Hierarchies")
	TArray<TObjectPtr<UClassBasedModifierGroup>> Children;
};

/**
 * 
 */
UCLASS()
class VCAMEXTENSIONS_API UClassBasedModifierHierarchyRules : public UModifierHierarchyRules
{
	GENERATED_BODY()
public:

	UClassBasedModifierHierarchyRules();

	//~ Begin UModifierHierarchyRules Interface
	virtual FName GetRootGroup_Implementation() const override;
	virtual bool GetParentGroup_Implementation(FName ChildGroup, FName& ParentGroup) const override;
	virtual bool GetGroupOfModifier_Implementation(UVCamModifier* Modifier, FName& Group) const override;
	virtual TSet<FName> GetChildGroups_Implementation(FName ParentGroup) const override;
	virtual TSet<UVCamModifier*> GetModifiersInGroup_Implementation(UVCamComponent* Component, FName GroupName) const override;
	//~ End UModifierHierarchyRules Interface

private:
	
	UPROPERTY(EditAnywhere, Instanced, Category = "Virtual Camera", meta = (NoResetToDefault))
	TObjectPtr<UClassBasedModifierGroup> RootGroup;

	enum class EBreakBehavior
	{
		Continue,
		Break
	};

	UClassBasedModifierGroup* FindGroupByName(FName GroupName) const;
	TSet<UVCamModifier*> EnumerateModifiersInGroup(UClassBasedModifierGroup& Group, UVCamComponent& Component) const;
	
	UClassBasedModifierGroup* FindBestMatchFor(UVCamModifier& Modifier) const;
	void ForEachGroup(TFunctionRef<EBreakBehavior(UClassBasedModifierGroup& CurrentGroup, UClassBasedModifierGroup* Parent)> Callback) const;
};
