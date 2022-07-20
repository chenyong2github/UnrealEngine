// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Vector.h"
#include "ChaosCheck.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/DynamicParticles.h"
#include "Chaos/UniformGrid.h"
#include "Algo/Unique.h"

namespace Chaos {

template<class T>
class TMPMTransfer
{
public:

	uint32 NPerSec = 2 * 2;
	uint32 NPerEle = 2 * 2 * 2;
	uint32 NTransfer = 3;
	int32 NumCells;

	//TODO(Yizhou): Think whether mpm transfer should just own the grid
	//(Is the grid used by the constraint as well?)
	TMPMGrid<T>& Grid;

	TArray<TVector<T, 3>> Weights;
	TArray<TVector<int32, 3>> Indices;
	TArray<TArray<int32>> CellData; //CellData[i] registers which particles are in ith cell
	//TArray<TArray<int32>> CellColors;

	//meta data for grid based cons
	TArray<TArray<int32>> ElementGridNodes;
	TArray<TArray<T>> ElementGridNodeWeights;
	TArray<TArray<TArray<int32>>> ElementGridNodeIncidentElements;


	TMPMTransfer(){}
	TMPMTransfer(TMPMGrid<T>& _Grid) :Grid(_Grid) 
	{
		ensure(Grid.NPerDir == 2 || Grid.NPerDir == 3);
		NPerSec = Grid.NPerDir * Grid.NPerDir;
		NPerEle = NPerSec * Grid.NPerDir;
		//CellColors.SetNum(NPerEle);
	}

	//template <typename ReorderFunctor, typename SplatFunctor>
	//This one does an initial splat of momentum and mass to the grid. 
	//One can add splat functor in the future passibly for other kinds of splat
	void InitialP2G(const TDynamicParticles<T, 3>& InParticles, TArray<T>& GridData) 
	{
		int32 N = InParticles.Size();

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosMPMTransferInitialBinning"));
			Indices.SetNum(N);
			Weights.SetNum(N);

			/////////////////////////
			// compute weights and bin
			/////////////////////////

			//TODO(Yizhou): Do a timing and determine whether the following loop should 
			//use physics parallel for:
			for (int32 p = 0; p < N; p++)
			{
				Grid.BaseNodeIndex(InParticles.X(p), Indices[p], Weights[p]);
			}

		}
		/////////////////////
		// Computes which particles in the same cell
		/////////////////////
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosMPMTransferCellMetaCalc"));
			NumCells = Grid.Size();
			CellData.SetNum(NumCells);
			for (int32 c = 0; c < NumCells; c++)
			{
				CellData[c].SetNum(0);
			}
			for (int32 p = 0; p < N; p++)
			{
				CellData[Grid.FlatIndex(Indices[p])].Emplace(p);
			}
		}

		/////////////////////
		// splat data to cells
		/////////////////////
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("STAT_ChaosMPMTransferSplatData"));
			GridData.Init((T)0., CellData.Num() * (NTransfer + 1));

			TVector<int32, 3> GridCells = Grid.GetCells();
			
			for (int32 ii = 0; ii < int32(Grid.NPerDir); ii++)
			{
				for (int32 jj = 0; jj < int32(Grid.NPerDir); jj++)
				{
					for (int32 kk = 0; kk < int32(Grid.NPerDir); kk++)
					{
						int32 CurrentLocIndex = ii * NPerSec + jj * Grid.NPerDir + kk;
						PhysicsParallelFor(GridCells[0] / Grid.NPerDir, [&](const int32 iii)
							{
								for (int32 jjj = 0; jjj < int32(GridCells[1] / Grid.NPerDir); jjj++)
								{
									for (int32 kkk = 0; kkk < int32(GridCells[2] / Grid.NPerDir); kkk++)
									{
										TVector<int32, 3> MultiIndex = { iii * int32(Grid.NPerDir) + ii, jjj * int32(Grid.NPerDir) + jj, kkk * int32(Grid.NPerDir) + kk};
										int32 CellIndex = Grid.FlatIndex(MultiIndex);
										P2GApplyHelper(InParticles, CellIndex, GridData);
									}
								}
						});

					}
				}
			}

		}
	
	}

	//currently only splats mass and momentum
	void P2GApplyHelper(const TDynamicParticles<T, 3>& InParticles, const int32 CellIndex, TArray<T>& GridData)
	{
		//check if valid cell:
		if (CellIndex < NumCells && CellData[CellIndex].Num() > 0)
		{
			for (int32 i = 0; i < CellData[CellIndex].Num(); i++)
			{
				int32 p = CellData[CellIndex][i];
				for (int iii = 0; iii < int32(Grid.NPerDir); iii++)
				{
					T Nii = Grid.Nijk(Weights[p][0], iii);
					for (int jjj = 0; jjj < int32(Grid.NPerDir); jjj++)
					{
						T Njj = Grid.Nijk(Weights[p][1], jjj);
						for (int kkk = 0; kkk < int32(Grid.NPerDir); kkk++)
						{
							TVector<int32, 3> LocIndex = { iii, jjj, kkk };
							TVector<int32, 3> GlobMultiIndex = Grid.Loc2GlobIndex(Indices[p], LocIndex);
							int32 GlobIndex = Grid.FlatIndex(GlobMultiIndex);
							T Nkk = Grid.Nijk(Weights[p][2], kkk);
							T NProd = Nii * Njj * Nkk;
							GridData[(NTransfer + 1) * GlobIndex] += NProd * InParticles.M(p);
							for (int32 alpha = 0; alpha < 3; alpha++)
							{
								GridData[(NTransfer + 1) * GlobIndex + alpha + 1] += NProd * InParticles.M(p) * InParticles.V(p)[alpha];
							}

						}
					}
				}
			}
		}
	}

	void ComputeElementMetaData(const TArray<TVector<int32, 4>>& InMesh)
	{
		ElementGridNodes.SetNum(InMesh.Num());
		ElementGridNodeWeights.SetNum(InMesh.Num());
		ElementGridNodeIncidentElements.SetNum(InMesh.Num());
		//TODO(Yizhou): make following loop parallel for with appropriate bool condition
		for (int32 e = 0; e < InMesh.Num(); e++)
		{
			ElementGridNodes[e].SetNum(NPerEle * 4);
			ElementGridNodeWeights[e].SetNum(NPerEle * 4);
			for (int32 ie = 0; ie < 4; ie++)
			{
				int32 p = InMesh[e][ie];
				TVector<int32, 3> Index = Indices[p];
				for (int32 ii = 0; ii < Grid.NPerDir; ii++)
				{
					T Nii = Grid.Nijk(Weights[p][0], ii);
					for (int32 jj = 0; jj < Grid.NPerDir; jj++)
					{
						T Njj = Grid.Nijk(Weights[p][1], jj);
						for (int32 kk = 0; kk < Grid.NPerDir; kk++)
						{
							T Nkk = Grid.Nijk(Weights[p][2], kk);
							TVector<int32, 3> LocIndex = { ii, jj, kk };
							TVector<int32, 3> GlobIndex = Grid.Loc2GlobIndex(Index, LocIndex);
							int32 GlobIndexFlat = Grid.FlatIndex(GlobIndex);
							ElementGridNodes[e][ie * NPerEle + ii * NPerSec + jj * Grid.NPerDir + kk] = GlobIndexFlat;
							ElementGridNodeWeights[e][ie * NPerEle + ii * NPerSec + jj * Grid.NPerDir + kk] = Nii * Njj * Nkk;
						}
					}
				}
			}
			ComputeIncidentElements(ElementGridNodes[e], ElementGridNodeIncidentElements[e]);
		}
	}

	//computes incident elements in serial
	static void ComputeIncidentElements(const TArray<int32>& ArrayIn, TArray<TArray<int32>>& IncidentElements) 
	{
		TArray<int32> Ordering, Ranges;
		Ordering.SetNum(ArrayIn.Num());
		Ranges.SetNum(ArrayIn.Num()+1);

		for (int32 i = 0; i < ArrayIn.Num(); ++i) 
		{
			Ordering[i] = i;
			Ranges[i] = i;
		}
		
		//TODO(Yizhou): decide to use sort or heap sort or merge sort. 
		Ordering.Sort([&ArrayIn](const int32 A, const int32 B) 
		{
		return ArrayIn[A] < ArrayIn[B];
		});

		int32 Last = Algo::Unique(Ranges, [&ArrayIn, &Ordering](int32 i, int32 j) { return ArrayIn[Ordering[i]] == ArrayIn[Ordering[j]]; });

		int32 NumNodes = Last - 1;
		Ranges[NumNodes] = ArrayIn.Num();
		Ranges.SetNum(NumNodes + 1);

		IncidentElements.SetNum(Ranges.Num() - 1);

		for (int32 p = 0; p < IncidentElements.Num(); ++p) 
		{
			IncidentElements[p].SetNum(Ranges[p + 1] - Ranges[p]);
			for (int32 e = Ranges[p]; e < Ranges[p + 1]; e++) 
			{
				IncidentElements[p][e - Ranges[p]] = Ordering[e];
			}
		}
	}

	


};

}  
