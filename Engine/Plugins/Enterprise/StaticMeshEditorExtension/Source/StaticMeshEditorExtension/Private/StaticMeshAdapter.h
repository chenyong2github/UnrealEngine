// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshEditorStaticMeshAdapter.h"
#include "StaticMeshAdapter.generated.h"

class UStaticMesh;

UCLASS()
class UStaticMeshEditorStaticMeshAdapter  : public UMeshEditorStaticMeshAdapter
{
	GENERATED_BODY()

public:
	UStaticMeshEditorStaticMeshAdapter() : StaticMesh(nullptr), LODIndex(-1) {}

	virtual void OnEndModification( const UEditableMesh* EditableMesh ) override;
	virtual void OnRebuildRenderMesh( const UEditableMesh* EditableMesh ) override;
	virtual void OnCreateEdges( const UEditableMesh* EditableMesh, const TArray<FEdgeID>& EdgeIDs ) override;
	virtual void OnSetEdgeAttribute( const UEditableMesh* EditableMesh, const FEdgeID EdgeID, const FMeshElementAttributeData& Attribute ) override;

	void SetContext(UStaticMesh* InStaticMesh, int32 InLODIndex)
	{
		StaticMesh = InStaticMesh;
		LODIndex = InLODIndex;
	}

private:
	UStaticMesh* StaticMesh;
	int32 LODIndex;
};
