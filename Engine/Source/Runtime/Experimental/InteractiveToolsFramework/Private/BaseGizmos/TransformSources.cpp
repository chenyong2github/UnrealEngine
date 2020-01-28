// Copyright Epic Games, Inc. All Rights Reserved.

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





FTransform UGizmoScaledTransformSource::GetTransform() const
{
	// get transform from child, and replace scale with external scale
	FTransform Transform = ChildTransformSource->GetTransform();
	FVector ExternalScale = ScaleProvider.GetScale();
	Transform.SetScale3D(ExternalScale);
	return Transform;
}

void UGizmoScaledTransformSource::SetTransform(const FTransform& NewTransform)
{
	// forward incoming scale to external provider
	FVector ExternalScale = NewTransform.GetScale3D();
	ScaleProvider.SetScale(ExternalScale);

	// remove scale from transform and forward to child source
	FTransform Unscaled(NewTransform);
	Unscaled.SetScale3D(FVector::OneVector);
	ChildTransformSource->SetTransform(Unscaled);

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