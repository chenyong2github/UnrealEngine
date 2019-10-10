// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

