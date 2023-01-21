// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModifierHierarchyRules.h"
#include "Modifier/VCamModifier.h"
#include "Templates/SubclassOf.h"
#include "ClassBasedModifierHierarchyRules.generated.h"

class UVCamComponent;

UCLASS(EditInlineNew)
class VCAMEXTENSIONS_API UClassBasedModifierGroup : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Virtual Camera")
	FName GroupName;
	
	UPROPERTY(EditAnywhere, Category = "Virtual Camera")
	TSet<TSubclassOf<UVCamModifier>> ModifierClasses;

	UPROPERTY(EditAnywhere, Instanced, Category = "Virtual Camera")
	TArray<TObjectPtr<UClassBasedModifierGroup>> Children;

	virtual void PostInitProperties() override;
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
	virtual bool GetGroupOfModifier_Implementation(UVCamModifier* Modifier, FName& Group) const override;
	virtual TSet<FName> GetChildGroups_Implementation(FName ParentGroup) const override;
	virtual TSet<UVCamModifier*> GetModifiersInGroup_Implementation(UVCamComponent* Component, FName GroupName) const override;
	//~ End UModifierHierarchyRules Interface

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif

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
	void ForEachGroup(TFunctionRef<EBreakBehavior(UClassBasedModifierGroup& Group)> Callback) const;
};
