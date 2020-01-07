// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIWarpTexture.h"
#include "MPCDIData.h"
#include "MPCDIHelpers.h"

THIRD_PARTY_INCLUDES_START

#include "mpcdiProfile.h"
#include "mpcdiReader.h"
#include "mpcdiDisplay.h"
#include "mpcdiBuffer.h"
#include "mpcdiRegion.h"
#include "mpcdiAlphaMap.h"
#include "mpcdiBetaMap.h"
#include "mpcdiDistortionMap.h"
#include "mpcdiGeometryWarpFile.h"
#include "mpcdiPFM.h"

THIRD_PARTY_INCLUDES_END

// Select mpcdi frustum calc method
static TAutoConsoleVariable<int32> CVarMPCDIFrustumMethod(
	TEXT("nDisplay.render.mpcdi.Frustum"),
	(int)MPCDI::EFrustumType::TextureBOX,
	TEXT("Frustum computation method:\n")
	TEXT(" 0: mesh AABB based, lower quality but fast\n")
	TEXT(" 1: mesh vertices based, best quality but slow\n")
	TEXT(" 2: texture box, get A*B distributed points from texture, fast, good quality for flat panels\n"),
	ECVF_RenderThreadSafe
);

// Select mpcdi stereo mode
static TAutoConsoleVariable<int32> CVarMPCDIStereoMode(
	TEXT("nDisplay.render.mpcdi.StereoMode"),
	(int)MPCDI::EStereoMode::AsymmetricAABB,
	TEXT("Stereo mode:\n")
	TEXT(" 0: Asymmetric to AABB center\n")
	TEXT(" 1: Symmetric to AABB center\n"),
	ECVF_RenderThreadSafe
);

// Select mpcdi projection mode
static TAutoConsoleVariable<int32> CVarMPCDIProjectionMode(
	TEXT("nDisplay.render.mpcdi.Projection"),
	(int)MPCDI::EProjectionType::StaticSurfaceNormal,
	TEXT("Projection method:\n")
	TEXT(" 0: Static, aligned to average region surface normal\n")
	TEXT(" 1: Static, aligned to average region surface corners plane\n")
	TEXT(" 2: Dynamic, to view target center\n")
	TEXT(" 3: Dynamic, aligned to cave axis, eye is zero point\n"),
	ECVF_RenderThreadSafe
);

// Allow frustum projection mode runtime auto-change for invalid view planes
static TAutoConsoleVariable<int32> CVarMPCDIProjectionAuto(
	TEXT("nDisplay.render.mpcdi.ProjectionAuto"),
	1, // Default on
	TEXT("Frustum method runtime changed for bad view planes.\n")
	TEXT("(1) Enable\n"),
	ECVF_RenderThreadSafe
);

// Setup frustum projection cache
static TAutoConsoleVariable<int32> CVarMPCDIFrustumCacheDepth(
	TEXT("nDisplay.render.mpcdi.cache_depth"),
	0, // By default cache is disabled (to better performance for EFrustumType::PerfectCPU set value to 8)
	TEXT("Frustum calculated values cache depth\n"),
	ECVF_RenderThreadSafe
);
static TAutoConsoleVariable<float> CVarMPCDIFrustumCachePrecision(
	TEXT("nDisplay.render.mpcdi.cache_precision"),
	0.1f, // 1mm
	TEXT("Frustum cache values comparison precision (float, unit is sm)\n"),
	ECVF_RenderThreadSafe
);

namespace // Helpers
{

	FMatrix GetProjectionMatrixAssymetric(const IMPCDI::FFrustum::FAngles& ProjectionAngles, float zNear, float zFar)
	{
		const float l = ProjectionAngles.Left;
		const float r = ProjectionAngles.Right;
		const float t = ProjectionAngles.Top;
		const float b = ProjectionAngles.Bottom;

		return DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(l, r, t, b, zNear, zFar);
	}

	FMatrix GetProjectionMatrixAssymetricFromFrustum(float LeftAngle, float RightAngle, float TopAngle, float BottomAngle, float ZNear, float ZFar)
	{
		return DisplayClusterHelpers::math::GetProjectionMatrixFromAngles(LeftAngle, RightAngle, TopAngle, BottomAngle, ZNear, ZFar);
	}
};

namespace MPCDI
{
	inline bool CalcFrustumFromVertex(const FVector4& PFMVertice, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right)
	{
		bool bResult = true;

		if (PFMVertice.W > 0)
		{
			FVector4 PrjectedVertice = World2Local.TransformFVector4(PFMVertice);

			float Scale = 1.0f / PrjectedVertice.X;
			if (Scale <= 0)
			{
				// This point out of view plane
				bResult = false;
				return bResult;
			}

			// Use only points over view plane, ignore backside pts
			PrjectedVertice.Y *= Scale;
			PrjectedVertice.Z *= Scale;

			if (PrjectedVertice.Z > Top)
			{
				Top = PrjectedVertice.Z;
			}

			if (PrjectedVertice.Z < Bottom)
			{
				Bottom = PrjectedVertice.Z;
			}

			if (PrjectedVertice.Y > Right)
			{
				Right = PrjectedVertice.Y;
			}

			if (PrjectedVertice.Y < Left)
			{
				Left = PrjectedVertice.Y;
			}
		}

		return bResult;
	}
	
	struct FValidPFMPoint
	{
		int X;
		int Y;

		FValidPFMPoint(const FVector4* InData, int InW, int InH)
			: Data(InData)
			, W(InW)
			, H(InH)
		{ }

		inline int GetSavedPointIndex()
		{
			return GetPointIndex(X, Y);
		}

		inline int GetPointIndex(int InX, int InY) const
		{
			return InX + InY * W;
		}

		inline const FVector4& GetPoint(int InX, int InY) const
		{
			return Data[GetPointIndex(InX, InY)];
		}

		inline const FVector4& GetPoint(int PointIndex) const
		{
			return Data[PointIndex];
		}

		inline bool IsValidPoint(int InX, int InY) const
		{
			return GetPoint(InX, InY).W > 0;
		}

		inline bool FindValidPoint(int InX, int InY)
		{
			X0 = InX;
			Y0 = InY;

			for (int Range = 1; Range < W; Range++)
			{
				if (FindValidPointInRange(Range))
				{
					return true;
				}
			}

			return false;
		}

		FVector GetSurfaceViewNormal() const
		{
			int Ncount = 0;
			double Nxyz[3] = { 0,0,0 };

			for (int ItY = 0; ItY < (H - 2); ++ItY)
			{
				for (int ItX = 0; ItX < (W - 2); ++ItX)
				{
					const FVector4& Pts0 = GetPoint(ItX,   ItY);
					const FVector4& Pts1 = GetPoint(ItX+1, ItY);
					const FVector4& Pts2 = GetPoint(ItX,   ItY+1);

					if (Pts0.W > 0 && Pts1.W > 0 && Pts2.W > 0)
					{
						const FVector N1 = Pts1 - Pts0;
						const FVector N2 = Pts2 - Pts0;
						const FVector N = FVector::CrossProduct(N2, N1).GetSafeNormal();

						for (int i = 0; i < 3; i++)
						{
							Nxyz[i] += N[i];
						}

						Ncount++;
					}
				}
			}

			double Scale = double(1) / Ncount;
			for (int i = 0; i < 3; i++)
			{
				Nxyz[i] *= Scale;
			}

			return FVector(Nxyz[0], Nxyz[1], Nxyz[2]).GetSafeNormal();
		}

		FVector GetSurfaceViewPlane()
		{
			const FVector4& Pts0 = GetValidPoint(0, 0);
			const FVector4& Pts1 = GetValidPoint(W-1, 0);
			const FVector4& Pts2 = GetValidPoint(0, H-1);
			
			const FVector N1 = Pts1 - Pts0;
			const FVector N2 = Pts2 - Pts0;
			return FVector::CrossProduct(N2, N1).GetSafeNormal();
		}

	private:
		const FVector4* Data;
		int X0;
		int Y0;
		int W;
		int H;

	private:
		inline const FVector4& GetValidPoint(int InX, int InY)
		{
			if (!IsValidPoint(InX, InY))
			{
				if (FindValidPoint(InX, InY))
				{
					return GetPoint(GetSavedPointIndex());
				}
			}

			return GetPoint(InX, InY);
		}

		inline bool FindValidPointInRange(int Range)
		{
			for (int i = -Range; i <= Range; i++)
			{
				// Top or bottom rows
				if (IsValid(X0 + i, Y0 - Range) || IsValid(X0 + i, Y0 + Range))
				{
					return true;
				}

				// Left or Right columns
				if (IsValid(X0 - Range, Y0 + i) || IsValid(X0 + Range, Y0 + i))
				{
					return true;
				}
			}

			return false;
		}

		inline bool IsValid(int newX, int newY)
		{
			if (newX < 0 || newY < 0 || newX >= W || newY >= H)
			{
				// Out of texture
				return false;
			}

			if (Data[GetPointIndex(newX, newY)].W > 0)
			{
				// Store valid result
				X = newX;
				Y = newY;

				return true;
			}

			return false;
		}
	};

	// Return false, for invalid view planes (for any warpmesh point is under view plane)
	bool FMPCDIWarpTexture::CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
	{
		const FVector4* v = (FVector4*)GetData();

		if (TextureBoxCache.Num() < 1)
		{
			TextureBoxCache.Reserve(DivX*DivY);

			FValidPFMPoint PFMPoints(v, GetWidth(), GetHeight());
			
			// Generate valid points for texturebox method:
			for (int low_y = 0; low_y < DivY; low_y++)
			{
				int y = (GetHeight() - 1)*(float(low_y) / (DivY - 1));

				for (int low_x = 0; low_x < DivX; low_x++)
				{
					int x = (GetWidth() - 1)*(float(low_x) / (DivX - 1));

					if (PFMPoints.IsValidPoint(x, y))
					{
						//Just use direct point
						TextureBoxCache.Add(PFMPoints.GetPointIndex(x, y));
					}
					else
					{
						//Search for nearset valid point
						if (PFMPoints.FindValidPoint(x, y))
						{
							TextureBoxCache.Add(PFMPoints.GetSavedPointIndex());
						}
					}
				}
			}
		}

		// Search a camera space frustum
		bool bResult = true;
		for(const auto It: TextureBoxCache)
		{
			const FVector4& Pts = v[It];
			if (!CalcFrustumFromVertex(Pts, World2Local, Top, Bottom, Left, Right))
			{
				bResult = false;
			}
		}

		return bResult;
	}

	bool FMPCDIWarpTexture::CalcFrustum(EFrustumType FrustumType, IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local) const
	{
		// Extent of the frustum
		float Top = -FLT_MAX;
		float Bottom = FLT_MAX;
		float Left = FLT_MAX;
		float Right = -FLT_MAX;

		//Compute rendering frustum with current method
		bool bIsFrustumPointsValid = false;
		switch (FrustumType)
		{
			case EFrustumType::AABB:
				bIsFrustumPointsValid = CalcFrustum_simpleAABB(OutFrustum, World2Local, Top, Bottom, Left, Right);
				break;

			case EFrustumType::PerfectCPU:
				bIsFrustumPointsValid = CalcFrustum_fullCPU(OutFrustum, World2Local, Top, Bottom, Left, Right);
				break;

			case EFrustumType::TextureBOX:
			{
				//Texture box frustum method size
				static const FIntPoint TextureBoxSize(16, 16);

				bIsFrustumPointsValid = CalcFrustum_TextureBOX(TextureBoxSize.X, TextureBoxSize.Y, OutFrustum, World2Local, Top, Bottom, Left, Right);
				break;
			}

			default:
				break;
		}

		OutFrustum.ProjectionAngles = IMPCDI::FFrustum::FAngles(Top, Bottom, Left, Right);

		return bIsFrustumPointsValid;
	}

	bool FMPCDIWarpTexture::CalcFrustum_fullCPU(const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
	{
		int Count = GetWidth()*GetHeight();
		const FVector4* v = (FVector4*)GetData();

		bool bResult = true;

		// Search a camera space frustum
		for (int i = 0; i < Count; ++i)
		{
			const FVector4& Pts = v[i];
			if(!CalcFrustumFromVertex(Pts, World2Local, Top, Bottom, Left, Right))
			{
				bResult = false;
			}
		}

		return bResult;
	}

	bool FMPCDIWarpTexture::CalcFrustum_simpleAABB(const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
	{
		bool bResult = true;
		// Search a camera space frustum
		for (int i = 0; i < 8; ++i)
		{
			const FVector4& Pts = OutFrustum.AABBoxPts[i];
			if(!CalcFrustumFromVertex(Pts, World2Local, Top, Bottom, Left, Right))
			{
				bResult = false;
			}
		}

		return bResult;
	}

	void FMPCDIWarpTexture::CalcViewProjection(EProjectionType ProjectionType, const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const
	{
		FVector WarpSurfaceNormal = SurfaceViewNormal;
		FVector  WarpSurfacePlane = SurfaceViewPlane;
		switch (ProjectionType)
		{
			case EProjectionType::RuntimeStaticSurfacePlaneInverted:
				WarpSurfacePlane = -WarpSurfacePlane;
			case EProjectionType::StaticSurfacePlane:
			{
				// Use fixed surface view plane:
				OutViewMatrix = FRotationMatrix::MakeFromXZ(WarpSurfacePlane, FVector(0.f, 0.f, 1.f));
				OutViewMatrix.SetOrigin(EyeOrigin); // Finally set view origin to eye location
				break;
			}

			case EProjectionType::RuntimeStaticSurfaceNormalInverted:
				WarpSurfaceNormal = -WarpSurfaceNormal;
			case EProjectionType::StaticSurfaceNormal:
			{
				// Use fixed surface view normal:
				OutViewMatrix = FRotationMatrix::MakeFromXZ(WarpSurfaceNormal, FVector(0.f, 0.f, 1.f));
				OutViewMatrix.SetOrigin(EyeOrigin); // Finally set view origin to eye location
				break;
			}

			case EProjectionType::DynamicAABBCenter:
			{
				OutViewMatrix = FRotationMatrix::MakeFromXZ(ViewDirection, FVector(0.f, 0.f, 1.f));
				OutViewMatrix.SetOrigin(EyeOrigin); // Finally set view origin to eye location
				break;
			}

			case EProjectionType::DynamicAxisAligned:
			{
				// Search best axis direction
				int PtsCount[6] = {0,0, 0,0, 0,0};
				for (int i = 0; i < 8; ++i)
				{
					FVector LookVector = Frustum.AABBoxPts[i] - ViewOrigin;
					if (LookVector.X > 0)
						{ PtsCount[0]++; }
					if (LookVector.X < 0)
						{ PtsCount[1]++; }
					if (LookVector.Y > 0)
						{ PtsCount[2]++; }
					if (LookVector.Y < 0)
						{ PtsCount[3]++; }
					if (LookVector.Z > 0)
						{ PtsCount[4]++; }
					if (LookVector.Z < 0)
						{ PtsCount[5]++; }
				}

				int X = abs(PtsCount[0] - PtsCount[1]);
				int Y = abs(PtsCount[2] - PtsCount[3]);
				int Z = abs(PtsCount[4] - PtsCount[5]);

				FVector Direction(1, 1, 1);

				if (PtsCount[0] < PtsCount[1])
					{ Direction.X = -1; }
				if (PtsCount[2] < PtsCount[3])
					{ Direction.Y = -1; }
				if (PtsCount[4] < PtsCount[5])
					{ Direction.Z = -1; }

				if (X > Y && X > Z)
				{
					Direction.Y = Direction.Z = 0;
				}
				else if (Y > X && Y > Z)
				{
					Direction.X = Direction.Z = 0;
				}
				else if (Z > X && Z > Y)
				{
					Direction.X = Direction.Y = 0;
				}
				else
				{
					// Eye is outside of region, optimize surface normal
					Direction = WarpSurfaceNormal;

					if (X == Y && X > Z)
					{
						Direction.Z = 0;
					}
					else if (X == Z && X > Y)
					{
						Direction.Y = 0;
					}
					else if (Y == Z && Y > X)
					{
						Direction.X = 0;
					}
				}

				Direction.GetSafeNormal();
				OutViewMatrix = FRotationMatrix::MakeFromXZ(Direction, FVector(0.f, 0.f, 1.f));
				OutViewMatrix.SetOrigin(EyeOrigin); // Finally set view origin to eye location

				break;
			}
		}
	}


//---------------------------------------------
//   FMPCDITexture
//---------------------------------------------
	void FMPCDIWarpTexture::CalcView(EStereoMode StereoMode, IMPCDI::FFrustum& OutFrustum, FVector& OutViewDirection, FVector& OutViewOrigin, FVector& OutEyeOrigin) const
	{
		FVector4 AABBMaxExtent = GetAABB().Max * OutFrustum.WorldScale;
		FVector4 AABBMinExtent = GetAABB().Min * OutFrustum.WorldScale;

		{
			// Build AABB points
			OutFrustum.AABBoxPts[0] = FVector(AABBMaxExtent.X, AABBMaxExtent.Y, AABBMaxExtent.Z);
			OutFrustum.AABBoxPts[1] = FVector(AABBMaxExtent.X, AABBMaxExtent.Y, AABBMinExtent.Z);
			OutFrustum.AABBoxPts[2] = FVector(AABBMinExtent.X, AABBMaxExtent.Y, AABBMinExtent.Z);
			OutFrustum.AABBoxPts[3] = FVector(AABBMinExtent.X, AABBMaxExtent.Y, AABBMaxExtent.Z);
			OutFrustum.AABBoxPts[4] = FVector(AABBMaxExtent.X, AABBMinExtent.Y, AABBMaxExtent.Z);
			OutFrustum.AABBoxPts[5] = FVector(AABBMaxExtent.X, AABBMinExtent.Y, AABBMinExtent.Z);
			OutFrustum.AABBoxPts[6] = FVector(AABBMinExtent.X, AABBMinExtent.Y, AABBMinExtent.Z);
			OutFrustum.AABBoxPts[7] = FVector(AABBMinExtent.X, AABBMinExtent.Y, AABBMaxExtent.Z);
		}

		switch (StereoMode)
		{
			case EStereoMode::AsymmetricAABB:
			{
				// Use AABB center as view target
				FVector AABBCenter = (AABBMaxExtent + AABBMinExtent) * 0.5f;
				// Use eye view location to build view vector
				FVector LookAt = OutFrustum.OriginLocation + OutFrustum.OriginEyeOffset;

				// Create view transform matrix from look direction vector:
				FVector LookVector = AABBCenter - LookAt;

				OutViewDirection = LookVector.GetSafeNormal();
				OutViewOrigin = LookAt;
				OutEyeOrigin = LookAt;

				break;
			}

			case EStereoMode::SymmetricAABB:
			{
				// Use AABB center as view target
				FVector AABBCenter = (AABBMaxExtent + AABBMinExtent) * 0.5f;
				// Use camera origin location to build view vector
				FVector LookAt = OutFrustum.OriginLocation;

				// Create view transform matrix from look direction vector:
				FVector LookVector = AABBCenter - LookAt;

				OutViewDirection = LookVector.GetSafeNormal();
				OutViewOrigin = LookAt;
				OutEyeOrigin = FVector(LookAt + OutFrustum.OriginEyeOffset);

				break;
			}
		}
	}

	bool FMPCDIWarpTexture::UpdateProjectionType(EProjectionType& ProjectionType) const
	{
		switch (ProjectionType)
		{
			case EProjectionType::DynamicAABBCenter:
				//! Accept any frustum for aabb center method
				break;

			case EProjectionType::StaticSurfaceNormal:
				ProjectionType = EProjectionType::RuntimeStaticSurfaceNormalInverted; // Make mirror for backside eye position
				return true;

			case EProjectionType::StaticSurfacePlane:
				ProjectionType = EProjectionType::RuntimeStaticSurfacePlaneInverted; // Make mirror for backside eye position
				return true;

			default:
				ProjectionType = EProjectionType::DynamicAABBCenter;
				return true;
		}

		// Accept current projection
		return false;
	}

	bool FMPCDIWarpTexture::GetFrustum_A3D(IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar) const
	{
		if (!IsValid())
		{
			return false;
		}

		const int FrustumCacheDepth = (int)CVarMPCDIFrustumCacheDepth.GetValueOnAnyThread();
		{
			// Try to use old frustum values from cache (reduce CPU cost)
			if (FrustumCacheDepth>0 && FrustumCache.Num() > 0)
			{
				FScopeLock lock(&DataGuard);

				// If frustum cache used ,try to search valid value
				const float FrustumCachePrecision = (float)CVarMPCDIFrustumCachePrecision.GetValueOnAnyThread();
				for (int i=0;i<FrustumCache.Num(); i++)
				{
					if (FrustumCache[i].IsEyeLocationEqual(OutFrustum, FrustumCachePrecision))
					{
						// Use cached value
						OutFrustum = FrustumCache[i];
						const int OnTopIndex = FrustumCache.Num() - 1;
						if (OnTopIndex > i)
						{
							FrustumCache.Add(OutFrustum); // Add to on top of cache
							FrustumCache.RemoveAt(i, 1); // Remove from prev cache position
						}

						return true;
					}
				}
			}
			else
			{
				FrustumCache.Empty();
			}
		}

		OutFrustum.WorldScale = WorldScale;

		const int32 ProjectionAuto = CVarMPCDIProjectionAuto.GetValueOnAnyThread();

		EFrustumType       FrustumType = (EFrustumType)CVarMPCDIFrustumMethod.GetValueOnAnyThread();
		EStereoMode         StereoMode = (EStereoMode)CVarMPCDIStereoMode.GetValueOnAnyThread();
		EProjectionType ProjectionMode = (EProjectionType)CVarMPCDIProjectionMode.GetValueOnAnyThread();

		// Protect runtime methods:
		if (ProjectionMode >= EProjectionType::RuntimeProjectionModes)
		{
			// Runtime method disabled for console vars
			ProjectionMode = EProjectionType::DynamicAABBCenter;
		}

		// Calc Frustum:
		FMatrix Local2world = FMatrix::Identity;
		FMatrix World2Local = FMatrix::Identity;

		// Get view base:
		FVector ViewDirection, ViewOrigin, EyeOrigin;
		CalcView(StereoMode, OutFrustum, ViewDirection, ViewOrigin, EyeOrigin);

		if (ProjectionAuto == 0)
		{
			//Directly build frustum
			CalcViewProjection(ProjectionMode, OutFrustum, ViewDirection, ViewOrigin, EyeOrigin, Local2world);
			World2Local = Local2world.Inverse();
			CalcFrustum(FrustumType, OutFrustum, World2Local);
		}
		else
		{
			// projection type changed runtime. Validate frustum points, all must be over view plane:			
			EProjectionType BaseProjectionMode = ProjectionMode;

			if(FrustumType== EFrustumType::PerfectCPU)
			{
				//Optimize for PerfectCPU:
				while (true)
				{
					// Fast check for bad view
					CalcViewProjection(ProjectionMode, OutFrustum, ViewDirection, ViewOrigin, EyeOrigin, Local2world);
					World2Local = Local2world.Inverse();

					if (CalcFrustum(EFrustumType::TextureBOX, OutFrustum, World2Local))
					{
						break;
					}

					if(!UpdateProjectionType(ProjectionMode))
					{
						break;
					}
				}
			}

			if (BaseProjectionMode == ProjectionMode)
			{
				// Search better projection mode:
				while (true)
				{
					//Full check for bad view:
					CalcViewProjection(ProjectionMode, OutFrustum, ViewDirection, ViewOrigin, EyeOrigin, Local2world);
					World2Local = Local2world.Inverse();
					
					if (CalcFrustum(FrustumType, OutFrustum, World2Local))
					{
						break;
					}

					if (!UpdateProjectionType(ProjectionMode))
					{
						break;
					}
				}
			}
		}

		// These matrices were copied from LocalPlayer.cpp.
		// They change the coordinate system from the Unreal "Game" coordinate system to the Unreal "Render" coordinate system
		static const FMatrix Game2Render(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		static const FMatrix Render2Game = Game2Render.Inverse();

		// Compute warp projection matrix
		OutFrustum.Local2WorldMatrix = Local2world;
		OutFrustum.ProjectionMatrix = GetProjectionMatrixAssymetric(OutFrustum.ProjectionAngles, ZNear, ZFar);
		OutFrustum.UVMatrix = World2Local * Game2Render * OutFrustum.ProjectionMatrix;
	
		{
			// Store current used frustum value to cache
			FScopeLock lock(&DataGuard);

			// Save current frustum value to cache
			if (FrustumCacheDepth > 0)
			{
				FrustumCache.Add(OutFrustum);
			}

			// Remove too old cached values
			int TotalTooOldValuesCount = FrustumCache.Num() - FrustumCacheDepth;
			if(TotalTooOldValuesCount > 0)
			{
				FrustumCache.RemoveAt(0, TotalTooOldValuesCount);
			}
		}

		return true;
	}

	void FMPCDIWarpTexture::BuildAABBox()
	{
		{
			// Calc static normal and plane
			const FVector4* v = (FVector4*)GetData();
			FValidPFMPoint PFMPoints(v, GetWidth(), GetHeight());

			SurfaceViewNormal = PFMPoints.GetSurfaceViewNormal();
			SurfaceViewPlane  = PFMPoints.GetSurfaceViewPlane();
		}

		TextureBoxCache.Empty();
		FrustumCache.Empty();

		//Build bbox from valid points:
		for (uint32 Y = 0; Y < GetHeight(); ++Y)
		{
			for (uint32 X = 0; X < GetWidth(); ++X)
			{
				const FVector4& Pts = ((FVector4*)GetData())[(X + Y * GetWidth())];
				if (Pts.W > 0)
				{
					AABBox.Min.X = FMath::Min(AABBox.Min.X, Pts.X);
					AABBox.Min.Y = FMath::Min(AABBox.Min.Y, Pts.Y);
					AABBox.Min.Z = FMath::Min(AABBox.Min.Z, Pts.Z);

					AABBox.Max.X = FMath::Max(AABBox.Max.X, Pts.X);
					AABBox.Max.Y = FMath::Max(AABBox.Max.Y, Pts.Y);
					AABBox.Max.Z = FMath::Max(AABBox.Max.Z, Pts.Z);
				}
			}
		}
	}

	bool FMPCDIWarpTexture::LoadCustom3DWarpMap(const TArray<FVector>& InPoints, int WarpX, int WarpY, IMPCDI::EMPCDIProfileType ProfileType, float WorldScale, bool bIsUnrealGameSpace)
	{
		FMatrix m = FMatrix::Identity;

		if (ProfileType == IMPCDI::EMPCDIProfileType::mpcdi_A3D)
		{
			if (bIsUnrealGameSpace)
			{
				m = FMatrix(
					FPlane(WorldScale, 0.f, 0.f, 0.f),
					FPlane(0.f, WorldScale, 0.f, 0.f),
					FPlane(0.f, 0.f, WorldScale, 0.f),
					FPlane(0.f, 0.f, 0.f, 1.f));
			}
			else
			{
				// Convert from MPCDI convention to Unreal convention
				// MPCDI is Right Handed (Y is up, X is left, Z is in the screen)
				// Unreal is Left Handed (Z is up, X in the screen, Y is right)
				m = FMatrix(
					FPlane(0.f, WorldScale, 0.f, 0.f),
					FPlane(0.f, 0.f, WorldScale, 0.f),
					FPlane(-WorldScale, 0.f, 0.f, 0.f),
					FPlane(0.f, 0.f, 0.f, 1.f));
			}
		}

		FVector4* data = new FVector4[WarpX*WarpY];

		static const float kEpsilon = 0.00001f;
		for (int i = 0; i < InPoints.Num(); ++i)
		{
			const FVector& t = InPoints[i];
			FVector4& Pts = data[i];

			if ((!(fabsf(t.X) < kEpsilon && fabsf(t.Y) < kEpsilon && fabsf(t.Z) < kEpsilon))
				&& (!FMath::IsNaN(t.X) && !FMath::IsNaN(t.Y) && !FMath::IsNaN(t.Z)))
			{
				Pts = m.TransformPosition(t);
				Pts.W = 1;
			}
			else
			{
				Pts = FVector4(0.f, 0.f, 0.f, -1.f);
			}
		}
		
		ReleaseTextureData();

		// Create texture
		const EPixelFormat pixelFormat = PF_A32B32G32R32F;
		SetTextureData(reinterpret_cast<void*>(data), WarpX, WarpY, pixelFormat, false);

		BuildAABBox();

		if (IsInitialized())
		{
			BeginUpdateResourceRHI(this);
		}

		BeginInitResource(this);
		
		return true;
	}

	bool FMPCDIWarpTexture::LoadPFMFile(mpcdi::PFM& SourcePFM, IMPCDI::EMPCDIProfileType ProfileType, float PFMScale, bool bIsUnrealGameSpace)
	{
		int PFMWidth = SourcePFM.GetSizeX();
		int PFMHeight = SourcePFM.GetSizeY();

		TArray<FVector> WarpMeshPoints;
		WarpMeshPoints.Reserve(PFMWidth*PFMHeight);

		for (int y = 0; y < PFMHeight; ++y)
		{
			for (int x = 0; x < PFMWidth; ++x)
			{
				mpcdi::NODE node = SourcePFM(x, y);

				FVector pts = (FVector&)(node);
				WarpMeshPoints.Add( pts );
			}
		}

		return LoadCustom3DWarpMap(WarpMeshPoints, PFMWidth, PFMHeight, ProfileType, PFMScale, bIsUnrealGameSpace);
	}

	bool FMPCDIWarpTexture::LoadWarpMap(mpcdi::GeometryWarpFile *SourceWarpMap, IMPCDI::EMPCDIProfileType ProfileType)
	{
		FMatrix m = FMatrix::Identity;
		bool is2DData = true;
		if (ProfileType == IMPCDI::EMPCDIProfileType::mpcdi_A3D)
		{
			// Unreal is in cm, so we need to convert to cm.
			float scale = 1.f;
			switch (SourceWarpMap->GetGeometricUnit())
			{
				case mpcdi::GeometricUnitmm: { scale = 1.f / 10.f; break; }
				case mpcdi::GeometricUnitcm: { scale = 1.f;        break; }
				case mpcdi::GeometricUnitdm: { scale = 10.f;       break; }
				case mpcdi::GeometricUnitm:  { scale = 100.f;      break; }
				case mpcdi::GeometricUnitin: { scale = 2.54f;      break; }
				case mpcdi::GeometricUnitft: { scale = 30.48f;     break; }
				case mpcdi::GeometricUnityd: { scale = 91.44f;     break; }
				case mpcdi::GeometricUnitunkown: { scale = 1.f;    break; }
				default: { check(false); break; }
			};

			// Convert from MPCDI convention to Unreal convention
			// MPCDI is Right Handed (Y is up, X is left, Z is in the screen)
			// Unreal is Left Handed (Z is up, X in the screen, Y is right)
			m = FMatrix(
				FPlane(0.f, scale, 0.f, 0.f),
				FPlane(0.f, 0.f, scale, 0.f),
				FPlane(-scale, 0.f, 0.f, 0.f),
				FPlane(0.f, 0.f, 0.f, 1.f));

			is2DData = false;
		}

		int WarpX = SourceWarpMap->GetSizeX();
		int WarpY = SourceWarpMap->GetSizeY();

		FVector4* data = new FVector4[WarpX*WarpY];

		static const float kEpsilon = 0.00001f;
		for (int j = 0; j < WarpY; ++j)
		{
			for (int i = 0; i < WarpX; ++i)
			{
				mpcdi::NODE &node = (*SourceWarpMap)(i, j);
				FVector t(node.r, node.g, is2DData ? 0.f : node.b);

				FVector4& Pts = data[i + j * WarpX];

				if ((!(fabsf(t.X) < kEpsilon && fabsf(t.Y) < kEpsilon && fabsf(t.Z) < kEpsilon))
					&& (!FMath::IsNaN(t.X) && !FMath::IsNaN(t.Y) && !FMath::IsNaN(t.Z)))
				{
					Pts = m.TransformPosition(t);
					Pts.W = 1;
				}
				else
				{
					Pts = FVector4(0.f, 0.f, 0.f, -1.f);
				}
			}
		}

		if (ProfileType == IMPCDI::EMPCDIProfileType::mpcdi_A3D)
		{
			// Remove noise from warp mesh (small areas less than 3*3 quads)
			ClearNoise(FIntPoint(3, 3), FIntPoint(2, 3));
		}

		ReleaseTextureData();

		// Create texture
		const EPixelFormat pixelFormat = PF_A32B32G32R32F;
		SetTextureData(reinterpret_cast<void*>(data), WarpX, WarpY, pixelFormat, false);

		BuildAABBox();

		if (IsInitialized())
		{
			BeginUpdateResourceRHI(this);
		}

		BeginInitResource(this);
		
		return true;
	}

	bool FMPCDIWarpTexture::Is3DPointValid(int X, int Y) const
	{
		if (X >= 0 && X < (int)GetWidth() && Y >= 0 && Y < (int)GetHeight())
		{
			FVector4* pts = (FVector4*)GetData();
			return pts[(X + Y * (int)GetWidth())].W > 0;
		}

		return false;
	}

	void FMPCDIWarpTexture::ClearNoise(const FIntPoint& SearchXYDepth, const FIntPoint& AllowedXYDepthRules)
	{
		if (GetWidth() > 10 && GetHeight() > 10)
		{
			//Remove noise for large warp mesh
			int MaxLoops = 50;
			while (MaxLoops-- > 0)
			{
				if (!RemoveDetachedPoints(SearchXYDepth, AllowedXYDepthRules))
				{
					break;
				}
			}
		}
	}

	int FMPCDIWarpTexture::RemoveDetachedPoints(const FIntPoint& SearchLen, const FIntPoint& RemoveRule)
	{
		FVector4* pts = (FVector4*)GetData();

		int SearchX = SearchLen.X  * GetWidth()  / 100;
		int SearchY = SearchLen.Y  * GetHeight() / 100;
		int Rule1X  = RemoveRule.X * GetWidth()  / 100;
		int Rule1Y  = RemoveRule.Y * GetHeight() / 100;

		int TotalChangesCount = 0;
		static int DirIndexValue[] = { -1, 1 };

		for (uint32 Y = 0; Y < GetHeight(); ++Y)
		{
			for (uint32 X = 0; X < GetWidth(); ++X)
			{
				if (Is3DPointValid(X, Y))
				{
					int XLen = 0;
					int YLen = 0;

					for (int DirIndex = 0; DirIndex < 2; DirIndex++)
					{
						int dx = 0;
						int dy = 0;

						for (int Offset = 1; Offset <= SearchX; Offset++)
						{
							if (Is3DPointValid(X + DirIndexValue[DirIndex] * Offset, Y))
							{
								dx++;
							}
							else
							{
								break;
							}
						}
						for (int Offset = 1; Offset <= SearchY; Offset++)
						{
							if (Is3DPointValid(X, Y + DirIndexValue[DirIndex] * Offset))
							{
								dy++;
							}
							else
							{
								break;
							}
						}

						XLen = FMath::Max(XLen, dx);
						YLen = FMath::Max(YLen, dy);
					}

					bool Test1 = XLen >= Rule1X && YLen >= Rule1Y;
					bool Test2 = YLen >= Rule1X && XLen >= Rule1Y;

					if (!Test1 && !Test2)
					{
						// Both test failed, remove it
						pts[(X + Y * GetWidth())] = FVector4(0.f, 0.f, 0.f, -1.f);
						TotalChangesCount++;
					}
				}
			}
		}

		return TotalChangesCount;
	}

	void FMPCDIWarpTexture::ExportMeshData(FMPCDIGeometryExportData& Dst)
	{
		int DownScaleFactor=1;

		TMap<int, int> VIndexMap;
		int VIndex = 0;

		FVector4* pts = (FVector4*)GetData();

		uint32 maxHeight = GetHeight() / DownScaleFactor;
		uint32 maxWidth = GetWidth() / DownScaleFactor;

		{
			//Pts + Normals + UV
			float ScaleU = 1.0f / float(maxWidth);
			float ScaleV = 1.0f / float(maxHeight);

			for (uint32 j = 0; j < maxHeight; ++j)
			{
				for (uint32 i = 0; i < maxWidth; ++i)
				{
					int idx = ((i*DownScaleFactor) + (j*DownScaleFactor) * GetWidth());
					const FVector4& v = pts[idx];
					if (v.W > 0)
					{
						Dst.Vertices.Add(FVector(v.X,v.Y,v.Z));
						VIndexMap.Add(idx, VIndex++);

						Dst.UV.Add(FVector2D(
							float(i)*ScaleU,
							float(j)*ScaleV
						));

						Dst.Normal.Add(FVector(0, 0, 0)); // Fill on face pass
					}
				}
			}
		}

		{
			//faces
			for (uint32 j = 0; j < maxHeight - 1; ++j)
			{
				for (uint32 i = 0; i < maxWidth - 1; ++i)
				{
					int idx[4];

					idx[0] = ((i)*DownScaleFactor + (j)*DownScaleFactor * GetWidth());
					idx[1] = ((i + 1)*DownScaleFactor + (j)*DownScaleFactor * GetWidth());
					idx[2] = ((i)*DownScaleFactor + (j + 1)*DownScaleFactor * GetWidth());
					idx[3] = ((i + 1)*DownScaleFactor + (j + 1)*DownScaleFactor * GetWidth());

					for (int a = 0; a < 4; a++)
					{
						if (VIndexMap.Contains(idx[a]))
						{
							idx[a] = VIndexMap[idx[a]];
						}
						else
						{
							idx[a] = 0;
						}
					}

					if (idx[0] && idx[2] && idx[3])
					{
						Dst.PostAddFace(idx[0], idx[2], idx[3]);
					}

					if (idx[3] && idx[1] && idx[0])
					{
						Dst.PostAddFace(idx[3], idx[1], idx[0]);
					}
				}
			}

		}
	}

	void FMPCDIWarpTexture::ImportMeshData(const FMPCDIGeometryImportData& Src)
	{
		LoadCustom3DWarpMap(Src.Vertices, Src.Width, Src.Height, IMPCDI::EMPCDIProfileType::mpcdi_A3D, 1, true);
	}
}
