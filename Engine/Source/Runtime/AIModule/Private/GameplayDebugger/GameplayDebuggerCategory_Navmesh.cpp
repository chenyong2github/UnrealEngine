// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GameplayDebugger/GameplayDebuggerCategory_Navmesh.h"

#if WITH_GAMEPLAY_DEBUGGER

#include "NavigationSystem.h"
#include "GameFramework/PlayerController.h"
#include "NavMesh/RecastNavMesh.h"

namespace
{
	int32 bDrawExcludedFlags = 0;
	FAutoConsoleVariableRef CVar(TEXT("ai.debug.nav.DrawExcludedFlags"), bDrawExcludedFlags, TEXT("If we want to mark \"forbidden\" nav polys while debug-drawing."), ECVF_Default);
}


FGameplayDebuggerCategory_Navmesh::FGameplayDebuggerCategory_Navmesh()
{
	bShowOnlyWithDebugActor = false;
	bShowDataPackReplication = true;
	CollectDataInterval = 5.0f;
	SetDataPackReplication<FNavMeshSceneProxyData>(&NavmeshRenderData);
	SetDataPackReplication<FRepData>(&DataPack);

	const FGameplayDebuggerInputHandlerConfig CycleActorReference(TEXT("Cycle Actor Reference"), TEXT("Subtract"), FGameplayDebuggerInputModifier::Shift);
	const FGameplayDebuggerInputHandlerConfig CycleNavigationData(TEXT("Cycle NavData"), TEXT("Add"), FGameplayDebuggerInputModifier::Shift);
	
	BindKeyPress(CycleActorReference, this, &FGameplayDebuggerCategory_Navmesh::CycleActorReference, EGameplayDebuggerInputMode::Replicated);
	BindKeyPress(CycleNavigationData, this, &FGameplayDebuggerCategory_Navmesh::CycleNavData, EGameplayDebuggerInputMode::Replicated);
}

void FGameplayDebuggerCategory_Navmesh::CycleNavData()
{
	bSwitchToNextNavigationData = true;
	ForceImmediateCollect();
}

void FGameplayDebuggerCategory_Navmesh::CycleActorReference()
{
	switch (ActorReferenceMode)
	{
	case EActorReferenceMode::PlayerActorOnly:
		// Nothing to do since we don't have a debug actor
		break;
	
	case EActorReferenceMode::DebugActor:
		ActorReferenceMode = EActorReferenceMode::PlayerActor;
		ForceImmediateCollect();
		break;
	
	case EActorReferenceMode::PlayerActor:
		ActorReferenceMode = EActorReferenceMode::DebugActor;
		ForceImmediateCollect();
		break;
	}
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_Navmesh::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_Navmesh());
}

void FGameplayDebuggerCategory_Navmesh::FRepData::Serialize(FArchive& Ar)
{
	Ar << NumDirtyAreas;
	Ar << NavDataName;

	uint8 Flags =
		((bCanChangeReference			? 1 : 0) << 0) |
		((bIsUsingPlayerActor			? 1 : 0) << 1) |
		((bReferenceTooFarFromNavData	? 1 : 0) << 2);
	

	Ar << Flags;

	bCanChangeReference			= (Flags & (1 << 0)) != 0;
	bIsUsingPlayerActor			= (Flags & (1 << 1)) != 0;
	bReferenceTooFarFromNavData = (Flags & (1 << 2)) != 0;
}

void FGameplayDebuggerCategory_Navmesh::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
#if WITH_RECAST
	ANavigationData* NavData = nullptr;
	const APawn* RefPawn = nullptr;
	int32 NumNavData = 0;

	if (OwnerPC != nullptr)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(OwnerPC->GetWorld());
		if (NavSys) 
		{
			DataPack.NumDirtyAreas = NavSys->GetNumDirtyAreas();
			NumNavData = NavSys->NavDataSet.Num();
			
			APawn* DebugActorAsPawn = Cast<APawn>(DebugActor);
			
			// Manage actor reference mode:
			// - As soon as we get a new valid debug actor: use it as reference to preserve legacy behavior 
			// - Debug actor is no longer valid: use player actor
			if (ActorReferenceMode == EActorReferenceMode::PlayerActorOnly && DebugActorAsPawn != nullptr)
			{
				ActorReferenceMode = EActorReferenceMode::DebugActor;
			}
			else if (ActorReferenceMode != EActorReferenceMode::PlayerActorOnly && DebugActorAsPawn == nullptr)
			{
				ActorReferenceMode = EActorReferenceMode::PlayerActorOnly;
			}

			if (bSwitchToNextNavigationData || NavDataIndexToDisplay == INDEX_NONE)
			{
				NavDataIndexToDisplay = FMath::Max(0, ++NavDataIndexToDisplay % NumNavData);
				bSwitchToNextNavigationData = false;
			}

			if (NavSys->NavDataSet.IsValidIndex(NavDataIndexToDisplay))
			{
				NavData = NavSys->NavDataSet[NavDataIndexToDisplay];
			}
			
			if (ActorReferenceMode == EActorReferenceMode::DebugActor)
			{
				RefPawn = DebugActorAsPawn;

				// Switch to new debug actor NavigationData
				if (PrevDebugActorReference != RefPawn)
				{
					const FNavAgentProperties& NavAgentProperties = RefPawn->GetNavAgentPropertiesRef();
					NavData = NavSys->GetNavDataForProps(NavAgentProperties, RefPawn->GetNavAgentLocation());
					NavDataIndexToDisplay = NavSys->NavDataSet.Find(NavData);

					PrevDebugActorReference = RefPawn;
				}
			}
			else
			{
				RefPawn = OwnerPC ? OwnerPC->GetPawnOrSpectator() : nullptr;
			}
		}
	}

	const ARecastNavMesh* RecastNavMesh = Cast<const ARecastNavMesh>(NavData);
	if (RecastNavMesh && RefPawn)
	{
		DataPack.bIsUsingPlayerActor = (ActorReferenceMode != EActorReferenceMode::DebugActor);
		DataPack.bCanChangeReference = (ActorReferenceMode != EActorReferenceMode::PlayerActorOnly);

		if (NumNavData > 1)
		{
			DataPack.NavDataName = FString::Printf(TEXT("[%d/%d] %s"), NavDataIndexToDisplay + 1, NumNavData, *NavData->GetFName().ToString());
		}
		else
		{
			DataPack.NavDataName = NavData->GetFName().ToString();
		}

		// add 3x3 neighborhood of target
		const FVector TargetLocation = RefPawn->GetActorLocation();

		TArray<int32> TileSet;
		int32 TileX = 0;
		int32 TileY = 0;
		const int32 DeltaX[] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
		const int32 DeltaY[] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };

		int32 TargetTileX = 0;
		int32 TargetTileY = 0;
		RecastNavMesh->GetNavMeshTileXY(TargetLocation, TargetTileX, TargetTileY);
		for (int32 Idx = 0; Idx < ARRAY_COUNT(DeltaX); Idx++)
		{
			const int32 NeiX = TargetTileX + DeltaX[Idx];
			const int32 NeiY = TargetTileY + DeltaY[Idx];
			RecastNavMesh->GetNavMeshTilesAt(NeiX, NeiY, TileSet);
		}

		const int32 DetailFlags =
			(1 << static_cast<int32>(ENavMeshDetailFlags::PolyEdges)) |
			(1 << static_cast<int32>(ENavMeshDetailFlags::FilledPolys)) |
			(1 << static_cast<int32>(ENavMeshDetailFlags::NavLinks)) |
			(bDrawExcludedFlags ? (1 << static_cast<int32>(ENavMeshDetailFlags::MarkForbiddenPolys)) : 0);

		// Do not attempt to gather render data when TileSet is empty otherwise the whole nav mesh will be displayed
		DataPack.bReferenceTooFarFromNavData = (TileSet.Num() == 0);
		if (DataPack.bReferenceTooFarFromNavData)
		{
			NavmeshRenderData.Reset();
		}
		else
		{
			NavmeshRenderData.GatherData(RecastNavMesh, DetailFlags, TileSet);
		}
	}
#endif // WITH_RECAST
}

void FGameplayDebuggerCategory_Navmesh::DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext)
{
	CanvasContext.Printf(TEXT("Num dirty areas: {%s}%d"), DataPack.NumDirtyAreas > 0 ? TEXT("red") : TEXT("green"), DataPack.NumDirtyAreas);

	if (!DataPack.NavDataName.IsEmpty())
	{
		CanvasContext.Printf(TEXT("Navigation Data: {silver}%s%s"), *DataPack.NavDataName, DataPack.bReferenceTooFarFromNavData ? TEXT(" (too far from navmesh)") : TEXT(""));
		CanvasContext.Printf(TEXT("[{yellow}%s{white}]: Cycle NavData"), *GetInputHandlerDescription(1));
	}

	if (DataPack.bCanChangeReference)
	{
		CanvasContext.Printf(TEXT("[{yellow}%s{white}]: Display around %s actor"), *GetInputHandlerDescription(0), DataPack.bIsUsingPlayerActor ? TEXT("Debug") : TEXT("Player"));
	}
}

void FGameplayDebuggerCategory_Navmesh::OnDataPackReplicated(int32 DataPackId)
{
	MarkRenderStateDirty();
}

FDebugRenderSceneProxy* FGameplayDebuggerCategory_Navmesh::CreateDebugSceneProxy(const UPrimitiveComponent* InComponent, FDebugDrawDelegateHelper*& OutDelegateHelper)
{
	FNavMeshSceneProxy* NavMeshSceneProxy = new FNavMeshSceneProxy(InComponent, &NavmeshRenderData, true);

	auto* OutDelegateHelper2 = new FNavMeshDebugDrawDelegateHelper();
	OutDelegateHelper2->InitDelegateHelper(NavMeshSceneProxy);
	OutDelegateHelper = OutDelegateHelper2;

	return NavMeshSceneProxy;
}

#endif // WITH_GAMEPLAY_DEBUGGER
