// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementDetailsInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"

class FComponentTypedElementDetailsObject : public ITypedElementDetailsObject
{
public:
	explicit FComponentTypedElementDetailsObject(UActorComponent* InComponent)
		: ComponentPtr(InComponent)
	{
	}

	virtual UObject* GetObject() override
	{
		return ComponentPtr.Get();
	}

private:
	TWeakObjectPtr<UActorComponent> ComponentPtr;
};

TUniquePtr<ITypedElementDetailsObject> UComponentElementDetailsInterface::GetDetailsObject(const FTypedElementHandle& InElementHandle)
{
	if (UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandle(InElementHandle))
	{
		return MakeUnique<FComponentTypedElementDetailsObject>(Component);
	}
	return nullptr;
}