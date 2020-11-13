// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelEditorToolsContextInterfaces.h"

#include "InteractiveToolsContext.h"
#include "Math/Quat.h"

FLevelEditorToolsContextQueriesImpl::FLevelEditorToolsContextQueriesImpl(UInteractiveToolsContext* InContext)
	: ToolsContext(InContext)
{
	check(ToolsContext);
}

void FLevelEditorToolsContextQueriesImpl::GetCurrentSelectionState(FToolBuilderState& StateOut) const
{
	StateOut.ToolManager = ToolsContext->ToolManager;
	StateOut.GizmoManager = ToolsContext->GizmoManager;
	StateOut.World = nullptr;
	StateOut.SelectedActors.Empty();
	StateOut.SelectedComponents.Empty();
}

void FLevelEditorToolsContextQueriesImpl::GetCurrentViewState(FViewCameraState& StateOut) const
{
	StateOut.bIsOrthographic = false;
	StateOut.Position = FVector::ZeroVector;
	StateOut.HorizontalFOVDegrees = 100.f;
	StateOut.AspectRatio = 1.f;
	StateOut.Orientation = FQuat::Identity;
	StateOut.bIsVR = false;
}

EToolContextCoordinateSystem FLevelEditorToolsContextQueriesImpl::GetCurrentCoordinateSystem() const
{
	return EToolContextCoordinateSystem::World;
}

bool FLevelEditorToolsContextQueriesImpl::ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
{
	return false;
}

UMaterialInterface* FLevelEditorToolsContextQueriesImpl::GetStandardMaterial(EStandardToolContextMaterials MaterialType) const
{
	return nullptr;
}

HHitProxy* FLevelEditorToolsContextQueriesImpl::GetHitProxy(int32 X, int32 Y) const
{
	return nullptr;
}

void FLevelEditorContextTransactionImpl::DisplayMessage(const FText& Message, EToolMessageLevel Level)
{
}

void FLevelEditorContextTransactionImpl::PostInvalidation()
{
}

void FLevelEditorContextTransactionImpl::BeginUndoTransaction(const FText& Description)
{
}

void FLevelEditorContextTransactionImpl::EndUndoTransaction()
{
}

void FLevelEditorContextTransactionImpl::AppendChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description)
{
}

bool FLevelEditorContextTransactionImpl::RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange)
{
	return false;
}

bool FLevelEditorContextTransactionImpl::RequestToolSelectionStore(const UInteractiveToolStorableSelection* StorableSelection,
	const FToolSelectionStoreParams& Params)
{
	return false;
}