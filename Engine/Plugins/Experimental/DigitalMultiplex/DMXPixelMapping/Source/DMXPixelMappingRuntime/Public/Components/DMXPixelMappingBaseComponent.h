// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingRuntimeCommon.h"
#include "UObject/Object.h"
#include "Stats/Stats.h"
#include "Tickable.h"
#include "DMXPixelMappingBaseComponent.generated.h"


DECLARE_STATS_GROUP(TEXT("DMXPixelMapping"), STATGROUP_DMXPIXELMAPPING, STATCAT_Advanced);

class UDMXPixelMappingRendererComponent;

/**
 * Base class for all DMX Pixel Mapping components
 */
UCLASS(BlueprintType, Blueprintable, Abstract)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingBaseComponent
	: public UObject
	, public FTickableGameObject
{
	GENERATED_BODY()

	DECLARE_DELEGATE_OneParam(FDMXOnPixelMappingComponentBeginDestroy, UDMXPixelMappingBaseComponent* /** Component about to be destroyed */);

public:
	/** Public constructor */
	UDMXPixelMappingBaseComponent();

	/*----------------------------------
		UDMXPixelMappingBaseComponent interface
	----------------------------------*/

	/**
	* Called when the component was assigned to its parent
	*/
	virtual void PostParentAssigned() {}

	/**
	* Called when the component was removed from its parent
	*/
	virtual void PostRemovedFromParent() {}

	/**
	 * Should log properties that were changed in underlying fixture patch or fixture type
	 *
	 * @return		Returns true if properties are valid.
	 */
	virtual bool ValidateProperties() { return true; }

	/**
	* Helper function for generating UObject name, the child should implement their own logic for Prefix name generation.
	*/
	virtual const FName& GetNamePrefix();

	// ~Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override {}
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickable() const override { return false; }
	// ~End FTickableGameObject interface

	/*----------------------------------------------------------
		Non virtual functions, not intended to be overridden
	----------------------------------------------------------*/

	/**
	 * Looking for the first child by given Class
	 *
	 * @return An instance of the templated Component
	 */
	template <typename TComponentClass>
	TComponentClass* GetFirstChildOfClass()
	{
		TComponentClass* FoundObject = nullptr;
		ForEachComponentOfClass<TComponentClass>([this, &FoundObject](TComponentClass* InObject)
		{
			FoundObject = InObject;
			return;
		}, true);

		return FoundObject;
	}

	/** Get the number of children components */
	int32 GetChildrenCount() const;

	/** Get the child component by the given index. */
	UDMXPixelMappingBaseComponent* GetChildAt(int32 Index) const;

	/** Get all children to belong to this component */
	const TArray<UDMXPixelMappingBaseComponent*>& GetChildren() const { return Children; }

	/** Gathers descendant child Components of a parent Component. */
	void GetChildComponentsRecursively(TArray<UDMXPixelMappingBaseComponent*>& Components);

	/**
	 * Add a new child componet
	 *
	 * @param InComponent    Component instance object
	 */
	void AddChild(UDMXPixelMappingBaseComponent* InComponent);

	/** Remove the child component by the given component object. */
	void RemoveChild(UDMXPixelMappingBaseComponent* InComponent);

	/** Remove all children */
	void ClearChildren();

	/** 
	 * Loop through all child by given Predicate 
	 *
	 * @param bIsRecursive		Should it loop recursively
	 */
	void ForEachChild(TComponentPredicate Predicate, bool bIsRecursive);

	/** DEPRECATED 4.27  */
	template <typename TComponentClass>
	UE_DEPRECATED(4.27, "Use ForEachChildOfClass in favor of a clearer name instead.")
	void ForEachComponentOfClass(TComponentPredicateType<TComponentClass> Predicate, bool bIsRecursive)
	{
		ForEachChild([&Predicate](UDMXPixelMappingBaseComponent* InComponent) {
			if (TComponentClass* CastComponent = Cast<TComponentClass>(InComponent))
			{
				Predicate(CastComponent);
			}
		}, bIsRecursive);
	}

	/** 
	 * Loop through all templated child class by given Predicate
	 *
	 * @param bIsRecursive		Should it loop recursively
	 */
	template <typename TComponentClass>
	void ForEachChildOfClass(TComponentPredicateType<TComponentClass> Predicate, bool bIsRecursive)
	{
		ForEachChild([&Predicate](UDMXPixelMappingBaseComponent* InComponent) {
			if (TComponentClass* CastComponent = Cast<TComponentClass>(InComponent))
			{
				Predicate(CastComponent);
			}
		}, bIsRecursive);
	}

	/** Get Pixel Mapping asset UObject */
	UDMXPixelMapping* GetPixelMapping();

	/** Get root component of the component tree */
	const UDMXPixelMappingRootComponent* GetRootComponent() const;

	/** Get the root component and not allow a null option. */
	const UDMXPixelMappingRootComponent* GetRootComponentChecked() const;

	/**
	 * Get renderer component associated with current component
	 * It could be this component itself if this component is root component
	 * It could be parent component if that is pixel component
	 * It could be nullptr if that is Root component
	 */
	UDMXPixelMappingRendererComponent* GetRendererComponent();

	/*----------------------------------------------------------
		Public blueprint accessible function
	----------------------------------------------------------*/

	/** Reset all sending DMX channels to 0 for this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void ResetDMX() {};

	/** Send DMX values of this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void SendDMX() {};

	/** Render downsample texture for this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void Render() {};

	/** Render downsample texture and send DMX for this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void RenderAndSendDMX() {};

public:
	/*----------------------------------------------------------
		Public static functions
	----------------------------------------------------------*/

	/**
	 * Recursively looking for the first parent by given Class
	 *
	 * @return An instance of the templated Component
	 */
	template <typename TComponentClass>
	static TComponentClass* GetFirstParentByClass(UDMXPixelMappingBaseComponent* InComponent)
	{
		if (InComponent->Parent == nullptr)
		{
			return nullptr;
		}

		if (TComponentClass* SearchComponentbyType = Cast<TComponentClass>(InComponent->Parent))
		{
			return SearchComponentbyType;
		}
		else
		{
			return GetFirstParentByClass<TComponentClass>(InComponent->Parent);
		}
	}

	/** Recursively loop through all child by given Component and Predicate */
	static void ForComponentAndChildren(UDMXPixelMappingBaseComponent* Component, TComponentPredicate Predicate);

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const
	{
		return false;
	}

#if WITH_EDITOR
	/** Returns the name of the component used across all widgets that draw it */
	virtual FString GetUserFriendlyName() const;
#endif 

public:
	/** Array of children belong to this component */
	UPROPERTY(Instanced)
	TArray<UDMXPixelMappingBaseComponent*> Children;

	/** Parent component */
	UPROPERTY(Instanced)
	UDMXPixelMappingBaseComponent* Parent;
};
