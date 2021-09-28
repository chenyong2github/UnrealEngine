// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZoneGraphAnnotationComponent.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphDelegates.h"
#include "Debug/DebugDrawService.h"

#if WITH_EDITOR
#include "Editor.h"
#endif


#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
//////////////////////////////////////////////////////////////////////////
// FZoneGraphAnnotationSceneProxy

FZoneGraphAnnotationSceneProxy::FZoneGraphAnnotationSceneProxy(const UPrimitiveComponent& InComponent, const EDrawType InDrawType)
	: FDebugRenderSceneProxy(&InComponent)
{
	DrawType = InDrawType;
	ViewFlagName = TEXT("ZoneGraph");
	ViewFlagIndex = uint32(FEngineShowFlags::FindIndexByName(*ViewFlagName));
}

SIZE_T FZoneGraphAnnotationSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FPrimitiveViewRelevance FZoneGraphAnnotationSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && ViewFlagIndex != INDEX_NONE && View->Family->EngineShowFlags.GetSingleFlag(ViewFlagIndex);
	Result.bDynamicRelevance = true;
	// ideally the TranslucencyRelevance should be filled out by the material, here we do it conservative
	Result.bSeparateTranslucency = Result.bNormalTranslucency = true;
	return Result;
}

uint32 FZoneGraphAnnotationSceneProxy::GetMemoryFootprint(void) const
{
	return sizeof(*this) + FDebugRenderSceneProxy::GetAllocatedSize();
}

#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST


//////////////////////////////////////////////////////////////////////////
// UZoneGraphAnnotationComponent
UZoneGraphAnnotationComponent::UZoneGraphAnnotationComponent(const FObjectInitializer& ObjectInitializer)
	: UPrimitiveComponent(ObjectInitializer)
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);

#if WITH_EDITORONLY_DATA
	HitProxyPriority = HPP_Wireframe;
#endif
}

#if WITH_EDITOR
void UZoneGraphAnnotationComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	// Trigger tag registering when properties change.
	UWorld* World = GetWorld();
    if (World == nullptr)
    {
    	return;
    }

	if (UZoneGraphAnnotationSubsystem* ZoneGraphAnnotation = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(World))
	{
		ZoneGraphAnnotation->ReregisterTagsInEditor();
	}
}
#endif

void UZoneGraphAnnotationComponent::OnRegister()
{
	Super::OnRegister();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	UWorld* World = GetWorld();

#if WITH_EDITOR
	// Do not process any component registered to preview world
	if (World && World->WorldType == EWorldType::EditorPreview)
	{
		return;
	}
#endif

	if (UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(World))
	{
		PostSubsystemsInitialized();
	}
	else
	{
		OnPostWorldInitDelegateHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UZoneGraphAnnotationComponent::OnPostWorldInit);
	}

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	CanvasDebugDrawDelegateHandle = UDebugDrawService::Register(TEXT("ZoneGraph"), FDebugDrawDelegate::CreateUObject(this, &UZoneGraphAnnotationComponent::DebugDrawCanvas));
#endif
}

void UZoneGraphAnnotationComponent::OnPostWorldInit(UWorld* World, const UWorld::InitializationValues)
{
	if (World == GetWorld())
	{
		PostSubsystemsInitialized();
	}

	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitDelegateHandle);
}

void UZoneGraphAnnotationComponent::PostSubsystemsInitialized()
{
	UWorld* World = GetWorld();
	check(World);

	if (UZoneGraphAnnotationSubsystem* ZoneGraphAnnotation = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(World))
	{
		ZoneGraphAnnotation->RegisterAnnotationComponent(*this);
	}

	// Add the zonegraph data that already exists in the system
	if (const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(World))
	{
		for (const FRegisteredZoneGraphData& Registered : ZoneGraph->GetRegisteredZoneGraphData())
		{
			if (Registered.bInUse && Registered.ZoneGraphData)
			{
				PostZoneGraphDataAdded(*Registered.ZoneGraphData);
			}
		}

		OnPostZoneGraphDataAddedHandle = UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.AddUObject(this, &UZoneGraphAnnotationComponent::OnPostZoneGraphDataAdded);
		OnPreZoneGraphDataRemovedHandle = UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.AddUObject(this, &UZoneGraphAnnotationComponent::OnPreZoneGraphDataRemoved);
	}
}

void UZoneGraphAnnotationComponent::OnUnregister()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

#if WITH_EDITOR
	// Do not process any component registered to preview world
	if (World->WorldType == EWorldType::EditorPreview)
	{
		return;
	}
#endif

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if (UZoneGraphAnnotationSubsystem* ZoneGraphAnnotation = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(World))
	{
		ZoneGraphAnnotation->UnregisterAnnotationComponent(*this);
	}

	UE::ZoneGraphDelegates::OnPostZoneGraphDataAdded.Remove(OnPostZoneGraphDataAddedHandle);
	UE::ZoneGraphDelegates::OnPreZoneGraphDataRemoved.Remove(OnPreZoneGraphDataRemovedHandle);

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	UDebugDrawService::Unregister(CanvasDebugDrawDelegateHandle);
#endif
	
	Super::OnUnregister();
}

void UZoneGraphAnnotationComponent::OnPostZoneGraphDataAdded(const AZoneGraphData* ZoneGraphData)
{
	// Only consider valid graph from our world
	if (ZoneGraphData == nullptr || ZoneGraphData->GetWorld() != GetWorld())
	{
		return;
	}

	PostZoneGraphDataAdded(*ZoneGraphData);
}

void UZoneGraphAnnotationComponent::OnPreZoneGraphDataRemoved(const AZoneGraphData* ZoneGraphData)
{
	// Only consider valid graph from our world
	if (ZoneGraphData == nullptr || ZoneGraphData->GetWorld() != GetWorld())
	{
		return;
	}

	PreZoneGraphDataRemoved(*ZoneGraphData);
}

void UZoneGraphAnnotationComponent::DestroyRenderState_Concurrent()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	DebugDrawDelegateHelper.UnregisterDebugDrawDelegate();
#endif

	Super::DestroyRenderState_Concurrent();
}

FPrimitiveSceneProxy* UZoneGraphAnnotationComponent::CreateSceneProxy()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	
	FZoneGraphAnnotationSceneProxy* DebugProxy = new FZoneGraphAnnotationSceneProxy(*this);

	if(bEnableDebugDrawing)
	{
		DebugDraw(DebugProxy);
	}

	DebugDrawDelegateHelper.InitDelegateHelper(DebugProxy);
	DebugDrawDelegateHelper.RegisterDebugDrawDelegate();
	
	return DebugProxy;

#else
	return nullptr;
#endif //!UE_BUILD_SHIPPING && !UE_BUILD_TEST
}

FBoxSphereBounds UZoneGraphAnnotationComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BoundingBox(ForceInit);

	if (const UZoneGraphSubsystem* ZoneGraph = UWorld::GetSubsystem<UZoneGraphSubsystem>(GetWorld()))
	{
		BoundingBox = ZoneGraph->GetCombinedBounds();
	}
	
	return FBoxSphereBounds(BoundingBox);
}
