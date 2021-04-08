// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterWarpBlendExporter_WarpMap.h"

#include "Render/DisplayClusterRenderTexture.h"
#include "Blueprints/MPCDIGeometryData.h"

bool FDisplayClusterWarpBlendExporter_WarpMap::ExportWarpMap(IDisplayClusterRenderTexture* InWarpMap, struct FMPCDIGeometryExportData& Dst)
{
	if (InWarpMap == nullptr || InWarpMap->IsValid() == false)
	{
		return false;
	}

	const uint32 Width = InWarpMap->GetWidth();
	const uint32 Height = InWarpMap->GetHeight();

	const FVector4* WarpData = (FVector4*)InWarpMap->GetData();

	const int DownScaleFactor = 1;

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
			for (uint32 i = 0; i < MaxWidth; ++i)
			{
				int idx = ((i * DownScaleFactor) + (j * DownScaleFactor) * Width);
				const FVector4& v = WarpData[idx];
				if (v.W > 0)
				{
					Dst.Vertices.Add(FVector(v.X, v.Y, v.Z));
					VIndexMap.Add(idx, VIndex++);

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
			for (uint32 i = 0; i < MaxWidth - 1; ++i)
			{
				int idx[4];

				idx[0] = ((i)*DownScaleFactor + (j)*DownScaleFactor * Width);
				idx[1] = ((i + 1) * DownScaleFactor + (j)*DownScaleFactor * Width);
				idx[2] = ((i)*DownScaleFactor + (j + 1) * DownScaleFactor * Width);
				idx[3] = ((i + 1) * DownScaleFactor + (j + 1) * DownScaleFactor * Width);

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

	return true;
};
