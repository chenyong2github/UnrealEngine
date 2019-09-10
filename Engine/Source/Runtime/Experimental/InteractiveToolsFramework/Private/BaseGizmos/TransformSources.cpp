// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/TransformSources.h"
#include "Components/SceneComponent.h" 



FTransform UGizmoComponentWorldTransformSource::GetTransform() const
{
	return Component->GetComponentToWorld();
}

void UGizmoComponentWorldTransformSource::SetTransform(const FTransform& NewTransform)
{
	if (bModifyComponentOnTransform)
	{
		Component->Modify();
	}
	Component->SetWorldTransform(NewTransform);
	OnTransformChanged.Broadcast(this);
}




FTransform UGizmoTransformProxyTransformSource::GetTransform() const
{
	return Proxy->GetTransform();
}

void UGizmoTransformProxyTransformSource::SetTransform(const FTransform& NewTransform)
{
	Proxy->SetTransform(NewTransform);
	OnTransformChanged.Broadcast(this);
}