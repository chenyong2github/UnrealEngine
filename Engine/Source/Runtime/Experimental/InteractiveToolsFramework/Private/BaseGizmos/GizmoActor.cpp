// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoActor.h"

#include "BaseGizmos/GizmoArrowComponent.h"
#include "BaseGizmos/GizmoRectangleComponent.h"
#include "BaseGizmos/GizmoCircleComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"


#define LOCTEXT_NAMESPACE "UTransformGizmo"


AGizmoActor::AGizmoActor()
{
	// generally gizmo actor creation/destruction should not be transacted
	ClearFlags(RF_Transactional);

#if WITH_EDITORONLY_DATA
	// hide this actor in the scene outliner
	bListedInSceneOutliner = false;
#endif
}


UGizmoArrowComponent* AGizmoActor::AddDefaultArrowComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& LocalDirection)
{
	UGizmoArrowComponent* NewArrow = NewObject<UGizmoArrowComponent>(Actor);
	Actor->AddInstanceComponent(NewArrow);
	NewArrow->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewArrow->Direction = LocalDirection;
	NewArrow->Color = Color;
	NewArrow->RegisterComponent();
	return NewArrow;
}



UGizmoRectangleComponent* AGizmoActor::AddDefaultRectangleComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& PlaneAxis1, const FVector& PlaneAxisx2)
{
	UGizmoRectangleComponent* NewRectangle = NewObject<UGizmoRectangleComponent>(Actor);
	Actor->AddInstanceComponent(NewRectangle);
	NewRectangle->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewRectangle->DirectionX = PlaneAxis1;
	NewRectangle->DirectionY = PlaneAxisx2;
	NewRectangle->Color = Color;
	NewRectangle->RegisterComponent();
	return NewRectangle;
}


UGizmoCircleComponent* AGizmoActor::AddDefaultCircleComponent(
	UWorld* World, AActor* Actor,
	const FLinearColor& Color, const FVector& PlaneNormal)
{
	UGizmoCircleComponent* NewCircle = NewObject<UGizmoCircleComponent>(Actor);
	Actor->AddInstanceComponent(NewCircle);
	NewCircle->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewCircle->Normal = PlaneNormal;
	NewCircle->Color = Color;
	NewCircle->RegisterComponent();
	return NewCircle;
}



#undef LOCTEXT_NAMESPACE