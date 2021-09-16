// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "EditorGizmos/GizmoGroupObject.h"
#include "BaseGizmos/GizmoRenderingUtil.h"

UGizmoGroupObject::UGizmoGroupObject()
{
}

void UGizmoGroupObject::Render(IToolsContextRenderAPI* RenderAPI)
{
	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->Render(RenderAPI);
		}
	}
}

FInputRayHit UGizmoGroupObject::LineTraceObject(const FVector RayOrigin, const FVector RayDirection)
{
	FInputRayHit RayHit;
	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			RayHit = Object->LineTraceObject(RayOrigin, RayDirection);
			if (RayHit.bHit)
			{
				return RayHit;
			}
		}
	}
	return RayHit;
}

void UGizmoGroupObject::SetHoverState(bool bHoverIn)
{
	bHovering = bHoverIn;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetHoverState(bHoverIn);
		}
	}
}

void UGizmoGroupObject::SetInteractingState(bool bHoverIn)
{
	bHovering = bHoverIn;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetInteractingState(bHoverIn);
		}
	}
}

void UGizmoGroupObject::SetWorldLocalState(bool bWorldIn)
{
	bWorld = bWorldIn;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetWorldLocalState(bWorldIn);
		}
	}
}

void UGizmoGroupObject::SetVisibility(bool bVisibleIn)
{
	bVisible = bVisibleIn;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetVisibility(bVisibleIn);
		}
	}
}

void UGizmoGroupObject::SetLocalToWorldTransform(FTransform LocalToWorldTransformIn)
{
	LocalToWorldTransform = LocalToWorldTransformIn;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetLocalToWorldTransform(LocalToWorldTransformIn);
		}
	}
}

void UGizmoGroupObject::SetMaterial(UMaterialInterface* InMaterial)
{
	Material = InMaterial;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetMaterial(InMaterial);
		}
	}
}

void UGizmoGroupObject::SetCurrentMaterial(UMaterialInterface* InCurrentMaterial)
{
	CurrentMaterial = InCurrentMaterial;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetCurrentMaterial(InCurrentMaterial);
		}
	}
}

void UGizmoGroupObject::SetGizmoScale(float InGizmoScale)
{
	GizmoScale = InGizmoScale;

	for (UGizmoBaseObject* Object : Objects)
	{
		if (Object)
		{
			Object->SetGizmoScale(InGizmoScale);
		}
	}
}

