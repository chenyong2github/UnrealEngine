// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/TransformProxy.h"
#include "Components/SceneComponent.h"


#define LOCTEXT_NAMESPACE "UTransformProxy"


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

	if (bSetPivotMode)
	{
		UpdateObjectTransforms();
	}
	else
	{
		UpdateObjects();
		OnTransformChanged.Broadcast(this, SharedTransform);
	}
}


void UTransformProxy::BeginTransformEditSequence()
{
	OnBeginTransformEdit.Broadcast(this);
}

void UTransformProxy::EndTransformEditSequence()
{
	OnEndTransformEdit.Broadcast(this);
}





void UTransformProxy::UpdateObjects()
{
	for (FRelativeObject& Obj : Objects)
	{
		FTransform CombinedTransform;
		if (bRotatePerObject && Objects.Num() > 1)
		{
			FTransform Temp = SharedTransform.GetRelativeTransform(InitialSharedTransform);

			CombinedTransform = Obj.StartTransform;
			CombinedTransform.AddToTranslation(Temp.GetTranslation());
			CombinedTransform.ConcatenateRotation(Temp.GetRotation());
			CombinedTransform.SetScale3D(CombinedTransform.GetScale3D() * Temp.GetScale3D());
		}
		else
		{
			FTransform::Multiply(&CombinedTransform, &Obj.RelativeTransform, &SharedTransform);
		}
		
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

	InitialSharedTransform = SharedTransform;
}



void UTransformProxy::UpdateObjectTransforms()
{
	for (FRelativeObject& Obj : Objects)
	{
		if (Obj.Component != nullptr)
		{
			Obj.StartTransform = Obj.Component->GetComponentToWorld();
		}
		Obj.RelativeTransform = Obj.StartTransform;
		Obj.RelativeTransform.SetToRelativeTransform(SharedTransform);
	}
}





void FTransformProxyChange::Apply(UObject* Object)
{
	UTransformProxy* Proxy = CastChecked<UTransformProxy>(Object);
	Proxy->SetTransform(To);
}

void FTransformProxyChange::Revert(UObject* Object)
{
	UTransformProxy* Proxy = CastChecked<UTransformProxy>(Object);
	Proxy->SetTransform(From);
}


void FTransformProxyChangeSource::BeginChange()
{
	if (Proxy.IsValid())
	{
		ActiveChange = MakeUnique<FTransformProxyChange>();
		ActiveChange->From = Proxy->GetTransform();
		Proxy->BeginTransformEditSequence();
	}
}

TUniquePtr<FToolCommandChange> FTransformProxyChangeSource::EndChange()
{
	if (Proxy.IsValid())
	{
		Proxy->EndTransformEditSequence();
		ActiveChange->To = Proxy->GetTransform();
		return MoveTemp(ActiveChange);
	}
	return TUniquePtr<FToolCommandChange>();
}

UObject* FTransformProxyChangeSource::GetChangeTarget()
{
	return Proxy.Get();
}

FText FTransformProxyChangeSource::GetChangeDescription()
{
	return LOCTEXT("FTransformProxyChangeDescription", "TransformProxyChange");
}


#undef LOCTEXT_NAMESPACE
