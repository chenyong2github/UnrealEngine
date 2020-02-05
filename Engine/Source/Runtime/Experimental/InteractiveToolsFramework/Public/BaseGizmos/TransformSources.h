// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/TransformProxy.h"
#include "TransformSources.generated.h"



/**
 * UGizmoBaseTransformSource is a base implementation of IGizmoTransformSource that 
 * adds an OnTransformChanged delegate. This class cannot be used directly and must be subclassed.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoBaseTransformSource : public UObject, public IGizmoTransformSource
{
	GENERATED_BODY()
public:
	virtual FTransform GetTransform() const
	{
		return FTransform::Identity;
	}

	virtual void SetTransform(const FTransform& NewTransform)
	{
		check(false);   // not implemented
	}

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGizmoTransformSourceChanged, IGizmoTransformSource*);
	FOnGizmoTransformSourceChanged OnTransformChanged;
};




/**
 * UGizmoComponentWorldTransformSource implements IGizmoTransformSource (via UGizmoBaseTransformSource)
 * based on the internal transform of a USceneComponent.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoComponentWorldTransformSource : public UGizmoBaseTransformSource
{
	GENERATED_BODY()
public:

	virtual FTransform GetTransform() const override;

	virtual void SetTransform(const FTransform& NewTransform) override;

	UPROPERTY()
	USceneComponent* Component;

	/** If true, Component->Modify() is called on SetTransform */
	UPROPERTY()
	bool bModifyComponentOnTransform = true;


public:
	/**
	 * Construct a default instance of UGizmoComponentWorldTransformSource with the given Component
	 */
	static UGizmoComponentWorldTransformSource* Construct(
		USceneComponent* Component,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoComponentWorldTransformSource* NewSource = NewObject<UGizmoComponentWorldTransformSource>(Outer);
		NewSource->Component = Component;
		return NewSource;
	}
};




/**
 * FSeparateScaleProvider provides TFunction-implementable API that sets/gets a
 * Scaling Vector from an external source. 
 */
struct FSeparateScaleProvider
{
	TFunction<FVector(void)> GetScale = []() { return FVector::OneVector; };
	TFunction<void(FVector)> SetScale = [](FVector) {};
};


/**
 * UGizmoScaledTransformSource wraps another IGizmoTransformSource implementation and adds a
 * separate scaling vector to the Transform. The main use of this class is to support scaling
 * in a 3D gizmo without actually scaling the Gizmo itself. Generally our pattern is to apply
 * the gizmo's position/rotation transform to the target object via a TransformProxy, but
 * that does not work with Scaling. So this class stores the scaling vector separately, provided by
 * an external source via FSeparateScaleProvider, and in GetTransform/SetTransform rewrites the
 * Transform from the child IGizmoTransformSource with the new scale.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoScaledTransformSource : public UGizmoBaseTransformSource
{
	GENERATED_BODY()
public:

	/**
	 * IGizmoTransformSource implementation, returns child transform with local sclae
	 */
	virtual FTransform GetTransform() const override;

	/**
	 * IGizmoTransformSource implementation, removes scale and sends to ScaleProvider, then forwards remaining rotate+translate transform to child
	 */
	virtual void SetTransform(const FTransform& NewTransform) override;

	/**
	 * Child transform source
	 */
	UPROPERTY()
	TScriptInterface<IGizmoTransformSource> ChildTransformSource;

	/**
	 * Provider for external scale value/storage
	 */
	FSeparateScaleProvider ScaleProvider;

	/**
	 * Return the child transform with combined scale
	 */
	FTransform GetScaledTransform() const;

public:
	/**
	 * Construct a default instance of UGizmoComponentWorldTransformSource with the given Component
	 */
	static UGizmoScaledTransformSource* Construct(
		IGizmoTransformSource* ChildSource,
		FSeparateScaleProvider ScaleProviderIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoScaledTransformSource* NewSource = NewObject<UGizmoScaledTransformSource>(Outer);
		NewSource->ChildTransformSource = Cast<UObject>(ChildSource);
		NewSource->ScaleProvider = ScaleProviderIn;
		return NewSource;
	}
};






/**
 * UGizmoTransformProxyTransformSource implements IGizmoTransformSource (via UGizmoBaseTransformSource)
 * based on the internal transform of a UTransformProxy.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoTransformProxyTransformSource : public UGizmoBaseTransformSource
{
	GENERATED_BODY()
public:

	virtual FTransform GetTransform() const override;

	virtual void SetTransform(const FTransform& NewTransform) override;

	UPROPERTY()
	UTransformProxy* Proxy;

public:
	/**
	 * Construct a default instance of UGizmoComponentWorldTransformSource with the given Proxy
	 */
	static UGizmoTransformProxyTransformSource* Construct(
		UTransformProxy* Proxy,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoTransformProxyTransformSource* NewSource = NewObject<UGizmoTransformProxyTransformSource>(Outer);
		NewSource->Proxy = Proxy;
		return NewSource;
	}
};

