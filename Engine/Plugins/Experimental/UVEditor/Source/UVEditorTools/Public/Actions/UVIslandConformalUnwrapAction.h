// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h"
#include "IndexTypes.h"

#include "Actions/UVToolAction.h"

#include "UVIslandConformalUnwrapAction.generated.h"

PREDECLARE_GEOMETRY(class FUVEditorDynamicMeshSelection);

UCLASS()
class UVEDITORTOOLS_API UUVIslandConformalUnwrapAction : public UUVToolAction
{
	GENERATED_BODY()

public:
	virtual bool CanExecuteAction() const override;
	virtual bool ExecuteAction() override;
};
