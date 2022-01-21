// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h"
#include "IndexTypes.h"

#include "UVToolAction.h"

#include "UVIslandConformalUnwrapAction.generated.h"

PREDECLARE_GEOMETRY(class FUVEditorDynamicMeshSelection);

UCLASS()
class UVEDITORTOOLS_API UUVIslandConformalUnwrapAction : public UUVToolAction
{
	GENERATED_BODY()

public:

	UUVIslandConformalUnwrapAction();

	void SetWorld(UWorld* WorldIn) override;
	void Setup(UInteractiveTool* ParentToolIn) override;
	void Shutdown() override;
	void SetSelection(int32 SelectionTargetIndexIn, const UE::Geometry::FUVEditorDynamicMeshSelection* NewSelection);

protected:

	TArray<int32> ConcatenatedIslandTids;
	TArray<int32> IslandStartIndices;
	int32 MaxIslandSize;

	int32 SelectionTargetIndex;
	TSharedPtr<UE::Geometry::FUVEditorDynamicMeshSelection> CurrentSelection;

	bool GatherIslandTids();
	bool PreCheckAction();
	bool ApplyAction(UUVToolEmitChangeAPI& EmitChangeAPI);	
};
