// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIWarp.h"
#include "MPCDIHelpers.h"
#include "MPCDIWarpHelpers.h"


// Select mpcdi frustum calc method
static TAutoConsoleVariable<int32> CVarMPCDIFrustumMethod(
	TEXT("nDisplay.render.mpcdi.Frustum"),
	(int)EFrustumType::TextureBOX,
	TEXT("Frustum computation method:\n")
	TEXT(" 0: mesh AABB based, lower quality but fast\n")
	TEXT(" 1: mesh vertices based, best quality but slow\n")
	TEXT(" 2: texture box, get A*B distributed points from texture, fast, good quality for flat panels\n"),
	ECVF_RenderThreadSafe
);

// Select mpcdi stereo mode
static TAutoConsoleVariable<int32> CVarMPCDIStereoMode(
	TEXT("nDisplay.render.mpcdi.StereoMode"),
	(int)EStereoMode::AsymmetricAABB,
	TEXT("Stereo mode:\n")
	TEXT(" 0: Asymmetric to AABB center\n")
	TEXT(" 1: Symmetric to AABB center\n"),
	ECVF_RenderThreadSafe
);

// Select mpcdi projection mode
static TAutoConsoleVariable<int32> CVarMPCDIProjectionMode(
	TEXT("nDisplay.render.mpcdi.Projection"),
	(int)EProjectionType::StaticSurfaceNormal,
	TEXT("Projection method:\n")
	TEXT(" 0: Static, aligned to average region surface normal\n")
	TEXT(" 1: Static, aligned to average region surface corners plane\n")
	TEXT(" 2: Dynamic, to view target center\n"),
	ECVF_RenderThreadSafe
);

// Frustum projection fix (back-side view planes)
static TAutoConsoleVariable<int32> CVarMPCDIProjectionAuto(
	TEXT("nDisplay.render.mpcdi.ProjectionAuto"),
	1, // Default on
	TEXT("Runtime frustum method, fix back-side view projection.\n")
	TEXT(" 0: Disabled\n")
	TEXT(" 1: Enabled (default)\n"),
	ECVF_RenderThreadSafe
);

// Setup frustum projection cache
static TAutoConsoleVariable<int32> CVarMPCDIFrustumCacheDepth(
	TEXT("nDisplay.render.mpcdi.cache_depth"),
	0,// Default disabled
	TEXT("Frustum values cache (depth, num).\n")
	TEXT("By default cache is disabled. For better performance (EFrustumType::PerfectCPU) set value to 512).\n")
	TEXT(" 0: Disabled\n")
	TEXT(" N: Cache size, integer\n"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarMPCDIFrustumCachePrecision(
	TEXT("nDisplay.render.mpcdi.cache_precision"),
	0.1f, // 1mm
	TEXT("Frustum cache values comparison precision (float, unit is sm).\n"),
	ECVF_RenderThreadSafe
);


FMatrix GetProjectionMatrixAssymetric(const IMPCDI::FFrustum::FAngles& ProjectionAngles, float zNear, float zFar)
{
	const float l = ProjectionAngles.Left;
	const float r = ProjectionAngles.Right;
	const float t = ProjectionAngles.Top;
	const float b = ProjectionAngles.Bottom;

	return DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(l, r, t, b, zNear, zFar);
}

bool FMPCDIWarp::CalcFrustum(EFrustumType FrustumType, IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local) const
{
	// Extent of the frustum
	float Top = -FLT_MAX;
	float Bottom = FLT_MAX;
	float Left = FLT_MAX;
	float Right = -FLT_MAX;

	bool bIsFrustumPointsValid = false;

	//Compute rendering frustum with current method
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

void FMPCDIWarp::CalcViewProjection(EProjectionType ProjectionType, const IMPCDI::FFrustum& Frustum, const FVector& ViewDirection, const FVector& ViewOrigin, const FVector& EyeOrigin, FMatrix& OutViewMatrix) const
{
	FVector ViewDir = ViewDirection;

	switch (ProjectionType)
	{
		case EProjectionType::StaticSurfacePlane:
			ViewDir = SurfaceViewPlane;
			break;

		case EProjectionType::RuntimeStaticSurfacePlaneInverted:
			ViewDir = -SurfaceViewPlane; // Frustum projection fix (back-side view planes)
			break;

		case EProjectionType::StaticSurfaceNormal:
			ViewDir = SurfaceViewNormal; // Use fixed surface view normal
			break;

		case EProjectionType::RuntimeStaticSurfaceNormalInverted:
			ViewDir = -SurfaceViewNormal; // Frustum projection fix (back-side view planes)
			break;

		default:
			break;
	}

	OutViewMatrix = FRotationMatrix::MakeFromXZ(ViewDir, FVector(0.f, 0.f, 1.f));
	OutViewMatrix.SetOrigin(EyeOrigin); // Finally set view origin to eye location
}

void FMPCDIWarp::CalcView(EStereoMode StereoMode, IMPCDI::FFrustum& OutFrustum, FVector& OutViewDirection, FVector& OutViewOrigin, FVector& OutEyeOrigin) const
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

bool FMPCDIWarp::UpdateProjectionType(EProjectionType& ProjectionType) const
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

bool FMPCDIWarp::GetFrustum_A3D(IMPCDI::FFrustum &OutFrustum, float WorldScale, float ZNear, float ZFar)
{
	BeginBuildFrustum(OutFrustum);
	if (!IsValidWarpData())
	{
		return false;
	}

	const int FrustumCacheDepth = (int)CVarMPCDIFrustumCacheDepth.GetValueOnAnyThread();

	bool bIsFrustumCacheUsed = FrustumCacheDepth > 0 || FrustumCache.Num() > 0;

	if(bIsFrustumCacheUsed && !IsFrustumCacheDisabled())
	{
		FScopeLock lock(&DataGuard);

		if (FrustumCacheDepth == 0)
		{
			// Cache disabled, clear old values
			FrustumCache.Empty();
		}
		else
		{
			// Try to use old frustum values from cache (reduce CPU cost)
			if (FrustumCache.Num() > 0)
			{
				// If frustum cache used ,try to search valid value
				const float FrustumCachePrecision = (float)CVarMPCDIFrustumCachePrecision.GetValueOnAnyThread();
				for (int i = 0; i < FrustumCache.Num(); i++)
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

	// Build projection frustum:
	if (ProjectionAuto == 0)
	{
		//Directly build frustum
		CalcViewProjection(ProjectionMode, OutFrustum, ViewDirection, ViewOrigin, EyeOrigin, Local2world);
		World2Local = Local2world.Inverse();
		CalcFrustum(FrustumType, OutFrustum, World2Local);
	}
	else
	{
		// Frustum projection with auto-fix (back-side view planes)
		// Projection type changed runtime. Validate frustum points, all must be over view plane:
		EProjectionType BaseProjectionMode = ProjectionMode;

		// Optimize for PerfectCPU:
		if(FrustumType == EFrustumType::PerfectCPU && GetWarpGeometryType() == EWarpGeometryType::PFM_Texture)
		{
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

		// Search valid projection mode:
		if (BaseProjectionMode == ProjectionMode)
		{				
			while (true)
			{
				//Full check for bad projection:
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
	OutFrustum.OutCameraRotation = Local2world.Rotator();
	OutFrustum.OutCameraOrigin   = Local2world.GetOrigin();
		
	OutFrustum.ProjectionMatrix = GetProjectionMatrixAssymetric(OutFrustum.ProjectionAngles, ZNear, ZFar);
	OutFrustum.UVMatrix = World2Local * Game2Render * OutFrustum.ProjectionMatrix;
	
	// Update frustum cache
	if(bIsFrustumCacheUsed && !IsFrustumCacheDisabled())
	{
		FScopeLock lock(&DataGuard);
		
		// Store current used frustum value to cache
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

bool FMPCDIWarp::CalcFrustum_simpleAABB(const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
{
	bool bResult = true;
	// Search a camera space frustum
	for (int i = 0; i < 8; ++i)
	{
		const FVector4& Pts = OutFrustum.AABBoxPts[i];
		if (!CalcFrustumFromVertex(Pts, World2Local, Top, Bottom, Left, Right))
		{
			bResult = false;
		}
	}

	return bResult;
}
