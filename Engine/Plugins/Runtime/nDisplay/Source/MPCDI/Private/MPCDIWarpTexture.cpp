// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIWarpTexture.h"
#include "MPCDIData.h"
#include "MPCDIHelpers.h"
#include "MPCDIWarpHelpers.h"

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

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"


void FMPCDIWarpTexture::BeginBuildFrustum(IMPCDI::FFrustum& OutFrustum)
{
	OutFrustum.MeshToCaveMatrix = FMatrix::Identity;
}

void FMPCDIWarpTexture::BeginRender(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit)
{
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
}

void FMPCDIWarpTexture::FinishRender(FRHICommandListImmediate& RHICmdList)
{
	// Render quad
	FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
}

// Return false, for invalid view planes (for any warpmesh point is under view plane)
bool FMPCDIWarpTexture::CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
{
	const FVector4* v = (FVector4*)GetData();

	if (TextureBoxCache.Num() < 1)
	{
		// Build new texture box cache:
		FScopeLock lock(&DataGuard);
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
		
void FMPCDIWarpTexture::BuildAABBox()
{
	ResetAABB();

	{
		// Clear caches:
		FScopeLock lock(&DataGuard);

		TextureBoxCache.Empty();
		FrustumCache.Empty();
	}

	{
		// Calc static normal and plane
		const FVector4* v = (FVector4*)GetData();
		FValidPFMPoint PFMPoints(v, GetWidth(), GetHeight());

		SurfaceViewNormal = PFMPoints.GetSurfaceViewNormal();
		SurfaceViewPlane  = PFMPoints.GetSurfaceViewPlane();
	}

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
