// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudLODManager.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudComponent.h"
#include "LidarPointCloudOctree.h"
#include "LidarPointCloudSettings.h"
#include "Rendering/LidarPointCloudRenderBuffers.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "Classes/EditorStyleSettings.h"
#include "EditorViewportClient.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Node Selection"), STAT_NodeSelection, STATGROUP_LidarPointCloud)
DECLARE_CYCLE_STAT(TEXT("Node Processing"), STAT_NodeProcessing, STATGROUP_LidarPointCloud)
DECLARE_CYCLE_STAT(TEXT("Render Data Update"), STAT_UpdateRenderData, STATGROUP_LidarPointCloud)

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Total Point Count [thousands]"), STAT_PointCountTotal, STATGROUP_LidarPointCloud)
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Points In Frustum"), STAT_PointCountFrustum, STATGROUP_LidarPointCloud)
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Point Budget"), STAT_PointBudget, STATGROUP_LidarPointCloud)
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Visible Points"), STAT_PointCount, STATGROUP_LidarPointCloud)

static TAutoConsoleVariable<int32> CVarLidarPointBudget(
	TEXT("r.LidarPointBudget"),
	0,
	TEXT("If set to > 0, this will overwrite the Target FPS setting, and apply a fixed budget.\n")
	TEXT("Determines the maximum number of points to be visible on the screen.\n")
	TEXT("Higher values will produce better image quality, but will require faster hardware."),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarLidarScreenCenterImportance(
	TEXT("r.LidarScreenCenterImportance"),
	0.0f,
	TEXT("Determines the preference towards selecting nodes closer to screen center\n")
	TEXT("with larger values giving more priority towards screen center.\n")
	TEXT("Usefulf for VR, where edge vision is blurred anyway.\n")
	TEXT("0 to disable."),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarBaseLODImportance(
	TEXT("r.LidarBaseLODImportance"),
	0.1f,
	TEXT("Determines the importance of selecting at least the base LOD of far assets.\n")
	TEXT("Increase it, if you're experiencing actor 'popping'.\n")
	TEXT("0 to use purely screensize-driven algorithm."),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarTargetFPS(
	TEXT("r.LidarTargetFPS"),
	59.0f,
	TEXT("The LOD system will continually adjust the quality of the assets to maintain\n")
	TEXT("the specified target FPS."),
	ECVF_Scalability);

static TAutoConsoleVariable<bool> CVarLidarIncrementalBudget(
	TEXT("r.LidarIncrementalBudget"),
	false,
	TEXT("If enabled, the point budget will automatically increase whenever the\n")
	TEXT("camera's location and orientation remain unchanged."),
	ECVF_Scalability);

FLidarPointCloudViewData::FLidarPointCloudViewData(bool bCompute)
	: bValid(false)
	, ViewOrigin(FVector::ZeroVector)
	, ViewDirection(FVector::ForwardVector)
	, ScreenSizeFactor(0)
	, bSkipMinScreenSize(false)
	, bPIE(false)
	, bHasFocus(false)
{
	if (bCompute)
	{
		Compute();
	}
}

void FLidarPointCloudViewData::Compute()
{
	// Attempt to get the first local player's viewport
	if (GEngine)
	{
		ULocalPlayer* const LP = GEngine->FindFirstLocalPlayerFromControllerId(0);
		if (LP && LP->ViewportClient)
		{
			FSceneViewProjectionData ProjectionData;
			if (LP->GetProjectionData(LP->ViewportClient->Viewport, eSSP_FULL, ProjectionData))
			{
				ViewOrigin = ProjectionData.ViewOrigin;
				FMatrix ViewRotationMatrix = ProjectionData.ViewRotationMatrix;
				if (!ViewRotationMatrix.GetOrigin().IsNearlyZero(0.0f))
				{
					ViewOrigin += ViewRotationMatrix.InverseTransformPosition(FVector::ZeroVector);
					ViewRotationMatrix = ViewRotationMatrix.RemoveTranslation();
				}

				FMatrix ViewMatrix = FTranslationMatrix(-ViewOrigin) * ViewRotationMatrix;
				ViewDirection = ViewMatrix.GetColumn(2);
				FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(ProjectionData.ProjectionMatrix);

				ScreenSizeFactor = FMath::Square(FMath::Max(0.5f * ProjectionMatrix.M[0][0], 0.5f * ProjectionMatrix.M[1][1]));

				// Skip SS check, if not in the projection view nor game world
				bSkipMinScreenSize = (ProjectionMatrix.M[3][3] >= 1.0f) && !LP->GetWorld()->IsGameWorld();
				GetViewFrustumBounds(ViewFrustum, ViewMatrix * ProjectionMatrix, false);

				bHasFocus = LP->ViewportClient->Viewport->HasFocus();

				bValid = true;
			}
		}
	}

#if WITH_EDITOR
	bPIE = false;
	if (GIsEditor && GEditor && GEditor->GetActiveViewport())
	{
		bPIE = GEditor->GetActiveViewport() == GEditor->GetPIEViewport();
		
		// PIE needs a different computation method
		if (!bValid && !bPIE)
		{
			ComputeFromEditorViewportClient(GEditor->GetActiveViewport()->GetClient());
		}

		// Simulating counts as PIE for the purpose of LOD calculation
		bPIE |= GEditor->bIsSimulatingInEditor;
	}
#endif
}

bool FLidarPointCloudViewData::ComputeFromEditorViewportClient(FViewportClient* ViewportClient)
{
#if WITH_EDITOR
	if (FEditorViewportClient* Client = (FEditorViewportClient*)ViewportClient)
	{
		if (Client->Viewport && Client->Viewport->GetSizeXY() != FIntPoint::ZeroValue)
		{
			FSceneViewFamily::ConstructionValues CVS(nullptr, nullptr, FEngineShowFlags(EShowFlagInitMode::ESFIM_Game));
			CVS.SetWorldTimes(0, 0, 0);
			FSceneViewFamily ViewFamily(CVS);
			FSceneView* View = Client->CalcSceneView(&ViewFamily);

			const FMatrix& ProjectionMatrix = View->ViewMatrices.GetProjectionMatrix();
			ScreenSizeFactor = FMath::Square(FMath::Max(0.5f * ProjectionMatrix.M[0][0], 0.5f * ProjectionMatrix.M[1][1]));
			ViewOrigin = View->ViewMatrices.GetViewOrigin();
			ViewDirection = View->GetViewDirection();
			ViewFrustum = View->ViewFrustum;
			bSkipMinScreenSize = !View->bIsGameView && !View->IsPerspectiveProjection();
			bHasFocus = Client->Viewport->HasFocus();

			bValid = true;

			return true;
		}
	}
#endif
	return false;
}

void FLidarPointCloudTraversalOctree::GetVisibleNodes(TArray<FLidarPointCloudLODManager::FNodeSizeData>& NodeSizeData, const FLidarPointCloudViewData& ViewData, const int32& ProxyIndex, const FLidarPointCloudNodeSelectionParams& SelectionParams, const float& CurrentTime)
{
	// Skip processing, if the asset is not visible
	if (!ViewData.ViewFrustum.IntersectBox(GetCenter(), GetExtent()))
	{
		return;
	}

	float MinScreenSizeSq = SelectionParams.MinScreenSize * SelectionParams.MinScreenSize;
	float BoundsScaleSq = SelectionParams.BoundsScale * SelectionParams.BoundsScale;

	const float BaseLODImportance = FMath::Max(0.0f, CVarBaseLODImportance.GetValueOnAnyThread());

	bool bStartClipped = false;
	if (SelectionParams.ClippingVolumes)
	{
		for (const ALidarClippingVolume* Volume : *SelectionParams.ClippingVolumes)
		{
			if (Volume->Mode == ELidarClippingVolumeMode::ClipOutside)
			{
				bStartClipped = true;
				break;
			}
		}
	}

	TQueue<FLidarPointCloudTraversalOctreeNode*> Nodes;
	FLidarPointCloudTraversalOctreeNode* CurrentNode = nullptr;
	Nodes.Enqueue(&Root);
	while (Nodes.Dequeue(CurrentNode))
	{
		// Reset selection flag
		CurrentNode->bSelected = false;

		// Update number of visible points, if needed
		CurrentNode->DataNode->UpdateNumVisiblePoints();

		const FVector NodeExtent = Extents[CurrentNode->Depth] * SelectionParams.BoundsScale;

		bool bFullyContained = true;

		if ((CurrentNode->Depth == 0 || !CurrentNode->bFullyContained) && !ViewData.ViewFrustum.IntersectBox(CurrentNode->Center, NodeExtent, bFullyContained))
		{
			continue;
		}

		const FBox NodeBounds(CurrentNode->Center - NodeExtent, CurrentNode->Center + NodeExtent);

		// Check vs Clipping Volumes
		bool bClip = bStartClipped;
		if (SelectionParams.ClippingVolumes)
		{
			for (const ALidarClippingVolume* Volume : *SelectionParams.ClippingVolumes)
			{
				if (Volume->Mode == ELidarClippingVolumeMode::ClipOutside)
				{
					if (Volume->GetBounds().GetBox().Intersect(NodeBounds))
					{
						bClip = false;
					}
				}
				else
				{
					if (Volume->GetBounds().GetBox().IsInside(NodeBounds))
					{
						bClip = true;
					}
				}
			}

			if (bClip)
			{
				continue;
			}
		}

		// Only process this node if it has any visible points - do not use continue; as the children may still contain visible points!
		if (CurrentNode->DataNode->GetNumVisiblePoints() > 0 && CurrentNode->Depth >= SelectionParams.MinDepth)
		{
			float ScreenSizeSq = 0;

			FVector VectorToNode = CurrentNode->Center - ViewData.ViewOrigin;
			const float DistSq = VectorToNode.SizeSquared();
			const float AdjustedRadiusSq = RadiiSq[CurrentNode->Depth] * BoundsScaleSq;

			// Make sure to show at least the minimum depth for each visible asset
			if (CurrentNode->Depth == SelectionParams.MinDepth)
			{
				// Add screen size to maintain hierarchy
				ScreenSizeSq = BaseLODImportance + ViewData.ScreenSizeFactor * AdjustedRadiusSq / FMath::Max(1.0f, DistSq);
			}
			else
			{
				// If the camera is within this node's bounds, it should always be qualified for rendering
				if (DistSq <= AdjustedRadiusSq)
				{
					// Subtract Depth to maintain hierarchy 
					ScreenSizeSq = 1000 - CurrentNode->Depth;
				}
				else
				{
					ScreenSizeSq = ViewData.ScreenSizeFactor * AdjustedRadiusSq / FMath::Max(1.0f, DistSq);

					// Check for minimum screen size
					if (!ViewData.bSkipMinScreenSize && ScreenSizeSq < MinScreenSizeSq)
					{
						continue;
					}

					// Add optional preferential selection for nodes closer to the screen center
					if (SelectionParams.ScreenCenterImportance > 0)
					{
						VectorToNode.Normalize();
						float Dot = FVector::DotProduct(ViewData.ViewDirection, VectorToNode);

						ScreenSizeSq = FMath::Lerp(ScreenSizeSq, ScreenSizeSq * Dot, SelectionParams.ScreenCenterImportance);
					}
				}
			}

			NodeSizeData.Emplace(CurrentNode, ScreenSizeSq, ProxyIndex);
		}

		if (SelectionParams.MaxDepth < 0 || CurrentNode->Depth < SelectionParams.MaxDepth)
		{
			for (FLidarPointCloudTraversalOctreeNode& Child : CurrentNode->Children)
			{
				Child.bFullyContained = bFullyContained;
				Nodes.Enqueue(&Child);
			}
		}
	}
}

/** Calculates the correct point budget to use for current frame */
uint32 GetPointBudget(float DeltaTime, int64 NumPointsInFrustum)
{
	constexpr int32 NumFramesToAcumulate = 30;

	static int64 CurrentPointBudget = 0;
	static int64 LastDynamicPointBudget = 0;
	static bool bLastFrameIncremental = false;
	static FLidarPointCloudViewData LastViewData;
	static TArray<float> AcumulatedFrameTime;

	if (AcumulatedFrameTime.Num() == 0)
	{
		AcumulatedFrameTime.Reserve(NumFramesToAcumulate + 1);
	}

	const FLidarPointCloudViewData ViewData(true);

	if (!LastViewData.bValid)
	{
		LastViewData = ViewData;
	}

	bool bUseIncrementalBudget = CVarLidarIncrementalBudget.GetValueOnAnyThread();
	const int32 ManualPointBudget = CVarLidarPointBudget.GetValueOnAnyThread();

	if (bUseIncrementalBudget && ViewData.ViewOrigin.Equals(LastViewData.ViewOrigin) && ViewData.ViewDirection.Equals(LastViewData.ViewDirection))
	{
		CurrentPointBudget += 500000;
		bLastFrameIncremental = true;
	}
	else
	{
		// Check if the point budget is manually set
		if (ManualPointBudget > 0)
		{
			CurrentPointBudget = ManualPointBudget;
		}
		else
		{
			CurrentPointBudget = LastDynamicPointBudget;

			// Do not recalculate if just exiting incremental budget, to avoid spikes
			if (!bLastFrameIncremental)
			{
				if (AcumulatedFrameTime.Add(DeltaTime) == NumFramesToAcumulate)
				{
					AcumulatedFrameTime.RemoveAt(0);
				}

				// The -0.5f is to prevent the system treating values as unachievable (as the frame time is usually just under)
				const float TargetFPS = FMath::Max(FMath::Min(CVarTargetFPS.GetValueOnAnyThread(), GEngine->GetMaxTickRate(0.001f, false)) - 0.5f, 1.0f);

				TArray<float> CurrentFrameTimes = AcumulatedFrameTime;
				CurrentFrameTimes.Sort();
				const float AvgFrameTime = CurrentFrameTimes[CurrentFrameTimes.Num() / 2];

				const int32 DeltaBudget = (1 / TargetFPS - AvgFrameTime) * 10000000;

				// Not having enough points in frustum to fill the requested budget would otherwise continually increase the value
				if (DeltaBudget < 0 || NumPointsInFrustum >= CurrentPointBudget)
				{
					CurrentPointBudget += DeltaBudget;
				}
			}
		}

		bLastFrameIncremental = false;
	}

	// Just in case
	if (ManualPointBudget == 0)
	{
		CurrentPointBudget = FMath::Clamp(CurrentPointBudget, 350000LL, 100000000LL);
	}

	if (!bUseIncrementalBudget)
	{
		LastDynamicPointBudget = CurrentPointBudget;
	}

	LastViewData = ViewData;

	return CurrentPointBudget;
}

FLidarPointCloudLODManager::FLidarPointCloudLODManager()
	: NumPointsInFrustum(0)
{
}

void FLidarPointCloudLODManager::Tick(float DeltaTime)
{
	// Skip processing, if a previous one is still going
	if (bProcessing)
	{
		return;
	}

	bProcessing = true;

	Time += DeltaTime;

	const uint32 PointBudget = GetPointBudget(DeltaTime, NumPointsInFrustum.GetValue());

	SET_DWORD_STAT(STAT_PointBudget, PointBudget);

	PrepareProxies();

	// Gather clipping volumes
	TArray<const ALidarClippingVolume*> ClippingVolumes = GetClippingVolumes();

	// Sort clipping volumes by priority
	Algo::Sort(ClippingVolumes, [](const ALidarClippingVolume* A, const ALidarClippingVolume* B) { return (A->Priority < B->Priority) || (A->Priority == B->Priority && A->Mode > B->Mode); });

	// A copy of the array will be passed, to avoid concurrency issues
	TArray<FRegisteredProxy> CurrentRegisteredProxies = RegisteredProxies;

	Async(EAsyncExecution::ThreadPool, [this, CurrentRegisteredProxies, ClippingVolumes, PointBudget]
	{
		NumPointsInFrustum.Set(ProcessLOD(CurrentRegisteredProxies, Time, PointBudget, ClippingVolumes));
	});
}

TStatId FLidarPointCloudLODManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(LidarPointCloudLODManager, STATGROUP_Tickables);
}

void FLidarPointCloudLODManager::RegisterProxy(ULidarPointCloudComponent* Component, TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper)
{
	if (IsValid(Component))
	{
		static FLidarPointCloudLODManager Instance;
		Instance.RegisteredProxies.Emplace(Component, SceneProxyWrapper);
	}
}

int64 FLidarPointCloudLODManager::ProcessLOD(const TArray<FLidarPointCloudLODManager::FRegisteredProxy>& InRegisteredProxies, const float CurrentTime, const uint32 PointBudget, const TArray<const ALidarClippingVolume*>& ClippingVolumes)
{
	uint32 TotalPointsSelected = 0;
	int64 NewNumPointsInFrustum = 0;

	TArray<TArray<FLidarPointCloudTraversalOctreeNode*>> SelectedNodesData;

	// Node selection
	{
		SCOPE_CYCLE_COUNTER(STAT_NodeSelection);

		const float ScreenCenterImportance = CVarLidarScreenCenterImportance.GetValueOnAnyThread();

		int32 NumSelectedNodes = 0;

		TArray<FNodeSizeData> NodeSizeData;

		for (int32 i = 0; i < InRegisteredProxies.Num(); ++i)
		{
			const FLidarPointCloudLODManager::FRegisteredProxy& RegisteredProxy = InRegisteredProxies[i];

			// Acquire a Shared Pointer from the Weak Pointer and check that it references a valid object
			if (TSharedPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper = RegisteredProxy.SceneProxyWrapper.Pin())
			{
				FScopeLock OctreeLock(&RegisteredProxy.PointCloud->Octree.DataLock);

				// If the octree has been invalidated, skip processing
				if (!RegisteredProxy.TraversalOctree->bValid)
				{
					continue;
				}

#if WITH_EDITOR
				// Avoid doubling the point allocation of the same asset (once in Editor world and once in PIE world)
				if (RegisteredProxy.bSkip)
				{
					continue;
				}
#endif

				// Construct selection params
				FLidarPointCloudNodeSelectionParams SelectionParams;
				SelectionParams.MinScreenSize = FMath::Max(RegisteredProxy.Component->MinScreenSize, 0.0f);
				SelectionParams.ScreenCenterImportance = ScreenCenterImportance;
				SelectionParams.MinDepth = RegisteredProxy.Component->MinDepth;
				SelectionParams.MaxDepth = RegisteredProxy.Component->MaxDepth;
				SelectionParams.BoundsScale = RegisteredProxy.Component->BoundsScale;

				// Ignore clipping if in editor viewport
				SelectionParams.ClippingVolumes = RegisteredProxy.Component->IsOwnedByEditor() ? nullptr : &ClippingVolumes;

				// Append visible nodes
				RegisteredProxy.TraversalOctree->GetVisibleNodes(NodeSizeData, RegisteredProxy.ViewData, i, SelectionParams, CurrentTime);
			}
		}

		// Sort Nodes
		Algo::Sort(NodeSizeData, [](const FNodeSizeData& A, const FNodeSizeData& B) { return A.Size > B.Size; });

		// Limit nodes using specified Point Budget
		SelectedNodesData.AddDefaulted(InRegisteredProxies.Num());
		for (FNodeSizeData& Element : NodeSizeData)
		{
			const uint32 NumPoints = Element.Node->DataNode->GetNumVisiblePoints();
			const uint32 NewNumPointsSelected = TotalPointsSelected + NumPoints;
			NewNumPointsInFrustum += NumPoints;

			if (NewNumPointsSelected <= PointBudget)
			{
				SelectedNodesData[Element.ProxyIndex].Add(Element.Node);
				TotalPointsSelected = NewNumPointsSelected;
				Element.Node->bSelected = true;
				++NumSelectedNodes;
			}
		}

		SET_DWORD_STAT(STAT_PointCount, TotalPointsSelected);
		SET_DWORD_STAT(STAT_PointCountFrustum, NewNumPointsInFrustum);
	}

	// Used to pass render data updates to render thread
	TArray<FLidarPointCloudProxyUpdateData> ProxyUpdateData;

	// Process Nodes
	{
		SCOPE_CYCLE_COUNTER(STAT_NodeProcessing);

		// Set when to release the BulkData, if no longer visible
		const float BulkDataLifetime = CurrentTime + GetDefault<ULidarPointCloudSettings>()->CachedNodeLifetime;

		for (int32 i = 0; i < SelectedNodesData.Num(); ++i)
		{
			const FLidarPointCloudLODManager::FRegisteredProxy& RegisteredProxy = InRegisteredProxies[i];

			// Only calculate if needed
			if (RegisteredProxy.Component->PointSize > 0)
			{
				for (FLidarPointCloudTraversalOctreeNode* Node : SelectedNodesData[i])
				{
					Node->CalculateVirtualDepth(RegisteredProxy.TraversalOctree->LevelWeights, RegisteredProxy.TraversalOctree->VirtualDepthMultiplier, RegisteredProxy.Component->PointSizeBias);
				}
			}

			FLidarPointCloudProxyUpdateData UpdateData;
			UpdateData.SceneProxyWrapper = RegisteredProxy.SceneProxyWrapper;
			UpdateData.NumElements = 0;
			UpdateData.VDMultiplier = RegisteredProxy.TraversalOctree->ReversedVirtualDepthMultiplier;
			UpdateData.RootCellSize = RegisteredProxy.PointCloud->Octree.GetRootCellSize();
			UpdateData.ClippingVolumes = ClippingVolumes;

			const bool bUseNormals = RegisteredProxy.Component->ShouldRenderFacingNormals();

			// Since the process is async, make sure we can access the data!
			{
				FScopeLock OctreeLock(&RegisteredProxy.PointCloud->Octree.DataLock);
				
				// If the octree has been invalidated, skip processing
				if (!RegisteredProxy.TraversalOctree->bValid)
				{
					continue;
				}

				// Queue nodes to be streamed
				for (FLidarPointCloudTraversalOctreeNode* Node : SelectedNodesData[i])
				{
					RegisteredProxy.PointCloud->Octree.QueueNode(Node->DataNode, BulkDataLifetime);

					if (Node->DataNode->HasData())
					{
						UpdateData.NumElements += Node->DataNode->GetNumVisiblePoints();
						UpdateData.SelectedNodes.Emplace(Node->VirtualDepth, Node->DataNode->GetNumVisiblePoints(), Node->DataNode);
					}
				}
			}

#if !(UE_BUILD_SHIPPING)
			// Prepare bounds
			if (RegisteredProxy.Component->bDrawNodeBounds)
			{
				UpdateData.Bounds.Reset(SelectedNodesData[i].Num());

				for (FLidarPointCloudTraversalOctreeNode* Node : SelectedNodesData[i])
				{
					FVector Extent = RegisteredProxy.TraversalOctree->Extents[Node->Depth];
					UpdateData.Bounds.Emplace(Node->Center - Extent, Node->Center + Extent);
				}
			}
#endif

			ProxyUpdateData.Add(UpdateData);
		}
	}

	// Begin streaming data
	for (int32 i = 0; i < InRegisteredProxies.Num(); ++i)
	{
		const FLidarPointCloudLODManager::FRegisteredProxy& RegisteredProxy = InRegisteredProxies[i];

		FScopeLock OctreeLock(&RegisteredProxy.PointCloud->Octree.DataLock);
		RegisteredProxy.PointCloud->Octree.UnloadOldNodes(CurrentTime);
		RegisteredProxy.PointCloud->Octree.StreamQueuedNodes();
	}

	// Update Render Data
	if (TotalPointsSelected > 0)
	{
		ENQUEUE_RENDER_COMMAND(ProcessLidarPointCloudLOD)([PointBudget, TotalPointsSelected, ProxyUpdateData](FRHICommandListImmediate& RHICmdList)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateRenderData);

			uint32 MaxPointsPerNode = 0;

			// Iterate over proxies and, if valid, update their data
			for (const FLidarPointCloudProxyUpdateData& UpdateData : ProxyUpdateData)
			{
				// Check for proxy's validity, in case it has been destroyed since the update was issued
				if (TSharedPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper = UpdateData.SceneProxyWrapper.Pin())
				{
					for (const FLidarPointCloudProxyUpdateDataNode& Node : UpdateData.SelectedNodes)
					{
						if (Node.DataNode->BuildDataCache())
						{
							MaxPointsPerNode = FMath::Max(MaxPointsPerNode, Node.DataNode->GetNumVisiblePoints());
						}
					}

					SceneProxyWrapper->Proxy->UpdateRenderData(UpdateData);
				}
			}

			if (MaxPointsPerNode > GLidarPointCloudIndexBuffer.GetCapacity())
			{
				GLidarPointCloudIndexBuffer.Resize(MaxPointsPerNode);
			}
		});
	}

	bProcessing = false;

	return NewNumPointsInFrustum;
}

void FLidarPointCloudLODManager::PrepareProxies()
{
	FLidarPointCloudViewData ViewData(true);

	const bool bPrioritizeActiveViewport = GetDefault<ULidarPointCloudSettings>()->bPrioritizeActiveViewport;

	// Contains the total number of points contained by all assets (including invisible and culled)
	int64 TotalPointCount = 0;

	// Prepare proxies
	for (int32 i = 0; i < RegisteredProxies.Num(); ++i)
	{
		FRegisteredProxy& RegisteredProxy = RegisteredProxies[i];
		bool bValidProxy = false;

		if (RegisteredProxy.Component->GetPointCloud())
		{
			// Acquire a Shared Pointer from the Weak Pointer and check that it references a valid object
			if (TSharedPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper = RegisteredProxy.SceneProxyWrapper.Pin())
			{
#if WITH_EDITOR
				// Avoid doubling the point allocation of the same asset (once in Editor world and once in PIE world)
				RegisteredProxy.bSkip = ViewData.bPIE && RegisteredProxy.Component->GetWorld()->WorldType == EWorldType::Type::Editor;
#endif

				// Check if the component's transform has changed, and invalidate the Traversal Octree if so
				const FTransform Transform = RegisteredProxy.Component->GetComponentTransform();
				if (!RegisteredProxy.LastComponentTransform.Equals(Transform))
				{
					RegisteredProxy.TraversalOctree->bValid = false;
					RegisteredProxy.LastComponentTransform = Transform;
				}

				// Re-initialize the traversal octree, if needed
				if (!RegisteredProxy.TraversalOctree->bValid)
				{
					// Update asset reference
					RegisteredProxy.PointCloud = RegisteredProxy.Component->GetPointCloud();

					// Recreate the Traversal Octree
					RegisteredProxy.TraversalOctree = MakeShareable(new FLidarPointCloudTraversalOctree(&RegisteredProxy.PointCloud->Octree, RegisteredProxy.Component->GetComponentTransform()));
					RegisteredProxy.PointCloud->Octree.RegisterTraversalOctree(RegisteredProxy.TraversalOctree);
				}

				// If this is an editor component, use its own ViewportClient
				if (TSharedPtr<FViewportClient> Client = RegisteredProxy.Component->GetOwningViewportClient().Pin())
				{
					// If the ViewData cannot be successfully retrieved from the editor viewport, fall back to using main view
					if (!RegisteredProxy.ViewData.ComputeFromEditorViewportClient(Client.Get()))
					{
						RegisteredProxy.ViewData = ViewData;
					}
				}
				// ... otherwise, use the ViewData provided
				else
				{
					RegisteredProxy.ViewData = ViewData;
				}

				// Increase priority, if the viewport has focus
				if (bPrioritizeActiveViewport && RegisteredProxy.ViewData.bHasFocus)
				{
					RegisteredProxy.ViewData.ScreenSizeFactor *= 6;
				}

				// Don't count the skippable proxies
				if (!RegisteredProxy.bSkip)
				{
					TotalPointCount += RegisteredProxy.PointCloud->GetNumPoints();
				}

				bValidProxy = true;
			}
		}
		
		// If the SceneProxy has been destroyed, remove it from the list and reiterate
		if(!bValidProxy)
		{
			RegisteredProxies.RemoveAtSwap(i--, 1, false);
		}
	}

	SET_DWORD_STAT(STAT_PointCountTotal, TotalPointCount / 1000);
}

TArray<const ALidarClippingVolume*> FLidarPointCloudLODManager::GetClippingVolumes() const
{
	TArray<const ALidarClippingVolume*> ClippingVolumes;
	TArray<UWorld*> Worlds;

	for (int32 i = 0; i < RegisteredProxies.Num(); ++i)
	{
		if (ULidarPointCloudComponent* Component = RegisteredProxies[i].Component)
		{
			if (!Component->IsOwnedByEditor())
			{
				if (UWorld* World = Component->GetWorld())
				{
					Worlds.AddUnique(World);
				}
			}
		}
	}

	for (UWorld* World : Worlds)
	{
		for (TActorIterator<ALidarClippingVolume> It(World); It; ++It)
		{
			ALidarClippingVolume* Volume = *It;
			if (Volume->bEnabled)
			{
				ClippingVolumes.Add(Volume);
			}
		}
	}

	return ClippingVolumes;
}

FLidarPointCloudLODManager::FRegisteredProxy::FRegisteredProxy(ULidarPointCloudComponent* Component, TWeakPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper)
	: Component(Component)
	, PointCloud(Component->GetPointCloud())
	, SceneProxyWrapper(SceneProxyWrapper)
	, TraversalOctree(new FLidarPointCloudTraversalOctree(&PointCloud->Octree, Component->GetComponentTransform()))
	, LastComponentTransform(Component->GetComponentTransform())
	, bSkip(false)
{
	PointCloud->Octree.RegisterTraversalOctree(TraversalOctree);
}
