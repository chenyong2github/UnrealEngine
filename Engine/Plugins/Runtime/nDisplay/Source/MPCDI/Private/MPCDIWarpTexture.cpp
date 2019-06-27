// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIWarpTexture.h"
#include "MPCDIData.h"

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

THIRD_PARTY_INCLUDES_END


// Select mpcdi frustum calc method
enum EVarMPCDIFrustumMethod
{
	AABB = 0,
	PerfectCPU,
#if 0
	PerfectGPU, // optimization purpose, project warp texture to one-pixel rendertarget, in min\max colorop pass
#endif
};


static TAutoConsoleVariable<int32> CVarMPCDIFrustumMethod(
	TEXT("nDisplay.render.mpcdi.Frustum"),
	(int)EVarMPCDIFrustumMethod::PerfectCPU,
	TEXT("Frustum computation method:\n0 = mesh AABB based, lower quality but fast\n1 = mesh vertices based, best quality but slow\n"),
	ECVF_RenderThreadSafe
);

// Select mpcdi stereo mode
enum EVarMPCDIStereoMode
{
	AsymmetricAABB = 0,
	SymmetricAABB,
};

static TAutoConsoleVariable<int32> CVarMPCDIStereoMode(
	TEXT("nDisplay.render.mpcdi.StereoMode"),
	(int)EVarMPCDIStereoMode::AsymmetricAABB,
	TEXT("Stereo mode:\n (0 = Asymmetric to AABB center\n(1 = Symmetric to AABB center)\n"),
	ECVF_RenderThreadSafe
);

// Select mpcdi projection mode
enum EVarMPCDIProjectionMode
{
	Dynamic = 0,
	StaticAxisAligned,
};

static TAutoConsoleVariable<int32> CVarMPCDIProjectionMode(
	TEXT("nDisplay.render.mpcdi.Projection"),
	(int)EVarMPCDIProjectionMode::Dynamic,
	TEXT("Projection method:\n0 = Dynamic, to view target\n1 = Static, aligned to mpcdi origin space axis\n"),
	ECVF_RenderThreadSafe
);


namespace // Helpers
{
	FMatrix GetProjectionMatrixAssymetric(float l, float r, float t, float b, float n, float f)
	{
		static const FMatrix FlipZAxisToUE4 = FMatrix(
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, -1, 0),
			FPlane(0, 0, 1, 1));

		const float mx = 2.f * n / (r - l);
		const float my = 2.f * n / (t - b);
		const float ma = -(r + l) / (r - l);
		const float mb = -(t + b) / (t - b);
		const float mc = f / (f - n);
		const float md = -(f * n) / (f - n);
		const float me = 1.f;

		// Normal LHS
		FMatrix ProjectionMatrix = FMatrix(
			FPlane(mx, 0, 0, 0),
			FPlane(0, my, 0, 0),
			FPlane(ma, mb, mc, me),
			FPlane(0, 0, md, 0));

		return ProjectionMatrix * FlipZAxisToUE4;
	}

	template<class T>
	static T degToRad(T degrees)
	{
		return degrees * (T)(PI / 180.0);
	}

	FMatrix GetProjectionMatrixAssymetricFromFrustum(float LeftAngle, float RightAngle, float TopAngle, float BottomAngle, float ZNear, float ZFar)
	{
		float l = float(ZNear*tan(degToRad(LeftAngle)));
		float r = float(ZNear*tan(degToRad(RightAngle)));
		float b = float(ZNear*tan(degToRad(BottomAngle)));
		float t = float(ZNear*tan(degToRad(TopAngle)));

		return GetProjectionMatrixAssymetric(l, r, t, b, ZNear, ZFar);
	}
};

namespace MPCDI
{
	void FMPCDIWarpTexture::CalcFrustum_fullCPU(const IMPCDI::FFrustum& OutFrustum, const FMatrix& world2local, float& top, float& bottom, float& left, float& right) const
	{
		int Count = GetWidth()*GetHeight();
		const FVector4* v = (FVector4*)GetData();

		// Create a camera space frustum
		for (int i = 0; i < Count; ++i)
		{
			if (v[i].W > 0) {
				FVector4 prjV = world2local.TransformFVector4(v[i]);
				float x = 1.0f / prjV.X;
				prjV.Y *= x;
				prjV.Z *= x;

				if (prjV.Z > top) { top = prjV.Z; }
				if (prjV.Z < bottom) { bottom = prjV.Z; }
				if (prjV.Y > right) { right = prjV.Y; }
				if (prjV.Y < left) { left = prjV.Y; }
			}
		}
	}

	void FMPCDIWarpTexture::CalcFrustum_simpleAABB(const IMPCDI::FFrustum& OutFrustum, const FMatrix& world2local, float& top, float& bottom, float& left, float& right) const
	{
		FVector4 maxExtent = GetAABB().Max * OutFrustum.WorldScale;
		FVector4 minExtent = GetAABB().Min * OutFrustum.WorldScale;


		FVector4 v[8];
		v[0] = FVector4(maxExtent.X, maxExtent.Y, maxExtent.Z);
		v[1] = FVector4(maxExtent.X, maxExtent.Y, minExtent.Z);
		v[2] = FVector4(minExtent.X, maxExtent.Y, minExtent.Z);
		v[3] = FVector4(minExtent.X, maxExtent.Y, maxExtent.Z);
		v[4] = FVector4(maxExtent.X, minExtent.Y, maxExtent.Z);
		v[5] = FVector4(maxExtent.X, minExtent.Y, minExtent.Z);
		v[6] = FVector4(minExtent.X, minExtent.Y, minExtent.Z);
		v[7] = FVector4(minExtent.X, minExtent.Y, maxExtent.Z);


		// Create a camera space frustum
		FVector4 local[8];
		for (int i = 0; i < 8; ++i)
		{
			local[i] = world2local.TransformFVector4(v[i]);
			float x = local[i].X;
			local[i].X /= x;
			local[i].Y /= x;
			local[i].Z /= x;
			//! OutFrustum.Bounds[i] = v[i];
		}

		for (int i = 0; i < 8; ++i)
		{
			if (local[i].Z > top) { top = local[i].Z; }
			if (local[i].Z < bottom) { bottom = local[i].Z; }
			if (local[i].Y > right) { right = local[i].Y; }
			if (local[i].Y < left) { left = local[i].Y; }
		}


	}

	void FMPCDIWarpTexture::CalcViewProjection(const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const
	{
		const EVarMPCDIProjectionMode ProjMode = (EVarMPCDIProjectionMode)CVarMPCDIProjectionMode.GetValueOnAnyThread();
		switch (ProjMode)
		{
			case EVarMPCDIProjectionMode::Dynamic:
			{
				OutViewMatrix = FRotationMatrix::MakeFromXZ(ViewDirection, FVector(0.f, 0.f, 1.f));
				OutViewMatrix.SetOrigin(EyeOrigin); // Finally set view origin to eye location
				break;
			}

			case EVarMPCDIProjectionMode::StaticAxisAligned:
			{
				float X = fabs(ViewDirection.X);
				float Y = fabs(ViewDirection.Y);
				float Z = fabs(ViewDirection.Z);

				FVector Direction = ViewDirection;

				if (X > Y && X > Z)
				{
					Direction.Y = Direction.Z = 0;
				}
				if (Y > X && Y > Z)
				{
					Direction.X = Direction.Z = 0;
				}
				if (Z > X && Z > Y)
				{
					Direction.X = Direction.Y = 0;
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
	bool FMPCDIWarpTexture::GetFrustum_A3D(IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar) const
	{
		OutFrustum.WorldScale = WorldScale;

		FVector AABBMaxExtent = GetAABB().Max * WorldScale;
		FVector AABBMinExtent = GetAABB().Min * WorldScale;

		FMatrix Local2world = FMatrix::Identity;

		const EVarMPCDIStereoMode StereoMode = (EVarMPCDIStereoMode)CVarMPCDIStereoMode.GetValueOnAnyThread();
		switch (StereoMode)
		{
			case EVarMPCDIStereoMode::AsymmetricAABB:
			{
				// Use AABB center as view target
				FVector AABBCenter = (AABBMaxExtent + AABBMinExtent) * 0.5f;
				// Use eye view location to build view vector
				FVector LookAt = OutFrustum.OriginLocation + OutFrustum.OriginEyeOffset;

				// Create view transform matrix from look direction vector:
				FVector LookVector = AABBCenter - LookAt;
				FVector LookDirection= LookVector.GetSafeNormal();
				CalcViewProjection(OutFrustum, LookDirection, LookAt, LookAt, Local2world);
			
				break;
			}

			case EVarMPCDIStereoMode::SymmetricAABB:
			{
				// Use AABB center as view target
				FVector AABBCenter = (AABBMaxExtent + AABBMinExtent) * 0.5f;
				// Use camera origin location to build view vector
				FVector LookAt = OutFrustum.OriginLocation;

				// Create view transform matrix from look direction vector:
				FVector LookVector = AABBCenter - LookAt;
				FVector LookDirection = LookVector.GetSafeNormal();
				CalcViewProjection(OutFrustum, LookDirection, LookAt, FVector(LookAt + OutFrustum.OriginEyeOffset), Local2world);

				break;
			}
		}

		// View Matrix
		FMatrix World2local = Local2world.Inverse();

		// Extent of the frustum
		float top = -FLT_MAX;
		float bottom = FLT_MAX;
		float left = FLT_MAX;
		float right = -FLT_MAX;

		const EVarMPCDIFrustumMethod FrustumComputeMethod = (EVarMPCDIFrustumMethod)CVarMPCDIFrustumMethod.GetValueOnAnyThread();
		switch (FrustumComputeMethod)
		{
		case EVarMPCDIFrustumMethod::AABB:
			CalcFrustum_simpleAABB(OutFrustum, World2local, top, bottom, left, right);
			break;

		case EVarMPCDIFrustumMethod::PerfectCPU:
			CalcFrustum_fullCPU(OutFrustum, World2local, top, bottom, left, right);
			break;
		}

		OutFrustum.ProjectionAngles = IMPCDI::FFrustum::Angles(top, bottom, left, right);

		// These matrices were copied from LocalPlayer.cpp.
		// They change the coordinate system from the Unreal "Game" coordinate system to the Unreal "Render" coordinate system
		static const FMatrix Game2Render(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		
		static const FMatrix Render2Game = Game2Render.Inverse();

		//!
		ZNear = 1.0f;

		
		// Compute view matrix
		//!OutFrustum.ViewMatrix = Game2Render;

		// Compute projection matrix
		FMatrix OriginViewMatrix = World2local * Game2Render;

		OutFrustum.Local2WorldMatrix = Local2world;
		OutFrustum.ProjectionMatrix  = GetProjectionMatrixAssymetric(left, right, top, bottom, ZNear, ZFar);
		//!OutFrustum.OriginViewMatrix  = OriginViewMatrix;
		
		//OutFrustum.UVMatrix = OutFrustum.ViewMatrix * (OutFrustum.OriginViewMatrix * OutFrustum.ProjectionMatrix);
		OutFrustum.UVMatrix = OriginViewMatrix * OutFrustum.ProjectionMatrix;

		return true;
	}

	void FMPCDIWarpTexture::BuildAABBox()
	{
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

	void FMPCDIWarpTexture::LoadCustom3DWarpMap(const TArray<FVector>& InPoints, int WarpX, int WarpY)
	{
		FVector& OutMinBBox = AABBox.Min;
		FVector& OutMaxBBox = AABBox.Max;

		// Convert from MPCDI convention to Unreal convention
		// MPCDI is Right Handed (Y is up, X is left, Z is in the screen)
		// Unreal is Left Handed (Z is up, X in the screen, Y is right)
		float scale = 100.f;
		FMatrix m(
			FPlane(0.f, scale, 0.f, 0.f),
			FPlane(0.f, 0.f, scale, 0.f),
			FPlane(-scale, 0.f, 0.f, 0.f),
			FPlane(0.f, 0.f, 0.f, 1.f));
		
		FVector4* data = new FVector4[WarpX*WarpY];

		static const float kEpsilon = 0.00001f;
		for (int i = 0; i < InPoints.Num(); ++i)
		{
			const FVector4& t = InPoints[i];
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

		// Create texture
		const EPixelFormat pixelFormat = PF_A32B32G32R32F;
		SetTextureData(reinterpret_cast<void*>(data), WarpX, WarpY, pixelFormat, false);

		BuildAABBox();
		BeginInitResource(this);
	}

	void FMPCDIWarpTexture::LoadWarpMap(mpcdi::GeometryWarpFile *SourceWarpMap, EMPCDIProfileType ProfileType)
	{
		FVector& OutMinBBox = AABBox.Min;
		FVector& OutMaxBBox = AABBox.Max;

		FMatrix m = FMatrix::Identity;
		bool is2DData = true;
		if (ProfileType == EMPCDIProfileType::mpcdi_A3D)
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

		if (ProfileType == EMPCDIProfileType::mpcdi_A3D)
		{
			// Remove noise from warp mesh (small areas less than 3*3 quads)
			ClearNoise(FIntPoint(3, 3), FIntPoint(2, 3));
		}

		// Create texture
		const EPixelFormat pixelFormat = PF_A32B32G32R32F;
		SetTextureData(reinterpret_cast<void*>(data), WarpX, WarpY, pixelFormat, false);

		BuildAABBox();
		//BuildWarpMapData(ProfileType);

		BeginInitResource(this);
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


		{//Pts + Normals + UV
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
		{//faces
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
						if (VIndexMap.Contains(idx[a]))
							idx[a] = VIndexMap[idx[a]];
						else
							idx[a] = 0;

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
}


