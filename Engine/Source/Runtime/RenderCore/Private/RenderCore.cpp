// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderCore.h: Render core module implementation.
=============================================================================*/

#include "RenderCore.h"
#include "HAL/IConsoleManager.h"
#include "UniformBuffer.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, RenderCore);

DEFINE_LOG_CATEGORY(LogRendererCore);

/*------------------------------------------------------------------------------
	Stat declarations.
-----------------------------------------------------------------------------*/
// Cycle stats are rendered in reverse order from what they are declared in.
// They are organized so that stats at the top of the screen are earlier in the frame, 
// And stats that are indented are lower in the call hierarchy.

// The purpose of the SceneRendering stat group is to show where rendering thread time is going from a high level.
// It should only contain stats that are likely to track a lot of time in a typical scene, not edge case stats that are rarely non-zero.


// Amount of time measured by 'RenderViewFamily' that is not accounted for in its children stats
// Use a more detailed profiler (like an instruction trace or sampling capture on Xbox 360) to track down where this time is going if needed
DEFINE_STAT(STAT_RenderVelocities);
DEFINE_STAT(STAT_FinishRenderViewTargetTime);
DEFINE_STAT(STAT_CacheUniformExpressions);
DEFINE_STAT(STAT_TranslucencyDrawTime);
DEFINE_STAT(STAT_BeginOcclusionTestsTime);
// Use 'stat shadowrendering' to get more detail
DEFINE_STAT(STAT_ProjectedShadowDrawTime);
DEFINE_STAT(STAT_LightingDrawTime);
DEFINE_STAT(STAT_DynamicPrimitiveDrawTime);
DEFINE_STAT(STAT_StaticDrawListDrawTime);
DEFINE_STAT(STAT_BasePassDrawTime);
DEFINE_STAT(STAT_DepthDrawTime);
DEFINE_STAT(STAT_DynamicShadowSetupTime);
DEFINE_STAT(STAT_RenderQueryResultTime);
// Use 'stat initviews' to get more detail
DEFINE_STAT(STAT_InitViewsTime);
DEFINE_STAT(STAT_InitViewsPossiblyAfterPrepass);
// Measures the time spent in RenderViewFamily_RenderThread
// Note that this is not the total rendering thread time, any other rendering commands will not be counted here
DEFINE_STAT(STAT_TotalSceneRenderingTime);
DEFINE_STAT(STAT_TotalGPUFrameTime);
DEFINE_STAT(STAT_PresentTime);

DEFINE_STAT(STAT_SceneLights);
DEFINE_STAT(STAT_MeshDrawCalls);
DEFINE_STAT(STAT_DynamicPathMeshDrawCalls);
DEFINE_STAT(STAT_StaticDrawListMeshDrawCalls);

DEFINE_STAT(STAT_SceneDecals);
DEFINE_STAT(STAT_Decals);
DEFINE_STAT(STAT_DecalsDrawTime);

// Memory stats for tracking virtual allocations used by the renderer to represent the scene
// The purpose of these memory stats is to capture where most of the renderer allocated memory is going, 
// Not to track all of the allocations, and not to track resource memory (index buffers, vertex buffers, etc).


DEFINE_STAT(STAT_StaticDrawListMemory);
DEFINE_STAT(STAT_PrimitiveInfoMemory);
DEFINE_STAT(STAT_RenderingSceneMemory);
DEFINE_STAT(STAT_ViewStateMemory);
DEFINE_STAT(STAT_RenderingMemStackMemory);
DEFINE_STAT(STAT_LightInteractionMemory);

// The InitViews stats group contains information on how long visibility culling took and how effective it was

DEFINE_STAT(STAT_GatherShadowPrimitivesTime);
DEFINE_STAT(STAT_BuildCSMVisibilityState);
DEFINE_STAT(STAT_UpdateIndirectLightingCache);
DEFINE_STAT(STAT_UpdateIndirectLightingCachePrims);
DEFINE_STAT(STAT_UpdateIndirectLightingCacheBlocks);
DEFINE_STAT(STAT_InterpolateVolumetricLightmapOnCPU);
DEFINE_STAT(STAT_UpdateIndirectLightingCacheTransitions);
DEFINE_STAT(STAT_UpdateIndirectLightingCacheFinalize);
DEFINE_STAT(STAT_SortStaticDrawLists);
DEFINE_STAT(STAT_InitDynamicShadowsTime);
DEFINE_STAT(STAT_InitProjectedShadowVisibility);
DEFINE_STAT(STAT_UpdatePreshadowCache);
DEFINE_STAT(STAT_CreateWholeSceneProjectedShadow);
DEFINE_STAT(STAT_AddViewDependentWholeSceneShadowsForView);
DEFINE_STAT(STAT_SetupInteractionShadows);
DEFINE_STAT(STAT_GetDynamicMeshElements);
DEFINE_STAT(STAT_UpdateStaticMeshesTime);
DEFINE_STAT(STAT_StaticRelevance);
DEFINE_STAT(STAT_ViewRelevance);
DEFINE_STAT(STAT_ComputeViewRelevance);
DEFINE_STAT(STAT_OcclusionCull);
DEFINE_STAT(STAT_SoftwareOcclusionCull);
DEFINE_STAT(STAT_UpdatePrimitiveFading);
DEFINE_STAT(STAT_FrustumCull);
DEFINE_STAT(STAT_DecompressPrecomputedOcclusion);
DEFINE_STAT(STAT_ViewVisibilityTime);

DEFINE_STAT(STAT_ProcessedPrimitives);
DEFINE_STAT(STAT_CulledPrimitives);
DEFINE_STAT(STAT_StaticallyOccludedPrimitives);
DEFINE_STAT(STAT_OccludedPrimitives);
DEFINE_STAT(STAT_OcclusionQueries);
DEFINE_STAT(STAT_VisibleStaticMeshElements);
DEFINE_STAT(STAT_VisibleDynamicPrimitives);
DEFINE_STAT(STAT_IndirectLightingCacheUpdates);
DEFINE_STAT(STAT_PrecomputedLightingBufferUpdates);
DEFINE_STAT(STAT_CSMSubjects);
DEFINE_STAT(STAT_CSMStaticMeshReceivers);
DEFINE_STAT(STAT_CSMStaticPrimitiveReceivers);

// The ShadowRendering stats group shows what kind of shadows are taking a lot of rendering thread time to render
// Shadow setup is tracked in the InitViews group

DEFINE_STAT(STAT_RenderWholeSceneShadowProjectionsTime);
DEFINE_STAT(STAT_WholeSceneDynamicShadowDepthsTime);
DEFINE_STAT(STAT_WholeSceneStaticShadowDepthsTime);
DEFINE_STAT(STAT_WholeSceneStaticDrawListShadowDepthsTime);
DEFINE_STAT(STAT_RenderWholeSceneShadowDepthsTime);
DEFINE_STAT(STAT_RenderPerObjectShadowProjectionsTime);
DEFINE_STAT(STAT_RenderPerObjectShadowDepthsTime);

DEFINE_STAT(STAT_WholeSceneShadows);
DEFINE_STAT(STAT_CachedWholeSceneShadows);
DEFINE_STAT(STAT_PerObjectShadows);
DEFINE_STAT(STAT_PreShadows);
DEFINE_STAT(STAT_CachedPreShadows);
DEFINE_STAT(STAT_ShadowDynamicPathDrawCalls);

DEFINE_STAT(STAT_TranslucentInjectTime);
DEFINE_STAT(STAT_DirectLightRenderingTime);
DEFINE_STAT(STAT_LightRendering);

DEFINE_STAT(STAT_NumShadowedLights);
DEFINE_STAT(STAT_NumLightFunctionOnlyLights);
DEFINE_STAT(STAT_NumUnshadowedLights);
DEFINE_STAT(STAT_NumLightsInjectedIntoTranslucency);
DEFINE_STAT(STAT_NumLightsUsingTiledDeferred);
DEFINE_STAT(STAT_NumLightsUsingSimpleTiledDeferred);
DEFINE_STAT(STAT_NumLightsUsingStandardDeferred);

DEFINE_STAT(STAT_LightShaftsLights);

DEFINE_STAT(STAT_ParticleUpdateRTTime);
DEFINE_STAT(STAT_InfluenceWeightsUpdateRTTime);
DEFINE_STAT(STAT_GPUSkinUpdateRTTime);
DEFINE_STAT(STAT_CPUSkinUpdateRTTime);

DEFINE_STAT(STAT_RemoveSceneLightTime);
DEFINE_STAT(STAT_UpdateSceneLightTime);
DEFINE_STAT(STAT_AddSceneLightTime);

DEFINE_STAT(STAT_RemoveScenePrimitiveTime);
DEFINE_STAT(STAT_AddScenePrimitiveRenderThreadTime);
DEFINE_STAT(STAT_UpdatePrimitiveTransformRenderThreadTime);

DEFINE_STAT(STAT_RemoveScenePrimitiveGT);
DEFINE_STAT(STAT_AddScenePrimitiveGT);
DEFINE_STAT(STAT_UpdatePrimitiveTransformGT);

DEFINE_STAT(STAT_Scene_SetShaderMapsOnMaterialResources_RT);
DEFINE_STAT(STAT_Scene_UpdateStaticDrawListsForMaterials_RT);
DEFINE_STAT(STAT_GameToRendererMallocTotal);

DEFINE_STAT(STAT_UpdateLPVs);
DEFINE_STAT(STAT_ReflectiveShadowMaps);
DEFINE_STAT(STAT_ReflectiveShadowMapDrawTime);
DEFINE_STAT(STAT_NumReflectiveShadowMapLights);
DEFINE_STAT(STAT_RenderWholeSceneReflectiveShadowMapsTime);

DEFINE_STAT(STAT_ShadowmapAtlasMemory);
DEFINE_STAT(STAT_CachedShadowmapMemory);

DEFINE_STAT(STAT_RenderTargetPoolSize);
DEFINE_STAT(STAT_RenderTargetPoolUsed);
DEFINE_STAT(STAT_RenderTargetPoolCount);

#define EXPOSE_FORCE_LOD !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

#if EXPOSE_FORCE_LOD

static TAutoConsoleVariable<int32> CVarForceLOD(
	TEXT("r.ForceLOD"),
	-1,
	TEXT("LOD level to force, -1 is off."),
	ECVF_Scalability | ECVF_Default | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarForceLODShadow(
	TEXT("r.ForceLODShadow"),
	-1,
	TEXT("LOD level to force for the shadow map generation only, -1 is off."),
	ECVF_Scalability | ECVF_Default | ECVF_RenderThreadSafe
);

#endif // EXPOSE_FORCE_LOD

/** Whether to pause the global realtime clock for the rendering thread (read and write only on main thread). */
bool GPauseRenderingRealtimeClock;

/** Global realtime clock for the rendering thread. */
FTimer GRenderingRealtimeClock;

FInputLatencyTimer GInputLatencyTimer( 2.0f );

//
// FInputLatencyTimer implementation.
//

/** Potentially starts the timer on the gamethread, based on the UpdateFrequency. */
void FInputLatencyTimer::GameThreadTick()
{
#if STATS
	if (FThreadStats::IsCollectingData())
	{
		if ( !bInitialized )
		{
			LastCaptureTime	= FPlatformTime::Seconds();
			bInitialized	= true;
		}
		float CurrentTimeInSeconds = FPlatformTime::Seconds();
		if ( (CurrentTimeInSeconds - LastCaptureTime) > UpdateFrequency )
		{
			LastCaptureTime		= CurrentTimeInSeconds;
			StartTime			= FPlatformTime::Cycles();
			GameThreadTrigger	= true;
		}
	}
#endif
}

TLinkedList<FUniformBufferStruct*>*& FUniformBufferStruct::GetStructList()
{
	static TLinkedList<FUniformBufferStruct*>* GUniformStructList = NULL;
	return GUniformStructList;
}

TMap<FName, FUniformBufferStruct*>& FUniformBufferStruct::GetNameStructMap()
{
	static 	TMap<FName, FUniformBufferStruct*> GlobalNameStructMap;
	return GlobalNameStructMap;
}


FUniformBufferStruct* FindUniformBufferStructByName(const TCHAR* StructName)
{
	FName FindByName(StructName, FNAME_Find);
	FUniformBufferStruct* FoundStruct = FUniformBufferStruct::GetNameStructMap().FindRef(FindByName);
	return FoundStruct;
}

FUniformBufferStruct* FindUniformBufferStructByFName(FName StructName)
{
	return FUniformBufferStruct::GetNameStructMap().FindRef(StructName);
}

// Can be optimized to avoid the virtual function call but it's compiled out for final release anyway
RENDERCORE_API int32 GetCVarForceLOD()
{
	int32 Ret = -1;

#if EXPOSE_FORCE_LOD
	{
		Ret = CVarForceLOD.GetValueOnRenderThread();
	}
#endif // EXPOSE_FORCE_LOD

	return Ret;
}

RENDERCORE_API int32 GetCVarForceLODShadow()
{
	int32 Ret = -1;

#if EXPOSE_FORCE_LOD
	{
		Ret = CVarForceLODShadow.GetValueOnRenderThread();
	}
#endif // EXPOSE_FORCE_LOD

	return Ret;
}

RENDERCORE_API bool IsHDREnabled()
{
	static const auto CVarHDROutputEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.EnableHDROutput"));
	if (CVarHDROutputEnabled)
	{
		if (CVarHDROutputEnabled->GetValueOnAnyThread() != 0)
		{
			return true;
		}
	}
	return false;
}

RENDERCORE_API bool IsHDRAllowed()
{
	// HDR can be forced on or off on the commandline. Otherwise we check the cvar r.AllowHDR
	if (FParse::Param(FCommandLine::Get(), TEXT("hdr")))
	{
		return true;
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("nohdr")))
	{
		return false;
	}

	static const auto CVarHDRAllow = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowHDR"));
	if (CVarHDRAllow && CVarHDRAllow->GetValueOnAnyThread() != 0)
	{
		return true;
	}

	return false;
}

class FUniformBufferMemberAndOffset
{
public:
	FUniformBufferMemberAndOffset(const FUniformBufferStruct::FMember& InMember, int32 InStructOffset) :
		Member(InMember),
		StructOffset(InStructOffset)
	{}

	FUniformBufferStruct::FMember Member;
	int32 StructOffset;
};

FUniformBufferStruct::FUniformBufferStruct(const FName& InLayoutName, const TCHAR* InStructTypeName, const TCHAR* InShaderVariableName, ConstructUniformBufferParameterType InConstructRef, uint32 InSize, const TArray<FMember>& InMembers, bool bRegisterForAutoBinding)
:	StructTypeName(InStructTypeName)
,	ShaderVariableName(InShaderVariableName)
,	ConstructUniformBufferParameterRef(InConstructRef)
,	Size(InSize)
,	bLayoutInitialized(false)
,	Layout(InLayoutName)
,	Members(InMembers)
,	GlobalListLink(this)
{
	if (bRegisterForAutoBinding)
	{
		GlobalListLink.LinkHead(GetStructList());
		FName StrutTypeFName(StructTypeName);
		// Verify that during FName creation there's no case conversion
		checkSlow(FCString::Strcmp(StructTypeName, *StrutTypeFName.GetPlainNameString()) == 0);
		GetNameStructMap().Add(FName(StructTypeName), this);
	}
	else
	{
		// We cannot initialize the layout during global initialization, since we have to walk nested struct members.
		// Structs created during global initialization will have bRegisterForAutoBinding==false, and are initialized during startup.
		// Structs created at runtime with bRegisterForAutoBinding==true can be initialized now.
		InitializeLayout();
	}
}

void FUniformBufferStruct::InitializeStructs()
{
	for (TLinkedList<FUniformBufferStruct*>::TIterator StructIt(FUniformBufferStruct::GetStructList()); StructIt; StructIt.Next())
	{
		StructIt->InitializeLayout();
	}
}

void FUniformBufferStruct::InitializeLayout()
{
	check(!bLayoutInitialized);
	Layout.ConstantBufferSize = Size;

	TArray<FUniformBufferMemberAndOffset> MemberStack;
	MemberStack.Reserve(Members.Num());

	for (int32 MemberIndex = 0; MemberIndex < Members.Num(); MemberIndex++)
	{
		MemberStack.Push(FUniformBufferMemberAndOffset(Members[MemberIndex], 0));
	}

	for (int32 i = 0; i < MemberStack.Num(); ++i)
	{
		const FMember& CurrentMember = MemberStack[i].Member;
		bool bIsResource = IsUniformBufferResourceType(CurrentMember.GetBaseType());

		if (bIsResource)
		{
			Layout.Resources.Add(CurrentMember.GetBaseType());
			const uint32 AbsoluteMemberOffset = CurrentMember.GetOffset() + MemberStack[i].StructOffset;
			check(AbsoluteMemberOffset < (1u << (Layout.ResourceOffsets.GetTypeSize() * 8)));
			Layout.ResourceOffsets.Add(AbsoluteMemberOffset);
		}

		const FUniformBufferStruct* MemberStruct = CurrentMember.GetStruct();

		if (MemberStruct)
		{
			int32 AbsoluteStructOffset = CurrentMember.GetOffset() + MemberStack[i].StructOffset;

			for (int32 StructMemberIndex = 0; StructMemberIndex < MemberStruct->Members.Num(); StructMemberIndex++)
			{
				FMember StructMember = MemberStruct->Members[StructMemberIndex];
				MemberStack.Insert(FUniformBufferMemberAndOffset(StructMember, AbsoluteStructOffset), i + 1 + StructMemberIndex);
			}
		}
	}

	Layout.ComputeHash();

	bLayoutInitialized = true;
}

void FUniformBufferStruct::GetNestedStructs(TArray<const FUniformBufferStruct*>& OutNestedStructs) const
{
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const FMember& CurrentMember = Members[i];

		const FUniformBufferStruct* MemberStruct = CurrentMember.GetStruct();

		if (MemberStruct)
		{
			OutNestedStructs.Add(MemberStruct);
			MemberStruct->GetNestedStructs(OutNestedStructs);
		}
	}
}

void FUniformBufferStruct::AddResourceTableEntries(TMap<FString,FResourceTableEntry>& ResourceTableMap, TMap<FString,uint32>& ResourceTableLayoutHashes) const
{
	uint16 ResourceIndex = 0;
	FString Prefix = FString::Printf(TEXT("%s_"), ShaderVariableName);
	AddResourceTableEntriesRecursive(ShaderVariableName, *Prefix, ResourceIndex, ResourceTableMap);
	ResourceTableLayoutHashes.Add(ShaderVariableName,GetLayout().GetHash());
}

void FUniformBufferStruct::AddResourceTableEntriesRecursive(const TCHAR* UniformBufferName, const TCHAR* Prefix, uint16& ResourceIndex, TMap<FString, FResourceTableEntry>& ResourceTableMap) const
{
	for (int32 MemberIndex = 0; MemberIndex < Members.Num(); ++MemberIndex)
	{
		const FMember& Member = Members[MemberIndex];
		if (IsUniformBufferResourceType(Member.GetBaseType()))
		{
			FResourceTableEntry& Entry = ResourceTableMap.FindOrAdd(FString::Printf(TEXT("%s%s"), Prefix, Member.GetName()));
			if (Entry.UniformBufferName.IsEmpty())
			{
				Entry.UniformBufferName = UniformBufferName;
				Entry.Type = Member.GetBaseType();
				Entry.ResourceIndex = ResourceIndex++;
			}
		}
		else if (Member.GetBaseType() == UBMT_STRUCT)
		{
			check(Member.GetStruct());
			FString MemberPrefix = FString::Printf(TEXT("%s%s_"), Prefix, Member.GetName());
			Member.GetStruct()->AddResourceTableEntriesRecursive(UniformBufferName, *MemberPrefix, ResourceIndex, ResourceTableMap);
		}
	}
}