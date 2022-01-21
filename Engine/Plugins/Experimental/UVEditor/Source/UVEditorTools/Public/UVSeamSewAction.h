// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryBase.h"
#include "IndexTypes.h"

#include "UVToolAction.h"

#include "UVSeamSewAction.generated.h"

PREDECLARE_GEOMETRY(class FUVEditorDynamicMeshSelection);
class APreviewGeometryActor;
class ULineSetComponent;

// We store edges by pairs of Vids here, since Edge Ids seem unreliable between meshes
// when Undo/Redos are happening and modifiying the canonical and preview meshes
typedef UE::Geometry::FIndex2i FEdgeByVids;
struct FEdgePair {
	FEdgeByVids A;
	FEdgeByVids B;

	bool operator==(const FEdgePair& Other) const 
	{
		return A == Other.A && B == Other.B;
	}
	bool operator<(const FEdgePair& Other) const
	{
		return ((A[0] < Other.A[0]) || (A[0] == Other.A[0] && A[1] < Other.A[1])) ||
			((A == Other.A) && ((B[0] < Other.B[0]) || (B[0] == Other.B[0] && B[1] < Other.B[1])));
	}
};

UCLASS()
class UVEDITORTOOLS_API UUVSeamSewAction : public UUVToolAction
{	
	GENERATED_BODY()

public:

	UUVSeamSewAction();

	void SetWorld(UWorld* WorldIn) override;
	void Setup(UInteractiveTool* ParentToolIn) override;
	void Shutdown() override;

	void SetSelection(int32 SelectionTargetIndexIn, const UE::Geometry::FUVEditorDynamicMeshSelection* NewSelection);
	virtual void UpdateVisualizations() override;

protected:

	TArray<FEdgePair> EdgeSewCandidates;

	UPROPERTY()
	TObjectPtr<APreviewGeometryActor> UnwrapPreviewGeometryActor = nullptr;

	UPROPERTY()
	TObjectPtr<ULineSetComponent> SewEdgePairingLineSet = nullptr;

	int32 SelectionTargetIndex;
	TSharedPtr<UE::Geometry::FUVEditorDynamicMeshSelection> CurrentSelection;

	int32 FindSewEdgeOppositePairing(int32 UnwrapEid) const;
	bool PreCheckAction() override;
	bool ApplyAction(UUVToolEmitChangeAPI& EmitChangeAPI) override;
	void UpdateSewEdgePreviewLines();
};