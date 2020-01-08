// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkedSimulationModelDebugger.h"
#include "NetworkPredictionTypes.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/NetworkGuid.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/ScopedTimers.h"

FNetworkSimulationModelDebuggerManager& FNetworkSimulationModelDebuggerManager::Get()
{
	static FNetworkSimulationModelDebuggerManager Manager;
	return Manager;
}

FNetworkSimulationModelDebuggerManager::FNetworkSimulationModelDebuggerManager()
{
	DrawDebugServicesHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateRaw(this, &FNetworkSimulationModelDebuggerManager::DrawDebugService));
	check(DrawDebugServicesHandle.IsValid());
}

FNetworkSimulationModelDebuggerManager::~FNetworkSimulationModelDebuggerManager()
{
	if (Graph.IsValid())
	{
		Graph->RemoveFromRoot();
	}
}

void FNetworkSimulationModelDebuggerManager::SetDebuggerActive(AActor* OwningActor, bool InActive)
{
	if (INetworkSimulationModelDebugger* Debugger = Find(OwningActor))
	{
		Debugger->SetActive(InActive);
	}
	ResetCache();
	Gather(LastCanvas.Get());
}

void FNetworkSimulationModelDebuggerManager::ToggleDebuggerActive(AActor* OwningActor)
{
	if (INetworkSimulationModelDebugger* Debugger = Find(OwningActor))
	{
		Debugger->SetActive(!Debugger->IsActive());
	}
	ResetCache();
	Gather(LastCanvas.Get());
}

void FNetworkSimulationModelDebuggerManager::SetContinousGather(bool InGather)
{
	bContinousGather = InGather;
	if (!bContinousGather)
	{
		Gather(LastCanvas.Get());
	}
}

void FNetworkSimulationModelDebuggerManager::DrawDebugService(UCanvas* C, APlayerController* PC)
{
	LastCanvas = C;
	if (bContinousGather)
	{
		Gather(C);
	}

	FDisplayDebugManager& DisplayDebugManager = C->DisplayDebugManager;
	DisplayDebugManager.Initialize(C, GEngine->GetSmallFont(), FVector2D(4.0f, 150.0f));

	if (!NetworkSimulationModelDebugCVars::DrawCanvas())
	{
		return;
	}

	if (Lines.Num() > 0)
	{
		const float TextScale = FMath::Max(C->SizeX / 1920.0f, 1.0f);
		FCanvasTileItem TextBackgroundTile(FVector2D(0.0f, 120.0f), FVector2D(400.0f, 1800.0f) * TextScale, FColor(0, 0, 0, 100));
		TextBackgroundTile.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(TextBackgroundTile);
	}

	// --------------------------------------------------------
	//	Lines
	// --------------------------------------------------------

	for (FDebugLine& Line : Lines)
	{
		DisplayDebugManager.SetDrawColor(Line.Color);
		DisplayDebugManager.DrawString(Line.Str);
	}

	// --------------------------------------------------------
	//	Canvas Items (graphs+text)
	// --------------------------------------------------------

	for (TUniquePtr<FCanvasItem>& Item : CanvasItems[0])
	{
		C->DrawItem(*Item.Get());
	}

	if (NetworkSimulationModelDebugCVars::DrawFrames() > 0)
	{
		for (TUniquePtr<FCanvasItem>& Item : CanvasItems[1])
		{
			C->DrawItem(*Item.Get());
		}
	}
}

void FNetworkSimulationModelDebuggerManager::Tick(float DeltaTime)
{
	for (auto It = DebuggerMap.CreateIterator(); It; ++It)
	{
		const AActor* Owner = It.Key().Get();
		if (!Owner)
		{
			It.RemoveCurrent();
			continue;;
		}

		if (It.Value()->IsActive())
		{
			It.Value()->Tick(DeltaTime);
		}
	}
}

TStatId FNetworkSimulationModelDebuggerManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FNetworkSimulationModelDebuggerManager, STATGROUP_TaskGraphTasks);
}

void FNetworkSimulationModelDebuggerManager::LogSingleFrame(FOutputDevice& Ar)
{
	Gather(LastCanvas.Get());

	for (const FDebugLine& Line : Lines)
	{
		Ar.Logf(TEXT("%s"), *Line.Str);
	}
}

void FNetworkSimulationModelDebuggerManager::Emit(FString Str, FColor Color, float XOffset, float YOffset)
{
	Lines.Emplace(FDebugLine{ MoveTemp(Str), Color, XOffset, YOffset });
}

void FNetworkSimulationModelDebuggerManager::EmitQuad(FVector2D ScreenPosition, FVector2D ScreenSize, FColor Color)
{
	FVector2D Quad[4];

	Quad[0].X = ScreenPosition.X;
	Quad[0].Y = ScreenPosition.Y;

	Quad[1].X = ScreenPosition.X;
	Quad[1].Y = ScreenPosition.Y + ScreenSize.Y;

	Quad[2].X = ScreenPosition.X + ScreenSize.X;
	Quad[2].Y = ScreenPosition.Y + ScreenSize.Y;

	Quad[3].X = ScreenPosition.X + ScreenSize.X;
	Quad[3].Y = ScreenPosition.Y;

	CanvasItems[0].Emplace(MakeUnique<FCanvasTriangleItem>(Quad[0], Quad[1], Quad[2], GWhiteTexture));
	CanvasItems[0].Last()->SetColor(Color);

	CanvasItems[0].Emplace(MakeUnique<FCanvasTriangleItem>(Quad[2], Quad[3], Quad[0], GWhiteTexture));
	CanvasItems[0].Last()->SetColor(Color);
}

void FNetworkSimulationModelDebuggerManager::EmitText(FVector2D ScreenPosition, FColor Color, FString Str)
{
	CanvasItems[1].Emplace(MakeUnique<FCanvasTextItem>(ScreenPosition, FText::FromString(MoveTemp(Str)), GEngine->GetTinyFont(), Color));
}

void FNetworkSimulationModelDebuggerManager::EmitLine(FVector2D StartPosition, FVector2D EndPosition, FColor Color, float Thickness)
{
	CanvasItems[0].Emplace(MakeUnique<FCanvasLineItem>(StartPosition, EndPosition));
	CanvasItems[0].Last()->SetColor(Color);
	((FCanvasLineItem*)CanvasItems[0].Last().Get())->LineThickness = Thickness;
}

INetworkSimulationModelDebugger* FNetworkSimulationModelDebuggerManager::Find(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	INetworkSimulationModelDebugger* Debugger = DebuggerMap.FindRef(TWeakObjectPtr<const AActor>(Actor));
	if (!Debugger)
	{
		UE_LOG(LogNetworkSimDebug, Warning, TEXT("Could not find NetworkSimulationModel associated with %s"), *GetPathNameSafe(Actor));
	}
	return Debugger;
}

void FNetworkSimulationModelDebuggerManager::Gather(UCanvas* C)
{
	ResetCache();

	for (auto It = DebuggerMap.CreateIterator(); It; ++It)
	{
		const AActor* Owner = It.Key().Get();
		if (!Owner)
		{
			It.RemoveCurrent();
			continue;;
		}

		if (It.Value()->IsActive())
		{
			It.Value()->GatherCurrent(*this, C);
			if (NetworkSimulationModelDebugCVars::GatherServerSidePIE() > 0)
			{
				if (const AActor* ServerSideActor = Cast<AActor>(FindReplicatedObjectOnPIEServer(Owner)))
				{
					if (INetworkSimulationModelDebugger* ServerSideSim = Find(ServerSideActor))
					{
						Emit();
						Emit();
						ServerSideSim->GatherCurrent(*this, nullptr); // Dont do graphs for server side state
					}
				}
			}

			// Only gather first active debugger (it would be great to have more control over this when debugging multiples)
			break;
		}
	}
}

UObject* FindReplicatedObjectOnPIEServer(const UObject* ClientObject)
{
	if (ClientObject == nullptr)
	{
		return nullptr;
	}

	UObject* ServerObject = nullptr;

#if WITH_EDITOR
	if (UWorld* World = ClientObject->GetWorld())
	{
		if (UNetDriver* NetDriver = World->GetNetDriver())
		{
			if (UNetConnection* NetConnection = NetDriver->ServerConnection)
			{
				FNetworkGUID NetGUID = NetConnection->PackageMap->GetNetGUIDFromObject(ClientObject);

				// Find the PIE server world
				for (TObjectIterator<UWorld> It; It; ++It)
				{
					if (It->WorldType == EWorldType::PIE && It->GetNetMode() != NM_Client)
					{
						if (UNetDriver* ServerNetDriver = It->GetNetDriver())
						{
							if (ServerNetDriver->ClientConnections.Num() > 0)
							{
								UPackageMap* ServerPackageMap = ServerNetDriver->ClientConnections[0]->PackageMap;
								ServerObject = ServerPackageMap->GetObjectFromNetGUID(NetGUID, true);
								break;
							}
						}
					}
				}
			}
		}
	}
#endif

	return ServerObject;
}

// ------------------------------------------------------------------------------------------------------------------------
//	Debug functions for toggling debugger on specific actors
// ------------------------------------------------------------------------------------------------------------------------

// Debug the first locally controlled pawn
FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelDebugCmd(TEXT("nms.Debug.LocallyControlledPawn"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& InArgs, UWorld* World) 
{

	if (!World || !World->GetFirstLocalPlayerFromController())
	{
		return;
	}
	ULocalPlayer* Player = World->GetFirstLocalPlayerFromController();
	if (!Player || !Player->GetPlayerController(World) || !Player->GetPlayerController(World)->GetPawn())
	{
		UE_LOG(LogNetworkSimDebug, Error, TEXT("Could not find valid locally controlled pawn."));
		return;
	}

	APawn* Pawn = Player->GetPlayerController(World)->GetPawn();
	FNetworkSimulationModelDebuggerManager::Get().ToggleDebuggerActive(Pawn);
}));

// Toggles continous updates to debugger for locally controlled player
FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelDebugToggleContinousCmd(TEXT("nms.Debug.ToggleContinous"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& InArgs, UWorld* World) 
{

	if (!World || !World->GetFirstLocalPlayerFromController())
	{
		return;
	}
	ULocalPlayer* Player = World->GetFirstLocalPlayerFromController();
	if (!Player || !Player->GetPlayerController(World) || !Player->GetPlayerController(World)->GetPawn())
	{
		UE_LOG(LogNetworkSimDebug, Error, TEXT("Could not find valid locally controlled pawn. "));
		return;
	}

	APawn* Pawn = Player->GetPlayerController(World)->GetPawn();
	FNetworkSimulationModelDebuggerManager::Get().SetDebuggerActive(Pawn, true);
	FNetworkSimulationModelDebuggerManager::Get().ToggleContinousGather();
}));

// Debug actors by class filter. 2nd parameter can filter by name
FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelDebugClassCmd(TEXT("nms.Debug.Class"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* World) 
{
	if (Args.Num() <= 0)
	{
		UE_LOG(LogNetworkSimDebug, Display, TEXT("Usage: nms.Debug.Class <Class> <Name>"));
		return;
	}

	UClass* Class = FindObject<UClass>(ANY_PACKAGE, *Args[0]);
	if (Class == nullptr)
	{
		UE_LOG(LogNetworkSimDebug, Display, TEXT("Could not find Class: %s. Searching for partial match."), *Args[0]);

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			if (ClassIt->IsChildOf(AActor::StaticClass()) && ClassIt->GetName().Contains(Args[0]))
			{
				Class = *ClassIt;
				UE_LOG(LogNetworkSimDebug, Display, TEXT("Found: %s"), *Class->GetName());
				break;
			}

		}

		if (Class == nullptr)
		{
			UE_LOG(LogNetworkSimDebug, Display, TEXT("No Matches"));
			return;
		}
	}

	// Probably need to do distance based sorting or a way to specify which one you want.
	bool bFound = false;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetClass()->IsChildOf(Class))
		{
			if (Args.Num() < 2 || It->GetName().Contains(Args[1]))
			{
				UE_LOG(LogNetworkSimDebug, Display, TEXT("Toggling NetworkSim debugger for %s"), *It->GetName());
				FNetworkSimulationModelDebuggerManager::Get().ToggleDebuggerActive(*It);
				bFound = true;
			}
		}
	}
	if (!bFound)
	{
		UE_LOG(LogNetworkSimDebug, Display, TEXT("Unable to find actors matching search criteria. Class: %s"), *GetNameSafe(Class));
	}
}));