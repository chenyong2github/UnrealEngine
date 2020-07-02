// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemeshMeshTool.h"
#include "ToolBuilderUtil.h"
#include "ProjectToTargetTool.generated.h"

/**
 * Determine if/how we can build UProjectToTargetTool. It requires two selected mesh components.
 */
UCLASS()
class MESHMODELINGTOOLS_API UProjectToTargetToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


/**
 * Subclass URemeshMeshToolProperties just so we can set default values for some properties. Setting these values in the
 * Setup function of UProjectToTargetTool turns out to be tricky to achieve with the property cache.
 */
UCLASS()
class MESHMODELINGTOOLS_API UProjectToTargetToolProperties : public URemeshMeshToolProperties
{
	GENERATED_BODY()

public:

	UProjectToTargetToolProperties() :
		URemeshMeshToolProperties()
	{
		bPreserveSharpEdges = false;
		RemeshType = ERemeshType::NormalFlow;
	}
};


/**
 * Project one mesh surface onto another, while undergoing remeshing. Subclass of URemeshMeshTool to avoid duplication.
 */
UCLASS()
class MESHMODELINGTOOLS_API UProjectToTargetTool : public URemeshMeshTool
{
	GENERATED_BODY()

public:

	UProjectToTargetTool(const FObjectInitializer& ObjectInitializer) :
		Super(ObjectInitializer.SetDefaultSubobjectClass<UProjectToTargetToolProperties>(TEXT("RemeshProperties")))
	{}

	virtual void Setup() override;

	virtual TUniquePtr<FDynamicMeshOperator> MakeNewOperator() override;

private:

	TUniquePtr<FDynamicMesh3> ProjectionTarget;
	TUniquePtr<FDynamicMeshAABBTree3> ProjectionTargetSpatial;

};
