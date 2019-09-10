// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/TransformProxy.h"
#include "Components/SceneComponent.h"


void UTransformProxy::AddComponent(USceneComponent* Component, bool bModifyComponentOnTransform)
{
	check(Component);

	FRelativeObject NewObj;
	NewObj.Component = Component;
	NewObj.bModifyComponentOnTransform = bModifyComponentOnTransform;
	NewObj.StartTransform = Component->GetComponentToWorld();
	NewObj.RelativeTransform = FTransform::Identity;
	Objects.Add(NewObj);

	UpdateSharedTransform();
	OnTransformChanged.Broadcast(this, SharedTransform);
}


FTransform UTransformProxy::GetTransform() const
{
	return SharedTransform;
}

void UTransformProxy::SetTransform(const FTransform& TransformIn)
{
	SharedTransform = TransformIn;

	UpdateObjects();

	OnTransformChanged.Broadcast(this, SharedTransform);
}




void UTransformProxy::UpdateObjects()
{
	for (FRelativeObject& Obj : Objects)
	{
		FTransform CombinedTransform;
		FTransform::Multiply(&CombinedTransform, &SharedTransform, &Obj.RelativeTransform);
		
		if (Obj.Component.IsValid())
		{
			if (Obj.bModifyComponentOnTransform)
			{
				Obj.Component->Modify();
			}

			Obj.Component->SetWorldTransform(CombinedTransform);
		}
	}
}




void UTransformProxy::UpdateSharedTransform()
{
	if (Objects.Num() == 0)
	{
		SharedTransform = FTransform::Identity;
	}
	else if (Objects.Num() == 1)
	{
		SharedTransform = Objects[0].StartTransform;

		Objects[0].RelativeTransform = FTransform::Identity;
	}
	else
	{
		FVector Origin = FVector::ZeroVector;
		for (const FRelativeObject& Obj : Objects)
		{
			Origin += Obj.StartTransform.GetLocation();
		}
		Origin /= (float)Objects.Num();

		SharedTransform = FTransform(Origin);

		for (FRelativeObject& Obj : Objects)
		{
			Obj.RelativeTransform = Obj.StartTransform;
			Obj.RelativeTransform.SetToRelativeTransform(SharedTransform);
		}
	}

}