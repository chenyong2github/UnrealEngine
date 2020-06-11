// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleToolsContextInterfaces.h"

#include "InteractiveToolsContext.h"
#include "Math/Quat.h"

FToolsContextQueriesImpl::FToolsContextQueriesImpl(UInteractiveToolsContext* InContext)
	: ToolsContext(InContext)
{
	check(ToolsContext);
}

void FToolsContextQueriesImpl::GetCurrentSelectionState(FToolBuilderState& StateOut) const
{
	StateOut.ToolManager = ToolsContext->ToolManager;
	StateOut.GizmoManager = ToolsContext->GizmoManager;
	StateOut.World = nullptr;
	StateOut.SelectedActors.Empty();
	StateOut.SelectedComponents.Empty();
}

void FToolsContextQueriesImpl::GetCurrentViewState(FViewCameraState& StateOut) const
{
	StateOut.bIsOrthographic = false;
	StateOut.Position = FVector::ZeroVector;
	StateOut.HorizontalFOVDegrees = 100.f;
	StateOut.AspectRatio = 1.f;
	StateOut.Orientation = FQuat::Identity;
	StateOut.bIsVR = false;
}

EToolContextCoordinateSystem FToolsContextQueriesImpl::GetCurrentCoordinateSystem() const
{
	return EToolContextCoordinateSystem::World;
}

bool FToolsContextQueriesImpl::ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
{
	return false;
}

UMaterialInterface* FToolsContextQueriesImpl::GetStandardMaterial(EStandardToolContextMaterials MaterialType) const
{
	return nullptr;
}

HHitProxy* FToolsContextQueriesImpl::GetHitProxy(int32 X, int32 Y) const
{
	return nullptr;
}

void FToolsContextTransactionImpl::DisplayMessage(const FText& Message, EToolMessageLevel Level)
{
}

void FToolsContextTransactionImpl::PostInvalidation()
{
}

void FToolsContextTransactionImpl::BeginUndoTransaction(const FText& Description)
{
}

void FToolsContextTransactionImpl::EndUndoTransaction()
{
}

void FToolsContextTransactionImpl::AppendChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
}

bool FToolsContextTransactionImpl::RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange)
{
	return false;
}
