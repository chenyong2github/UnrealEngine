// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADData.h"
#include "CADSceneGraph.h"

#include "TUniqueTechSoftObj.h"

#ifdef USE_TECHSOFT_SDK

namespace TechSoftInterfaceUtils
{
	class FTechSoftTessellationExtractor
	{
	public:
		FTechSoftTessellationExtractor(const A3DTess3D* InTessellationPtr)
			: TessellationPtr(InTessellationPtr)
		{
		}

		bool  FillBodyMesh(CADLibrary::FBodyMesh& BodyMesh, double FileUnit)
		{
			FillVertexArray(FileUnit, BodyMesh.VertexArray);

			if (BodyMesh.VertexArray.Num() == 0)
			{
				return false;
			}

			FillFaceArray(BodyMesh);

			return BodyMesh.Faces.Num() > 0 ? true : false;
		}

	private:
		void FillVertexArray(double FileUnit, TArray<FVector>& VertexArray)
		{
			using namespace CADLibrary;

			TUniqueTSObj<A3DTessBaseData> TessellationBaseData(TessellationPtr);

			if (!TessellationBaseData.IsValid() || TessellationBaseData->m_uiCoordSize == 0)
			{
				return;
			}

			int32 VertexCount = TessellationBaseData->m_uiCoordSize / 3;
			VertexArray.Reserve(VertexCount);

			double* Coordinates = TessellationBaseData->m_pdCoords;
			for (unsigned int Index = 0; Index < TessellationBaseData->m_uiCoordSize; ++Index)
			{
				Coordinates[Index] *= FileUnit;
			}

			for (unsigned int Index = 0; Index < TessellationBaseData->m_uiCoordSize; Index += 3)
			{
				VertexArray.Emplace(Coordinates[Index], Coordinates[Index + 1], Coordinates[Index + 2]);
			}
		}

		// #ueent_techsoft: TODO: Make it more in line with the actual implementation of FillFaceArray
		uint32 CountTriangles(const A3DTessFaceData& FaceTessData)
		{
			const int32 TessellationFaceDataWithTriangle = 0x2222;
			const int32 TessellationFaceDataWithFan = 0x4444;
			const int32 TessellationFaceDataWithStripe = 0x8888;
			const int32 TessellationFaceDataWithOneNormal = 0xE0E0;

			uint32 UsedEntitiesFlags = FaceTessData.m_usUsedEntitiesFlags;

			uint32 TriangleCount = 0;
			uint32 FaceSetIndex = 0;
			if (UsedEntitiesFlags & TessellationFaceDataWithTriangle)
			{
				TriangleCount += FaceTessData.m_puiSizesTriangulated[FaceSetIndex];
				FaceSetIndex++;
			}

			if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
			{
				if (UsedEntitiesFlags & TessellationFaceDataWithFan)
				{
					uint32 LastFanIndex = 1 + FaceSetIndex + FaceTessData.m_puiSizesTriangulated[FaceSetIndex];
					FaceSetIndex++;
					for (; FaceSetIndex < LastFanIndex; FaceSetIndex++)
					{
						uint32 FanSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
						TriangleCount += (FanSize - 2);
					}
				}
			}

			if (FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex)
			{
				FaceSetIndex++;
				for (; FaceSetIndex < FaceTessData.m_uiSizesTriangulatedSize; FaceSetIndex++)
				{
					uint32 StripeSize = (FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalMask);
					TriangleCount += (StripeSize - 2);
				}
			}
			return TriangleCount;
		}

#pragma optimize ("", off)
		void FillFaceArray(CADLibrary::FBodyMesh& BodyMesh)
		{
			using namespace CADLibrary;

			TArray<FTessellationData>& Faces = BodyMesh.Faces;

			TUniqueTSObj<A3DTess3DData> TessellationData(TessellationPtr);

			if (!TessellationData.IsValid() || TessellationData->m_uiFaceTessSize == 0)
			{
				return;
			}

			TessellationNormals = TessellationData->m_pdNormals;
			TessellationTexCoords = TessellationData->m_pdTextureCoords;
			TriangulatedIndexes = TessellationData->m_puiTriangulatedIndexes;

			for (unsigned int Index = 0; Index < TessellationData->m_uiFaceTessSize; ++Index)
			{
				const A3DTessFaceData& FaceTessData = TessellationData->m_psFaceTessData[Index];
				FTessellationData& Tessellation = Faces.Emplace_GetRef();

				Tessellation.MaterialName = 0xffffffff; // Assumes -1 is an invalid value for m_puiStyleIndexes
				if (FaceTessData.m_uiStyleIndexesSize == 1)
				{
					// Store the StyleIndex on the MaterialName. It will be processed after tessellation
					Tessellation.MaterialName = FaceTessData.m_puiStyleIndexes[0];
				}

				// Pre-allocate memory for triangles' data
				uint32 TriangleCount = CountTriangles(FaceTessData);
				Tessellation.PositionIndices.Reserve(3 * TriangleCount);
				Tessellation.VertexIndices.Reserve(3 * TriangleCount);
				Tessellation.NormalArray.Reserve(3 * TriangleCount);
				if (FaceTessData.m_uiTextureCoordIndexesSize > 0)
				{
					Tessellation.TexCoordArray.Reserve(3 * TriangleCount);
				}

				uint32 UsedEntitiesFlags = FaceTessData.m_usUsedEntitiesFlags;

				LastTrianguleIndex = FaceTessData.m_uiStartTriangulated;
				LastVertexIndex = 0;

				uint32 FaceSetIndex = 0;
				bool bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangle)
				{
					AddFaceTriangle(Tessellation, FaceTessData.m_puiSizesTriangulated[FaceSetIndex++], LastTrianguleIndex, LastVertexIndex);
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleOneNormal)
				{
					AddFaceTriangleWithUniqueNormal(Tessellation, FaceTessData.m_puiSizesTriangulated[FaceSetIndex++], LastTrianguleIndex, LastVertexIndex);
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleTextured)
				{
					AddFaceTriangleWithTexture(Tessellation, FaceTessData.m_puiSizesTriangulated[FaceSetIndex++], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleOneNormalTextured)
				{
					AddFaceTriangleWithUniqueNormalAndTexture(Tessellation, FaceTessData.m_puiSizesTriangulated[FaceSetIndex++], FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleFan)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
						AddFaceTriangleFan(Tessellation, VertexCount, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleFanOneNormal)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						ensure((FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle) != 0);

						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
						AddFaceTriangleFanWithUniqueNormal(Tessellation, VertexCount, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleFanTextured)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
						AddFaceTriangleFanWithTexture(Tessellation, VertexCount, FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleFanOneNormalTextured)
				{
					uint32 FanCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
					for (uint32 FanIndex = 0; FanIndex < FanCount; ++FanIndex)
					{
						ensure((FaceTessData.m_puiSizesTriangulated[FaceSetIndex] & kA3DTessFaceDataNormalSingle) != 0);

						uint32 VertexCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
						AddFaceTriangleFanWithUniqueNormalAndTexture(Tessellation, VertexCount, FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleStripe)
				{
					A3DUns32 StripeSize = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
					for (A3DUns32 StripeIndex = 0; StripeIndex < StripeSize; ++StripeIndex)
					{
						A3DUns32 PointCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
						AddFaceTriangleStripe(Tessellation, PointCount, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleStripeOneNormal)
				{
					A3DUns32 StripeSize = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
					for (A3DUns32 StripeIndex = 0; StripeIndex < StripeSize; ++StripeIndex)
					{
						// #ueent_techsoft: There are some specificities with the case of oneNormal
						//					See TessConnector.cpp around line 237
						//	bool bIsOneNormal = (pFaceTessData->m_puiSizesTriangulated[uiCurrentSize] & kA3DTessFaceDataNormalSingle) != 0;????
						//	A3DUns32* pStripeNormalIndice = puiTriangulatedIndexes;
						//	A3DUns32 uiNbPoint = pFaceTessData->m_puiSizesTriangulated[uiCurrentSize++] & kA3DTessFaceDataNormalMask;
						A3DUns32 PointCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
						AddFaceTriangleStripeWithUniqueNormal(Tessellation, PointCount, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleStripeTextured)
				{
					A3DUns32 StripeSize = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
					for (A3DUns32 StripeIndex = 0; StripeIndex < StripeSize; ++StripeIndex)
					{
						A3DUns32 PointCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++];
						AddFaceTriangleStripeWithTexture(Tessellation, PointCount, FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				if (bMustProcess && UsedEntitiesFlags & kA3DTessFaceDataTriangleStripeOneNormalTextured)
				{
					A3DUns32 StripeSize = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
					for (A3DUns32 StripeIndex = 0; StripeIndex < StripeSize; ++StripeIndex)
					{
						A3DUns32 PointCount = FaceTessData.m_puiSizesTriangulated[FaceSetIndex++] & kA3DTessFaceDataNormalMask;
						AddFaceTriangleStripeWithUniqueNormalAndTexture(Tessellation, PointCount, FaceTessData.m_uiTextureCoordIndexesSize, LastTrianguleIndex, LastVertexIndex);
					}
					bMustProcess = FaceTessData.m_uiSizesTriangulatedSize > FaceSetIndex;
				}

				ensure(!bMustProcess);
			}
		}
#pragma optimize ("", on)

		typedef double A3DDouble;
		bool AddFace(int32 FaceIndex[3], CADLibrary::FTessellationData& Tessellation, int32& InOutVertexIndex)
		{
			if (FaceIndex[0] == FaceIndex[1] || FaceIndex[0] == FaceIndex[2] || FaceIndex[1] == FaceIndex[2])
			{
				return false;
			}

			for (int32 Index = 0; Index < 3; ++Index)
			{
				Tessellation.VertexIndices.Add(InOutVertexIndex++);
			}
			Tessellation.PositionIndices.Append(FaceIndex, 3);
			return true;
		};

		void AddNormals(const A3DDouble* Normals, const int32 Indices[3], TArray<FVector>& NormalsArray)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				int32 NormalIndex = Indices[Index];
				NormalsArray.Emplace(Normals[NormalIndex], Normals[NormalIndex + 1], Normals[NormalIndex + 2]);
			}
		};

		void AddTextureCoordinates(const A3DDouble* TextureCoords, const int32 Indices[3], TArray<FVector2D>& TessellationTextures)
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				int32 TextureIndex = Indices[Index];
				TessellationTextures.Emplace(TextureCoords[TextureIndex], TextureCoords[TextureIndex + 1]);
			}
		};

		void AddFaceTriangle(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, uint32& InOutStartIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };

			// Get Triangles
			for (uint32 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[0] = TriangulatedIndexes[InOutStartIndex++];
				FaceIndex[0] = TriangulatedIndexes[InOutStartIndex++] / 3;
				NormalIndex[1] = TriangulatedIndexes[InOutStartIndex++];
				FaceIndex[1] = TriangulatedIndexes[InOutStartIndex++] / 3;
				NormalIndex[2] = TriangulatedIndexes[InOutStartIndex++];
				FaceIndex[2] = TriangulatedIndexes[InOutStartIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}
			}
		}

		void AddFaceTriangleWithUniqueNormal(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };

			// Get Triangles
			for (uint32 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
				NormalIndex[1] = NormalIndex[0];
				NormalIndex[2] = NormalIndex[0];

				FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
				FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (!AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					continue;
				}

				AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
			}
		}

		void AddFaceTriangleWithUniqueNormalAndTexture(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutStartIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };
			int32 TextureIndex[3] = { 0, 0, 0 };

			// Get Triangles
			for (uint32 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[0] = TriangulatedIndexes[InOutStartIndex++];
				NormalIndex[1] = NormalIndex[0];
				NormalIndex[2] = NormalIndex[0];

				TextureIndex[0] = TriangulatedIndexes[InOutStartIndex];
				InOutStartIndex += TextureCount;
				FaceIndex[0] = TriangulatedIndexes[InOutStartIndex++] / 3;
				TextureIndex[1] = TriangulatedIndexes[InOutStartIndex];
				InOutStartIndex += TextureCount;
				FaceIndex[1] = TriangulatedIndexes[InOutStartIndex++] / 3;
				TextureIndex[2] = TriangulatedIndexes[InOutStartIndex];
				InOutStartIndex += TextureCount;
				FaceIndex[2] = TriangulatedIndexes[InOutStartIndex++] / 3;

				if (!AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					continue;
				}

				AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				AddTextureCoordinates(TessellationTexCoords, TextureIndex, Tessellation.TexCoordArray);
			}
		}

		void AddFaceTriangleWithTexture(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, const uint32 InTextureCount, uint32& InOutStartIndex, int32& inOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };
			int32 TextureIndex[3] = { 0, 0, 0 };

			// Get Triangles
			for (uint64 TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[0] = TriangulatedIndexes[InOutStartIndex++];
				TextureIndex[0] = TriangulatedIndexes[InOutStartIndex];
				InOutStartIndex += InTextureCount;
				FaceIndex[0] = TriangulatedIndexes[InOutStartIndex++] / 3;
				NormalIndex[1] = TriangulatedIndexes[InOutStartIndex++];
				TextureIndex[1] = TriangulatedIndexes[InOutStartIndex];
				InOutStartIndex += InTextureCount;
				FaceIndex[1] = TriangulatedIndexes[InOutStartIndex++] / 3;
				NormalIndex[2] = TriangulatedIndexes[InOutStartIndex++];
				TextureIndex[2] = TriangulatedIndexes[InOutStartIndex];
				InOutStartIndex += InTextureCount;
				FaceIndex[2] = TriangulatedIndexes[InOutStartIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, inOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
					AddTextureCoordinates(TessellationTexCoords, TextureIndex, Tessellation.TexCoordArray);
				}
			}
		}

		void AddFaceTriangleFan(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
			NormalIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++];
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (unsigned long TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++];
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				NormalIndex[1] = NormalIndex[2];
				FaceIndex[1] = FaceIndex[2];
			}
		}

		void AddFaceTriangleFanWithUniqueNormal(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& /*LastVertexIndex*/)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			NormalIndex[1] = NormalIndex[0];
			NormalIndex[2] = NormalIndex[0];

			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			// Get Triangles
			for (uint32 TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, LastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				FaceIndex[1] = FaceIndex[2];
			}
		}

		void AddFaceTriangleFanWithUniqueNormalAndTexture(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };
			int32 TextureIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			NormalIndex[1] = NormalIndex[0];
			NormalIndex[2] = NormalIndex[0];

			TextureIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			TextureIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (uint32 TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				TextureIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex];
				InOutLastTriangleIndex += TextureCount;
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
					AddTextureCoordinates(TessellationTexCoords, TextureIndex, Tessellation.TexCoordArray);
				}

				FaceIndex[1] = FaceIndex[2];
				TextureIndex[1] = TextureIndex[2];
			}
		}

		void AddFaceTriangleFanWithTexture(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };
			int32 TextureIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			TextureIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			NormalIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++];
			TextureIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (uint32 TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++];
				TextureIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex];
				InOutLastTriangleIndex += TextureCount;
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
					AddTextureCoordinates(TessellationTexCoords, TextureIndex, Tessellation.TexCoordArray);
				}

				NormalIndex[1] = NormalIndex[2];
				TextureIndex[1] = TextureIndex[2];
				FaceIndex[1] = FaceIndex[2];
			}
		}

		void AddFaceTriangleStripe(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& /*LastVertexIndex*/)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
			NormalIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++];
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (unsigned long TriangleIndex = 2; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++];
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, LastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				TriangleIndex++;
				if (TriangleIndex == InTriangleCount)
				{
					break;
				}

				Swap(FaceIndex[1], FaceIndex[2]);
				Swap(NormalIndex[1], NormalIndex[2]);

				NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
				FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, LastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				Swap(FaceIndex[0], FaceIndex[1]);
				Swap(NormalIndex[0], NormalIndex[1]);
			}
		}

		void AddFaceTriangleStripeWithTexture(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };
			int32 TextureIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			TextureIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
			NormalIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++];
			TextureIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (unsigned long TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				NormalIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++];
				TextureIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex];
				InOutLastTriangleIndex += TextureCount;
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
					AddTextureCoordinates(TessellationTexCoords, TextureIndex, Tessellation.TexCoordArray);
				}

				TriangleIndex++;
				if (TriangleIndex == InTriangleCount)
				{
					break;
				}

				Swap(FaceIndex[1], FaceIndex[2]);
				Swap(NormalIndex[1], NormalIndex[2]);
				Swap(TextureIndex[1], TextureIndex[2]);

				NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
				FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				Swap(FaceIndex[0], FaceIndex[1]);
				Swap(NormalIndex[0], NormalIndex[1]);
				Swap(TextureIndex[0], TextureIndex[1]);
			}
		}

		void AddFaceTriangleStripeWithUniqueNormal(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			NormalIndex[1] = NormalIndex[0];
			NormalIndex[2] = NormalIndex[0];

			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (unsigned long TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				TriangleIndex++;
				if (TriangleIndex == InTriangleCount)
				{
					break;
				}

				Swap(FaceIndex[1], FaceIndex[2]);

				NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
				FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				Swap(FaceIndex[0], FaceIndex[1]);
			}
		}

		void AddFaceTriangleStripeWithUniqueNormalAndTexture(CADLibrary::FTessellationData& Tessellation, const uint32 InTriangleCount, const uint32 TextureCount, uint32& InOutLastTriangleIndex, int32& InOutLastVertexIndex)
		{
			int32 FaceIndex[3] = { 0, 0, 0 };
			int32 NormalIndex[3] = { 0, 0, 0 };
			int32 TextureIndex[3] = { 0, 0, 0 };

			NormalIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++];
			NormalIndex[1] = NormalIndex[0];
			NormalIndex[2] = NormalIndex[0];

			TextureIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			TextureIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex];
			InOutLastTriangleIndex += TextureCount;
			FaceIndex[1] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

			for (unsigned long TriangleIndex = 0; TriangleIndex < InTriangleCount; TriangleIndex++)
			{
				TextureIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex];
				InOutLastTriangleIndex += TextureCount;
				FaceIndex[2] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
					AddTextureCoordinates(TessellationTexCoords, TextureIndex, Tessellation.TexCoordArray);
				}

				TriangleIndex++;
				if (TriangleIndex == InTriangleCount)
				{
					break;
				}

				Swap(FaceIndex[1], FaceIndex[2]);
				Swap(TextureIndex[1], TextureIndex[2]);

				FaceIndex[0] = TriangulatedIndexes[InOutLastTriangleIndex++] / 3;

				if (AddFace(FaceIndex, Tessellation, InOutLastVertexIndex))
				{
					AddNormals(TessellationNormals, NormalIndex, Tessellation.NormalArray);
				}

				Swap(FaceIndex[0], FaceIndex[1]);
				Swap(TextureIndex[0], TextureIndex[1]);
			}
		}

	private:
		const A3DTess3D* TessellationPtr;

		uint32 LastTrianguleIndex = 0;
		int32 LastVertexIndex = 0;
		A3DUns32* TriangulatedIndexes = nullptr;
		A3DDouble* TessellationNormals;
		A3DDouble* TessellationTexCoords;				/*!< Array of \ref A3DDouble, as texture coordinates. */
	};
} // ns TechSoftUtils

#endif