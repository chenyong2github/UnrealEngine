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
	OnPivotChanged.Broadcast(this, SharedTransform);
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
		OnPivotChanged.Broadcast(this, SharedTransform);
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

void UTransformProxy::BeginPivotEditSequence()
{
	OnBeginPivotEdit.Broadcast(this);
}

void UTransformProxy::EndPivotEditSequence()
{
	OnEndPivotEdit.Broadcast(this);
}



void UTransformProxy::UpdateObjects()
{
	for (FRelativeObject& Obj : Objects)
	{
		FTransform CombinedTransform;
		if (bRotatePerObject && Objects.Num() > 1)
		{
			// We want to apply the compare the shared transform to the shared transform that existed
			// at the time the object's StartTransform was set, then apply the changes to the StartTransform.
			// It may seem that FTransform::RelativeTransform() might be of use here, but that gives the
			// transform from the point of view of the initial frame, which would give us incorrect
			// translation if the initial frame axes didn't line up with world axes.
			CombinedTransform = Obj.StartTransform;

			CombinedTransform.AddToTranslation(SharedTransform.GetTranslation() - InitialSharedTransform.GetTranslation());
			CombinedTransform.ConcatenateRotation(InitialSharedTransform.GetRotation().Inverse());
			CombinedTransform.ConcatenateRotation(SharedTransform.GetRotation());
			CombinedTransform.SetScale3D(CombinedTransform.GetScale3D() * SharedTransform.GetScale3D() / InitialSharedTransform.GetScale3D());
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

	InitialSharedTransform = SharedTransform;
}





void FTransformProxyChange::Apply(UObject* Object)
{
	UTransformProxy* Proxy = CastChecked<UTransformProxy>(Object);

	bool bSavedSetPivotMode = Proxy->bSetPivotMode;
	Proxy->bSetPivotMode = bSetPivotMode;
	Proxy->SetTransform(To);
	Proxy->bSetPivotMode = bSavedSetPivotMode;
}

void FTransformProxyChange::Revert(UObject* Object)
{
	UTransformProxy* Proxy = CastChecked<UTransformProxy>(Object);

	bool bSavedSetPivotMode = Proxy->bSetPivotMode;
	Proxy->bSetPivotMode = bSetPivotMode;
	Proxy->SetTransform(From);
	Proxy->bSetPivotMode = bSavedSetPivotMode;
}


void FTransformProxyChangeSource::BeginChange()
{
	if (Proxy.IsValid())
	{
		ActiveChange = MakeUnique<FTransformProxyChange>();
		ActiveChange->From = Proxy->GetTransform();
		ActiveChange->bSetPivotMode = bOverrideSetPivotMode ? true : Proxy->bSetPivotMode;

		if (ActiveChange->bSetPivotMode)
		{
			Proxy->BeginPivotEditSequence();
		}
		else
		{
			Proxy->BeginTransformEditSequence();
		}
	}
}

TUniquePtr<FToolCommandChange> FTransformProxyChangeSource::EndChange()
{
	if (Proxy.IsValid())
	{
		if (ActiveChange->bSetPivotMode)
		{
			Proxy->EndPivotEditSequence();
		}
		else
		{
			Proxy->EndTransformEditSequence();
		}

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
