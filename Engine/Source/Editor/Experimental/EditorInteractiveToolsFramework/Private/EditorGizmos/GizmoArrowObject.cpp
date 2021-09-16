// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "EditorGizmos/GizmoArrowObject.h"
#include "BaseGizmos/GizmoRenderingUtil.h"

UGizmoArrowObject::UGizmoArrowObject()
{
    CylinderObject = NewObject<UGizmoCylinderObject>();
    ConeObject = NewObject<UGizmoConeObject>();
    BoxObject = NewObject<UGizmoBoxObject>();
}

void UGizmoArrowObject::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (CylinderObject)
	{
		CylinderObject->Render(RenderAPI);
	}

	if (bHasConeHead)
	{
		if (ConeObject)
		{
			ConeObject->Render(RenderAPI);
		}
	}
	else
	{
		if (BoxObject)
		{
			BoxObject->Render(RenderAPI);
		}
	}
}

FInputRayHit UGizmoArrowObject::LineTraceObject(const FVector RayOrigin, const FVector RayDirection)
{
	FInputRayHit RayHit;
	if (CylinderObject)
	{
		RayHit = CylinderObject->LineTraceObject(RayOrigin, RayDirection);
		if (!RayHit.bHit)
		{
			if (bHasConeHead)
			{
				if (ConeObject)
				{
					RayHit = ConeObject->LineTraceObject(RayOrigin, RayDirection);
				}
			}
			else
			{
				if (BoxObject)
				{
					RayHit = BoxObject->LineTraceObject(RayOrigin, RayDirection);
				}
			}
		}
	}
	return RayHit;
}

void UGizmoArrowObject::SetHoverState(bool bHoveringIn)
{
	bHovering = bHoveringIn;

	if (CylinderObject)
	{
		CylinderObject->SetHoverState(bHoveringIn);
	}
	if (ConeObject)
	{
		ConeObject->SetHoverState(bHoveringIn);
	}
	if (BoxObject)
	{
		BoxObject->SetHoverState(bHoveringIn);
	}
}

void UGizmoArrowObject::SetInteractingState(bool bInteractingIn)
{
	bInteracting = bInteractingIn;

	if (CylinderObject)
	{
		CylinderObject->SetInteractingState(bInteractingIn);
	}
	if (ConeObject)
	{
		ConeObject->SetInteractingState(bInteractingIn);
	}
	if (BoxObject)
	{
		BoxObject->SetInteractingState(bInteractingIn);
	}
}

void UGizmoArrowObject::SetWorldLocalState(bool bWorldIn)
{
	bWorld = bWorldIn;

	if (CylinderObject)
	{
		CylinderObject->SetWorldLocalState(bWorldIn);
	}
	if (ConeObject)
	{
		ConeObject->SetWorldLocalState(bWorldIn);
	}
	if (BoxObject)
	{
		BoxObject->SetWorldLocalState(bWorldIn);
	}
}

void UGizmoArrowObject::SetVisibility(bool bVisibleIn)
{
	bVisible = bVisibleIn;

	if (CylinderObject)
	{
		CylinderObject->SetVisibility(bVisibleIn);
	}
	if (ConeObject)
	{
		ConeObject->SetVisibility(bVisibleIn);
	}
	if (BoxObject)
	{
		BoxObject->SetVisibility(bVisibleIn);
	}
}

void UGizmoArrowObject::SetLocalToWorldTransform(FTransform LocalToWorldTransformIn)
{
	LocalToWorldTransform = LocalToWorldTransformIn;

	if (CylinderObject)
	{
		CylinderObject->SetLocalToWorldTransform(LocalToWorldTransformIn);
	}
	if (ConeObject)
	{
		ConeObject->SetLocalToWorldTransform(LocalToWorldTransformIn);
	}
	if (BoxObject)
	{
		BoxObject->SetLocalToWorldTransform(LocalToWorldTransformIn);
	}
}

void UGizmoArrowObject::SetGizmoScale(float InGizmoScale)
{
	GizmoScale = InGizmoScale;

	if (CylinderObject)
	{
		CylinderObject->SetGizmoScale(InGizmoScale);
	}
	if (ConeObject)
	{
		ConeObject->SetGizmoScale(InGizmoScale);
	}
	if (BoxObject)
	{
		BoxObject->SetGizmoScale(InGizmoScale);
	}
}

void UGizmoArrowObject::SetMaterial(UMaterialInterface* MaterialIn)
{
	Material = MaterialIn;
	if (CylinderObject)
	{
		CylinderObject->SetMaterial(MaterialIn);
	}
	if (ConeObject)
	{
		ConeObject->SetMaterial(MaterialIn);
	}
	if (BoxObject)
	{
		BoxObject->SetMaterial(MaterialIn);
	}
}

void UGizmoArrowObject::SetCurrentMaterial(UMaterialInterface* CurrentMaterialIn)
{
	CurrentMaterial = CurrentMaterialIn;
	if (CylinderObject)
	{
		CylinderObject->SetCurrentMaterial(CurrentMaterialIn);
	}
	if (ConeObject)
	{
		ConeObject->SetCurrentMaterial(CurrentMaterialIn);
	}
	if (BoxObject)
	{
		BoxObject->SetCurrentMaterial(CurrentMaterialIn);
	}
}

