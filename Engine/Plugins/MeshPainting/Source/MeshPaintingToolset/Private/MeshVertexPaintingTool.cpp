// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshVertexPaintingTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "ToolContextInterfaces.h"
#include "Components/MeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "IMeshPaintComponentAdapter.h"
#include "ComponentReregisterContext.h"
#include "MeshPaintAdapterFactory.h"
#include "ToolDataVisualizer.h"


#define LOCTEXT_NAMESPACE "MeshVertexBrush"

/*
 * ToolBuilder
 */

bool HasPaintableMesh(UActorComponent* Component)
{
	return Cast<UMeshComponent>(Component) != nullptr;
}


bool UMeshColorPaintingToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, HasPaintableMesh);
	return ActorComponent != nullptr;
}

UInteractiveTool* UMeshColorPaintingToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshColorPaintingTool* NewTool = NewObject<UMeshColorPaintingTool>(SceneState.ToolManager, "ColorBrushTool");
	return NewTool;
}

bool UMeshWeightPaintingToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, HasPaintableMesh);
	return ActorComponent != nullptr;
}

UInteractiveTool* UMeshWeightPaintingToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshWeightPaintingTool* NewTool = NewObject<UMeshWeightPaintingTool>(SceneState.ToolManager, "ColorWeightTool");
	return NewTool;
}


/*
 * Tool
 */

UMeshVertexPaintingToolProperties::UMeshVertexPaintingToolProperties()
	:UBrushBaseProperties(),
	PaintColor(FLinearColor::White),
	EraseColor(FLinearColor::Black),
	bEnableFlow(false),
	VertexPreviewSize(6.0f)
{
}



void UMeshVertexPaintingToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UBrushBaseProperties::SaveProperties(SaveFromTool);
	UMeshVertexPaintingToolProperties* PropertyCache = GetPropertyCache<UMeshVertexPaintingToolProperties>();
	PropertyCache->PaintColor = this->PaintColor;
	PropertyCache->EraseColor = this->EraseColor;
	PropertyCache->bEnableFlow = this->bEnableFlow;
	PropertyCache->bOnlyFrontFacingTriangles = this->bOnlyFrontFacingTriangles;
}

void UMeshVertexPaintingToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UBrushBaseProperties::RestoreProperties(RestoreToTool);
	UMeshVertexPaintingToolProperties* PropertyCache = GetPropertyCache<UMeshVertexPaintingToolProperties>();
	this->PaintColor = PropertyCache->PaintColor;
	this->EraseColor = PropertyCache->EraseColor;
	this->bEnableFlow = PropertyCache->bEnableFlow;
	this->bOnlyFrontFacingTriangles = PropertyCache->bOnlyFrontFacingTriangles;
}

UMeshVertexPaintingTool::UMeshVertexPaintingTool()
{
	PropertyClass = UMeshVertexPaintingToolProperties::StaticClass();
}


void UMeshVertexPaintingTool::Setup()
{
	Super::Setup();
	VertexProperties = Cast<UMeshVertexPaintingToolProperties>(BrushProperties);
	bResultValid = false;
	bStampPending = false;
	BrushProperties->RestoreProperties(this);
}


void UMeshVertexPaintingTool::Shutdown(EToolShutdownType ShutdownType)
{
	FinishPainting();
	BrushProperties->SaveProperties(this);
	Super::Shutdown(ShutdownType);
}

void UMeshVertexPaintingTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager());
	if (MeshToolManager && LastBestHitResult.Component != nullptr)
	{
		Super::Render(RenderAPI);
		FToolDataVisualizer Draw;
		Draw.BeginFrame(RenderAPI);
		static float WidgetLineThickness = 1.0f;
		static FLinearColor VertexPointColor = FLinearColor::White;
		static FLinearColor	HoverVertexPointColor = FLinearColor(0.3f, 1.0f, 0.3f);
		const float NormalLineSize(BrushProperties->BrushRadius * 0.35f);	// Make the normal line length a function of brush size
		static const FLinearColor NormalLineColor(0.3f, 1.0f, 0.3f);
		const FLinearColor BrushCueColor = (bArePainting ? FLinearColor(1.0f, 1.0f, 0.3f) : FLinearColor(0.3f, 1.0f, 0.3f));
 		const FLinearColor InnerBrushCueColor = (bArePainting ? FLinearColor(0.5f, 0.5f, 0.1f) : FLinearColor(0.1f, 0.5f, 0.1f));
		const float PointDrawSize = VertexProperties->VertexPreviewSize;
		// Draw trace surface normal
		const FVector NormalLineEnd(LastBestHitResult.Location + LastBestHitResult.Normal * NormalLineSize);
		Draw.DrawLine(FVector(LastBestHitResult.Location), NormalLineEnd, NormalLineColor, WidgetLineThickness);

		for (UMeshComponent* CurrentComponent : MeshToolManager->GetPaintableMeshComponents())
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = MeshToolManager->GetAdapterForComponent(Cast<UMeshComponent>(CurrentComponent));

			if (MeshAdapter->IsValid() /*&& bRenderVertices*/ && MeshAdapter->SupportsVertexPaint())
			{
				const FMatrix ComponentToWorldMatrix = MeshAdapter->GetComponentToWorldMatrix();
				FViewCameraState CameraState;
				GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
				const FVector ComponentSpaceCameraPosition(ComponentToWorldMatrix.InverseTransformPosition(CameraState.Position));
				const FVector ComponentSpaceBrushPosition(ComponentToWorldMatrix.InverseTransformPosition(LastBestHitResult.Location));

				// @todo MeshPaint: Input vector doesn't work well with non-uniform scale
				const float ComponentSpaceBrushRadius = ComponentToWorldMatrix.InverseTransformVector(FVector(BrushProperties->BrushRadius, 0.0f, 0.0f)).Size();
				const float ComponentSpaceSquaredBrushRadius = ComponentSpaceBrushRadius * ComponentSpaceBrushRadius;

				const TArray<FVector>& InRangeVertices = MeshAdapter->SphereIntersectVertices(ComponentSpaceSquaredBrushRadius, ComponentSpaceBrushPosition, ComponentSpaceCameraPosition, VertexProperties->bOnlyFrontFacingTriangles);

				for (const FVector& Vertex : InRangeVertices)
				{
					const FVector WorldPositionVertex = ComponentToWorldMatrix.TransformPosition(Vertex);
					if ((LastBestHitResult.Location - WorldPositionVertex).Size() <= BrushProperties->BrushRadius)
					{
						const float VisualBiasDistance = 0.15f;
						const FVector VertexVisualPosition = WorldPositionVertex + LastBestHitResult.Normal * VisualBiasDistance;
						Draw.DrawPoint(VertexVisualPosition, HoverVertexPointColor, PointDrawSize, SDPG_World);
					}
				}
			}
		}
		Draw.EndFrame();
	}
	UpdateResult();

}

void UMeshVertexPaintingTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		if (MeshToolManager->bNeedsRecache)
		{
			CacheSelectionData();
			bDoRestoreRenTargets = true;
		}
	}

	if (bStampPending)
	{
		Paint(PendingStampRay.Origin, PendingStampRay.Direction);
		bStampPending = false;

		// flow
		if (bInDrag && VertexProperties && VertexProperties->bEnableFlow)
		{
			bStampPending = true;
		}
	}
}

void UMeshVertexPaintingTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	bResultValid = false;
}


double UMeshVertexPaintingTool::CalculateTargetEdgeLength(int TargetTriCount)
{
	double TargetTriArea = InitialMeshArea / (double)TargetTriCount;
	double EdgeLen = (TargetTriArea);
	return (double)FMath::RoundToInt(EdgeLen*100.0) / 100.0;
}

bool UMeshVertexPaintingTool::Paint(const FVector& InRayOrigin, const FVector& InRayDirection)
{
	// Determine paint action according to whether or not shift is held down
	const EMeshPaintModeAction PaintAction = GetShiftToggle() ? EMeshPaintModeAction::Erase : EMeshPaintModeAction::Paint;
	const float PaintStrength = 1.0f; //Viewport->IsPenActive() ? Viewport->GetTabletPressure() : 1.f;
	// Handle internal painting functionality
	TPair<FVector, FVector> Ray(InRayOrigin, InRayDirection);
	return PaintInternal(MakeArrayView(&Ray, 1), PaintAction, PaintStrength);
}

bool UMeshVertexPaintingTool::Paint(const TArrayView<TPair<FVector, FVector>>& Rays)
{
	// Determine paint action according to whether or not shift is held down
	const EMeshPaintModeAction PaintAction = GetShiftToggle() ? EMeshPaintModeAction::Erase : EMeshPaintModeAction::Paint;

	const float PaintStrength = 1.0f; //Viewport->IsPenActive() ? Viewport->GetTabletPressure() : 1.f;
	// Handle internal painting functionality
	return PaintInternal(Rays, PaintAction, PaintStrength);
}


bool UMeshVertexPaintingTool::PaintInternal(const TArrayView<TPair<FVector, FVector>>& Rays, EMeshPaintModeAction PaintAction, float PaintStrength)
{
	TArray<FPaintRayResults> PaintRayResults;
	PaintRayResults.AddDefaulted(Rays.Num());
	LastBestHitResult.Reset();
	TMap<UMeshComponent*, TArray<int32>> HoveredComponents;

	const float BrushRadius = BrushProperties->BrushRadius;
	const bool bIsPainting = PaintAction == EMeshPaintModeAction::Paint;
	const float InStrengthScale = PaintStrength;

	bool bPaintApplied = false;
	UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager());
	// Fire out a ray to see if there is a *selected* component under the mouse cursor that can be painted.
	for (int32 i = 0; i < Rays.Num(); ++i)
	{
		const FVector& RayOrigin = Rays[i].Key;
		const FVector& RayDirection = Rays[i].Value;
		FRay Ray = FRay(RayOrigin, RayDirection);
		FHitResult& BestTraceResult = PaintRayResults[i].BestTraceResult;
		const FVector TraceStart(RayOrigin);
		const FVector TraceEnd(RayOrigin + RayDirection * HALF_WORLD_MAX);

		for (UMeshComponent* MeshComponent : MeshToolManager->GetPaintableMeshComponents())
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = MeshToolManager->GetAdapterForComponent(MeshComponent);

			// Ray trace
			FHitResult TraceHitResult(1.0f);

			if (MeshAdapter->LineTraceComponent(TraceHitResult, TraceStart, TraceEnd, FCollisionQueryParams(SCENE_QUERY_STAT(Paint), true)))
			{
				// Find the closest impact
				if ((BestTraceResult.GetComponent() == nullptr) || (TraceHitResult.Time < BestTraceResult.Time))
				{
					BestTraceResult = TraceHitResult;
				}
			}
		}

		bool bUsed = false;

		if (BestTraceResult.GetComponent() != nullptr)
		{
			FBox BrushBounds = FBox::BuildAABB(BestTraceResult.Location, FVector(BrushRadius * 1.25f, BrushRadius * 1.25f, BrushRadius * 1.25f));

			// Vertex paint mode, so we want all valid components overlapping the brush hit location
			for (auto TestComponent : MeshToolManager->GetPaintableMeshComponents())
			{
				const FBox ComponentBounds = TestComponent->Bounds.GetBox();

				if (MeshToolManager->GetComponentToAdapterMap().Contains(TestComponent) && ComponentBounds.Intersect(BrushBounds))
				{
					// OK, this mesh potentially overlaps the brush!
					HoveredComponents.FindOrAdd(TestComponent).Add(i);
					bUsed = true;
				}
			}
		}
		if (bUsed)
		{
			FVector BrushXAxis, BrushYAxis;
			BestTraceResult.Normal.FindBestAxisVectors(BrushXAxis, BrushYAxis);
			// Display settings
			const float VisualBiasDistance = 0.15f;
			const FVector BrushVisualPosition = BestTraceResult.Location + BestTraceResult.Normal * VisualBiasDistance;

			const FLinearColor PaintColor = VertexProperties->PaintColor;
			const FLinearColor EraseColor = VertexProperties->EraseColor;

			// NOTE: We square the brush strength to maximize slider precision in the low range
			const float BrushStrength = BrushProperties->BrushStrength *  BrushProperties->BrushStrength * InStrengthScale;

			const float BrushDepth = BrushRadius;
			LastBestHitResult = BestTraceResult;
			// Mesh paint settings
			FMeshPaintParameters& Params = PaintRayResults[i].Params;
			Params.PaintAction = PaintAction;
			Params.BrushPosition = BestTraceResult.Location;
			Params.BrushNormal = BestTraceResult.Normal;
			Params.BrushColor = bIsPainting ? PaintColor : EraseColor;
			Params.SquaredBrushRadius = BrushRadius * BrushRadius;
			Params.BrushRadialFalloffRange = BrushProperties->BrushFalloffAmount * BrushRadius;
			Params.InnerBrushRadius = BrushRadius - Params.BrushRadialFalloffRange;
			Params.BrushDepth = BrushDepth;
			Params.BrushDepthFalloffRange = BrushProperties->BrushFalloffAmount * BrushDepth;
			Params.BrushDepthFalloffRange = BrushProperties->BrushFalloffAmount * BrushDepth;
			Params.InnerBrushDepth = BrushDepth - Params.BrushDepthFalloffRange;
			Params.BrushStrength = BrushStrength;
			Params.BrushToWorldMatrix = FMatrix(BrushXAxis, BrushYAxis, Params.BrushNormal, Params.BrushPosition);
			Params.InverseBrushToWorldMatrix = Params.BrushToWorldMatrix.Inverse();

			SetAdditionalPaintParameters(Params);
		
		}
	}

	if (HoveredComponents.Num() > 0)
	{
		if (bArePainting == false)
		{
				// Vertex painting is an ongoing transaction, while texture painting is handled separately later in a single transaction
				GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshPaintMode_VertexPaint_TransactionPaintStroke", "Vertex Paint"));
				bArePainting = true;
				TimeSinceStartedPainting = 0.0f;
		}

		// Iterate over the selected meshes under the cursor and paint them!
		for (auto& Entry : HoveredComponents)
		{
			UMeshComponent* HoveredComponent = Entry.Key;
			TArray<int32>& PaintRayResultIds = Entry.Value;

			IMeshPaintComponentAdapter* MeshAdapter = MeshToolManager->GetAdapterForComponent(HoveredComponent).Get();
			if (!ensure(MeshAdapter))
			{
				continue;
			}

			if (MeshAdapter->SupportsVertexPaint())
			{
				FPerVertexPaintActionArgs Args;
				Args.Adapter = MeshAdapter;
				FViewCameraState CameraState;
				GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
				Args.CameraPosition = CameraState.Position;
				Args.BrushProperties = VertexProperties;
				Args.Action = PaintAction;

				bool bMeshPreEditCalled = false;

				TSet<int32> InfluencedVertices;
				for (int32 PaintRayResultId : PaintRayResultIds)
				{
					InfluencedVertices.Reset();
					Args.HitResult = PaintRayResults[PaintRayResultId].BestTraceResult;
					bPaintApplied |= UMeshPaintingToolset::GetPerVertexPaintInfluencedVertices(Args, InfluencedVertices);

					if (InfluencedVertices.Num() == 0)
					{
						continue;
					}

					if (!bMeshPreEditCalled)
					{
						bMeshPreEditCalled = true;
						MeshAdapter->PreEdit();
					}

					for (const int32 VertexIndex : InfluencedVertices)
					{
						ApplyVertexData(Args, VertexIndex, PaintRayResults[PaintRayResultId].Params);
					}
				}

				if (bMeshPreEditCalled)
				{
					MeshAdapter->PostEdit();
				}
			}
		}
	}

	return bPaintApplied;
}

void UMeshVertexPaintingTool::ApplyVertexData(FPerVertexPaintActionArgs& InArgs, int32 VertexIndex, FMeshPaintParameters Parameters)
{
	/** Retrieve vertex position and color for applying vertex painting */
	FColor PaintColor;
	FVector Position;
	InArgs.Adapter->GetVertexPosition(VertexIndex, Position);
	Position = InArgs.Adapter->GetComponentToWorldMatrix().TransformPosition(Position);
	InArgs.Adapter->GetVertexColor(VertexIndex, PaintColor, true);
	UMeshPaintingToolset::PaintVertex(Position, Parameters, PaintColor);
	InArgs.Adapter->SetVertexColor(VertexIndex, PaintColor, true);
}


void UMeshVertexPaintingTool::UpdateResult()
{
	GetToolManager()->PostInvalidation();

	bResultValid = true;
}

bool UMeshVertexPaintingTool::HasAccept() const
{
	return false;
}

bool UMeshVertexPaintingTool::CanAccept() const
{
	return false;
}

void UMeshVertexPaintingTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);
	FHitResult OutHit;
	if (HitTest(Ray, OutHit))
	{
		bInDrag = true;

		// apply initial stamp
		PendingStampRay = Ray;
		bStampPending = true;
	}
}

void UMeshVertexPaintingTool::OnUpdateDrag(const FRay& Ray)
{
	Super::OnUpdateDrag(Ray);
	if (bInDrag)
	{
		PendingStampRay = Ray;
		bStampPending = true;
	}
}



void UMeshVertexPaintingTool::OnEndDrag(const FRay& Ray)
{
	FinishPainting();
	bStampPending = false;
	bInDrag = false;
}

bool UMeshVertexPaintingTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	bool bUsed = false;
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		bUsed = MeshToolManager->FindHitResult(Ray, OutHit);
		LastBestHitResult = OutHit;
	}
	return bUsed;
}

void UMeshVertexPaintingTool::FinishPainting()
{
	if (bArePainting)
	{
		bArePainting = false;
		GetToolManager()->EndUndoTransaction();
		OnPaintingFinishedDelegate.ExecuteIfBound();
	}
}


UMeshColorPaintingToolProperties::UMeshColorPaintingToolProperties()
	:UMeshVertexPaintingToolProperties(),
	bWriteRed(true),
	bWriteGreen(true),
	bWriteBlue(true),
	bWriteAlpha(false)
{

}

void UMeshColorPaintingToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	Super::SaveProperties(SaveFromTool);
	UMeshColorPaintingToolProperties* PropertyCache = GetPropertyCache<UMeshColorPaintingToolProperties>();
	PropertyCache->bWriteRed = this->bWriteRed;
	PropertyCache->bWriteGreen = this->bWriteGreen;
	PropertyCache->bWriteBlue = this->bWriteBlue;
	PropertyCache->bWriteRed = this->bWriteRed;
}

void UMeshColorPaintingToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	Super::RestoreProperties(RestoreToTool);
	UMeshColorPaintingToolProperties* PropertyCache = GetPropertyCache<UMeshColorPaintingToolProperties>();
	this->bWriteRed = PropertyCache->bWriteRed;
	this->bWriteGreen = PropertyCache->bWriteGreen;
	this->bWriteBlue = PropertyCache->bWriteBlue;
	this->bWriteRed = PropertyCache->bWriteRed;
}

UMeshColorPaintingTool::UMeshColorPaintingTool()
{
	PropertyClass = UMeshColorPaintingToolProperties::StaticClass();
}

void UMeshColorPaintingTool::Setup()
{
	Super::Setup();
	ColorProperties = Cast<UMeshColorPaintingToolProperties>(BrushProperties);
}

void UMeshColorPaintingTool::CacheSelectionData()
{
	Super::CacheSelectionData();
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		MeshToolManager->ClearPaintableMeshComponents();
		// Update(cached) Paint LOD level if necessary
		ColorProperties->LODIndex = FMath::Min<int32>(ColorProperties->LODIndex, GetMaxLODIndexToPaint());
		CachedLODIndex = ColorProperties->LODIndex;
		bCachedForceLOD = ColorProperties->bPaintOnSpecificLOD;
		//Determine LOD level to use for painting(can only paint on LODs in vertex mode)
		const int32 PaintLODIndex = ColorProperties->bPaintOnSpecificLOD ? ColorProperties->LODIndex : 0;
		//Determine UV channel to use while painting textures
		const int32 UVChannel = 0;

		MeshToolManager->CacheSelectionData(PaintLODIndex, UVChannel);
	}
}

void UMeshColorPaintingTool::SetAdditionalPaintParameters(FMeshPaintParameters& InPaintParameters)
{
	InPaintParameters.bWriteRed = ColorProperties->bWriteRed;
	InPaintParameters.bWriteGreen = ColorProperties->bWriteGreen;
	InPaintParameters.bWriteBlue = ColorProperties->bWriteBlue;
	InPaintParameters.bWriteAlpha = ColorProperties->bWriteAlpha;
	InPaintParameters.ApplyVertexDataDelegate.AddStatic(&UMeshPaintingToolset::ApplyVertexColorPaint);
}

int32 UMeshColorPaintingTool::GetMaxLODIndexToPaint() const
{
	//The maximum LOD we can paint is decide by the lowest number of LOD in the selection
	int32 LODMin = TNumericLimits<int32>::Max();

	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		TArray<UMeshComponent*> SelectedComponents = MeshToolManager->GetSelectedMeshComponents();

		for (UMeshComponent* MeshComponent : SelectedComponents)
		{
			int32 NumMeshLODs = 0;
			if (UMeshPaintingToolset::TryGetNumberOfLODs(MeshComponent, NumMeshLODs))
			{
				ensure(NumMeshLODs > 0);
				LODMin = FMath::Min(LODMin, NumMeshLODs - 1);
			}
		}
		if (LODMin == TNumericLimits<int32>::Max())
		{
			LODMin = 1;
		}
	}
	return LODMin;
}


void UMeshColorPaintingTool::LODPaintStateChanged(const bool bLODPaintingEnabled)
{
	bool AbortChange = false;

	// Set actual flag in the settings struct
	ColorProperties->bPaintOnSpecificLOD = bLODPaintingEnabled;

	if (!bLODPaintingEnabled)
	{
		// Reset painting LOD index
		ColorProperties->LODIndex = 0;

	}

	ApplyForcedLODIndex(bLODPaintingEnabled ? CachedLODIndex : -1);

	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		TArray<UMeshComponent*> PaintableComponents = MeshToolManager->GetPaintableMeshComponents();

		TUniquePtr< FComponentReregisterContext > ComponentReregisterContext;
		//Make sure all static mesh render is dirty since we change the force LOD
		for (UMeshComponent* SelectedComponent : PaintableComponents)
		{
			if (SelectedComponent)
			{
				ComponentReregisterContext.Reset(new FComponentReregisterContext(SelectedComponent));
			}
		}

		MeshToolManager->Refresh();
	}
}

void UMeshColorPaintingTool::ApplyForcedLODIndex(int32 ForcedLODIndex)
{
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		TArray<UMeshComponent*> PaintableComponents = MeshToolManager->GetPaintableMeshComponents();

		for (UMeshComponent* SelectedComponent : PaintableComponents)
		{
			if (SelectedComponent)
			{
				UMeshPaintingToolset::ForceRenderMeshLOD(SelectedComponent, ForcedLODIndex);
			}
		}
	}
}


void UMeshColorPaintingTool::PaintLODChanged()
{
	// Enforced LOD for painting
	if (CachedLODIndex != ColorProperties->LODIndex)
	{
		CachedLODIndex = ColorProperties->LODIndex;
		ApplyForcedLODIndex(bCachedForceLOD ? CachedLODIndex : -1);

		TUniquePtr< FComponentReregisterContext > ComponentReregisterContext;
		//Make sure all static mesh render is dirty since we change the force LOD
		if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
		{
			TArray<UMeshComponent*> PaintableComponents = MeshToolManager->GetPaintableMeshComponents();

			for (UMeshComponent* SelectedComponent : PaintableComponents)
			{
				if (SelectedComponent)
				{
					ComponentReregisterContext.Reset(new FComponentReregisterContext(SelectedComponent));
				}
			}

			MeshToolManager->Refresh();
		}
	}
}

UMeshWeightPaintingToolProperties::UMeshWeightPaintingToolProperties()
	:UMeshVertexPaintingToolProperties(),
	TextureWeightType(EMeshPaintWeightTypes::AlphaLerp),
	PaintTextureWeightIndex(EMeshPaintTextureIndex::TextureOne),
	EraseTextureWeightIndex(EMeshPaintTextureIndex::TextureTwo)
{

}

void UMeshWeightPaintingToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	Super::SaveProperties(SaveFromTool);
}

void UMeshWeightPaintingToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	Super::RestoreProperties(RestoreToTool);
}

UMeshWeightPaintingTool::UMeshWeightPaintingTool()
{
	PropertyClass = UMeshWeightPaintingToolProperties::StaticClass();
}

void UMeshWeightPaintingTool::Setup()
{
	Super::Setup();
	WeightProperties = Cast<UMeshWeightPaintingToolProperties>(BrushProperties);
}

void UMeshWeightPaintingTool::CacheSelectionData()
{
	Super::CacheSelectionData();
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		MeshToolManager->ClearPaintableMeshComponents();
		//Determine LOD level to use for painting(can only paint on LODs in vertex mode)
		const int32 PaintLODIndex = 0;
		//Determine UV channel to use while painting textures
		const int32 UVChannel = 0;
		MeshToolManager->CacheSelectionData(PaintLODIndex, UVChannel);
	}
}

void UMeshWeightPaintingTool::SetAdditionalPaintParameters(FMeshPaintParameters& InPaintParameters)
{
	InPaintParameters.TotalWeightCount = (int32)WeightProperties->TextureWeightType;

	// Select texture weight index based on whether or not we're painting or erasing
	{
		const int32 PaintWeightIndex = InPaintParameters.PaintAction == EMeshPaintModeAction::Paint ? (int32)WeightProperties->PaintTextureWeightIndex : (int32)WeightProperties->EraseTextureWeightIndex;

		// Clamp the weight index to fall within the total weight count
		InPaintParameters.PaintWeightIndex = FMath::Clamp(PaintWeightIndex, 0, InPaintParameters.TotalWeightCount - 1);
	}
	InPaintParameters.ApplyVertexDataDelegate.AddStatic(&UMeshPaintingToolset::ApplyVertexWeightPaint);
}

#undef LOCTEXT_NAMESPACE