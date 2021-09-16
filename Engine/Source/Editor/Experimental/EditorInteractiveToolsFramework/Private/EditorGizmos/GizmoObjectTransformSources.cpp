// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/GizmoObjectTransformSources.h"


FTransform UGizmoObjectWorldTransformSource::GetTransform() const
{
	return Object->LocalToWorldTransform;
}

void UGizmoObjectWorldTransformSource::SetTransform(const FTransform& TransformIn)
{
	if (bModifyObjectOnTransform)
	{
		Object->Modify();
	}
	Object->SetLocalToWorldTransform(TransformIn);
	OnTransformChanged.Broadcast(this);
}

