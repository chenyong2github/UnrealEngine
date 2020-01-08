// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "AttributeEditorTool.generated.h"


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};





/**
 * Standard properties
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UAttributeEditorToolProperties()
	{}
	
	int NumUVLayers = 8;

	// UObject interface
#if WITH_EDITOR
	virtual bool CanEditChange( const FProperty* InProperty ) const override;
#endif // WITH_EDITOR	
	// End of UObject interface

	/** Reset all normals to per-vertex smooth normals, removing all hard edges */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearNormals;

	// below -- just manually made 8 bools for the 8 possible uv layers in the dumbest possible way ~~

	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer0;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer1;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer2;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer3;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer4;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer5;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer6;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = Options)
	bool bClearUVLayer7;

};

/**
 * Simple Mesh Normal Updating Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	UAttributeEditorTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override;
	virtual bool CanAccept() const override;

	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

protected:

	UPROPERTY()
	UAttributeEditorToolProperties* RemovalProperties;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	FViewCameraState CameraState;

	void GenerateAssets();
};
