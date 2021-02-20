// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementDetailsInterface.h"
#include "Elements/SMInstance/SMInstanceElementDetailsProxyObject.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"

class FSMInstanceTypedElementDetailsObject : public ITypedElementDetailsObject
{
public:
	explicit FSMInstanceTypedElementDetailsObject(const FSMInstanceElementId& InSMInstanceElementId)
		: InstanceProxyObject(NewObject<USMInstanceElementDetailsProxyObject>())
	{
		InstanceProxyObject->Initialize(InSMInstanceElementId);
	}

	~FSMInstanceTypedElementDetailsObject()
	{
		InstanceProxyObject->Shutdown();
	}

	virtual UObject* GetObject() override
	{
		return InstanceProxyObject;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(InstanceProxyObject);
	}

private:
	USMInstanceElementDetailsProxyObject* InstanceProxyObject = nullptr;
};

TUniquePtr<ITypedElementDetailsObject> USMInstanceElementDetailsInterface::GetDetailsObject(const FTypedElementHandle& InElementHandle)
{
	if (const FSMInstanceElementData* SMInstanceElement = InElementHandle.GetData<FSMInstanceElementData>())
	{
		return MakeUnique<FSMInstanceTypedElementDetailsObject>(SMInstanceElement->InstanceElementId);
	}
	return nullptr;
}
