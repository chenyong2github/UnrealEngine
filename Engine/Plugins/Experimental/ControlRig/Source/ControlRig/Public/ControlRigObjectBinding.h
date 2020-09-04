// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "IControlRigObjectBinding.h"

class USceneComponent;

class CONTROLRIG_API FControlRigObjectBinding : public IControlRigObjectBinding
{
public:
	// IControlRigObjectBinding interface
	virtual void BindToObject(UObject* InObject) override;
	virtual void UnbindFromObject() override;
	virtual bool IsBoundToObject(UObject* InObject) const override;
	virtual UObject* GetBoundObject() const override;
	virtual AActor* GetHostingActor() const override;

private:
	/** The scene component we are bound to */
	TWeakObjectPtr<USceneComponent> SceneComponent;
};