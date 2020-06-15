// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ToolContextInterfaces.h"

class UInteractiveToolsContext;
class FEditorViewportClient;
class HHitProxy;
class UMaterialInterface;
class FText;
class FToolCommandChange;

class FToolsContextQueriesImpl : public IToolsContextQueriesAPI
{
public:
	FToolsContextQueriesImpl(UInteractiveToolsContext* InContext);

	virtual void GetCurrentSelectionState(FToolBuilderState& StateOut) const override;
	virtual void GetCurrentViewState(FViewCameraState& StateOut) const override;
	virtual EToolContextCoordinateSystem GetCurrentCoordinateSystem() const override;
	virtual bool ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const override;
	virtual UMaterialInterface* GetStandardMaterial(EStandardToolContextMaterials MaterialType) const override;
	virtual HHitProxy* GetHitProxy(int32 X, int32 Y) const override;

protected:
	UInteractiveToolsContext* ToolsContext;
	FEditorViewportClient* ViewportClient;
	UObject* EditingAsset;
};

class FToolsContextTransactionImpl : public IToolsContextTransactionsAPI
{
public:
	virtual void DisplayMessage(const FText& Message, EToolMessageLevel Level) override;
	virtual void PostInvalidation() override;
	virtual void BeginUndoTransaction(const FText& Description) override;
	virtual void EndUndoTransaction() override;
	virtual void AppendChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description) override;
	virtual bool RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange) override;
};
