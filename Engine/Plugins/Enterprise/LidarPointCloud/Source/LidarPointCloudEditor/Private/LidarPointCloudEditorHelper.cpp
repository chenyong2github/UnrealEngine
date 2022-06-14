// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEditorHelper.h"
#include "ContentBrowserModule.h"
#include "GeomTools.h"
#include "IContentBrowserSingleton.h"
#include "LevelEditorViewport.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudActor.h"
#include "LidarPointCloudComponent.h"
#include "Selection.h"

#include "AssetRegistry/AssetRegistryModule.h"

#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "LidarPointCloudEditorHelper"

class FContentBrowserModule;

namespace
{
	FString GetSaveAsLocation()
	{
		// Initialize SaveAssetDialog config
		FSaveAssetDialogConfig SaveAssetDialogConfig;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SelectDestination", "Select Destination");
		SaveAssetDialogConfig.DefaultPath = "/Game";
		SaveAssetDialogConfig.AssetClassNames.Emplace("/Script/PointCloudRuntime.LidarPointCloud");
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		return ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);
	}

	ULidarPointCloud* CreateNewAsset_Internal()
	{
		ULidarPointCloud* NewPointCloud = nullptr;

		const FString SaveObjectPath = GetSaveAsLocation();
		if (!SaveObjectPath.IsEmpty())
		{
			// Attempt to load existing asset first
			NewPointCloud = FindObject<ULidarPointCloud>(nullptr, *SaveObjectPath);

			// Proceed to creating a new asset, if needed
			if (!NewPointCloud)
			{
				const FString PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
				const FString ObjectName = FPackageName::ObjectPathToObjectName(SaveObjectPath);

				NewPointCloud = NewObject<ULidarPointCloud>(CreatePackage(*PackageName), ULidarPointCloud::StaticClass(), FName(*ObjectName), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);

				FAssetRegistryModule::AssetCreated(NewPointCloud);
				NewPointCloud->MarkPackageDirty();
			}		
		}

		return NewPointCloud;
	}

	void ProcessSelection(TFunction<void(ALidarPointCloudActor*)> Function)
	{
		if(!Function)
		{
			return;
		}
		
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if(ALidarPointCloudActor* LidarActor = Cast<ALidarPointCloudActor>(*It))
			{
				Function(LidarActor);
			}
		}
	}

	void ProcessAll(TFunction<void(ALidarPointCloudActor*)> Function)
	{
		if(!Function)
		{
			return;
		}
		
		for (TObjectIterator<ALidarPointCloudActor> It; It; ++It)
		{
			ALidarPointCloudActor* LidarActor = Cast<ALidarPointCloudActor>(*It);
			if(IsValid(LidarActor))
			{
				Function(LidarActor);
			}
		}
	}

	TArray<ALidarPointCloudActor*> GetSelectedActors()
	{
		TArray<ALidarPointCloudActor*> Actors;
		
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if(ALidarPointCloudActor* LidarActor = Cast<ALidarPointCloudActor>(*It))
			{
				Actors.Add(LidarActor);
			}
		}

		return Actors;
	}

	UWorld* GetFirstWorld()
	{
		for (TObjectIterator<ALidarPointCloudActor> It; It; ++It)
		{
			ALidarPointCloudActor* LidarActor = Cast<ALidarPointCloudActor>(*It);
			if(IsValid(LidarActor))
			{
				return LidarActor->GetWorld();
			}
		}
		return nullptr;
	}

	TArray<ULidarPointCloud*> GetSelectedClouds()
	{
		TArray<ULidarPointCloud*> PointClouds;
		
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if(ALidarPointCloudActor* LidarActor = Cast<ALidarPointCloudActor>(*It))
			{
				if(ULidarPointCloud* PointCloud = LidarActor->GetPointCloud())
				{
					PointClouds.Add(PointCloud);
				}
			}
		}

		return PointClouds;
	}

	ALidarPointCloudActor* SpawnActor()
	{
		ALidarPointCloudActor* NewActor = nullptr;
		if(UWorld* World = GetFirstWorld())
		{
			NewActor = Cast<ALidarPointCloudActor>(World->SpawnActor(ALidarPointCloudActor::StaticClass()));
		}
		return NewActor;
	}
	
	FSceneView* GetEditorView(FEditorViewportClient* ViewportClient)
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			ViewportClient->Viewport,
			ViewportClient->GetScene(),
			ViewportClient->EngineShowFlags).SetRealtimeUpdate(ViewportClient->IsRealtime()));
		return ViewportClient->CalcSceneView(&ViewFamily);
	}

	FConvexVolume BuildConvexVolumeForPoints(const TArray<FVector2D>& Points, FEditorViewportClient* ViewportClient)
	{
		if(!ViewportClient)
		{
			ViewportClient = GCurrentLevelEditingViewportClient;
		}
		
		FConvexVolume ConvexVolume;

		const FSceneView* View = GetEditorView(ViewportClient);
		const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();

		TArray<FVector> Origins; Origins.AddUninitialized(Points.Num() + 2);
		TArray<FVector> Normals; Normals.AddUninitialized(Points.Num() + 2);
		TArray<FVector> Directions; Directions.AddUninitialized(Points.Num());
		FVector MeanCenter = FVector::ZeroVector;

		for (int32 i = 0; i < Points.Num(); ++i)
		{
			FSceneView::DeprojectScreenToWorld(Points[i], FIntRect(FIntPoint(0, 0), ViewportClient->Viewport->GetSizeXY()), InvViewProjectionMatrix, Origins[i], Directions[i]);
			MeanCenter += Origins[i];
		}

		MeanCenter /= Points.Num();

		const FVector& ViewDirection = View->GetViewDirection();

		// Shared calculations
		Normals.Last(1) = ViewDirection;
		Normals.Last() = -ViewDirection;
		Origins.Last(1) = Origins[0] + ViewDirection * 99999999.0f;

		// Calculate plane normals
		const bool bFlipNormals = FVector::DotProduct(Normals[0], (MeanCenter - Origins[0])) > 0;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			Normals[i] = ((Origins[(i + 1) % Points.Num()] - Origins[i]).GetSafeNormal() ^ Directions[i]).GetSafeNormal();
			
			if (bFlipNormals)
			{
				Normals[i] = -Normals[i];
			}
		}

		// Perspective View
		if (View->IsPerspectiveProjection())
		{
			Origins.Last() = Origins[0];
		}
		// Ortho Views
		else
		{
			Origins.Last() = -Origins.Last(1);
		}

		for (int32 i = 0; i < Origins.Num(); ++i)
		{
			ConvexVolume.Planes.Emplace(Origins[i], Normals[i]);
		}

		ConvexVolume.Init();

		return ConvexVolume;
	}
	
	ULidarPointCloud* Extract_Internal()
	{
		int64 NumPoints = 0;
		ProcessAll([&NumPoints](ALidarPointCloudActor* Actor)
		{
			NumPoints += Actor->GetPointCloudComponent()->NumSelectedPoints();
		});

		TArray64<FLidarPointCloudPoint> SelectedPoints;
		SelectedPoints.Reserve(NumPoints);
		ProcessAll([&SelectedPoints](ALidarPointCloudActor* Actor)
		{
			Actor->GetPointCloudComponent()->GetSelectedPointsAsCopies(SelectedPoints);
		});
	
		ULidarPointCloud* NewPointCloud = CreateNewAsset_Internal();
		if (NewPointCloud)
		{
			NewPointCloud->SetData(SelectedPoints);
		}
		return NewPointCloud;
	}
	
	// Copied from GeomTools.cpp
	bool IsPolygonConvex(const TArray<FVector2D>& Points)
	{
		const int PointCount = Points.Num();
		float Sign = 0;
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const FVector2D& A = Points[PointIndex];
			const FVector2D& B = Points[(PointIndex + 1) % PointCount];
			const FVector2D& C = Points[(PointIndex + 2) % PointCount];
			float Det = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
			float DetSign = FMath::Sign(Det);
			if (DetSign != 0)
			{
				if (Sign == 0)
				{
					Sign = DetSign;
				}
				else if (Sign != DetSign)
				{
					return false;
				}
			}
		}

		return true;
	}
}

ULidarPointCloud* FLidarPointCloudEditorHelper::CreateNewAsset()
{
	return CreateNewAsset_Internal();
}

void FLidarPointCloudEditorHelper::AlignSelectionAroundWorldOrigin()
{
	ULidarPointCloud::AlignClouds(GetSelectedClouds());
}

void FLidarPointCloudEditorHelper::SetOriginalCoordinateForSelection()
{
	ProcessSelection([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->RestoreOriginalCoordinates();
		}
	});
}

void FLidarPointCloudEditorHelper::CenterSelection()
{
	ProcessSelection([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->CenterPoints();
		}
	});
}

void FLidarPointCloudEditorHelper::BuildCollisionForSelection()
{
	ProcessSelection([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->BuildCollision();
		}
	});
}

void FLidarPointCloudEditorHelper::SetCollisionErrorForSelection(float Error)
{
	ProcessSelection([Error](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			if(Error > 0)
			{
				PointCloud->MaxCollisionError = Error;
			}
			else
			{
				PointCloud->SetOptimalCollisionError();
			}
		}
	});
}

void FLidarPointCloudEditorHelper::RemoveCollisionForSelection()
{
	ProcessSelection([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->RemoveCollision();
		}
	});
}

void FLidarPointCloudEditorHelper::CalculateNormalsForSelection()
{
	ProcessSelection([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->CalculateNormals(nullptr, nullptr);
		}
	});
}

void FLidarPointCloudEditorHelper::SetNormalsQualityForSelection(int32 Quality, float NoiseTolerance)
{
	ProcessSelection([Quality, NoiseTolerance](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->NormalsQuality = Quality;
			PointCloud->NormalsNoiseTolerance = NoiseTolerance;
		}
	});
}

void FLidarPointCloudEditorHelper::ResetVisibility()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->UnhideAll();
		}
	});
}

void FLidarPointCloudEditorHelper::DeleteHidden()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->RemoveHiddenPoints();
		}
	});
}

void FLidarPointCloudEditorHelper::Extract()
{
	ULidarPointCloud* NewPointCloud = Extract_Internal();
	if(ALidarPointCloudActor* Actor = SpawnActor())
	{
		NewPointCloud->RestoreOriginalCoordinates();
		Actor->SetPointCloud(NewPointCloud);
	}

	DeleteSelected();
}

void FLidarPointCloudEditorHelper::ExtractAsCopy()
{
	ULidarPointCloud* NewPointCloud = Extract_Internal();
	if(ALidarPointCloudActor* Actor = SpawnActor())
	{
		NewPointCloud->RestoreOriginalCoordinates();
		Actor->SetPointCloud(NewPointCloud);
	}
	
	ClearSelection();
}

void FLidarPointCloudEditorHelper::CalculateNormals()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloud* PointCloud = Actor->GetPointCloud())
		{
			PointCloud->CalculateNormalsForSelection();
		}
	});
}

FConvexVolume FLidarPointCloudEditorHelper::BuildConvexVolumeFromCoordinates(FVector2d Start, FVector2d End, FEditorViewportClient* ViewportClient)
{
	FIntVector4 SelectionArea;
	SelectionArea.X = FMath::Min(Start.X, End.X);
	SelectionArea.Y = FMath::Min(Start.Y, End.Y);
	SelectionArea.Z = FMath::Max(Start.X, End.X);
	SelectionArea.W = FMath::Max(Start.Y, End.Y);

	return BuildConvexVolumeForPoints(TArray<FVector2D>({
			FVector2D(SelectionArea.X, SelectionArea.Y),
			FVector2D(SelectionArea.X, SelectionArea.W),
			FVector2D(SelectionArea.Z, SelectionArea.W),
			FVector2D(SelectionArea.Z, SelectionArea.Y) }), ViewportClient);
}

TArray<FConvexVolume> FLidarPointCloudEditorHelper::BuildConvexVolumesFromPoints(TArray<FVector2d> Points, FEditorViewportClient* ViewportClient)
{
	TArray<FConvexVolume> ConvexVolumes;
		
	if (IsPolygonConvex(Points))
	{
		ConvexVolumes.Add(BuildConvexVolumeForPoints(Points, ViewportClient));
	}
	else
	{
		// Check for self-intersecting shape
		if (!IsPolygonSelfIntersecting(Points, true))
		{
			TArray<TArray<FVector2D>> ConvexShapes;
			
			// The separation needs points in CCW order
			if (!FGeomTools2D::IsPolygonWindingCCW(Points))
			{
				Algo::Reverse(Points);
			}

			TArray<FVector2D> Triangles;
			FGeomTools2D::TriangulatePoly(Triangles, Points, false);
			FGeomTools2D::GenerateConvexPolygonsFromTriangles(ConvexShapes, Triangles);

			for (int32 i = 0; i < ConvexShapes.Num(); ++i)
			{
				ConvexVolumes.Add(BuildConvexVolumeForPoints(ConvexShapes[i], ViewportClient));
			}
		}
	}

	return ConvexVolumes;
}

FLidarPointCloudRay FLidarPointCloudEditorHelper::MakeRayFromScreenPosition(FVector2d Position, FEditorViewportClient* ViewportClient)
{
	if(!ViewportClient)
	{
		ViewportClient = GCurrentLevelEditingViewportClient;
	}
	
	const FSceneView* View = GetEditorView(ViewportClient);
	const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();

	FVector3d Origin, Direction;
	
	FSceneView::DeprojectScreenToWorld(Position, FIntRect(FIntPoint(0, 0), ViewportClient->Viewport->GetSizeXY()), InvViewProjectionMatrix, Origin, Direction);

	return FLidarPointCloudRay(Origin, Direction);
}

bool FLidarPointCloudEditorHelper::RayTracePointClouds(const FLidarPointCloudRay& Ray, float RadiusMulti, FVector3f& OutHitLocation)
{
	float MinDistance = FLT_MAX;
	
	ProcessAll([Ray, &MinDistance, &OutHitLocation, RadiusMulti](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent())
		{
			if(const ULidarPointCloud* PointCloud = Component->GetPointCloud())
			{
				const float TraceRadius = FMath::Max(PointCloud->GetEstimatedPointSpacing(), 0.5f) * RadiusMulti;
				if(const FLidarPointCloudPoint* Point = Component->LineTraceSingle(Ray, TraceRadius, true))
				{
					const FVector3f PointLocation = (FVector3f)(Component->GetComponentTransform().TransformPosition((FVector)Point->Location) + PointCloud->LocationOffset);
					
					const float DistanceSq = (Ray.Origin - PointLocation).SizeSquared();
					if(DistanceSq < MinDistance)
					{
						MinDistance = DistanceSq;
						OutHitLocation = PointLocation;
					}
				}
			}
		}
	});

	return MinDistance < FLT_MAX;
}

bool FLidarPointCloudEditorHelper::IsPolygonSelfIntersecting(const TArray<FVector2D>& Points, bool bAllowLooping)
{
	// Slow, O(n2), but sufficient for the current problem
	
	const int32 MaxIndex = bAllowLooping ? Points.Num() : Points.Num() - 1;

	for (int32 i = 0; i < MaxIndex; ++i)
	{
		const int32 i1 = (i + 1) % Points.Num();

		const FVector2D P1 = Points[i];
		const FVector2D P2 = Points[i1];

		for (int32 j = 0; j < MaxIndex; ++j)
		{
			const int32 j1 = j < Points.Num() - 1 ? j + 1 : 0;

			if (j1 != i && j != i && j != i1)
			{
				// Modified FMath::SegmentIntersection2D
				// Inlining the code and skipping calculation of an intersection point makes a slight difference for O(n2)
				const FVector2D SegmentStartA = P1;
				const FVector2D SegmentEndA = P2;
				const FVector2D SegmentStartB = Points[j];
				const FVector2D SegmentEndB = Points[j1];
				const FVector2D VectorA = P2 - SegmentStartA;
				const FVector2D VectorB = SegmentEndB - SegmentStartB;

				const float S = (-VectorA.Y * (SegmentStartA.X - SegmentStartB.X) + VectorA.X * (SegmentStartA.Y - SegmentStartB.Y)) / (-VectorB.X * VectorA.Y + VectorA.X * VectorB.Y);
				if (S >= 0 && S <= 1)
				{
					const float T = (VectorB.X * (SegmentStartA.Y - SegmentStartB.Y) - VectorB.Y * (SegmentStartA.X - SegmentStartB.X)) / (-VectorB.X * VectorA.Y + VectorA.X * VectorB.Y);
					if (T >= 0 && T <= 1)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

void FLidarPointCloudEditorHelper::SelectPointsByConvexVolume(const FConvexVolume& ConvexVolume, ELidarPointCloudSelectionMode SelectionMode)
{
	ProcessAll([ConvexVolume, SelectionMode](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent())
		{
			Component->SelectByConvexVolume(ConvexVolume, SelectionMode != ELidarPointCloudSelectionMode::Subtract, true);
		}
	});
}

void FLidarPointCloudEditorHelper::SelectPointsBySphere(FSphere Sphere, ELidarPointCloudSelectionMode SelectionMode)
{
	ProcessAll([Sphere, SelectionMode](ALidarPointCloudActor* Actor)
	{
		if(ULidarPointCloudComponent* Component = Actor->GetPointCloudComponent())
		{
			Component->SelectBySphere(Sphere, SelectionMode != ELidarPointCloudSelectionMode::Subtract, true);
		}
	});
}

void FLidarPointCloudEditorHelper::HideSelected()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		Actor->GetPointCloudComponent()->HideSelected();
	});
}

void FLidarPointCloudEditorHelper::DeleteSelected()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		Actor->GetPointCloudComponent()->DeleteSelected();
	});
}

void FLidarPointCloudEditorHelper::InvertSelection()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		Actor->GetPointCloudComponent()->InvertSelection();
	});
}

void FLidarPointCloudEditorHelper::ClearSelection()
{
	ProcessAll([](ALidarPointCloudActor* Actor)
	{
		Actor->GetPointCloudComponent()->ClearSelection();
	});
}

void FLidarPointCloudEditorHelper::MergeLidar(ULidarPointCloud* TargetAsset, TArray<ULidarPointCloud*> SourceAssets)
{
	if(!IsValid(TargetAsset) || SourceAssets.Num() == 0)
	{
		return;
	}
	
	FScopedSlowTask ProgressDialog(SourceAssets.Num() + 2, LOCTEXT("Merge", "Merging Point Clouds..."));
	ProgressDialog.MakeDialog();

	TargetAsset->Merge(SourceAssets, [&ProgressDialog]() { ProgressDialog.EnterProgressFrame(1.f); });

	FAssetRegistryModule::AssetCreated(TargetAsset);
	TargetAsset->MarkPackageDirty();
}

void FLidarPointCloudEditorHelper::MergeSelectionByData(bool bReplaceSource)
{
	TArray<ALidarPointCloudActor*> Actors = GetSelectedActors();
	TArray<ULidarPointCloud*> PointClouds = GetSelectedClouds();
	
	if (PointClouds.Num() > 1)
	{
		ULidarPointCloud* NewCloud = CreateNewAsset();
		
		MergeLidar(NewCloud, PointClouds);

		if(bReplaceSource)
		{
			// Repurpose the first actor
			Actors[0]->SetPointCloud(NewCloud);
			
			// Remove the rest
			for(int32 i = 1; i < Actors.Num(); ++i)
			{
				Actors[i]->Destroy();
			}
		}
		else
		{
			ALidarPointCloudActor* NewActor = Actors[0]->GetWorld()->SpawnActor<ALidarPointCloudActor>(Actors[0]->GetActorLocation(), Actors[0]->GetActorRotation());
			NewActor->SetPointCloud(NewCloud);
		}
	}
}

void FLidarPointCloudEditorHelper::MergeSelectionByComponent(bool bReplaceSource)
{
	TArray<ALidarPointCloudActor*> Actors = GetSelectedActors();

	if (Actors.Num() > 1)
	{
		AActor* TargetActor = Actors[0]->GetWorld()->SpawnActor<ALidarPointCloudActor>();
		
		for(ALidarPointCloudActor* Actor : Actors)
		{
			TArray<ULidarPointCloudComponent*> Components;
			Actor->GetComponents(Components);

			for(const ULidarPointCloudComponent* Component : Components)
			{
				ULidarPointCloudComponent* PCC = (ULidarPointCloudComponent*)TargetActor->AddComponentByClass(ULidarPointCloudComponent::StaticClass(), true, Component->GetComponentTransform(), false);
				PCC->SetPointCloud(Component->GetPointCloud());
				PCC->SetWorldTransform(Component->GetComponentTransform());
				PCC->AttachToComponent(TargetActor->GetRootComponent(), FAttachmentTransformRules(EAttachmentRule::KeepWorld, false));
			}

			if(bReplaceSource)
			{
				Actor->Destroy();
			}
			else
			{
				Actor->SetHidden(true);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
