// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterWarpBlendExporter_WarpMap.h"

#include "Render/DisplayClusterRenderTexture.h"
#include "Blueprints/MPCDIGeometryData.h"

bool FDisplayClusterWarpBlendExporter_WarpMap::ExportWarpMap(IDisplayClusterRenderTexture* InWarpMap, struct FMPCDIGeometryExportData& Dst, uint32 InMaxDimension)
{
	if (InWarpMap == nullptr || InWarpMap->IsValid() == false)
	{
		return false;
	}

	const uint32 Width = InWarpMap->GetWidth();
	const uint32 Height = InWarpMap->GetHeight();

	const FVector4* WarpData = (FVector4*)InWarpMap->GetData();

	uint32 DownScaleFactor = 1;

	if (InMaxDimension > 0)
	{
		uint32 MeshDimension = FMath::Max(Width, Height);
		if (MeshDimension > InMaxDimension)
		{
			DownScaleFactor = FMath::RoundToInt(float(MeshDimension) / InMaxDimension);
		}
	}


	TMap<int, int> VIndexMap;
	int VIndex = 0;

	uint32 MaxHeight = Height / DownScaleFactor;
	uint32 MaxWidth = Width / DownScaleFactor;

	{
		//Pts + Normals + UV
		const float ScaleU = 1.0f / float(MaxWidth);
		const float ScaleV = 1.0f / float(MaxHeight);

		for (uint32 j = 0; j < MaxHeight; ++j)
		{
			uint32 MeshY = (j == (MaxHeight - 1)) ? Height : (j * DownScaleFactor);
			for (uint32 i = 0; i < MaxWidth; ++i)
			{
				uint32 MeshX = (i == (MaxWidth - 1)) ? Width : (i * DownScaleFactor);

				int SrcIdx = MeshX + (MeshY) * Width;
				const FVector4& v = WarpData[SrcIdx];
				if (v.W > 0)
				{
					Dst.Vertices.Add(FVector(v.X, v.Y, v.Z));
					VIndexMap.Add(SrcIdx, VIndex++);

					Dst.UV.Add(FVector2D(
						float(i) * ScaleU,
						float(j) * ScaleV
					));

					Dst.Normal.Add(FVector(0, 0, 0)); // Fill on face pass
				}
			}
		}
	}

	{
		//faces
		for (uint32 j = 0; j < MaxHeight - 1; ++j)
		{
			uint32 MeshY     = (j == (MaxHeight - 1)) ? Height : (j * DownScaleFactor);
			uint32 NextMeshY = ((j+1) == (MaxHeight - 1)) ? Height : ((j+1) * DownScaleFactor);

			for (uint32 i = 0; i < MaxWidth - 1; ++i)
			{
				uint32 MeshX = (i == (MaxWidth - 1)) ? Width : (i * DownScaleFactor);
				uint32 NextMeshX = ((i + 1) == (MaxWidth - 1)) ? Width : ((i + 1) * DownScaleFactor);

				int idx[4];

				idx[0] = (MeshX + MeshY * Width);
				idx[1] = (NextMeshX + MeshY * Width);
				idx[2] = (MeshX + NextMeshY * Width);
				idx[3] = (NextMeshX + NextMeshY * Width);

				for (int a = 0; a < 4; a++)
				{
					if (VIndexMap.Contains(idx[a]))
					{
						idx[a] = VIndexMap[idx[a]];
					}
					else
					{
						idx[a] = -1;
					}
				}

				if (idx[0]>=0 && idx[2] >= 0 && idx[3] >= 0)
				{
					Dst.PostAddFace(idx[0], idx[2], idx[3]);
				}

				if (idx[3] >= 0 && idx[1] >= 0 && idx[0] >= 0)
				{
					Dst.PostAddFace(idx[3], idx[1], idx[0]);
				}
			}
		}
	}

	return true;
};
