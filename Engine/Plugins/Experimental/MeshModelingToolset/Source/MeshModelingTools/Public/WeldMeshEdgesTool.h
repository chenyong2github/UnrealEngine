// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SingleSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "BaseTools/SingleSelectionMeshEditingTool.h"
#include "WeldMeshEdgesTool.generated.h"


// predeclarations
struct FMeshDescription;
class USimpleDynamicMeshComponent;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UWeldMeshEdgesToolBuilder : public USingleSelectionMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	virtual USingleSelectionMeshEditingTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/**
 * Mesh Weld Edges Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UWeldMeshEdgesTool : public USingleSelectionMeshEditingTool
{
	GENERATED_BODY()

public:
	UWeldMeshEdgesTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	// need to update bResultValid if these are modified, so we don't publicly expose them. 
	// @todo setters/getters for these

	/** Edges are considered matching if both pairs of endpoint vertices are closer than this distance */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0.000001", UIMax = "0.01", ClampMin = "0.00000001", ClampMax = "1000.0"))
	float Tolerance;

	/** Only merge unambiguous pairs that have unique duplicate-edge matches */
	UPROPERTY(EditAnywhere, Category = Options, AdvancedDisplay)
	bool bOnlyUnique;

protected:
	USimpleDynamicMeshComponent* DynamicMeshComponent;
	UE::Geometry::FDynamicMesh3 OriginalMesh;
	bool bResultValid;
	void UpdateResult();
};
