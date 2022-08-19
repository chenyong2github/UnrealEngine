// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusComputeDataInterface.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Components/ActorComponent.h"

#include "OptimusComponentSource.generated.h"


class UOptimusDeformer;


UCLASS(Abstract)
class OPTIMUSCORE_API UOptimusComponentSource :
	public UObject
{
	GENERATED_BODY()

public:
	/** Returns the component display name to show in the lister. Should be unique. */
	virtual FText GetDisplayName() const
	PURE_VIRTUAL(UOptimusComponentSource::GetDisplayName, return {}; );

	/** Returns a suggested name for the binding. The name may be modified to preserve uniqueness. */
	virtual FName GetBindingName() const
	PURE_VIRTUAL(UOptimusComponentSource::GetSuggestedBindingName, return {}; );

	/** Returns the actor component that this provider can operate on */
	virtual TSubclassOf<UActorComponent> GetComponentClass() const
	PURE_VIRTUAL(UOptimusComponentSource::GetComponentClass, return {}; );

	virtual TArray<FName> GetExecutionContexts() const
	PURE_VIRTUAL(UOptimusComponentSource::GetExecutionContexts, return {}; );

	/** Returns true if the source can be used by primary bindings. */
	bool IsUsableAsPrimarySource() const;

	// TODO: Component color for additional indicator wire.

	/** Returns all registered component source objects */
	static TArray<const UOptimusComponentSource*> GetAllSources();

	/** Returns a component source that matches a data interface, or nullptr if nothing does */
	static const UOptimusComponentSource* GetSourceFromDataInterface(
		const UOptimusComputeDataInterface* InDataInterface
		);
};


UCLASS()
class OPTIMUSCORE_API UOptimusComponentSourceBinding :
	public UObject
{
	GENERATED_BODY()

public:
	/** Returns the owning deformer to operate on this variable */
	// FIXME: Move to interface-based system.
	UOptimusDeformer* GetOwningDeformer() const;

	/** The name to give the binding, to disambiguate it from other bindings of same component type. */
	UPROPERTY(EditAnywhere, Category=Binding, meta = (EditCondition = "!bIsPrimaryBinding", HideEditConditionToggle))
	FName BindingName;

	/** The component type that this binding applies to */
	UPROPERTY(EditAnywhere, Category=Binding)
	TSubclassOf<UOptimusComponentSource> ComponentType;
	
	/** Component tags to automatically bind this component binding to. */
	UPROPERTY(EditAnywhere, Category=Tags, meta=(EditCondition="!bIsPrimaryBinding", HideEditConditionToggle))
	TArray<FName> ComponentTags;

	bool IsPrimaryBinding() const { return bIsPrimaryBinding; }

	static FName GetPrimaryBindingName();

	const UOptimusComponentSource* GetComponentSource() const; 

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditUndo() override;
	void PostEditUndo() override;
#endif
	
protected:
	friend class UOptimusDeformer;
	
	UPROPERTY()
	bool bIsPrimaryBinding = false;

	static const FName PrimaryBindingName;

private:
#if WITH_EDITORONLY_DATA
	FName BindingNameForUndo;
#endif
};
