// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_GAMEPLAY_DEBUGGER

#include "CoreMinimal.h"
#include "GameplayDebuggerCategory.h"
#include "NavMesh/NavMeshRenderingComponent.h"

class APlayerController;

class FGameplayDebuggerCategory_Navmesh : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_Navmesh();

	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) override;
	virtual FDebugRenderSceneProxy* CreateDebugSceneProxy(const UPrimitiveComponent* InComponent, FDebugDrawDelegateHelper*& OutDelegateHelper) override;
	virtual void OnDataPackReplicated(int32 DataPackId) override;

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

protected:

	void CycleNavData();
	void CycleActorReference();

	struct FRepData
	{
		void Serialize(FArchive& Ar);

		FString NavDataName;
		int32 NumDirtyAreas = 0;
		bool bCanChangeReference = false;
		bool bIsUsingPlayerActor = false;
		bool bReferenceTooFarFromNavData = false;
	};

	FNavMeshSceneProxyData NavmeshRenderData;
	FRepData DataPack;
	
	enum class EActorReferenceMode : uint8
	{
		PlayerActorOnly,
		PlayerActor,
		DebugActor
	};
	EActorReferenceMode ActorReferenceMode = EActorReferenceMode::DebugActor;

	int32 NavDataIndexToDisplay = INDEX_NONE;
	bool bSwitchToNextNavigationData = false;
	TWeakObjectPtr<const APawn> PrevDebugActorReference;
};

#endif // WITH_GAMEPLAY_DEBUGGER
