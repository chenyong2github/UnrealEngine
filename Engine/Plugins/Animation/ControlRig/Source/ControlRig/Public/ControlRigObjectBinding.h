// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "IControlRigObjectBinding.h"

class USceneComponent;

class CONTROLRIG_API FControlRigObjectBinding : public IControlRigObjectBinding
{
public:
	
	virtual ~FControlRigObjectBinding();

	// IControlRigObjectBinding interface
	virtual void BindToObject(UObject* InObject) override;
	virtual void UnbindFromObject() override;
	virtual FControlRigBind& OnControlRigBind() override { return ControlRigBind; }
	virtual FControlRigUnbind& OnControlRigUnbind() override { return ControlRigUnbind; }
	virtual bool IsBoundToObject(UObject* InObject) const override;
	virtual UObject* GetBoundObject() const override;
	virtual AActor* GetHostingActor() const override;

private:
	/** The scene component we are bound to */
	TWeakObjectPtr<USceneComponent> SceneComponent;

	FControlRigBind ControlRigBind;
	FControlRigUnbind ControlRigUnbind;
};