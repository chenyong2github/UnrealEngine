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

#if WITH_EDITOR
#include "Classes/EditorStyleSettings.h"
#include "EditorViewportClient.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Buffer Creation"), STAT_BufferUpdate, STATGROUP_LidarPointCloud);
DECLARE_CYCLE_STAT(TEXT("Buffer Update"), STAT_BufferUpdateRT, STATGROUP_LidarPointCloud);
DECLARE_CYCLE_STAT(TEXT("Node Selection"), STAT_NodeSelection, STATGROUP_LidarPointCloud);
DECLARE_DWORD_COUNTER_STAT(TEXT("Registered Proxies"), STAT_ProxyCount, STATGROUP_LidarPointCloud)
DECLARE_DWORD_COUNTER_STAT(TEXT("Visible Nodes"), STAT_NodeCount, STATGROUP_LidarPointCloud)
DECLARE_DWORD_COUNTER_STAT(TEXT("Visible Points"), STAT_PointCount, STATGROUP_LidarPointCloud)
DECLARE_DWORD_COUNTER_STAT(TEXT("Total Point Count [thousands]"), STAT_PointCountTotal, STATGROUP_LidarPointCloud)

static TAutoConsoleVariable<int32> CVarLidarPointBudget(
	TEXT("r.LidarPointBudget"),
	1000000,
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

/**
 * Iterates over the provided nodes and sets location and color data.
 * Returns the total number of points processed.
 */
uint32 SetLocationAndColorData(uint8* Buffer, const TArray<FLidarPointCloudTraversalOctreeNode*>& Nodes, bool bUseClassification, bool OwningViewportClient)
{
	FColor SelectionColor = FColor::White;

#if WITH_EDITOR
	SelectionColor = GetDefault<UEditorStyleSettings>()->SelectionColor.ToFColor(false);
#endif

	uint8* BufferCurrent = Buffer;

	if (OwningViewportClient)
	{
		if (bUseClassification)
		{
			for (const FLidarPointCloudTraversalOctreeNode* Node : Nodes)
			{
				// Skip nodes with no available data
				if (!Node->DataNode->HasData())
				{
					continue;
				}

				for (FLidarPointCloudPoint* Data = Node->DataNode->GetData(), *DataEnd = Data + Node->DataNode->GetNumVisiblePoints(); Data != DataEnd; ++Data)
				{
					FMemory::Memcpy(BufferCurrent, Data, 12);
					BufferCurrent += 12;

					if (Data->bSelected)
					{
						FMemory::Memcpy(BufferCurrent, &SelectionColor, 4);
					}
					else
					{
						FColor ClassificationColor(Data->ClassificationID, Data->ClassificationID, Data->ClassificationID, Data->Color.A);
						FMemory::Memcpy(BufferCurrent, &ClassificationColor, 4);
					}

					BufferCurrent += 4;
				}
			}
		}
		else
		{
			for (const FLidarPointCloudTraversalOctreeNode* Node : Nodes)
			{
				// Skip nodes with no available data
				if (!Node->DataNode->HasData())
				{
					continue;
				}

				for (FLidarPointCloudPoint* Data = Node->DataNode->GetData(), *DataEnd = Data + Node->DataNode->GetNumVisiblePoints(); Data != DataEnd; ++Data)
				{
					if (Data->bSelected)
					{
						FMemory::Memcpy(BufferCurrent, Data, 12);
						BufferCurrent += 12;

						FMemory::Memcpy(BufferCurrent, &SelectionColor, 4);
						BufferCurrent += 4;
					}
					else
					{
						FMemory::Memcpy(BufferCurrent, Data, 16);
						BufferCurrent += 16;
					}
				}
			}
		}
	}
	else
	{
		if (bUseClassification)
		{
			for (const FLidarPointCloudTraversalOctreeNode* Node : Nodes)
			{
				// Skip nodes with no available data
				if (!Node->DataNode->HasData())
				{
					continue;
				}

				for (FLidarPointCloudPoint* Data = Node->DataNode->GetData(), *DataEnd = Data + Node->DataNode->GetNumVisiblePoints(); Data != DataEnd; ++Data)
				{
					FMemory::Memcpy(BufferCurrent, Data, 12);
					BufferCurrent += 12;

					FColor ClassificationColor(Data->ClassificationID, Data->ClassificationID, Data->ClassificationID, Data->Color.A);
					FMemory::Memcpy(BufferCurrent, &ClassificationColor, 4);
					BufferCurrent += 4;
				}
			}
		}
		else
		{
			for (const FLidarPointCloudTraversalOctreeNode* Node : Nodes)
			{
				// Skip nodes with no available data
				if (!Node->DataNode->HasData())
				{
					continue;
				}

				for (FLidarPointCloudPoint* Data = Node->DataNode->GetData(), *DataEnd = Data + Node->DataNode->GetNumVisiblePoints(); Data != DataEnd; ++Data)
				{
					FMemory::Memcpy(BufferCurrent, Data, 16);
					BufferCurrent += 16;
				}
			}
		}
	}

	// Calculates the actual number of instances copied to the buffer (accounts for the invisible points)
	return (BufferCurrent - Buffer) / 16;
}

/** Iterates over the provided nodes and sets scale data */
void SetScaleData(uint8* Buffer, const TArray<FLidarPointCloudTraversalOctreeNode*>& Nodes)
{
	for (const FLidarPointCloudTraversalOctreeNode* Node : Nodes)
	{
		// Skip nodes with no available data
		if (!Node->DataNode->HasData())
		{
			continue;
		}

		FMemory::Memset(Buffer, Node->VirtualDepth, Node->DataNode->GetNumVisiblePoints());
		Buffer += Node->DataNode->GetNumVisiblePoints();
	}
}

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

	TQueue<FLidarPointCloudTraversalOctreeNode*> Nodes;
	FLidarPointCloudTraversalOctreeNode* CurrentNode = nullptr;
	Nodes.Enqueue(&Root);
	while (Nodes.Dequeue(CurrentNode))
	{
		// Reset selection flag
		CurrentNode->bSelected = false;

		// Update number of visible points, if needed
		CurrentNode->DataNode->UpdateNumVisiblePoints();

		// In Frustum?
		// #todo: Skip frustum checks for nodes fully in frustum
		if (!ViewData.ViewFrustum.IntersectBox(CurrentNode->Center, Extents[CurrentNode->Depth] * SelectionParams.BoundsScale))
		{
			continue;
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
				Nodes.Enqueue(&Child);
			}
		}
	}
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
	
	PrepareProxies();

	// A copy of the array will be passed, to avoid concurrency issues
	TArray<FRegisteredProxy> CurrentRegisteredProxies = RegisteredProxies;

	Async(EAsyncExecution::ThreadPool, [this, CurrentRegisteredProxies] { ProcessLOD(CurrentRegisteredProxies, Time); });
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

void FLidarPointCloudLODManager::ProcessLOD(const TArray<FLidarPointCloudLODManager::FRegisteredProxy>& InRegisteredProxies, const float CurrentTime)
{
	int32 PointBudget = CVarLidarPointBudget.GetValueOnAnyThread();

#if PLATFORM_MAC
	static bool bMetalBudgetNotified = false;
	if (PointBudget > 9586980)
	{
		PointBudget = 9586980;
		
		if (!bMetalBudgetNotified)
		{
			bMetalBudgetNotified = true;
			PC_WARNING("Metal API supports a maximum point budget of 9,586,980. The requested budget has been automatically capped to avoid a crash. This will be fixed for 4.26.");
		}
	}
#endif

	static FLidarPointCloudDataBufferManager BufferManager(PointBudget * 17);
	BufferManager.Resize(PointBudget * 17);

	int32 TotalPointsSelected = 0;

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
			int32 NewNumPointsSelected = TotalPointsSelected + Element.Node->DataNode->GetNumVisiblePoints();

			if (NewNumPointsSelected <= PointBudget)
			{
				SelectedNodesData[Element.ProxyIndex].Add(Element.Node);
				TotalPointsSelected = NewNumPointsSelected;
				Element.Node->bSelected = true;
				++NumSelectedNodes;
			}
		}

		INC_DWORD_STAT_BY(STAT_PointCount, TotalPointsSelected);
		INC_DWORD_STAT_BY(STAT_NodeCount, NumSelectedNodes);
	}

	// Used to pass render data updates to render thread
	FLidarPointCloudDataBuffer* Buffer = BufferManager.GetFreeBuffer();
	TArray<FLidarPointCloudProxyUpdateData> ProxyUpdateData;

	// Build buffer data
	{
		SCOPE_CYCLE_COUNTER(STAT_BufferUpdate);

		// Prepare the data for structured buffer.
		uint8* BufferData = Buffer->GetData();
		uint8* LocationAndColorBufferPtr = BufferData;
		uint8* ScaleBufferPtr = BufferData + TotalPointsSelected * 16;

		int32 FirstElementIndex = 0;

		// Set when to release the BulkData, if no longer visible
		const float BulkDataLifetime = CurrentTime + 1;

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

			int32 NumPoints = 0;

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
				}

				NumPoints = SetLocationAndColorData(LocationAndColorBufferPtr, SelectedNodesData[i], RegisteredProxy.Component->ColorSource == ELidarPointCloudColorationMode::Classification, RegisteredProxy.Component->IsOwnedByEditor());
				SetScaleData(ScaleBufferPtr, SelectedNodesData[i]);
			}

			FLidarPointCloudProxyUpdateData UpdateData;
			UpdateData.SceneProxyWrapper = RegisteredProxy.SceneProxyWrapper;
			UpdateData.FirstElementIndex = FirstElementIndex;
			UpdateData.NumElements = NumPoints;
			UpdateData.PointBudget = PointBudget;
			UpdateData.VDMultiplier = RegisteredProxy.TraversalOctree->ReversedVirtualDepthMultiplier;
			UpdateData.RootCellSize = RegisteredProxy.PointCloud->Octree.GetRootCellSize();

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

			// Shift pointers
			FirstElementIndex += NumPoints;
			LocationAndColorBufferPtr += NumPoints * 16;
			ScaleBufferPtr += NumPoints;
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

	// Process buffer updates on RT
	ENQUEUE_RENDER_COMMAND(ProcessLidarPointCloudLOD)([PointBudget, Buffer, TotalPointsSelected, ProxyUpdateData](FRHICommandListImmediate& RHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_BufferUpdateRT);

		// Resize IndexBuffer
		GLidarPointCloudIndexBuffer.Resize(PointBudget);

		// 17 bytes per point, element size set to 4 bytes to minimize wastage. Rounded to 4.3 elements per point
		GLidarPointCloudRenderBuffer.Resize(PointBudget * 4.3f);

		GLidarPointCloudRenderBuffer.PointCount = TotalPointsSelected;

		if (TotalPointsSelected > 0)
		{
			int32 TotalDataSize = TotalPointsSelected * 17;

			// Update contents of the Structured Buffer
			uint8* StructuredBuffer = (uint8*)RHILockVertexBuffer(GLidarPointCloudRenderBuffer.Buffer, 0, TotalDataSize, RLM_WriteOnly);
			FMemory::Memcpy(StructuredBuffer, Buffer->GetData(), TotalDataSize);
			RHIUnlockVertexBuffer(GLidarPointCloudRenderBuffer.Buffer);

			// Iterate over proxies and, if valid, update its FirstElementIndex
			for (int32 i = 0; i < ProxyUpdateData.Num(); ++i)
			{
				// Check for proxy's validity, in case it has been destroyed since the update was issued
				if (TSharedPtr<FLidarPointCloudSceneProxyWrapper, ESPMode::ThreadSafe> SceneProxyWrapper = ProxyUpdateData[i].SceneProxyWrapper.Pin())
				{
					SceneProxyWrapper->Proxy->UpdateRenderData(ProxyUpdateData[i]);
				}
			}
		}

		Buffer->MarkAsFree();
	});

	bProcessing = false;
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

	INC_DWORD_STAT_BY(STAT_PointCountTotal, TotalPointCount / 1000);
	INC_DWORD_STAT_BY(STAT_ProxyCount, RegisteredProxies.Num());
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
