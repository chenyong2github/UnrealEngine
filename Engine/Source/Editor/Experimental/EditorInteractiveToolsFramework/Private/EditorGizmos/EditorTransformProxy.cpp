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
	else
	{
		return FTransform::Identity;
	}
}

void UEditorTransformProxy::InputTranslateDelta(const FVector& InDeltaTranslate, EAxisList::Type InAxisList)
{
	if (FEditorViewportClient* ViewportClient = GLevelEditorModeTools().GetFocusedViewportClient())
	{
		FVector Translate = InDeltaTranslate;
		FRotator Rot = FRotator::ZeroRotator;
		FVector Scale = FVector::ZeroVector;

		ViewportClient->InputWidgetDelta(ViewportClient->Viewport, InAxisList, Translate, Rot, Scale);
	}
}

void UEditorTransformProxy::InputScaleDelta(const FVector& InDeltaScale, EAxisList::Type InAxisList)
{
	if (FEditorViewportClient* ViewportClient = GLevelEditorModeTools().GetFocusedViewportClient())
	{
		FVector Translate = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FVector Scale = InDeltaScale;

		ViewportClient->InputWidgetDelta(ViewportClient->Viewport, InAxisList, Translate, Rot, Scale);
	}
}

void UEditorTransformProxy::InputRotateDelta(const FRotator& InDeltaRotate, EAxisList::Type InAxisList)
{
	if (FEditorViewportClient* ViewportClient = GLevelEditorModeTools().GetFocusedViewportClient())
	{
		FVector Translate = FVector::ZeroVector;
		FRotator Rot = InDeltaRotate;
		FVector Scale = FVector::ZeroVector;

		ViewportClient->InputWidgetDelta(ViewportClient->Viewport, InAxisList, Translate, Rot, Scale);
	}
}

#undef LOCTEXT_NAMESPACE
