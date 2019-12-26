// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BaseGizmos/StateTargets.h"
#include "TransformProxy.generated.h"

class USceneComponent;

/**
 * UTransformProxy is used to transform a set of sub-ojects. An internal
 * FTransform is generated based on the sub-object set, and the relative
 * FTransform of each sub-object is stored. Then as this main transform
 * is updated, the sub-objects are also updated.
 * 
 * Currently only USceneComponent sub-objects are supported.
 * 
 * If only one sub-object is set, the main transform is the sub-object transform.
 * Otherwise the main transform is centered at the average origin and
 * has no rotation.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UTransformProxy : public UObject
{
	GENERATED_BODY()
public:

	/**
	 * Add a component sub-object to the proxy set. 
	 * @param bModifyComponentOnTransform if true, Component->Modify() is called before the Component transform is updated
	 * @warning The internal shared transform is regenerated each time a component is added
	 */
	virtual void AddComponent(USceneComponent* Component, bool bModifyComponentOnTransform = true);

	/**
	 * @return the shared transform for all the sub-objects
	 */
	virtual FTransform GetTransform() const;

	/**
	 * Update the main transform and then update the sub-objects based on their relative transformations
	 */
	virtual void SetTransform(const FTransform& Transform);


public:
	/**
	 * This delegate is fired whenever the internal transform changes, ie
	 * on AddComponent and SetTransform
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTransformChanged, UTransformProxy*, FTransform);
	FOnTransformChanged OnTransformChanged;

	/**
	 * If true, relative rotation of shared transform is applied to objects before relative translation (ie they rotate in place)
	 */
	UPROPERTY()
	bool bRotatePerObject = false;

	/**
	 * If true, then on SetTransform() the components are not moved, and their local transforms are recalculated
	 */
	UPROPERTY()
	bool bSetPivotMode = false;

	
protected:

	struct FRelativeObject
	{
		TWeakObjectPtr<USceneComponent> Component;
		bool bModifyComponentOnTransform;

		/** The initial transform of the object, set during UpdateSharedTransform() */
		FTransform StartTransform;
		/** The transform of the object relative to */
		FTransform RelativeTransform;
	};

	/** List of sub-objects */
	TArray<FRelativeObject> Objects;

	/** The main transform */
	UPROPERTY()
	FTransform SharedTransform;

	/** The main transform */
	UPROPERTY()
	FTransform InitialSharedTransform;

	/** Recalculate main SharedTransform when object set changes*/
	virtual void UpdateSharedTransform();

	/** Recalculate per-object relative transforms */
	virtual void UpdateObjectTransforms();

	/** Propagate a transform update to the sub-objects */
	virtual void UpdateObjects();
};



/**
 * FTransformProxyChange tracks a change to the base transform for a TransformProxy
 */
class INTERACTIVETOOLSFRAMEWORK_API FTransformProxyChange : public FToolCommandChange
{
public:
	FTransform From;
	FTransform To;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;

	virtual FString ToString() const override { return TEXT("FTransformProxyChange"); }
};

/**
 * FTransformProxyChangeSource generates FTransformProxyChange instances on Begin/End.
 * Instances of this class can (for example) be attached to a UGizmoTransformChangeStateTarget for use TransformGizmo change tracking.
 */
class INTERACTIVETOOLSFRAMEWORK_API FTransformProxyChangeSource : public IToolCommandChangeSource
{
public:
	FTransformProxyChangeSource(UTransformProxy* ProxyIn)
	{
		Proxy = ProxyIn;
	}

	virtual ~FTransformProxyChangeSource() {}

	TWeakObjectPtr<UTransformProxy> Proxy;
	TUniquePtr<FTransformProxyChange> ActiveChange;

	virtual void BeginChange() override;
	virtual TUniquePtr<FToolCommandChange> EndChange() override;
	virtual UObject* GetChangeTarget() override;
	virtual FText GetChangeDescription() override;
};