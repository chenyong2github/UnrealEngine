// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayND.h"
#include "Chaos/Box.h"
#include "Chaos/ImplicitObject.h"
#include "Chaos/Particles.h"
#include "Chaos/UniformGrid.h"

namespace Chaos { class FErrorReporter; }

namespace Chaos
{

template<class T>
class TTriangleMesh;

template<class T, int D>
class TPlane;

template<class T, int d>
class CHAOS_API TLevelSet final : public FImplicitObject
{
  public:
	using FImplicitObject::SignedDistance;

	TLevelSet(FErrorReporter& ErrorReporter, const TUniformGrid<T, d>& InGrid, const TParticles<T, d>& InParticles, const TTriangleMesh<T>& Mesh, const int32 BandWidth = 0);
	TLevelSet(FErrorReporter& ErrorReporter, const TUniformGrid<T, d>& InGrid, const FImplicitObject& InObject, const int32 BandWidth = 0, const bool bUseObjectPhi = false);
	TLevelSet(std::istream& Stream);
	TLevelSet(const TLevelSet<T, d>& Other) = delete;
	TLevelSet(TLevelSet<T, d>&& Other);
	virtual ~TLevelSet();

	void Write(std::ostream& Stream) const;
	virtual T PhiWithNormal(const TVector<T, d>& x, TVector<T, d>& Normal) const override;
	T SignedDistance(const TVector<T, d>& x) const;

	virtual const TAABB<T, d>& BoundingBox() const override { return MOriginalLocalBoundingBox; }

	// Returns a const ref to the underlying phi grid
	const TArrayND<T, d>& GetPhiArray() const { return MPhi; }

	// Returns a const ref to the underlying grid of normals
	const TArrayND<TVector<T, d>, d>& GetNormalsArray() const { return MNormals; }

	// Returns a const ref to the underlying grid structure
	const TUniformGrid<T, d>& GetGrid() const { return MGrid; }

	FORCEINLINE void Shrink(const T Value)
	{
		for (int32 i = 0; i < MGrid.Counts().Product(); ++i)
		{
			MPhi[i] += Value;
		}
	}

	FORCEINLINE static constexpr EImplicitObjectType StaticType()
	{
		return ImplicitObjectType::LevelSet;
	}

	FORCEINLINE void SerializeImp(FArchive& Ar)
	{
		FImplicitObject::SerializeImp(Ar);
		Ar << MGrid;
		Ar << MPhi;
		Ar << MNormals;
		TBox<FReal, 3>::SerializeAsAABB(Ar, MLocalBoundingBox);
		TBox<FReal, 3>::SerializeAsAABB(Ar, MOriginalLocalBoundingBox);
		Ar << MBandWidth;
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		SerializeImp(Ar);
	}

	virtual void Serialize(FArchive& Ar) override
	{
		SerializeImp(Ar);
	}

	bool ComputeMassProperties(T& OutVolume, TVector<T, d>& OutCOM, PMatrix<T,d,d>& OutInertia, TRotation<T, d>& OutRotationOfMass) const;

	T ComputeLevelSetError(const TParticles<T, d>& InParticles, const TArray<TVector<T, 3>>& Normals, const TTriangleMesh<T>& Mesh, T& AngleError, T& MaxDistError);

	// Output a mesh and level set as obj files
	void OutputDebugData(FErrorReporter& ErrorReporter, const TParticles<T, d>& InParticles, const TArray<TVector<T, 3>>& Normals, const TTriangleMesh<T>& Mesh, const FString FileName);

	bool CheckData(FErrorReporter& ErrorReporter, const TParticles<T, d>& InParticles, const TTriangleMesh<T>& Mesh, const TArray<TVector<T, 3>> &Normals);

	virtual uint32 GetTypeHash() const override
	{
		uint32 Result = 0;

		const int32 NumValues = MPhi.Num();

		for(int32 Index = 0; Index < NumValues; ++Index)
		{
			Result = HashCombine(Result, ::GetTypeHash(MPhi[Index]));
		}

		return Result;
	}

  private:
	bool ComputeDistancesNearZeroIsocontour(FErrorReporter& ErrorReporter, const TParticles<T, d>& InParticles, const TArray<TVector<T, 3>> &Normals, const TTriangleMesh<T>& Mesh, TArrayND<bool, d>& BlockedFaceX, TArrayND<bool, d>& BlockedFaceY, TArrayND<bool, d>& BlockedFaceZ, TArray<TVector<int32, d>>& InterfaceIndices);
	void ComputeDistancesNearZeroIsocontour(const FImplicitObject& Object, const TArrayND<T, d>& ObjectPhi, TArray<TVector<int32, d>>& InterfaceIndices);
	void CorrectSign(const TArrayND<bool, d>& BlockedFaceX, const TArrayND<bool, d>& BlockedFaceY, const TArrayND<bool, d>& BlockedFaceZ, TArray<TVector<int32, d>>& InterfaceIndices);
	T ComputePhi(const TArrayND<bool, d>& Done, const TVector<int32, d>& CellIndex);
	void FillWithFastMarchingMethod(const T StoppingDistance, const TArray<TVector<int32, d>>& InterfaceIndices);
	void FloodFill(const TArrayND<bool, d>& BlockedFaceX, const TArrayND<bool, d>& BlockedFaceY, const TArrayND<bool, d>& BlockedFaceZ, TArrayND<int32, d>& Color, int32& NextColor);
	void FloodFillFromCell(const TVector<int32, d> CellIndex, const int32 NextColor, const TArrayND<bool, d>& BlockedFaceX, const TArrayND<bool, d>& BlockedFaceY, const TArrayND<bool, d>& BlockedFaceZ, TArrayND<int32, d>& Color);
	bool IsIntersectingWithTriangle(const TParticles<T, d>& Particles, const TVector<int32, 3>& Elements, const TPlane<T, d>& TrianglePlane, const TVector<int32, d>& CellIndex, const TVector<int32, d>& PrevCellIndex);
	void ComputeNormals();
	void ComputeConvexity(const TArray<TVector<int32, d>>& InterfaceIndices);
	
	void ComputeNormals(const TParticles<T, d>& InParticles, const TTriangleMesh<T>& Mesh, const TArray<TVector<int32, d>>& InterfaceIndices);

	TUniformGrid<T, d> MGrid;
	TArrayND<T, d> MPhi;
	TArrayND<TVector<T, d>, d> MNormals;
	TAABB<T, d> MLocalBoundingBox;
	TAABB<T, d> MOriginalLocalBoundingBox;
	int32 MBandWidth;
private:
	TLevelSet() : FImplicitObject(EImplicitObject::HasBoundingBox, ImplicitObjectType::LevelSet) {}	//needed for serialization
	friend FImplicitObject;	//needed for serialization
};
}
