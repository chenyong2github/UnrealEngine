// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseModifierGroup.h"
#include "ModifierHierarchyRules.h"
#include "Templates/Function.h"
#include "UI/VCamConnectionStructs.h"
#include "TargetModifierPerGroupHierarchyRules.generated.h"

UCLASS(EditInlineNew)
class VCAMEXTENSIONS_API USingleModifierPerGroupWithTargetSettings : public UBaseModifierGroup
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Hierarchy")
	FVCamConnectionTargetSettings TargetSettings;

	UPROPERTY(EditAnywhere, Instanced, Category = "Hierarchy")
	TArray<TObjectPtr<USingleModifierPerGroupWithTargetSettings>> ChildElements;
};

/**
 * 
 */
UCLASS()
class VCAMEXTENSIONS_API UTargetModifierPerGroupHierarchyRules : public UModifierHierarchyRules
{
	GENERATED_BODY()
public:
	
	UTargetModifierPerGroupHierarchyRules();

	//~ Begin UModifierHierarchyRules Interface
	virtual FName GetRootGroup_Implementation() const override;
	virtual bool GetParentGroup_Implementation(FName ChildGroup, FName& ParentGroup) const override;
	virtual bool GetGroupOfModifier_Implementation(UVCamModifier* Modifier, FName& Group) const override;
	virtual bool GetConnectionPointForModifier_Implementation(UVCamModifier* Modifier, FName& ConnectionPoint) const override;
	virtual TSet<FName> GetChildGroups_Implementation(FName ParentGroup) const override;
	virtual TSet<UVCamModifier*> GetModifiersInGroup_Implementation(UVCamComponent* Component, FName GroupName) const override;
	//~ End UModifierHierarchyRules Interface

private:

	UPROPERTY(EditAnywhere, Instanced, Category = "Hierarchy")
	TObjectPtr<USingleModifierPerGroupWithTargetSettings> Root;

	USingleModifierPerGroupWithTargetSettings* GetNodeForModifier(UVCamModifier* Modifier) const;
	USingleModifierPerGroupWithTargetSettings* FindGroupByName(FName GroupName) const;
	
	enum class EBreakBehavior
	{
		Continue,
		Break
	};
	void ForEachGroup(TFunctionRef<EBreakBehavior(USingleModifierPerGroupWithTargetSettings& CurrentGroup, USingleModifierPerGroupWithTargetSettings* Parent)> Callback) const;
};
