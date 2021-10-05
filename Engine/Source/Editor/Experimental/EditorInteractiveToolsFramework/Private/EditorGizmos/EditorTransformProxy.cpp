// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorTransformProxy.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"


#define LOCTEXT_NAMESPACE "UEditorTransformProxy"

FTransform UEditorTransformProxy::GetTransform() const
{
	if (FEditorViewportClient* ViewportClient = GLevelEditorModeTools().GetFocusedViewportClient())
	{
		FVector Location = ViewportClient->GetWidgetLocation();
		FMatrix RotMatrix = ViewportClient->GetWidgetCoordSystem();
		return FTransform(FQuat(RotMatrix), Location, FVector::OneVector);
	}
	
	return FTransform::Identity;
}

void UEditorTransformProxy::SetTransform(const FTransform& Transform)
{
	// @todo: use ViewportClient->HandleWidgetDelta();
	// @todo: handle Pivots, need to look at how this is currently handled for component visualizers, for example.
}

#undef LOCTEXT_NAMESPACE
