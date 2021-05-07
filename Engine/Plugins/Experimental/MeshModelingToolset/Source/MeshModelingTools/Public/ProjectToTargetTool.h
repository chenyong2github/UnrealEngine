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

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
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

	UPROPERTY(EditAnywhere, Category = ProjectionSpace)
	bool bWorldSpace = true;

	UProjectToTargetToolProperties() :
		URemeshMeshToolProperties(),
		bWorldSpace(true)
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

	virtual TUniquePtr<UE::Geometry::FDynamicMeshOperator> MakeNewOperator() override;

private:

	TUniquePtr<UE::Geometry::FDynamicMesh3> ProjectionTarget;
	TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> ProjectionTargetSpatial;

};
