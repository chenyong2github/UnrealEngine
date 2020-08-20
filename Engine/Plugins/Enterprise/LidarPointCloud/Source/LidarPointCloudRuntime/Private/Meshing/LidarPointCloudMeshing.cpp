// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "Meshing/LidarPointCloudMeshing.h"
#include "LidarPointCloudOctree.h"
#include "LidarPointCloudOctreeMacros.h"
#include "Async/Async.h"
#include "Async/Future.h"

FORCEINLINE uint64 CalculateNumPermutations(int32 NumPoints)
{
	return NumPoints > 4000000 ? UINT64_MAX : NumPoints * (NumPoints - 1) * (NumPoints - 2) / 6;
}

void LidarPointCloudMeshing::CalculateNormals(FLidarPointCloudOctree* Octree, FThreadSafeBool* bCancelled, int32 Quality, float Tolerance, TArray64<FLidarPointCloudPoint*>& InPointSelection)
{
	/** Groups sampling information together for readability */
	struct FSamplingUnit
	{
		FVector Center;
		FVector Extent;
		TArray64<FLidarPointCloudPoint*> Points;
		FLidarPointCloudOctreeNode* Node;

		FSamplingUnit(const FVector& Center, const FVector& Extent, FLidarPointCloudOctreeNode* Node)
			: Center(Center)
			, Extent(Extent)
			, Node(Node)
		{
		}

		FSamplingUnit* ConstructChildAtLocation(int32 i)
		{
			return new FSamplingUnit(Center + Extent * (FVector(-0.5f) + FVector((i & 4) == 4, (i & 2) == 2, (i & 1) == 1)), Extent / 2, Node ? Node->GetChildNodeAtLocation(i) : nullptr);
		}
	};

	int32 DesiredNumIterations = Quality;

	const FLidarPointCloudNormal UpNormal = FVector::UpVector;

	TArray64<int32> Indices;

	TQueue<FSamplingUnit*> Q;
	{
		FSamplingUnit* Root = new FSamplingUnit(FVector::ZeroVector, Octree->SharedData[0].Extent, &Octree->Root);
		if (InPointSelection.Num() == 0)
		{
			Octree->GetPoints(Root->Points);
		}
		else
		{
			Root->Points = InPointSelection;
		}
		Q.Enqueue(Root);
	}
	FSamplingUnit* SamplingUnit;
	while ((!bCancelled || !*bCancelled) && Q.Dequeue(SamplingUnit))
	{
		while (SamplingUnit->Points.Num() >= 3)
		{
			// Find Most Probable Plane
			FPlane BestPlane(EForceInit::ForceInit);
			{
				TArray64<FLidarPointCloudPoint*>* Points;
				bool bDestroyArray = false;

				// If the sampling unit is attached to an existing node, use its grid-allocated points to pick random models from - much more accurate and faster
				if (SamplingUnit->Node)
				{
					Points = new TArray64<FLidarPointCloudPoint*>();
					Points->Reserve(SamplingUnit->Node->GetNumPoints());
					FOR(Point, SamplingUnit->Node)
					{
						if (!Point->Normal.IsValid())
						{
							Points->Add(Point);
						}
					}

					// This is a temporary array - need to destroy it after it's used
					bDestroyArray = true;
				}
				// ... otherwise, just use whatever points are left
				else
				{
					Points = &SamplingUnit->Points;
				}

				TMap<FIntVector, uint32> PlaneModels;
				FIntVector CurrentModel;

				TArray64<FLidarPointCloudPoint*>& SelectedPoints = *Points;

				const int32 NumPoints = SelectedPoints.Num();
				const int32 MaxPointIndex = NumPoints - 1;
				const uint32 ConfidenceThreshold = NumPoints * 0.8f; // We are confident at 80% consensus
				const uint32 ValidThreshold = NumPoints / 2; // We need at least 50% for consensus

				Indices.Reset(Points->Num());
				for (int32 i = 0; i < NumPoints; ++i)
				{
					Indices.Add(i);
				}

				const int32 NumIterations = FMath::Min((uint64)DesiredNumIterations, CalculateNumPermutations(NumPoints));				
				for (int32 i = 0; i < NumIterations; ++i)
				{
					// Find Random Model
					do
					{
						int32 A = FMath::RandRange(0, MaxPointIndex);
						int32 B = FMath::RandRange(0, MaxPointIndex - 1);
						int32 C = FMath::RandRange(0, MaxPointIndex - 2);
						int32 X = Indices[A];
						Indices.RemoveAtSwap(A, 1, false);
						int32 Y = Indices[B];
						Indices.RemoveAtSwap(B, 1, false);
						int32 Z = Indices[C];
						Indices.Add(X);
						Indices.Add(Y);

						if (X > Y)
						{
							if (X > Z)
							{
								CurrentModel.X = X;
								CurrentModel.Y = Y > Z ? Y : Z;
								CurrentModel.Z = Y > Z ? Z : Y;
							}
							else
							{
								CurrentModel.X = Z;
								CurrentModel.Y = X;
								CurrentModel.Z = Y;
							}
						}
						else
						{
							if (Y > Z)
							{
								CurrentModel.X = Y;
								CurrentModel.Y = X > Z ? X : Z;
								CurrentModel.Z = X > Z ? Z : X;
							}
							else
							{
								CurrentModel.X = Z;
								CurrentModel.Y = Y;
								CurrentModel.Z = X;
							}
						}
					} while (PlaneModels.Find(CurrentModel));

					const FPlane Plane(SelectedPoints[CurrentModel.X]->Location, SelectedPoints[CurrentModel.Y]->Location, SelectedPoints[CurrentModel.Z]->Location);

					// Count Inner Points
					uint32 NumInnerPoints = 0;
					for (FLidarPointCloudPoint** Point = SelectedPoints.GetData(), **DataEnd = Point + NumPoints; Point != DataEnd; ++Point)
					{
						if (FMath::Abs(Plane.PlaneDot((*Point)->Location)) <= Tolerance)
						{
							++NumInnerPoints;
						}
					}

					// Confidence is high enough, we can stop here
					if (NumInnerPoints >= ConfidenceThreshold)
					{
						BestPlane = Plane;
						break;
					}

					PlaneModels.Emplace(CurrentModel, NumInnerPoints);
				}

				// If the best plane has not been found yet, pick the highest scoring one
				if (BestPlane.W == 0)
				{
					CurrentModel = FIntVector::NoneValue;

					// Anything with less points then the Valid Threshold will not be considered
					uint32 NumInnerPoints = ValidThreshold;
					for (TPair<FIntVector, uint32>& Model : PlaneModels)
					{
						if (Model.Value > NumInnerPoints)
						{
							CurrentModel = Model.Key;
							NumInnerPoints = Model.Value;
						}
					}

					BestPlane = CurrentModel != FIntVector::NoneValue ? FPlane(SelectedPoints[CurrentModel.X]->Location, SelectedPoints[CurrentModel.Y]->Location, SelectedPoints[CurrentModel.Z]->Location) : FPlane(EForceInit::ForceInit);
				}

				// If the points array was created temporarily, destroy it
				if (bDestroyArray)
				{
					delete Points;
					Points = nullptr;
				}
			}

			bool bSuccess = false;

			if (BestPlane.W != 0)
			{
				const FLidarPointCloudNormal Normal = BestPlane;

				int32 PointCount = SamplingUnit->Points.Num();

				// Apply Normals
				for (FLidarPointCloudPoint** DataStart = SamplingUnit->Points.GetData(), **Point = DataStart, **DataEnd = Point + SamplingUnit->Points.Num(); Point != DataEnd; ++Point)
				{
					if (FMath::Abs(BestPlane.PlaneDot((*Point)->Location)) <= Tolerance)
					{
						(*Point)->Normal = Normal;
						--DataEnd;
						SamplingUnit->Points.RemoveAtSwap(Point-- - DataStart, 1, false);
					}
				}

				bSuccess = (PointCount - SamplingUnit->Points.Num()) > 0;
			}

			if(!bSuccess)
			{
				FSamplingUnit* Sublevels[8];
				for (int32 i = 0; i < 8; ++i)
				{
					Sublevels[i] = SamplingUnit->ConstructChildAtLocation(i);
				}

				// Build and enqueue sub-levels
				for (FLidarPointCloudPoint** Point = SamplingUnit->Points.GetData(), **DataEnd = Point + SamplingUnit->Points.Num(); Point != DataEnd; ++Point)
				{
					Sublevels[((*Point)->Location.X > SamplingUnit->Center.X ? 4 : 0) + ((*Point)->Location.Y > SamplingUnit->Center.Y ? 2 : 0) + ((*Point)->Location.Z > SamplingUnit->Center.Z)]->Points.Add(*Point);
				}

				SamplingUnit->Points.Empty();

				// Enqueue
				for (int32 i = 0; i < 8; ++i)
				{
					if (Sublevels[i]->Points.Num() > 0)
					{
						Q.Enqueue(Sublevels[i]);
					}
					else
					{
						delete Sublevels[i];
					}
				}
			}
		}

		// To any stray points left simply apply Up Vector
		for (FLidarPointCloudPoint* P : SamplingUnit->Points)
		{
			P->Normal = UpNormal;
		}

		// Delete when finished processing
		delete SamplingUnit;
		SamplingUnit = nullptr;
	}
}
