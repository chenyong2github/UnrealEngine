// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transforms/MultiTransformer.h"



void UMultiTransformer::Setup(UInteractiveGizmoManager* GizmoManagerIn)
{
	GizmoManager = GizmoManagerIn;

	ActiveGizmoFrame = FFrame3d();

	ActiveMode = EMultiTransformerMode::DefaultGizmo;

	// Create a new TransformGizmo and associated TransformProxy. The TransformProxy will not be the
	// parent of any Components in this case, we just use it's transform and change delegate.
	TransformProxy = NewObject<UTransformProxy>(this);
	TransformProxy->SetTransform(ActiveGizmoFrame.ToFTransform());
	UpdateShowGizmoState(true);

	// listen for changes to the proxy and update the transform frame when that happens
	TransformProxy->OnTransformChanged.AddUObject(this, &UMultiTransformer::OnProxyTransformChanged);
	TransformProxy->OnBeginTransformEdit.AddUObject(this, &UMultiTransformer::OnBeginProxyTransformEdit);
	TransformProxy->OnEndTransformEdit.AddUObject(this, &UMultiTransformer::OnEndProxyTransformEdit);
}



void UMultiTransformer::Shutdown()
{
	GizmoManager->DestroyAllGizmosByOwner(this);
}




void UMultiTransformer::SetMode(EMultiTransformerMode NewMode)
{
	if (NewMode != ActiveMode)
	{
		if (NewMode == EMultiTransformerMode::DefaultGizmo)
		{
			UpdateShowGizmoState(true);
		}
		else
		{
			UpdateShowGizmoState(false);
		}
		ActiveMode = NewMode;
	}
}


void UMultiTransformer::SetGizmoVisibility(bool bVisible)
{
	if (bShouldBeVisible != bVisible)
	{
		bShouldBeVisible = bVisible;
		if (TransformGizmo != nullptr)
		{
			TransformGizmo->SetVisibility(bVisible);
		}
	}
}

void UMultiTransformer::SetSnapToWorldGridSourceFunc(TUniqueFunction<bool()> EnableSnapFunc)
{
	EnableSnapToWorldGridFunc = MoveTemp(EnableSnapFunc);
}

void UMultiTransformer::Tick(float DeltaTime)
{
	if (TransformGizmo != nullptr)
	{
		// todo this
		TransformGizmo->bSnapToWorldGrid =
			(EnableSnapToWorldGridFunc) ? EnableSnapToWorldGridFunc() : false;
	}
}



void UMultiTransformer::SetGizmoPositionFromWorldFrame(const FFrame3d& Frame)
{
	ActiveGizmoFrame = Frame;

	if (TransformGizmo != nullptr)
	{
		TransformGizmo->SetNewGizmoTransform(ActiveGizmoFrame.ToFTransform());
	}
}

void UMultiTransformer::SetGizmoPositionFromWorldPos(const FVector& Position, const FVector& Normal)
{
	ActiveGizmoFrame.Origin = FVector3d(Position);
	ActiveGizmoFrame.AlignAxis(2, FVector3d(Normal));
	ActiveGizmoFrame.ConstrainedAlignPerpAxes();

	if (TransformGizmo != nullptr)
	{
		TransformGizmo->SetNewGizmoTransform(ActiveGizmoFrame.ToFTransform());
	}
}

void UMultiTransformer::OnProxyTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	ActiveGizmoFrame = FFrame3d(Transform);
	OnTransformUpdated.Broadcast();
}


void UMultiTransformer::OnBeginProxyTransformEdit(UTransformProxy* Proxy)
{
	bInGizmoEdit = true;
	OnTransformStarted.Broadcast();
}

void UMultiTransformer::OnEndProxyTransformEdit(UTransformProxy* Proxy)
{
	bInGizmoEdit = false;
	OnTransformCompleted.Broadcast();
}



void UMultiTransformer::UpdateShowGizmoState(bool bNewVisibility)
{
	if (bNewVisibility == false)
	{
		GizmoManager->DestroyAllGizmosByOwner(this);
		TransformGizmo = nullptr;
	}
	else
	{
		check(TransformGizmo == nullptr);
		TransformGizmo = GizmoManager->Create3AxisTransformGizmo(this);
		TransformGizmo->SetActiveTarget(TransformProxy, GizmoManager);
		TransformGizmo->SetNewGizmoTransform(ActiveGizmoFrame.ToFTransform());
		TransformGizmo->SetVisibility(bShouldBeVisible);
	}
}

