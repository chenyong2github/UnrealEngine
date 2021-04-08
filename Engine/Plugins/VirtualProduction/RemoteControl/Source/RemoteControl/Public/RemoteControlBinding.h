// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/UnrealType.h"
#include "RemoteControlBinding.generated.h"

/**
 * Acts as a bridge between an exposed property/function and an object to act on.
 */
UCLASS(Abstract, BlueprintType)
class REMOTECONTROL_API URemoteControlBinding : public UObject
{
public:
	GENERATED_BODY()

	/**
	 * Set the object this binding should represent.
	 */
	virtual void SetBoundObject(const TSoftObjectPtr<UObject>& InObject) PURE_VIRTUAL(URemoteControlBinding::SetBoundObject,);

	/**
	 * Unset the underlying object this binding currently represents.
	 */
	virtual void UnbindObject(const TSoftObjectPtr<UObject>& InBoundObject) PURE_VIRTUAL(URemoteControlBinding::UnbindObject,);

	/**
	 * Resolve the bound object for the current map.
	 * @Note Will return the PIE object when in a PIE session.
	 */
	virtual UObject* Resolve() const PURE_VIRTUAL(URemoteControlBinding::Resolve, return nullptr;);

	/**
	 * Whether this binding represents a valid object.
	 */
	virtual bool IsValid() const PURE_VIRTUAL(URemoteControlBinding::IsValid, return false;);

	/**
	 * Check if the object is bound.
	 */
	virtual bool IsBound(const TSoftObjectPtr<UObject>& Object) const PURE_VIRTUAL(URemoteControlBinding::IsBound, return false;);

public:
	/**
	 * The name of this binding. Defaults to the bound object's name.
	 */
	UPROPERTY(EditAnywhere, Category=Default)
	FString Name;
};

UCLASS(BlueprintType)
class REMOTECONTROL_API URemoteControlLevelIndependantBinding : public URemoteControlBinding
{
public:
	GENERATED_BODY()

	virtual void SetBoundObject(const TSoftObjectPtr<UObject>& InObject) override;
	virtual void UnbindObject(const TSoftObjectPtr<UObject>& InBoundObject) override;
	virtual UObject* Resolve() const override;
	virtual bool IsValid() const override;
	virtual bool IsBound(const TSoftObjectPtr<UObject>& Object) const override;

private:
	/**
	 * Holds the bound object.
	 */
	UPROPERTY()
	TSoftObjectPtr<UObject> BoundObject;
};

UCLASS(BlueprintType)
class REMOTECONTROL_API URemoteControlLevelDependantBinding : public URemoteControlBinding
{
public:
	GENERATED_BODY()

	//~ Begin URemoteControlBinding Interface
	virtual void SetBoundObject(const TSoftObjectPtr<UObject>& BoundObject) override;
	virtual void UnbindObject(const TSoftObjectPtr<UObject>& BoundObject) override;
	virtual UObject* Resolve() const override;
	virtual bool IsValid() const override;
	virtual bool IsBound(const TSoftObjectPtr<UObject>& Object) const override;
	//~ Begin URemoteControlBinding Interface

	/**
	 *	Set the bound object by specifying the level it resides in.
	 *	@Note Useful is you want to set the bound object without loading the level/object.
	 */
	void SetBoundObject(const TSoftObjectPtr<ULevel>& Level, const TSoftObjectPtr<UObject>& BoundObject);

	/**
	 * Initialize this binding to be used with the new current level.
	 * Copies the binding from the last successful resolve in case the level was duplicated.
	 */
	void InitializeForNewLevel();

private:
	TSoftObjectPtr<UObject> FindObjectFromCurrentWorld() const;

private:
	/**
	 *	The map bound objects with their level as key.
	 */
	UPROPERTY()
	TMap<TSoftObjectPtr<ULevel>, TSoftObjectPtr<UObject>> BoundObjectMap;

	/**
	 * Caches the last level that had a successful resolve.
	 * Used to decide which level to use when reinitializing this binding in a new level.
	 */
	UPROPERTY()
	mutable TSoftObjectPtr<ULevel> LevelWithLastSuccessfulResolve;

private:
	/** Get the current world. */
	UWorld* GetCurrentWorld() const;
};
