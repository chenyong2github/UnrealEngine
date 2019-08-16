// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LODActorBase.cpp: Static mesh actor base class implementation.
=============================================================================*/

#include "Engine/LODActor.h"
#include "UObject/UObjectIterator.h"
#include "Engine/CollisionProfile.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/MapErrors.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "EngineUtils.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "Engine/HLODProxy.h"
#include "UObject/PropertyPortFlags.h"

#if WITH_EDITOR
#include "Editor.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "IHierarchicalLODUtilities.h"
#include "ObjectTools.h"
#include "HierarchicalLOD.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogHLOD, Log, All);

#define LOCTEXT_NAMESPACE "LODActor"

int32 GMaximumAllowedHLODLevel = -1;

static FAutoConsoleVariableRef CVarMaximumAllowedHLODLevel(
	TEXT("r.HLOD.MaximumLevel"),
	GMaximumAllowedHLODLevel,
	TEXT("How far down the LOD hierarchy to allow showing (can be used to limit quality loss and streaming texture memory usage on high scalability settings)\n")
	TEXT("-1: No maximum level (default)\n")
	TEXT("0: Prevent ever showing a HLOD cluster instead of individual meshes\n")
	TEXT("1: Allow only the first level of HLOD clusters to be shown\n")
	TEXT("2+: Allow up to the Nth level of HLOD clusters to be shown"),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarHLODDitherPauseTime(
	TEXT("r.HLOD.DitherPauseTime"),
	0.5f,
	TEXT("HLOD dither pause time in seconds\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

ENGINE_API TAutoConsoleVariable<FString> CVarHLODDistanceOverride(
	TEXT("r.HLOD.DistanceOverride"),
	"0.0",
	TEXT("If non-zero, overrides the distance that HLOD transitions will take place for all objects at the HLOD level index, formatting is as follows:\n")
	TEXT("'r.HLOD.DistanceOverride 5000, 10000, 20000' would result in HLOD levels 0, 1 and 2 transitioning at 5000, 1000 and 20000 respectively."),
	ECVF_Scalability);

ENGINE_API TArray<float> ALODActor::HLODDistances;

#if !(UE_BUILD_SHIPPING)
static void HLODConsoleCommand(const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() == 1)
	{
		const int32 State = FCString::Atoi(*Args[0]);

		if (State == 0 || State == 1)
		{
			const bool bHLODEnabled = (State == 1) ? true : false;
			FlushRenderingCommands();
			const TArray<ULevel*>& Levels = World->GetLevels();
			for (ULevel* Level : Levels)
			{
				for (AActor* Actor : Level->Actors)
				{
					ALODActor* LODActor = Cast<ALODActor>(Actor);
					if (LODActor)
					{
						LODActor->SetActorHiddenInGame(!bHLODEnabled);
#if WITH_EDITOR
						LODActor->SetIsTemporarilyHiddenInEditor(!bHLODEnabled);
#endif // WITH_EDITOR
						LODActor->MarkComponentsRenderStateDirty();
					}
				}
			}
		}
	}
	else if (Args.Num() == 2)
	{
#if WITH_EDITOR
		if (Args[0] == "force")
		{
			const int32 ForcedLevel = FCString::Atoi(*Args[1]);

			if (ForcedLevel >= -1 && ForcedLevel < World->GetWorldSettings()->GetNumHierarchicalLODLevels())
			{
				const TArray<ULevel*>& Levels = World->GetLevels();
				for (ULevel* Level : Levels)
				{
					for (AActor* Actor : Level->Actors)
					{
						ALODActor* LODActor = Cast<ALODActor>(Actor);

						if (LODActor)
						{
							if (ForcedLevel != -1)
							{
								if (LODActor->LODLevel == ForcedLevel + 1)
								{
									LODActor->SetForcedView(true);
								}
								else
								{
									LODActor->SetHiddenFromEditorView(true, ForcedLevel + 1);
								}
							}
							else
							{
								LODActor->SetForcedView(false);
								LODActor->SetIsTemporarilyHiddenInEditor(false);
							}
						}
					}
				}
			}
		}		
#endif // WITH_EDITOR
	}
}

static FAutoConsoleCommandWithWorldAndArgs GHLODCmd(
	TEXT("r.HLOD"),
	TEXT("Single argument: 0 or 1 to Disable/Enable HLOD System\nMultiple arguments: force X where X is the HLOD level that should be forced into view"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(HLODConsoleCommand)
	);

static void ListUnbuiltHLODActors(const TArray<FString>& Args, UWorld* World)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	int32 NumUnbuilt = 0;
	for (TActorIterator<ALODActor> HLODIt(World); HLODIt; ++HLODIt)
	{
		ALODActor* Actor = *HLODIt;
		if (!Actor->IsBuilt() && Actor->HasValidLODChildren())
		{
			++NumUnbuilt;
			FString ActorPathName = Actor->GetPathName(World);
			UE_LOG(LogHLOD, Warning, TEXT("HLOD %s is unbuilt (HLOD level %i)"), *ActorPathName, Actor->LODLevel);
		}
	}

	UE_LOG(LogHLOD, Warning, TEXT("%d HLOD actor(s) were unbuilt"), NumUnbuilt);
#endif	//  !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}

static FAutoConsoleCommandWithWorldAndArgs GHLODListUnbuiltCmd(
	TEXT("r.HLOD.ListUnbuilt"),
	TEXT("Lists all unbuilt HLOD actors in the world"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(ListUnbuiltHLODActors)
);

#endif // !(UE_BUILD_SHIPPING)

//////////////////////////////////////////////////////////////////////////
// ALODActor

FAutoConsoleVariableSink ALODActor::CVarSink(FConsoleCommandDelegate::CreateStatic(&ALODActor::OnCVarsChanged));

ALODActor::ALODActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LODDrawDistance(5000)
	, bHasActorTriedToRegisterComponents(false)
{
	bCanBeDamaged = false;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bAllowTickOnDedicatedServer = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;

#if WITH_EDITORONLY_DATA
	
	bListedInSceneOutliner = false;

	NumTrianglesInSubActors = 0;
	NumTrianglesInMergedMesh = 0;
	
#endif // WITH_EDITORONLY_DATA

	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent0"));
	SetupComponent(StaticMeshComponent);

	bNeedsDrawDistanceReset = false;
	bHasPatchedUpParent = false;
	ResetDrawDistanceTime = 0.0f;
	RootComponent = StaticMeshComponent;	
	CachedNumHLODLevels = 1;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bCachedIsBuilt = false;
	LastIsBuiltTime = 0.0;
#endif
}

void ALODActor::SetupComponent(UStaticMeshComponent* Component)
{
	// Cast shadows if any sub-actors do
	bool bCastsShadow = false;
	bool bCastsStaticShadow = false;
	bool bCastsDynamicShadow = false;

	Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Component->Mobility = EComponentMobility::Static;
	Component->SetGenerateOverlapEvents(false);
	Component->CastShadow = bCastsShadow;
	Component->bCastStaticShadow = bCastsStaticShadow;
	Component->bCastDynamicShadow = bCastsDynamicShadow;
	Component->bAllowCullDistanceVolume = false;
	Component->bNeverDistanceCull = true;

	Component->MinDrawDistance = LODDrawDistance;
}

FString ALODActor::GetDetailedInfoInternal() const
{
	return StaticMeshComponent ? StaticMeshComponent->GetDetailedInfoInternal() : TEXT("No_StaticMeshComponent");
}

void ALODActor::PostLoad()
{
	Super::PostLoad();
	SetComponentsMinDrawDistance(LODDrawDistance, false);
	StaticMeshComponent->bCastDynamicShadow = false;	
	UpdateRegistrationToMatchMaximumLODLevel();

#if WITH_EDITOR
	if (bRequiresLODScreenSizeConversion)
	{
		if (TransitionScreenSize == 0.0f)
		{
			TransitionScreenSize = 1.0f;
		}
		else
		{
			const float HalfFOV = PI * 0.25f;
			const float ScreenWidth = 1920.0f;
			const float ScreenHeight = 1080.0f;
			const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);
			FBoxSphereBounds Bounds = GetStaticMeshComponent()->CalcBounds(FTransform());
			for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
			{
				Bounds = Bounds + Component.Value->CalcBounds(FTransform());
			}

			// legacy transition screen size was previously a screen AREA fraction using resolution-scaled values, so we need to convert to distance first to correctly calculate the threshold
			const float ScreenArea = TransitionScreenSize * (ScreenWidth * ScreenHeight);
			const float ScreenRadius = FMath::Sqrt(ScreenArea / PI);
			const float ScreenDistance = FMath::Max(ScreenWidth / 2.0f * ProjMatrix.M[0][0], ScreenHeight / 2.0f * ProjMatrix.M[1][1]) * Bounds.SphereRadius / ScreenRadius;

			// Now convert using the query function
			TransitionScreenSize = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ScreenDistance), ProjMatrix);
		}
	}

	CachedNumHLODLevels = GetLevel()->GetWorldSettings()->GetNumHierarchicalLODLevels();
#endif

#if !WITH_EDITOR
	// Invalid runtime LOD actor with null static mesh is invalid, look for a possible way to patch this up
	if (GetStaticMeshComponent()->GetStaticMesh() == nullptr)
	{
		if (GetStaticMeshComponent() && GetStaticMeshComponent()->GetLODParentPrimitive())
		{
 			if (ALODActor* ParentLODActor = Cast<ALODActor>(GetStaticMeshComponent()->GetLODParentPrimitive()->GetOwner()))
			{
				if ( ParentLODActor->GetStaticMeshComponent()->GetStaticMesh() != nullptr )
				{				
					// Make the parent HLOD 			
					ParentLODActor->SubActors.Remove(this);
					ParentLODActor->SubActors.Append(SubActors);			
					for (AActor* Actor : SubActors)
					{
						if (Actor)
						{
							Actor->SetLODParent(ParentLODActor->GetStaticMeshComponent(), ParentLODActor->GetDrawDistance());
						}
					}
  
					SubActors.Empty();
					bHasPatchedUpParent = true;
				}
			}
		}
	}
#endif // !WITH_EDITOR

	ParseOverrideDistancesCVar();
	UpdateOverrideTransitionDistance();

#if WITH_EDITOR
	bool bMarkRenderStateDirty = false;
	for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
	{
		for (int32 i=0; i<Component.Value->PerInstanceSMData.Num() - 1; i++)
		{
			for (int32 j=i + 1; j<Component.Value->PerInstanceSMData.Num(); j++)
			{
				if (Component.Value->PerInstanceSMData[i].Transform == Component.Value->PerInstanceSMData[j].Transform)
				{
					Component.Value->PerInstanceSMData.RemoveAtSwap(j--);
					bMarkRenderStateDirty = true;
				}
			}
		}

		if (bMarkRenderStateDirty)
		{
			Component.Value->MarkRenderStateDirty();
		}
	}
#endif
}

void ALODActor::SetComponentsMinDrawDistance(float InMinDrawDistance, bool bInMarkRenderStateDirty)
{
	float MinDrawDistance = FMath::Max(0.0f, InMinDrawDistance);

	StaticMeshComponent->MinDrawDistance = MinDrawDistance;
	if (bInMarkRenderStateDirty)
	{
		StaticMeshComponent->MarkRenderStateDirty();
	}

	for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
	{
		Component.Value->MinDrawDistance = MinDrawDistance;
		if (bInMarkRenderStateDirty)
		{
			Component.Value->MarkRenderStateDirty();
		}
	}
}

void ALODActor::UpdateOverrideTransitionDistance()
{
	const int32 NumDistances = ALODActor::HLODDistances.Num();
	// Determine correct distance index to apply to ensure combinations of different levels will work			
	const int32 DistanceIndex = [&]()
	{
		if (CachedNumHLODLevels == NumDistances)
		{
			return LODLevel - 1;
		}
		else if (CachedNumHLODLevels < NumDistances)
		{
			return (LODLevel + (NumDistances - CachedNumHLODLevels)) - 1;
		}
		else
		{
			// We've reached the end of the array, change nothing
			return (int32)INDEX_NONE;
		}
	}();

	if (DistanceIndex != INDEX_NONE)
	{
		float MinDrawDistance = (!HLODDistances.IsValidIndex(DistanceIndex) || FMath::IsNearlyZero(HLODDistances[DistanceIndex])) ? LODDrawDistance : ALODActor::HLODDistances[DistanceIndex];
		SetComponentsMinDrawDistance(MinDrawDistance, true);
	}
}

void ALODActor::ParseOverrideDistancesCVar()
{
	// Parse HLOD override distance cvar into array
	const FString DistanceOverrideValues = CVarHLODDistanceOverride.GetValueOnAnyThread();
	TArray<FString> Distances;
	DistanceOverrideValues.ParseIntoArray(/*out*/ Distances, TEXT(","), /*bCullEmpty=*/ false);
	HLODDistances.Empty(Distances.Num());

	for (const FString& DistanceString : Distances)
	{
		const float DistanceForThisLevel = FCString::Atof(*DistanceString);
		HLODDistances.Add(DistanceForThisLevel);
	}	
}

float ALODActor::GetLODDrawDistanceWithOverride() const
{
	const int32 NumDistances = ALODActor::HLODDistances.Num();
	const int32 DistanceIndex = [&]()
	{
		if(CachedNumHLODLevels <= NumDistances)
		{
			return (LODLevel + (NumDistances - CachedNumHLODLevels)) - 1;
		}
		else
		{
			// We've reached the end of the array, change nothing
			return (int32)INDEX_NONE;
		}
	}();

	const float HLODDistanceOverride = (!ALODActor::HLODDistances.IsValidIndex(DistanceIndex)) ? 0.0f : ALODActor::HLODDistances[DistanceIndex];
	// Determine desired HLOD state
	float MinDrawDistance = LODDrawDistance;
	const bool bIsOverridingHLODDistance = HLODDistanceOverride != 0.0f;
	if(bIsOverridingHLODDistance)
	{
		MinDrawDistance = HLODDistanceOverride;
	}

	return MinDrawDistance;
}

void ALODActor::Tick(float DeltaSeconds)
{
	AActor::Tick(DeltaSeconds);
	if (bNeedsDrawDistanceReset)
	{		
		if (ResetDrawDistanceTime > CVarHLODDitherPauseTime.GetValueOnAnyThread())
		{
			// Determine desired HLOD state
			float MinDrawDistance = GetLODDrawDistanceWithOverride();

			SetComponentsMinDrawDistance(MinDrawDistance, true);
			bNeedsDrawDistanceReset = false;
			ResetDrawDistanceTime = 0.0f;
			PrimaryActorTick.SetTickFunctionEnable(false);
		}
		else
        {
			const float CurrentTimeDilation = FMath::Max(GetActorTimeDilation(), SMALL_NUMBER);
			ResetDrawDistanceTime += DeltaSeconds / CurrentTimeDilation;
        }
	}
}

void ALODActor::SetLODParent(UPrimitiveComponent* InLODParent, float InParentDrawDistance, bool bApplyToImposters)
{
	if (bApplyToImposters)
	{
		AActor::SetLODParent(InLODParent, InParentDrawDistance);
	}
	else
	{
		if(InLODParent)
		{
			InLODParent->MinDrawDistance = InParentDrawDistance;
			InLODParent->MarkRenderStateDirty();
		}

		StaticMeshComponent->SetLODParentPrimitive(InLODParent);
	}
}

void ALODActor::PauseDitherTransition()
{
	SetComponentsMinDrawDistance(0.0f, true);
	bNeedsDrawDistanceReset = true;
	ResetDrawDistanceTime = 0.0f;
}

void ALODActor::StartDitherTransition()
{
	PrimaryActorTick.SetTickFunctionEnable(bNeedsDrawDistanceReset);
}

void ALODActor::UpdateRegistrationToMatchMaximumLODLevel()
{
	// Determine if we can show this HLOD level and allow or prevent the SMC from being registered
	// This doesn't save the memory of the static mesh or lowest mip levels, but it prevents the proxy from being created
	// or high mip textures from being streamed in
	const int32 MaximumAllowedHLODLevel = GMaximumAllowedHLODLevel;
	const bool bAllowShowingThisLevel = (MaximumAllowedHLODLevel < 0) || (LODLevel <= MaximumAllowedHLODLevel);

	check(StaticMeshComponent);
	if (StaticMeshComponent->bAutoRegister != bAllowShowingThisLevel)
	{
		StaticMeshComponent->bAutoRegister = bAllowShowingThisLevel;

		if (!bAllowShowingThisLevel && StaticMeshComponent->IsRegistered())
		{
			ensure(bHasActorTriedToRegisterComponents);
			UnregisterMeshComponents();
		}
		else if (bAllowShowingThisLevel && !StaticMeshComponent->IsRegistered())
		{
			// We should only register components if the actor had already tried to register before (otherwise it'll be taken care of in the normal flow)
			if (bHasActorTriedToRegisterComponents)
			{
				RegisterMeshComponents();
			}
		}
	}
}

void ALODActor::PostRegisterAllComponents() 
{
	Super::PostRegisterAllComponents();

	bHasActorTriedToRegisterComponents = true;

	// In case we patched up the subactors to a parent LOD actor, we can unregister this component as it's not used anymore
	if (bHasPatchedUpParent)
	{
		UnregisterMeshComponents();
	}

#if WITH_EDITOR
	if(!GetWorld()->IsPlayInEditor())
	{
		// Clean up sub actor if assets were delete manually
		CleanSubActorArray();

		UpdateSubActorLODParents();
	}
#endif
}

void ALODActor::RegisterMeshComponents()
{
	if (!StaticMeshComponent->IsRegistered())
	{
		StaticMeshComponent->RegisterComponent();
	}

	for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
	{
		if (!Component.Value->IsRegistered())
		{
			Component.Value->RegisterComponent();
		}
	}
}

void ALODActor::UnregisterMeshComponents()
{
	if (StaticMeshComponent->IsRegistered())
	{
		StaticMeshComponent->UnregisterComponent();
	}
	else
	{
		StaticMeshComponent->bAutoRegister = false;
	}

	for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
	{
		if (Component.Value->IsRegistered())
		{
			Component.Value->UnregisterComponent();
		}
		else
		{
			Component.Value->bAutoRegister = false;
		}
	}
}

void ALODActor::SetDrawDistance(float InDistance)
{
	LODDrawDistance = InDistance;
	SetComponentsMinDrawDistance(LODDrawDistance, false);
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

const bool ALODActor::IsBuilt(bool bInForce/*=false*/) const
{
	auto IsBuiltHelper = [this]()
	{
		// Ensure all subactors are linked to a LOD static mesh component.
		for (AActor* SubActor : SubActors)
		{
			if(SubActor)
			{
				UStaticMeshComponent* LODComponent = GetLODComponentForActor(SubActor, false);
				if (LODComponent == nullptr || LODComponent->GetStaticMesh() == nullptr)
				{
					return false;
				}
			}
		}

		// No proxy mesh
		if (Proxy == nullptr)
		{
			return false;
		}

		// Mismatched key
		if (!Proxy->ContainsDataForActor(this))
		{
			return false;
		}

		// Unbuilt children
		for (AActor* SubActor : SubActors)
		{
			if (ALODActor* SubLODActor = Cast<ALODActor>(SubActor))
			{
				if (!SubLODActor->IsBuilt(true))
				{
					return false;
				}
			}
		}

		return true;
	};

	const double CurrentTime = FPlatformTime::Seconds();
	if (bInForce || (CurrentTime - LastIsBuiltTime > 0.5))
	{
		bCachedIsBuilt = IsBuiltHelper();
		LastIsBuiltTime = CurrentTime;
	}

	return bCachedIsBuilt;
}
#endif

const bool ALODActor::HasValidLODChildren() const
{
	if (SubActors.Num() > 0)
	{
		for (const AActor* Actor : SubActors)
		{
			if (Actor)
			{
				// Retrieve contained components for all sub-actors
				TArray<const UPrimitiveComponent*> Components;
				Actor->GetComponents(Components);

				// Try and find the parent primitive(s) and see if it matches this LODActor's static mesh component
				for (const UPrimitiveComponent* PrimitiveComponent : Components)
				{
					if (const UPrimitiveComponent* ParentPrimitiveComponent = PrimitiveComponent ? PrimitiveComponent->GetLODParentPrimitive() : nullptr)
					{
						if (ParentPrimitiveComponent && GetComponents().Contains(const_cast<UPrimitiveComponent*>(ParentPrimitiveComponent)))
						{
							return true;
						}
					}
				}
			}
		}		
	}

	return false;
}

#if WITH_EDITOR

void ALODActor::ForceUnbuilt()
{
	Key = NAME_None;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bCachedIsBuilt = false;
	LastIsBuiltTime = 0.0;
#endif
}

void ALODActor::PreEditChange(UProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	// Flush all pending rendering commands.
	FlushRenderingCommands();
}

void ALODActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	FName PropertyName = PropertyThatChanged != NULL ? PropertyThatChanged->GetFName() : NAME_None;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ALODActor, bOverrideTransitionScreenSize) || PropertyName == GET_MEMBER_NAME_CHECKED(ALODActor, TransitionScreenSize))
	{
		float CalculateScreenSize = 0.0f;

		if (bOverrideTransitionScreenSize)
		{
			CalculateScreenSize = TransitionScreenSize;
		}
		else
		{
			UWorld* World = GetWorld();
			check(World != nullptr);
			const TArray<struct FHierarchicalSimplification>& HierarchicalLODSetups = World->GetWorldSettings()->GetHierarchicalLODSetup();
			checkf(HierarchicalLODSetups.IsValidIndex(LODLevel - 1), TEXT("Out of range HLOD level (%i) found in LODActor (%s)"), LODLevel - 1, *GetName());
			CalculateScreenSize = HierarchicalLODSetups[LODLevel - 1].TransitionScreenSize;
		}

		RecalculateDrawingDistance(CalculateScreenSize);
	}

	UpdateRegistrationToMatchMaximumLODLevel();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

bool ALODActor::GetReferencedContentObjects( TArray<UObject*>& Objects ) const
{
	Super::GetReferencedContentObjects(Objects);
	
	// Retrieve referenced objects for sub actors as well
	for (AActor* SubActor : SubActors)
	{
		if (SubActor)
		{
			SubActor->GetReferencedContentObjects(Objects);
		}
	}
	return true;
}

void ALODActor::CheckForErrors()
{
	FMessageLog MapCheck("MapCheck");

	// Only check when this is not a preview actor and actually has a static mesh	
	Super::CheckForErrors();
	if (!StaticMeshComponent)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("ActorName"), FText::FromString(GetPathName()));
		MapCheck.Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_StaticMeshComponent", "{ActorName} : Static mesh actor has NULL StaticMeshComponent property - please delete."), Arguments)))
			->AddToken(FMapErrorToken::Create(FMapErrors::StaticMeshComponent));
	}

	if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh() == nullptr)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("ActorName"), FText::FromString(GetPathName()));
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_InvalidLODActorMissingMesh", "{ActorName} : Static mesh is missing for the built LODActor.  Did you remove the asset? Please delete it and build LOD again. "), Arguments)))
			->AddToken(FMapErrorToken::Create(FMapErrors::LODActorMissingStaticMesh));
	}
	
	if (SubActors.Num() == 0)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("ActorName"), FText::FromString(GetPathName()));
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_InvalidLODActorEmptyActor", "{ActorName} : NoActor is assigned. We recommend you to delete this actor. "), Arguments)))
			->AddToken(FMapErrorToken::Create(FMapErrors::LODActorNoActorFound));
	}
	else
	{
		for (AActor* Actor : SubActors)
		{
			// see if it's null, if so it is not good
			if(Actor == nullptr)
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("ActorName"), FText::FromString(GetPathName()));
				FMessageLog("MapCheck").Error()
					->AddToken(FUObjectToken::Create(this))
					->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_InvalidLODActorNullActor", "{ActorName} : Actor is missing. The actor might have been removed. We recommend you to build LOD again. "), Arguments)))
					->AddToken(FMapErrorToken::Create(FMapErrors::LODActorMissingActor));
			}
		}
	}
}

void ALODActor::EditorApplyTranslation(const FVector& DeltaTranslation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
}

void ALODActor::EditorApplyRotation(const FRotator& DeltaRotation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
}

void ALODActor::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
}

void ALODActor::EditorApplyMirror(const FVector& MirrorScale, const FVector& PivotLocation)
{
}

void ALODActor::AddSubActor(AActor* InActor)
{
	SubActors.Add(InActor);

	UStaticMeshComponent* LODComponent = GetOrCreateLODComponentForActor(InActor);
	InActor->SetLODParent(LODComponent, LODDrawDistance);

	// Adding number of triangles
	if (!InActor->IsA<ALODActor>())
	{
		TArray<UStaticMeshComponent*> StaticMeshComponents;
		InActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
		for (UStaticMeshComponent* Component : StaticMeshComponents)
		{
			const UStaticMesh* StaticMesh = (Component) ? Component->GetStaticMesh() : nullptr;
			if (StaticMesh && StaticMesh->RenderData && StaticMesh->RenderData->LODResources.Num() > 0)
			{
				NumTrianglesInSubActors += StaticMesh->RenderData->LODResources[0].GetNumTriangles();
			}
			Component->MarkRenderStateDirty();
		}
	}
	else
	{
		ALODActor* LODActor = Cast<ALODActor>(InActor);
		NumTrianglesInSubActors += LODActor->GetNumTrianglesInSubActors();
	}
	
	// Reset the shadowing flags and determine them according to our current sub actors
	DetermineShadowingFlags();
}

const bool ALODActor::RemoveSubActor(AActor* InActor)
{
	if ((InActor != nullptr) && SubActors.Contains(InActor))
	{
		SubActors.Remove(InActor);
		InActor->SetLODParent(nullptr, 0);

		// Deducting number of triangles
		if (!InActor->IsA<ALODActor>())
		{
			TArray<UStaticMeshComponent*> StaticMeshComponents;
			InActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
			for (UStaticMeshComponent* Component : StaticMeshComponents)
			{
				const UStaticMesh* StaticMesh = (Component) ? Component->GetStaticMesh() : nullptr;
				if (StaticMesh && StaticMesh->RenderData && StaticMesh->RenderData->LODResources.Num() > 0)
				{
					NumTrianglesInSubActors -= StaticMesh->RenderData->LODResources[0].GetNumTriangles();
				}

				Component->MarkRenderStateDirty();
			}
		}
		else
		{
			ALODActor* LODActor = Cast<ALODActor>(InActor);
			NumTrianglesInSubActors -= LODActor->GetNumTrianglesInSubActors();
		}

		if (StaticMeshComponent)
		{
			StaticMeshComponent->MarkRenderStateDirty();
		}	
				
		// In case the user removes an actor while the HLOD system is force viewing one LOD level
		InActor->SetIsTemporarilyHiddenInEditor(false);

		// Reset the shadowing flags and determine them according to our current sub actors
		DetermineShadowingFlags();
				
		return true;
	}

	return false;
}

void ALODActor::DetermineShadowingFlags()
{
	// Cast shadows if any sub-actors do
	StaticMeshComponent->CastShadow = false;
	StaticMeshComponent->bCastStaticShadow = false;
	StaticMeshComponent->bCastDynamicShadow = false;
	StaticMeshComponent->bCastFarShadow = false;
	StaticMeshComponent->MarkRenderStateDirty();

	for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
	{
		Component.Value->CastShadow = false;
		Component.Value->bCastStaticShadow = false;
		Component.Value->bCastDynamicShadow = false;
		Component.Value->bCastFarShadow = false;
		Component.Value->MarkRenderStateDirty();
	}

	for (AActor* Actor : SubActors)
	{
		if (Actor)
		{
			UStaticMeshComponent* LODComponent = GetLODComponentForActor(Actor);

			TArray<UStaticMeshComponent*> StaticMeshComponents;
			Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
			for (UStaticMeshComponent* Component : StaticMeshComponents)
			{
				LODComponent->CastShadow |= Component->CastShadow;
				LODComponent->bCastStaticShadow |= Component->bCastStaticShadow;
				LODComponent->bCastDynamicShadow |= Component->bCastDynamicShadow;
				LODComponent->bCastFarShadow |= Component->bCastFarShadow;
			}
		}
	}
}

const bool ALODActor::HasValidSubActors() const
{
#if WITH_EDITOR
	FHierarchicalLODUtilitiesModule& Module = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
	IHierarchicalLODUtilities* Utilities = Module.GetUtilities();
#endif

	int32 NumMeshes = 0;

	// Make sure there is at least one mesh in the subactors
	for (AActor* SubActor : SubActors)
	{
		if (SubActor)
		{
			for (UActorComponent* Comp : SubActor->GetComponents())
			{
				if (UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Comp))
				{
#if WITH_EDITOR
					if (!Component->bHiddenInGame && Component->ShouldGenerateAutoLOD(LODLevel - 1))
					{
						++NumMeshes;
					}
#else
					++NumMeshes;
#endif
				}
			}

			if (NumMeshes > 0)
			{
				break;
			}
		}
	}

	return NumMeshes > 0;
}

const bool ALODActor::HasAnySubActors() const
{
	return (SubActors.Num() != 0);
}

void ALODActor::ToggleForceView()
{
	// Toggle the forced viewing of this LODActor, set drawing distance to 0.0f or LODDrawDistance
	SetComponentsMinDrawDistance((StaticMeshComponent->MinDrawDistance == 0.0f) ? LODDrawDistance : 0.0f, true);
}

void ALODActor::SetForcedView(const bool InState)
{
	// Set forced viewing state of this LODActor, set drawing distance to 0.0f or LODDrawDistance
	SetComponentsMinDrawDistance(InState ? 0.0f : LODDrawDistance, true);
}

void ALODActor::SetHiddenFromEditorView(const bool InState, const int32 ForceLODLevel )
{
	// If we are also subactor for a higher LOD level or this actor belongs to a higher HLOD level than is being forced hide the actor
	if (GetStaticMeshComponent()->GetLODParentPrimitive() || LODLevel > ForceLODLevel )
	{
		SetIsTemporarilyHiddenInEditor(InState);			

		for (AActor* Actor : SubActors)
		{
			if (Actor)
			{
				// If this actor belongs to a lower HLOD level that is being forced hide the sub-actors
				if (LODLevel < ForceLODLevel)
				{
					Actor->SetIsTemporarilyHiddenInEditor(InState);
				}

				// Toggle/set the LOD parent to nullptr or this
				if (InState)
				{
					Actor->SetLODParent(nullptr, 0.0f);
				}
				else
				{
					Actor->SetLODParent(GetLODComponentForActor(Actor), LODDrawDistance);
				}
			}
		}
	}

	StaticMeshComponent->MarkRenderStateDirty();
}

const uint32 ALODActor::GetNumTrianglesInSubActors()
{
	return NumTrianglesInSubActors;
}

const uint32 ALODActor::GetNumTrianglesInMergedMesh()
{
	return NumTrianglesInMergedMesh;
}

void ALODActor::SetStaticMesh(class UStaticMesh* InStaticMesh)
{
	if (StaticMeshComponent)
	{
		StaticMeshComponent->SetStaticMesh(InStaticMesh);

		ensure(StaticMeshComponent->GetStaticMesh() == InStaticMesh);
		if (InStaticMesh && InStaticMesh->RenderData && InStaticMesh->RenderData->LODResources.Num() > 0)
		{
			NumTrianglesInMergedMesh = InStaticMesh->RenderData->LODResources[0].GetNumTriangles();
		}
	}
}

void ALODActor::SetupImposters(UMaterialInterface* InMaterial, UStaticMesh* InStaticMesh, const TArray<FTransform>& InTransforms)
{
	check(InMaterial);
	check(InStaticMesh);
	check(InTransforms.Num() > 0);

	UInstancedStaticMeshComponent* Component = GetOrCreateLODComponentForMaterial(InMaterial);
	Component->SetStaticMesh(InStaticMesh);
	Component->PerInstanceSMData.Empty();
	
	for (const FTransform& Transform : InTransforms)
	{
		Component->AddInstanceWorldSpace(Transform);
	}

	// Ensure parenting is up to date and take into account the newly created component.
	UpdateSubActorLODParents();
}

void ALODActor::UpdateSubActorLODParents()
{
	for (AActor* Actor : SubActors)
	{	
		if(Actor)
		{
			UStaticMeshComponent* LODComponent = GetLODComponentForActor(Actor);
			Actor->SetLODParent(LODComponent, LODComponent->MinDrawDistance);
		}
	}
}

void ALODActor::CleanSubActorArray()
{
	for (int32 SubActorIndex = 0; SubActorIndex < SubActors.Num(); ++SubActorIndex)
	{
		AActor* Actor = SubActors[SubActorIndex];
		if (Actor == nullptr)
		{
			SubActors.RemoveAtSwap(SubActorIndex);
			SubActorIndex--;
		}
	}
}

void ALODActor::RecalculateDrawingDistance(const float InTransitionScreenSize)
{
	// At the moment this assumes a fixed field of view of 90 degrees (horizontal and vertical axes)
	static const float FOVRad = 90.0f * (float)PI / 360.0f;
	static const FMatrix ProjectionMatrix = FPerspectiveMatrix(FOVRad, 1920, 1080, 0.01f);
	FBoxSphereBounds Bounds = GetStaticMeshComponent()->CalcBounds(FTransform());
	for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
	{
		Bounds = Bounds + Component.Value->CalcBounds(FTransform());
	}

	float DrawDistance = ComputeBoundsDrawDistance(InTransitionScreenSize, Bounds.SphereRadius, ProjectionMatrix);
	SetDrawDistance(DrawDistance);

	UpdateSubActorLODParents();
}

#endif // WITH_EDITOR

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

bool IsImposter(const UStaticMeshComponent* InComponent)
{
	check(InComponent);

	if (!InComponent->bUseMaxLODAsImposter || !InComponent->bBatchImpostersAsInstances)
	{
		return false;
	}

	const UStaticMesh* StaticMesh = InComponent->GetStaticMesh();
	if (StaticMesh == nullptr)
	{
		return false;
	}

	if (StaticMesh->GetNumLODs() == 0)
	{
		return false;
	}

	const int32 LODIndex = StaticMesh->GetNumLODs() - 1;
	const FStaticMeshLODResources& StaticMeshLOD = StaticMesh->RenderData->LODResources[LODIndex];

	if (StaticMeshLOD.Sections.Num() != 1)
	{
		return false;
	}

	return true;
}

UMaterialInterface* ALODActor::GetImposterMaterial(const UStaticMeshComponent* InComponent) const
{
	UMaterialInterface* ImposterMaterial = nullptr;

	if (IsImposter(InComponent))
	{
		UStaticMesh* StaticMesh = InComponent->GetStaticMesh();
		check(StaticMesh);

		// Retrieve imposter LOD mesh and material
		const int32 LODIndex = StaticMesh->GetNumLODs() - 1;

		// Retrieve the sections, we're expect 1 for imposter meshes
		const TArray<FStaticMeshSection>& Sections = StaticMesh->RenderData->LODResources[LODIndex].Sections;
		if (Sections.Num() == 1)
		{
			// Retrieve material for this section
			ImposterMaterial = InComponent->GetMaterial(Sections[0].MaterialIndex);
		}
		else
		{
			UE_LOG(LogHLOD, Warning, TEXT("Imposter's static mesh %s has multiple mesh sections for its lowest LOD"), *StaticMesh->GetName());
		}
	}

	return ImposterMaterial;
}

UMaterialInterface* ALODActor::GetImposterMaterial(const AActor* InActor) const
{
	UMaterialInterface* ImposterMaterial = nullptr;

	TArray<UStaticMeshComponent*> Components;
	InActor->GetComponents<UStaticMeshComponent>(Components);
	Components.RemoveAll([&](UStaticMeshComponent* Val)
	{
#if WITH_EDITOR
		return Val->GetStaticMesh() == nullptr || !Val->ShouldGenerateAutoLOD(LODLevel - 1);
#else
		return Val->GetStaticMesh() == nullptr;
#endif
	});

	if (Components.Num() == 1)
	{
		ImposterMaterial = GetImposterMaterial(Components[0]);
	}

	return ImposterMaterial;
}

UInstancedStaticMeshComponent* ALODActor::GetLODComponentForMaterial(const UMaterialInterface* InMaterial) const
{
	return ImpostersStaticMeshComponents.FindRef(InMaterial);
}

UInstancedStaticMeshComponent* ALODActor::GetOrCreateLODComponentForMaterial(const UMaterialInterface* InMaterial)
{
	UInstancedStaticMeshComponent* LODComponent = ImpostersStaticMeshComponents.FindRef(InMaterial);
	if (LODComponent == nullptr)
	{
		LODComponent = NewObject<UInstancedStaticMeshComponent>(this);
		SetupComponent(LODComponent);

		LODComponent->SetupAttachment(GetRootComponent());

		if (StaticMeshComponent->IsRegistered())
		{
			LODComponent->RegisterComponent();
		}
		else
		{
			LODComponent->bAutoRegister = StaticMeshComponent->bAutoRegister;
		}

		ImpostersStaticMeshComponents.Emplace(InMaterial, LODComponent);
	}

	check(LODComponent);
	return LODComponent;
}

UStaticMeshComponent* ALODActor::GetLODComponentForActor(const AActor* InActor, bool bInFallbackToDefault) const
{
	UStaticMeshComponent* LODComponent = StaticMeshComponent;

	if (!InActor->IsA<ALODActor>())
	{
		UMaterialInterface* ImposterMaterial = GetImposterMaterial(InActor);
		if (ImposterMaterial != nullptr)
		{
			LODComponent = GetLODComponentForMaterial(ImposterMaterial);
			if (LODComponent == nullptr && bInFallbackToDefault)
			{
				// Needs to be rebuilt... fallback to default component
				LODComponent = StaticMeshComponent;
			}
		}
	}

	return LODComponent;
}

UStaticMeshComponent* ALODActor::GetOrCreateLODComponentForActor(const AActor* InActor)
{
	UStaticMeshComponent* LODComponent = StaticMeshComponent;

	if (!InActor->IsA<ALODActor>())
	{
		UMaterialInterface* ImposterMaterial = GetImposterMaterial(InActor);
		if (ImposterMaterial != nullptr)
		{
			LODComponent = GetOrCreateLODComponentForMaterial(ImposterMaterial);
		}
	}

	check(LODComponent != nullptr);
	return LODComponent;
}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

FBox ALODActor::GetComponentsBoundingBox(bool bNonColliding) const 
{
	FBox BoundBox = Super::GetComponentsBoundingBox(bNonColliding);

	// If BoundBox ends up to nothing create a new invalid one
	if (BoundBox.GetVolume() == 0.0f)
	{
		BoundBox = FBox(ForceInit);
	}

	if (bNonColliding)
	{
		bool bHasStaticMeshes = StaticMeshComponent && StaticMeshComponent->GetStaticMesh();

		if (!bHasStaticMeshes)
		{
			for (const TPair<const UMaterialInterface*, UInstancedStaticMeshComponent*>& Component : ImpostersStaticMeshComponents)
			{
				UStaticMesh* StaticMesh = Component.Value->GetStaticMesh();
				if (StaticMesh)
				{
					bHasStaticMeshes = true;
					break;
				}
			}
		}

		// No valid static meshes found, use sub actors bounds instead.
		if (!bHasStaticMeshes)
		{
			FBox SMBoundBox(ForceInit);
			for (AActor* Actor : SubActors)
			{
				if (Actor)
				{
					BoundBox += Actor->GetComponentsBoundingBox(bNonColliding);
				}
			}
		}
	}

	return BoundBox;	
}

void ALODActor::OnCVarsChanged()
{
	// Initialized to MIN_int32 to make sure that we run this once at startup regardless of the CVar value (assuming it is valid)
	static int32 CachedMaximumAllowedHLODLevel = MIN_int32;
	const int32 MaximumAllowedHLODLevel = GMaximumAllowedHLODLevel;

	if (MaximumAllowedHLODLevel != CachedMaximumAllowedHLODLevel)
	{
		CachedMaximumAllowedHLODLevel = MaximumAllowedHLODLevel;

		for (ALODActor* Actor : TObjectRange<ALODActor>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
		{
			Actor->UpdateRegistrationToMatchMaximumLODLevel();
		}
	}
	
	static TArray<float> CachedDistances = HLODDistances;
	ParseOverrideDistancesCVar();

	const bool bInvalidatedCachedValues = [&]() -> bool
	{
		for (int32 Index = 0; Index < CachedDistances.Num(); ++Index)
		{
			const float CachedDistance = CachedDistances[Index];
			if (HLODDistances.IsValidIndex(Index))
			{
				const float NewDistance = HLODDistances[Index];
				if (NewDistance != CachedDistance)
				{
					return true;
				}
			}
			else
			{
				return true;
			}
		}

		return CachedDistances.Num() != HLODDistances.Num();
	}();

	if (bInvalidatedCachedValues)
	{
		CachedDistances = HLODDistances;
		const int32 NumDistances = CachedDistances.Num();
		for (ALODActor* Actor : TObjectRange<ALODActor>(RF_ClassDefaultObject | RF_ArchetypeObject, true, EInternalObjectFlags::PendingKill))
		{
			Actor->UpdateOverrideTransitionDistance();
		}
	}
}


void ALODActor::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	if (Ar.GetPortFlags() & PPF_DuplicateForPIE && Ar.IsSaving())
	{
		Key = UHLODProxy::GenerateKeyForActor(this);
	}
#endif

	Super::Serialize(Ar);
#if WITH_EDITOR
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	bRequiresLODScreenSizeConversion = Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::LODsUseResolutionIndependentScreenSize;

	if (Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::CullDistanceRefactor_NeverCullALODActorsByDefault)
	{
		if (UStaticMeshComponent* SMComponent = GetStaticMeshComponent())
		{
			SMComponent->LDMaxDrawDistance = 0.f;
			SMComponent->bNeverDistanceCull = true;
		}
	}
#endif
}

#if WITH_EDITOR

void ALODActor::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);	

	if(!GIsCookerLoadingPackage)
	{
		// Always rebuild key on save here. We dont do this while cooking as keys rely on platform derived data which is context-dependent during cook
		Key = UHLODProxy::GenerateKeyForActor(this);
	}

	// check & warn if we need building
	if(!IsBuilt(true))
	{
		UE_LOG(LogHLOD, Log, TEXT("HLOD actor %s in map %s is not built. Meshes may not match."), *GetName(), *GetOutermost()->GetName());
	}
}


#endif	// #if WITH_EDITOR

#undef LOCTEXT_NAMESPACE
