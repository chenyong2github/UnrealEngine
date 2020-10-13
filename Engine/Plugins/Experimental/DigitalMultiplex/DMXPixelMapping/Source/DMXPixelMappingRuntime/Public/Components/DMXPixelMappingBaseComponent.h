// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingRuntimeCommon.h"
#include "UObject/Object.h"
#include "Tickable.h"
#include "DMXPixelMappingBaseComponent.generated.h"

/**
 * Base class for all DMX Pixel Mapping components
 */
UCLASS(BlueprintType, Blueprintable)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingBaseComponent
	: public UObject
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Public constructor */
	UDMXPixelMappingBaseComponent();

	/*----------------------------------
		UDMXPixelMappingBaseComponent interface
	----------------------------------*/

	/**
	* The function might have custom behavior implementation after object has been assigned to the parent.
	*/
	virtual void PostParentAssigned() {}

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
	virtual bool IsTickableInEditor() const { return true; }
	virtual bool IsTickableWhenPaused() const { return true; }
	virtual bool IsTickable() const { return false; }
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
		});

		return FoundObject;
	}

	/** Get the number of children components */
	int32 GetChildrenCount() const;

	/** Get the index of this component, it returns -1 if the component doesn't have a parent */
	int32 GetChildIndex() const { return ChildIndex; }

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
	 * @return An index of added comonent
	 */
	int32 AddChild(UDMXPixelMappingBaseComponent* InComponent);

	/** Remove the child component by the given index. */
	bool RemoveChildAt(int32 Index);

	/** Remove the child component by the given component object. */
	bool RemoveChild(UDMXPixelMappingBaseComponent* InComponent);

	/** Remove all children */
	void ClearChildren();

	/** 
	 * Loop through all child by given Predicate 
	 *
	 * @param bIsRecursive		Should it loop recursively
	 */
	void ForEachChild(TComponentPredicate Predicate, bool bIsRecursive);

	/** 
	 * Loop through all templated child class by given Predicate
	 *
	 * @param bIsRecursive		Should it loop recursively
	 */
	template <typename TComponentClass>
	void ForEachComponentOfClass(TComponentPredicateType<TComponentClass> Predicate, bool bIsRecursive)
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
	UDMXPixelMappingRootComponent* GetRootComponent();

	/*----------------------------------------------------------
		Public blueprint accessible function
	----------------------------------------------------------*/

	/** Reset all sending DMX channels to 0 for this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void ResetDMX() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::ResetDMX);

	/** Send DMX values of this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void SendDMX() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::SendDMX);

	/** Render downsample texture for this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void Render() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::Render);

	/** Render downsample texture and send DMX for this component and all children */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	virtual void RenderAndSendDMX() PURE_VIRTUAL(UDMXPixelMappingBaseComponent::RenderAndSendDMX);

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
#endif // WITH_EDITOR

private:
	/** Set array index of this componet. It should be called if component belong to some parent */
	void SetChildIndex(int32 InIndex);

public:
	/** Array of children belong to this component */
	UPROPERTY(Instanced)
	TArray<UDMXPixelMappingBaseComponent*> Children;

	/** Parent component */
	UPROPERTY(Instanced)
	UDMXPixelMappingBaseComponent* Parent;

private:
	/** Index of this component */
	UPROPERTY()
	int32 ChildIndex;
};
