// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/Packed/PackedLevelInstanceBuilder.h"

#if WITH_EDITOR

#include "LevelInstance/Packed/PackedLevelInstanceActor.h"
#include "LevelInstance/Packed/ILevelInstancePacker.h"

#include "LevelInstance/Packed/LevelInstanceISMPacker.h"
#include "LevelInstance/Packed/LevelInstanceRecursivePacker.h"

#include "LevelInstance/LevelInstanceSubsystem.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

#include "Misc/ScopeExit.h"
#include "Logging/LogMacros.h"
#include "Logging/MessageLog.h"

#include "Components/SceneComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/LevelBounds.h"
#include "Engine/Brush.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"

#define LOCTEXT_NAMESPACE "FPackedLevelInstanceBuilder"

void FPackedLevelInstanceBuilderContext::ClusterLevelActor(AActor* InActor)
{
	PerActorClusteredComponents.FindOrAdd(InActor);

	for (const auto& Pair : Packers)
	{
		Pair.Value->GetPackClusters(*this, InActor);
	}
}

void FPackedLevelInstanceBuilderContext::FindOrAddCluster(FLevelInstancePackerClusterID&& InClusterID, UActorComponent* InComponent)
{
	TArray<UActorComponent*>& ClusterComponents = Clusters.FindOrAdd(MoveTemp(InClusterID));
	if (InComponent)
	{
		ClusterComponents.Add(InComponent);
		PerActorClusteredComponents.FindChecked(InComponent->GetOwner()).Add(InComponent);
	}
}

void FPackedLevelInstanceBuilderContext::DiscardActor(AActor* InActor)
{
	ActorDiscards.Add(InActor);
}

FPackedLevelInstanceBuilder::FPackedLevelInstanceBuilder()
{
}

const FString& FPackedLevelInstanceBuilder::GetPackedBPSuffix()
{
	static FString BPSuffix = "_BP_Packed";
	return BPSuffix;
}

UBlueprint* FPackedLevelInstanceBuilder::CreatePackedLevelInstanceBlueprint(const FString& InPackagePath, const FString& InAssetName, bool bInCompile)
{
	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = APackedLevelInstance::StaticClass();
	BlueprintFactory->bSkipClassPicker = true;

	if (UBlueprint* NewBP = Cast<UBlueprint>(AssetTools.CreateAssetWithDialog(InAssetName, InPackagePath, UBlueprint::StaticClass(), BlueprintFactory, FName("Create LevelInstance Blueprint"))))
	{
		APackedLevelInstance* CDO = CastChecked<APackedLevelInstance>(NewBP->GeneratedClass->GetDefaultObject());
		CDO->BlueprintAsset = NewBP;

		if (bInCompile)
		{
			FKismetEditorUtilities::CompileBlueprint(NewBP, EBlueprintCompileOptions::SkipGarbageCollection);
		}

		AssetTools.SyncBrowserToAssets(TArray<UObject*>{ NewBP });

		return NewBP;
	}

	return nullptr;
}

TSharedPtr<FPackedLevelInstanceBuilder> FPackedLevelInstanceBuilder::CreateDefaultBuilder()
{
	TSharedPtr<FPackedLevelInstanceBuilder> Builder = MakeShared<FPackedLevelInstanceBuilder>();

	// Class Discards are used to validate the packing result.
	// Components or Actor classes in this set will not generate warnings
	Builder->ClassDiscards.Add(ALevelBounds::StaticClass());
	// Avoid dependency (find class)
	UClass* ChaosDebugClass = FindObject<UClass>(ANY_PACKAGE, TEXT("ChaosDebugDrawComponent"));
	if (ChaosDebugClass)
	{
		Builder->ClassDiscards.Add(ChaosDebugClass);
	}

	Builder->ClassDiscards.Add(AWorldSettings::StaticClass());
	
	// Root Components that are SceneComponents (not child class of)
	Builder->ClassDiscards.Add(USceneComponent::StaticClass());
	
	Builder->Packers.Add(FLevelInstanceRecursivePacker::PackerID, MakeUnique<FLevelInstanceRecursivePacker>());
	Builder->Packers.Add(FLevelInstanceISMPacker::PackerID, MakeUnique<FLevelInstanceISMPacker>());

	return Builder;
}

void FPackedLevelInstanceBuilder::PackActor(APackedLevelInstance* InPackedLevelInstance)
{
	PackActor(InPackedLevelInstance, InPackedLevelInstance);
}

void FPackedLevelInstanceBuilder::PackActor(APackedLevelInstance* InPackedLevelInstance, ALevelInstance* InLevelInstanceToPack)
{
	FMessageLog LevelInstanceLog("LevelInstance");
	LevelInstanceLog.Info(FText::Format(LOCTEXT("PackingStarted", "Packing of '{0}' started..."), FText::FromString(InPackedLevelInstance->GetWorldAssetPackage())));
	
	FPackedLevelInstanceBuilderContext Context(*this, InPackedLevelInstance);

	InPackedLevelInstance->DestroyPackedComponents();

	ULevelInstanceSubsystem* LevelInstanceSubystem = InPackedLevelInstance->GetLevelInstanceSubsystem();
	check(LevelInstanceSubystem);
	
	ULevel* SourceLevel = LevelInstanceSubystem->GetLevelInstanceLevel(InLevelInstanceToPack);
		
	if (AActor* DefaultBrush = SourceLevel->GetDefaultBrush())
	{
		Context.DiscardActor(DefaultBrush);
	}
		
	for (AActor* LevelActor : SourceLevel->Actors)
	{
		if (LevelActor)
		{
			Context.ClusterLevelActor(LevelActor);
		}
	}

	for (const auto& Pair : Context.GetClusters())
	{
		TUniquePtr<ILevelInstancePacker>& Packer = Packers.FindChecked(Pair.Key.GetPackerID());
		Packer->PackActors(Context, InPackedLevelInstance, Pair.Key, Pair.Value);
	}
			
	Context.Report(LevelInstanceLog);
}

void FPackedLevelInstanceBuilderContext::Report(FMessageLog& LevelInstanceLog) const
{
	TSet<UActorComponent*> NotClusteredComponents;
	uint32 TotalWarningCount = 0;

	for (const auto& Pair : PerActorClusteredComponents)
	{
		AActor* Actor = Pair.Key;
		const TSet<UActorComponent*>& ClusteredComponents = Pair.Value;

		if (ActorDiscards.Contains(Actor))
		{
			LevelInstanceLog.Info(FText::Format(LOCTEXT("ActorDiscard", "Actor '{0}' ignored (Actor Discard)"), FText::FromString(Actor->GetPathName())));
			continue;
		}

		if (Actor->GetClass()->HasAnyClassFlags(CLASS_Transient))
		{
			LevelInstanceLog.Info(FText::Format(LOCTEXT("ActorTransientClassDiscard", "Actor '{0}' of type '{1}' ignored (Transient Class Discard)"), FText::FromString(Actor->GetPathName()), FText::FromString(Actor->GetClass()->GetPathName())));
			continue;
		}

		// Class must match (not a child)
		if (ClassDiscards.Contains(Actor->GetClass()))
		{
			LevelInstanceLog.Info(FText::Format(LOCTEXT("ActorClassDiscard", "Actor '{0}' of type '{1}' ignored (Class Discard)"), FText::FromString(Actor->GetPathName()), FText::FromString(Actor->GetClass()->GetPathName())));
			continue;
		}

		NotClusteredComponents = Actor->GetComponents().Difference(ClusteredComponents);
		uint32 WarningCount = 0;
		for (UActorComponent* Component : NotClusteredComponents)
		{
			if (Component == nullptr)
			{
				continue;
			}

			if (ClassDiscards.Contains(Component->GetClass()))
			{
				LevelInstanceLog.Info(FText::Format(LOCTEXT("ComponentClassDiscard", "Component '{0}' of type '{1}' ignored (Class Discard)"), FText::FromString(Component->GetPathName()), FText::FromString(Component->GetClass()->GetPathName())));
				continue;
			}

			if (Actor->GetClass()->HasAnyClassFlags(CLASS_Transient))
			{
				LevelInstanceLog.Info(FText::Format(LOCTEXT("ComopnentTransientClassDiscard", "Component '{0}' of type '{1}' ignored (Transient Class Discard)"), FText::FromString(Component->GetPathName()), FText::FromString(Component->GetClass()->GetPathName())));
				continue;
			}

			WarningCount++;

			LevelInstanceLog.Warning(FText::Format(LOCTEXT("ComponentNotPacked", "Component '{0}' was not packed"), FText::FromString(Component->GetPathName())));
		}

		if (WarningCount)
		{
			LevelInstanceLog.Warning(FText::Format(LOCTEXT("ActorNotPacked", "Actor '{0}' was not packed completely ({1} warning(s))"), FText::FromString(Actor->GetPathName()), FText::AsNumber(WarningCount)));
		}
		else
		{
			LevelInstanceLog.Info(FText::Format(LOCTEXT("ActorPacked", "Actor '{0}' packed successfully"), FText::FromString(Actor->GetPathName())));
		}
		TotalWarningCount += WarningCount;
	}

	if (TotalWarningCount)
	{
		LevelInstanceLog.Warning(LOCTEXT("WarningsReported", "Warnings have been reported. Consider using a regular ALevelInstance instead."));
		LevelInstanceLog.Open();
	}
	LevelInstanceLog.Info(FText::Format(LOCTEXT("PackCompleted", "Packing '{0}' completed with {1} warning(s)"), FText::FromString(PackedLevelInstance->GetWorldAssetPackage()), FText::AsNumber(TotalWarningCount)));
}

ALevelInstance* FPackedLevelInstanceBuilder::CreateTransientLevelInstanceForPacking(TSoftObjectPtr<UWorld> InWorldAsset, const FVector& InLocation, const FRotator& InRotator)
{
	// Create Temp Actor for Packing
	FActorSpawnParameters SpawnParams;
	SpawnParams.bCreateActorPackage = false;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transient;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	check(World);
	SpawnParams.OverrideLevel = World->PersistentLevel;
	ALevelInstance* LevelInstance = World->SpawnActor<ALevelInstance>(InLocation, InRotator, SpawnParams);
	LevelInstance->SetWorldAsset(InWorldAsset);

	// Wait for load
	LevelInstance->GetLevelInstanceSubsystem()->BlockLoadLevelInstance(LevelInstance);

	return LevelInstance;
}

void FPackedLevelInstanceBuilder::PackActor(APackedLevelInstance* InActor, TSoftObjectPtr<UWorld> InWorldAsset)
{
	ALevelInstance* TransientLevelInstance = CreateTransientLevelInstanceForPacking(InWorldAsset, InActor->GetActorLocation(), InActor->GetActorRotation());

	PackActor(InActor, TransientLevelInstance);

	TransientLevelInstance->GetWorld()->DestroyActor(TransientLevelInstance);
}

void FPackedLevelInstanceBuilder::UpdateBlueprint(UBlueprint* Blueprint)
{
	APackedLevelInstance* CDO = CastChecked<APackedLevelInstance>(Blueprint->GeneratedClass->GetDefaultObject());
	check(CDO);

	CreateOrUpdateBlueprint(CDO->GetWorldAsset(), Blueprint);
}

bool FPackedLevelInstanceBuilder::CreateOrUpdateBlueprint(TSoftObjectPtr<UWorld> InWorldAsset, TSoftObjectPtr<UBlueprint> InBlueprintAsset)
{
	bool bResult = true;
	
	ALevelInstance* TransientLevelInstance = CreateTransientLevelInstanceForPacking(InWorldAsset, FVector::ZeroVector, FRotator::ZeroRotator);
	
	bResult = CreateOrUpdateBlueprintFromUnpacked(TransientLevelInstance, InBlueprintAsset);

	TransientLevelInstance->GetWorld()->DestroyActor(TransientLevelInstance);

	return bResult;
}

bool FPackedLevelInstanceBuilder::CreateOrUpdateBlueprint(ALevelInstance* InLevelInstance, TSoftObjectPtr<UBlueprint> InBlueprintAsset)
{
	if (APackedLevelInstance* PackedLevelInstance = Cast<APackedLevelInstance>(InLevelInstance))
	{
		return CreateOrUpdateBlueprintFromPacked(PackedLevelInstance, InBlueprintAsset);
	}
	
	return CreateOrUpdateBlueprintFromUnpacked(InLevelInstance, InBlueprintAsset);
}

bool FPackedLevelInstanceBuilder::CreateOrUpdateBlueprintFromUnpacked(ALevelInstance* InActor, TSoftObjectPtr<UBlueprint> InBlueprintAsset)
{
	bool bResult = true;
	
	// Create Temp Actor for Packing
	FActorSpawnParameters SpawnParams;
	SpawnParams.bCreateActorPackage = false;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transient;

	UWorld* World = InActor->GetWorld();
	SpawnParams.OverrideLevel = World->PersistentLevel;

	APackedLevelInstance* PackedLevelInstance = World->SpawnActor<APackedLevelInstance>(InActor->GetActorLocation(), InActor->GetActorRotation(), SpawnParams);
	PackedLevelInstance->SetWorldAsset(InActor->GetWorldAsset());

	PackActor(PackedLevelInstance, InActor);

	PackedLevelInstance->BlueprintAsset = InBlueprintAsset;
	bResult &= CreateOrUpdateBlueprintFromPacked(PackedLevelInstance, PackedLevelInstance->BlueprintAsset);

	InActor->GetWorld()->DestroyActor(PackedLevelInstance);

	return bResult;
}

bool FPackedLevelInstanceBuilder::CreateOrUpdateBlueprintFromPacked(APackedLevelInstance* InActor, TSoftObjectPtr<UBlueprint> InBlueprintAsset)
{
	UBlueprint* BP = nullptr;
	if (!InBlueprintAsset.IsNull())
	{
		BP = InBlueprintAsset.LoadSynchronous();
		if (BP && BP->SimpleConstructionScript)
		{
			TArray<USCS_Node*> AllNodes = BP->SimpleConstructionScript->GetAllNodes();
			for (USCS_Node* Node : AllNodes)
			{
				BP->SimpleConstructionScript->RemoveNodeAndPromoteChildren(Node);
			}
		}
	}

	if (!BP)
	{
		int32 LastSlashIndex = 0;
		FString LongPackageName = InActor->GetWorldAsset().GetLongPackageName();
		LongPackageName.FindLastChar('/', LastSlashIndex);

		FString PackagePath = LongPackageName.Mid(0, LastSlashIndex == INDEX_NONE ? MAX_int32 : LastSlashIndex);
		FString AssetName = InActor->GetWorldAsset().GetAssetName() + GetPackedBPSuffix();
		const bool bCompile = false;
		BP = CreatePackedLevelInstanceBlueprint(PackagePath, AssetName, bCompile);
	}
		
	if (BP == nullptr)
	{
		return false;
	}

	BP->Modify();
	APackedLevelInstance* CDO = CastChecked<APackedLevelInstance>(BP->GeneratedClass->GetDefaultObject());
	CDO->SetWorldAsset(InActor->GetWorldAsset());
	
	// match root component mobility to source actor
	USceneComponent* Root = CDO->GetRootComponent();
	Root->SetMobility(InActor->GetRootComponent()->Mobility);
		
	// Prep AddComponentsToBlueprintParam
	FKismetEditorUtilities::FAddComponentsToBlueprintParams AddCompToBPParams;
	AddCompToBPParams.HarvestMode = FKismetEditorUtilities::EAddComponentToBPHarvestMode::None;
	AddCompToBPParams.bKeepMobility = true;

	// Add Components
	TArray<UActorComponent*> PackedComponents;
	InActor->GetPackedComponents(PackedComponents);
	FKismetEditorUtilities::AddComponentsToBlueprint(BP, PackedComponents, AddCompToBPParams);
	// If we are packing the actors BP then destroy packed components as they are now part of the BPs construction script
	if (UBlueprint* GeneratedBy = Cast<UBlueprint>(InActor->GetClass()->ClassGeneratedBy))
	{
		InActor->DestroyPackedComponents();
	}

	// Synchronous compile
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection);

	const bool bCheckDirty = false;
	const bool bPromptToSave = true;
	TArray<UPackage*> OutFailedPackages;
	FEditorFileUtils::PromptForCheckoutAndSave({ BP->GetPackage() }, bCheckDirty, bPromptToSave, &OutFailedPackages);
		
	return !OutFailedPackages.Num();
}
#endif

#undef LOCTEXT_NAMESPACE