// Copyright Epic Games, Inc. All Rights Reserved.


#include "OpMeshFormat.h"
#include "Platform.h"
#include "MutableMath.h"
#include "MeshPrivate.h"
#include "ConvertData.h"
#include "MutableTrace.h"


namespace mu
{


	//-------------------------------------------------------------------------------------------------
	// Eric Lengyel method's
	// http://www.terathon.com/code/tangent.html
	//-------------------------------------------------------------------------------------------------
	namespace
	{

#define MUTABLE_VERTEX_MERGE_TEX_RANGE          1024
#define MUTABLE_TANGENT_GENERATION_EPSILON_1    0.000001f
#define MUTABLE_TANGENT_GENERATION_EPSILON_2    0.001f
#define MUTABLE_TANGENT_MIN_AXIS_DIFFERENCE     0.999f

		struct TVertex
		{

			TVertex() = default;

			TVertex(const vec3<float>& p, const vec3<float>& n, const vec2<float>& t)
			{
				pos = p;
				nor = n;
				tex = t;
			}

			vec3<float> pos;
			vec3<float> nor;
			vec2<float> tex;

			inline bool operator< (const TVertex& other) const
			{
				if (pos < other.pos)
					return true;
				if (other.pos < pos)
					return false;

				if (nor < other.nor)
					return true;
				if (other.nor < nor)
					return false;

				// Compare the texture coordinates with a particular precission.
				vec2<int> uv0 = vec2<int>(int(tex[0] * MUTABLE_VERTEX_MERGE_TEX_RANGE),
					int(tex[1] * MUTABLE_VERTEX_MERGE_TEX_RANGE));
				vec2<int> uv1 = vec2<int>(int(other.tex[0] * MUTABLE_VERTEX_MERGE_TEX_RANGE),
					int(other.tex[1] * MUTABLE_VERTEX_MERGE_TEX_RANGE));

				if (uv0 < uv1)
					return true;
				if (uv1 < uv0)
					return false;

				return false;
			}

			inline bool operator== (const TVertex& other) const
			{
				return  !((*this) < other)
					&&
					!(other < (*this));
			}
		};


		struct TFace
		{
			vec3<float> T;
			vec3<float> B;
			vec3<float> N;

			TFace()
				: T(0, 0, 0)
				, B(0, 0, 0)
				, N(0, 0, 0)
			{}

			TFace
			(
				const vec3<float>& v1,
				const vec3<float>& v2,
				const vec3<float>& v3,
				const vec2<float>& w1,
				const vec2<float>& w2,
				const vec2<float>& w3
			)
			{
				vec3<float> E1 = v2 - v1;
				vec3<float> E2 = v3 - v1;

				vec2<float> UV1 = w2 - w1;
				vec2<float> UV2 = w3 - w1;

				float  UVdet = UV1[0] * UV2[1] - UV2[0] * UV1[1];

				N = normalise(cross(E1, E2));

				if (!(fabs(UVdet) <= MUTABLE_TANGENT_GENERATION_EPSILON_1))
				{
					double r = 1.0 / UVdet;

					T = vec3<float>
						(
							(UV2[1] * E1[0] - UV1[1] * E2[0]),
							(UV2[1] * E1[1] - UV1[1] * E2[1]),
							(UV2[1] * E1[2] - UV1[1] * E2[2])
							);

					B = vec3<float>
						(
							(UV1[0] * E2[0] - UV2[0] * E1[0]),
							(UV1[0] * E2[1] - UV2[0] * E1[1]),
							(UV1[0] * E2[2] - UV2[0] * E1[2])
							);

					T = T * float(r);
					B = B * float(r);

					T = normalise(T);
					B = normalise(B);
				}
				else
				{
					T = vec3<float>(0, 0, 0);
					B = vec3<float>(0, 0, 0);
					N = vec3<float>(0, 0, 0);
				}
			}

		};


		void RebuildTangents
		(
			const FMeshBufferSet& IndexBuffers,
			FMeshBufferSet& VertexBuffers
		)
		{
			// Because of these, this method only works for a specific mesh format
			UntypedMeshBufferIteratorConst vertex(VertexBuffers, MBS_POSITION, 0);
			UntypedMeshBufferIteratorConst normal(VertexBuffers, MBS_NORMAL, 0);
			UntypedMeshBufferIteratorConst texcoord(VertexBuffers, MBS_TEXCOORDS, 0);

			int vertexCount = VertexBuffers.GetElementCount();

			// Group similar vertices
			vector< TVertex > vertexData;
			vertexData.reserve(vertexCount);
			vector< uint32_t > lVertexGroups(vertexCount, 0);

			//Agrupamos los vertices por posicion, normal, texcoord
			for (int32_t i = 0; i < vertexCount; ++i)
			{
				TVertex v((vertex + i).GetAsVec4f().xyz(),
					(normal + i).GetAsVec4f().xyz(),
					(texcoord + i).GetAsVec4f().xy());
				vector<TVertex>::const_iterator it = std::find(vertexData.begin(),
					vertexData.end(),
					v);

				size_t pos = 0;
				if (it != vertexData.end())
				{
					pos = it - vertexData.begin();
				}
				else
				{
					pos = vertexData.size();
					vertexData.push_back(v);
				}

				lVertexGroups[i] = (int)pos;
			}

			// Calculate the tangent space
			MeshBufferIterator<MBF_FLOAT32, float, 3> tangent(VertexBuffers, MBS_TANGENT, 0);
			MeshBufferIterator<MBF_FLOAT32, float, 3> bitangent(VertexBuffers, MBS_BINORMAL, 0);
			UntypedMeshBufferIteratorConst indices(IndexBuffers, MBS_VERTEXINDEX, 0);
			int indexCount = IndexBuffers.GetElementCount();

			vector<TFace> vertexFaces(indexCount);

			// Worst case: a group for every vertex
			vector< vector< TFace > > grp_faces(indexCount);

			for (long faceidx = 0, count = indexCount / 3; faceidx < count; faceidx++)
			{
				long i1 = 0;
				long i2 = 0;
				long i3 = 0;

				if (indices.GetFormat() == MBF_UINT16)
				{
					auto pIndices = reinterpret_cast<const uint16_t*>(indices.ptr());
					i1 = pIndices[faceidx * 3];
					i2 = pIndices[faceidx * 3 + 1];
					i3 = pIndices[faceidx * 3 + 2];
				}
				else if (indices.GetFormat() == MBF_UINT32)
				{
					auto pIndices = reinterpret_cast<const uint32_t*>(indices.ptr());
					i1 = pIndices[faceidx * 3];
					i2 = pIndices[faceidx * 3 + 1];
					i3 = pIndices[faceidx * 3 + 2];
				}
				else
				{
					check(false);
				}

				TFace face;

				const vec3<float> v1 = (vertex + (int)i1).GetAsVec4f().xyz();
				const vec3<float> v2 = (vertex + (int)i2).GetAsVec4f().xyz();
				const vec3<float> v3 = (vertex + (int)i3).GetAsVec4f().xyz();

				// Compute face t & b
				const vec2<float> w1 = (texcoord + (int)i1).GetAsVec4f().xy();
				const vec2<float> w2 = (texcoord + (int)i2).GetAsVec4f().xy();
				const vec2<float> w3 = (texcoord + (int)i3).GetAsVec4f().xy();
				face = TFace(v1, v2, v3, w1, w2, w3);

				if (length(face.N) != 0)
				{
					vertexFaces[i1] = vertexFaces[i2] = vertexFaces[i3] = face;

					grp_faces[lVertexGroups[i1]].push_back(face);
					grp_faces[lVertexGroups[i2]].push_back(face);
					grp_faces[lVertexGroups[i3]].push_back(face);
				}
			}

			for (long vtxidx = 0; vtxidx < vertexCount; vtxidx++)
			{
				const vec3<float> n = (normal + (int)vtxidx).GetAsVec4f().xyz();
				const uint32_t group = lVertexGroups[vtxidx];

				vec3<float> lTangent(0, 0, 0);
				vec3<float> lBiTangent(0, 0, 0);

				const vector< TFace >& faces = grp_faces[group];

				const TFace& vFace = vertexFaces[vtxidx];

				// Ignore mirrors
				for (unsigned i = 0; i < faces.size(); ++i)
				{
					const float DN = dot(vFace.N, faces[i].N);
					const float DT = dot(vFace.T, faces[i].T);
					const float DB = dot(vFace.B, faces[i].B);

					if (DN > 0 && DT > 0 && DB > 0)
					{
						lTangent += faces[i].T;
						lBiTangent += faces[i].B;
					}
				}

				vec3<float> ortogonalizedTangent
					= normalise(lTangent
						- (n * dot(n, lTangent)));

				vec3<float> ortogonalizedBiTangent
					= normalise(lBiTangent
						- (n * dot(n, lBiTangent))
						- (ortogonalizedTangent * dot(ortogonalizedTangent, lBiTangent)));

				// this is where we can do a final check for zero length vectors
				// and set them to something appropriate
				float lenTan = length(ortogonalizedTangent);
				float lenBin = length(ortogonalizedBiTangent);

				if ((lenTan <= MUTABLE_TANGENT_GENERATION_EPSILON_2)
					|| (lenBin <= MUTABLE_TANGENT_GENERATION_EPSILON_2)) //should be approx 1.0f
				{
					// the tangent space is ill defined at this vertex
					// so we can generate a valid one based on the normal vector,
					// which I'm assuming is valid!
					if (lenTan > 0.5f)
					{
						// the tangent is valid, so we can just use that to calculate the binormal
						ortogonalizedBiTangent = cross(n, ortogonalizedTangent);
					}
					else if (lenBin > 0.5)
					{
						// the binormal is good and we can use it to calculate the tangent
						ortogonalizedTangent = cross(ortogonalizedBiTangent, n);
					}
					else
					{
						// both vectors are invalid, so we should create something
						// that is at least valid if not correct
						// I'm checking two possible axis, because the normal could be one of them,
						// and we want to chose a different one to start making our valid basis.
						// I can find out which is further away from it by checking the dot product
						vec3<float> startAxis;

						if (dot(vec3<float>(1, 0, 0), n) < dot(vec3<float>(0, 1, 0), n))
						{
							//the xAxis is more different than the yAxis when compared to the normal
							startAxis = vec3<float>(1, 0, 0);
						}
						else
						{
							// the yAxis is more different than the xAxis when compared to the normal
							startAxis = vec3<float>(0, 1, 0);
						}

						ortogonalizedTangent = cross(n, startAxis);
						ortogonalizedBiTangent = cross(n, ortogonalizedTangent);
					}
				}
				else
				{
					// one final sanity check, make sure that they tangent and binormal are different
					// enough
					if (dot(ortogonalizedTangent, ortogonalizedBiTangent)
						>
						MUTABLE_TANGENT_MIN_AXIS_DIFFERENCE)
					{
						// then they are too similar lets make them more different
						ortogonalizedBiTangent = cross(n, ortogonalizedTangent);
					}
				}

				tangent[vtxidx] = ortogonalizedTangent;
				bitangent[vtxidx] = ortogonalizedBiTangent;
			}

		}

	}


	//-------------------------------------------------------------------------------------------------
	void MeshFormatBuffer
	(
		const FMeshBufferSet& Source,
		FMeshBufferSet& Result,
		int bufferIndex
	)
	{
		int vCount = Source.GetElementCount();

		int b = bufferIndex;
		{
			// For every channel in this buffer
			for (int c = 0; c < Result.GetBufferChannelCount(b); ++c)
			{
				// Find this channel in the source mesh
				MESH_BUFFER_SEMANTIC resultSemantic;
				int resultSemanticIndex;
				MESH_BUFFER_FORMAT resultFormat;
				int resultComponents;
				int resultOffset;
				Result.GetChannel
				(
					b, c,
					&resultSemantic, &resultSemanticIndex,
					&resultFormat, &resultComponents,
					&resultOffset
				);

				int sourceBuffer;
				int sourceChannel;
				Source.FindChannel
				(resultSemantic, resultSemanticIndex, &sourceBuffer, &sourceChannel);

				int resultElemSize = Result.GetElementSize(b);
				int resultChannelSize = GetMeshFormatData(resultFormat).m_size * resultComponents;
				uint8_t* pResultBuf = Result.GetBufferData(b);
				pResultBuf += resultOffset;

				if (sourceBuffer < 0)
				{
					// Not found: fill with zeros.

					// Special case for derived channel data
					bool generated = false;
					if (resultSemantic == MBS_TANGENTSIGN)
					{
						// Look for the full tangent space
						int tanXBuf, tanXChan, tanYBuf, tanYChan, tanZBuf, tanZChan;
						Source.FindChannel(MBS_TANGENT, resultSemanticIndex, &tanXBuf, &tanXChan);
						Source.FindChannel(MBS_BINORMAL, resultSemanticIndex, &tanYBuf, &tanYChan);
						Source.FindChannel(MBS_NORMAL, resultSemanticIndex, &tanZBuf, &tanZChan);

						if (tanXBuf >= 0 && tanYBuf >= 0 && tanZBuf >= 0)
						{
							generated = true;
							UntypedMeshBufferIteratorConst xIt(Source, MBS_TANGENT, resultSemanticIndex);
							UntypedMeshBufferIteratorConst yIt(Source, MBS_BINORMAL, resultSemanticIndex);
							UntypedMeshBufferIteratorConst zIt(Source, MBS_NORMAL, resultSemanticIndex);
							for (int v = 0; v < vCount; ++v)
							{
								mat3f mat;
								mat[0] = xIt.GetAsVec4f().xyz();
								mat[1] = yIt.GetAsVec4f().xyz();
								mat[2] = zIt.GetAsVec4f().xyz();
								float sign = mat.GetDeterminant() < 0 ? -1.0f : 1.0f;
								ConvertData(0, pResultBuf, resultFormat, &sign, MBF_FLOAT32);

								for (int i = 1; i < resultComponents; ++i)
								{
									// Add zeros
									FMemory::Memzero
									(
										pResultBuf + GetMeshFormatData(resultFormat).m_size * i,
										GetMeshFormatData(resultFormat).m_size
									);
								}
								pResultBuf += resultElemSize;
								xIt++;
								yIt++;
								zIt++;
							}
						}
					}

					// If we have to add colour channels, we will add them as white, to be neutral.
					// \todo: normal channels also should have special values.
					else if (resultSemantic == MBS_COLOUR)
					{
						generated = true;

						switch (resultFormat)
						{
						case MBF_FLOAT32:
						{
							for (int v = 0; v < vCount; ++v)
							{
								auto pTypedResultBuf = (float*)pResultBuf;
								for (int i = 0; i < resultComponents; ++i)
								{
									pTypedResultBuf[i] = 1.0f;
								}
								pResultBuf += resultElemSize;
							}
							break;
						}

						case MBF_NUINT8:
						{
							for (int v = 0; v < vCount; ++v)
							{
								auto pTypedResultBuf = (uint8_t*)pResultBuf;
								for (int i = 0; i < resultComponents; ++i)
								{
									pTypedResultBuf[i] = 255;
								}
								pResultBuf += resultElemSize;
							}
							break;
						}

						case MBF_NUINT16:
						{
							for (int v = 0; v < vCount; ++v)
							{
								auto pTypedResultBuf = (uint16_t*)pResultBuf;
								for (int i = 0; i < resultComponents; ++i)
								{
									pTypedResultBuf[i] = 65535;
								}
								pResultBuf += resultElemSize;
							}
							break;
						}

						default:
							// Format not implemented
							check(false);
							break;
						}
					}

					if (!generated)
					{
						// TODO: and maybe raise a warning?
						for (int v = 0; v < vCount; ++v)
						{
							FMemory::Memzero(pResultBuf, resultChannelSize);
							pResultBuf += resultElemSize;
						}
					}
				}
				else
				{
					// Get the data about the source format
					MESH_BUFFER_SEMANTIC sourceSemantic;
					int sourceSemanticIndex;
					MESH_BUFFER_FORMAT sourceFormat;
					int sourceComponents;
					int sourceOffset;
					Source.GetChannel
					(
						sourceBuffer, sourceChannel,
						&sourceSemantic, &sourceSemanticIndex,
						&sourceFormat, &sourceComponents,
						&sourceOffset
					);
					check(sourceSemantic == resultSemantic
						&&
						sourceSemanticIndex == resultSemanticIndex);

					int sourceElemSize = Source.GetElementSize(sourceBuffer);
					const uint8_t* pSourceBuf = Source.GetBufferData(sourceBuffer);
					pSourceBuf += sourceOffset;

					// Copy element by element
					for (int v = 0; v < vCount; ++v)
					{
						if (resultFormat == sourceFormat && resultComponents == sourceComponents)
						{
							memcpy(pResultBuf, pSourceBuf, resultChannelSize);
						}
						else if (resultFormat == MBF_PACKEDDIR8_W_TANGENTSIGN
							||
							resultFormat == MBF_PACKEDDIRS8_W_TANGENTSIGN)
						{
							check(sourceComponents >= 3);
							check(resultComponents == 4);

							// convert the 3 first components
							//memcpy(pResultBuf, pSourceBuf, resultChannelSize);
							for (int i = 0; i < 3; ++i)
							{
								if (i < sourceComponents)
								{
									ConvertData
									(
										i,
										pResultBuf, resultFormat,
										pSourceBuf, sourceFormat
									);
								}
							}


							// Add the tangent sign
							auto pData = (uint8_t*)pResultBuf;

							// Look for the full tangent space
							int tanXBuf, tanXChan, tanYBuf, tanYChan, tanZBuf, tanZChan;
							Source.FindChannel(MBS_TANGENT, resultSemanticIndex, &tanXBuf, &tanXChan);
							Source.FindChannel(MBS_BINORMAL, resultSemanticIndex, &tanYBuf, &tanYChan);
							Source.FindChannel(MBS_NORMAL, resultSemanticIndex, &tanZBuf, &tanZChan);

							if (tanXBuf >= 0 && tanYBuf >= 0 && tanZBuf >= 0)
							{
								UntypedMeshBufferIteratorConst xIt(Source, MBS_TANGENT, resultSemanticIndex);
								UntypedMeshBufferIteratorConst yIt(Source, MBS_BINORMAL, resultSemanticIndex);
								UntypedMeshBufferIteratorConst zIt(Source, MBS_NORMAL, resultSemanticIndex);

								mat3f mat;
								xIt += v;
								yIt += v;
								zIt += v;
								mat[0] = xIt.GetAsVec4f().xyz();
								mat[1] = yIt.GetAsVec4f().xyz();
								mat[2] = zIt.GetAsVec4f().xyz();

								uint8_t sign = 0;
								if (resultFormat == MBF_PACKEDDIR8_W_TANGENTSIGN)
								{
									sign = mat.GetDeterminant() < 0 ? 0 : 255;
								}
								else if (resultFormat == MBF_PACKEDDIRS8_W_TANGENTSIGN)
								{
									sign = mat.GetDeterminant() < 0 ? -128 : 127;
								}
								pData[3] = sign;
							}
						}
						else
						{
							// Convert formats
							for (int i = 0; i < resultComponents; ++i)
							{
								if (i < sourceComponents)
								{
									ConvertData
									(
										i,
										pResultBuf, resultFormat,
										pSourceBuf, sourceFormat
									);
								}
								else
								{
									// Add zeros. TODO: Warning?
									FMemory::Memzero
									(
										pResultBuf + GetMeshFormatData(resultFormat).m_size * i,
										GetMeshFormatData(resultFormat).m_size
									);
								}
							}


							// Extra step to normalise some semantics in some formats
							// TODO: Make it optional, and add different normalisation types n, n^2
							// TODO: Optimise
							if (sourceSemantic == MBS_BONEWEIGHTS)
							{
								if (resultFormat == MBF_NUINT8)
								{
									auto pData = (uint8_t*)pResultBuf;
									uint8_t accum = 0;
									for (int i = 0; i < resultComponents; ++i)
									{
										accum += pData[i];
									}
									pData[0] += 255 - accum;
								}
							}
						}

						pResultBuf += resultElemSize;
						pSourceBuf += sourceElemSize;
					}
				}
			}
		}
	}


	//-------------------------------------------------------------------------------------------------
	void FormatBufferSet
	(
		const FMeshBufferSet& Source,
		FMeshBufferSet& Result,
		bool keepSystemBuffers,
		bool ignoreMissingChannels,
		bool isVertexBuffer
	)
	{
		if (ignoreMissingChannels)
		{
			// Remove from the result the channels that are not present in the source, and re-pack the
			// offsets.
			for (int b = 0; b < Result.GetBufferCount(); ++b)
			{
				vector<MESH_BUFFER_SEMANTIC> resultSemantics;
				vector<int> resultSemanticIndexs;
				vector<MESH_BUFFER_FORMAT> resultFormats;
				vector<int> resultComponentss;
				vector<int> resultOffsets;
				int offset = 0;

				// For every channel in this buffer
				for (int c = 0; c < Result.GetBufferChannelCount(b); ++c)
				{
					MESH_BUFFER_SEMANTIC resultSemantic;
					int resultSemanticIndex;
					MESH_BUFFER_FORMAT resultFormat;
					int resultComponents;

					// Find this channel in the source mesh
					Result.GetChannel
					(
						b, c,
						&resultSemantic, &resultSemanticIndex,
						&resultFormat, &resultComponents,
						nullptr
					);

					int sourceBuffer;
					int sourceChannel;
					Source.FindChannel
					(resultSemantic, resultSemanticIndex, &sourceBuffer, &sourceChannel);

					if (sourceBuffer >= 0)
					{
						resultSemantics.push_back(resultSemantic);
						resultSemanticIndexs.push_back(resultSemanticIndex);
						resultFormats.push_back(resultFormat);
						resultComponentss.push_back(resultComponents);
						resultOffsets.push_back(offset);

						offset += GetMeshFormatData(resultFormat).m_size * resultComponents;
					}
				}

				if (resultSemantics.empty())
				{
					Result.SetBuffer(b, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr);
				}
				else
				{
					Result.SetBuffer(b, offset, (int)resultSemantics.size(),
						&resultSemantics[0],
						&resultSemanticIndexs[0],
						&resultFormats[0],
						&resultComponentss[0],
						&resultOffsets[0]);
				}
			}
		}


		// For every vertex buffer in result
		int vCount = Source.GetElementCount();
		Result.SetElementCount(vCount);
		for (int b = 0; b < Result.GetBufferCount(); ++b)
		{
			MeshFormatBuffer(Source, Result, b);
		}


		// Detect internal system buffers and clone them unmodified.
		if (keepSystemBuffers)
		{
			for (int b = 0; b < Source.GetBufferCount(); ++b)
			{
				// Detect system buffers and clone them unmodified.
				if (Source.GetBufferChannelCount(b) > 0)
				{
					MESH_BUFFER_SEMANTIC sourceSemantic;
					int sourceSemanticIndex;
					MESH_BUFFER_FORMAT sourceFormat;
					int sourceComponents;
					int sourceOffset;
					Source.GetChannel
					(
						b, 0,
						&sourceSemantic, &sourceSemanticIndex,
						&sourceFormat, &sourceComponents,
						&sourceOffset
					);

					if (sourceSemantic == MBS_LAYOUTBLOCK
						|| sourceSemantic == MBS_CHART
						|| (isVertexBuffer && sourceSemantic == MBS_VERTEXINDEX)
						)
					{
						Result.AddBuffer(Source, b);
					}
				}
			}
		}

	}



	//-------------------------------------------------------------------------------------------------
	MeshPtr MeshFormat
	(
		const Mesh* pPureSource,
		const Mesh* pFormat,
		bool keepSystemBuffers,
		bool formatVertices,
		bool formatIndices,
		bool formatFaces,
		bool rebuildTangents,
		bool ignoreMissingChannels
	)
	{
		MUTABLE_CPUPROFILER_SCOPE(MeshFormat);

		if (!pPureSource) { return nullptr; }
		if (!pFormat) { return pPureSource->Clone(); }

		MeshPtrConst pSource = pPureSource;

		// Rebuild the tangent space if necessary
		if (rebuildTangents)
		{
			MeshPtr pClonedSource = pPureSource->Clone();
			pSource = pClonedSource;

			// Make sure there are the tangents and binormal channels.
			int tanBuf, tanChan, binBuf, binChan;
			pSource->GetVertexBuffers().FindChannel(MBS_TANGENT, 0, &tanBuf, &tanChan);
			pSource->GetVertexBuffers().FindChannel(MBS_BINORMAL, 0, &binBuf, &binChan);

			if (tanBuf <= 0 || binBuf <= 0)
			{
				MESH_BUFFER buffer;

				if (tanBuf <= 0)
				{
					MESH_BUFFER_CHANNEL channel;
					channel.m_semantic = MBS_TANGENT;
					channel.m_format = MBF_FLOAT32;
					channel.m_componentCount = 3;
					channel.m_semanticIndex = 0;
					channel.m_offset = 0;
					buffer.m_channels.Add(channel);
					buffer.m_elementSize += 3 * sizeof(float);
				}

				if (binBuf <= 0)
				{
					MESH_BUFFER_CHANNEL channel;
					channel.m_semantic = MBS_BINORMAL;
					channel.m_format = MBF_FLOAT32;
					channel.m_componentCount = 3;
					channel.m_semanticIndex = 0;
					channel.m_offset = (uint8)buffer.m_elementSize;
					buffer.m_channels.Add(channel);
					buffer.m_elementSize += 3 * sizeof(float);
				}

				buffer.m_data.SetNum(buffer.m_elementSize * pClonedSource->GetVertexCount());
				pClonedSource->GetVertexBuffers().m_buffers.Add(buffer);
			}

			RebuildTangents(pClonedSource->GetIndexBuffers(), pClonedSource->GetVertexBuffers());
		}

		MeshPtr pResult = pFormat->Clone();

		// Make sure that the bone indices will fit in this format, or extend it.
		if (formatVertices)
		{
			auto& buffs = pSource->GetVertexBuffers();

			int semanticIndex = 0;
			while (true)
			{
				int buf = 0;
				int chan = 0;
				buffs.FindChannel(MBS_BONEINDICES, semanticIndex, &buf, &chan);
				if (buf < 0)
					break;

				int resultBuf = 0;
				int resultChan = 0;
				auto& formBuffs = pResult->GetVertexBuffers();
				formBuffs.FindChannel(MBS_BONEINDICES, semanticIndex, &resultBuf, &resultChan);
				if (resultBuf >= 0)
				{
					UntypedMeshBufferIteratorConst it(buffs, MBS_BONEINDICES, semanticIndex);
					int32_t maxBoneIndex = 0;
					for (int v = 0; v < buffs.GetElementCount(); ++v)
					{
						auto va = it.GetAsVec8i();
						for (int c = 0; c < it.GetComponents(); ++c)
						{
							maxBoneIndex = std::max(maxBoneIndex, va[c]);
						}
						++it;
					}

					auto& format = formBuffs.m_buffers[resultBuf].m_channels[resultChan].m_format;
					if (maxBoneIndex > 0xffff && (format == MBF_UINT8 || format == MBF_UINT16))
					{
						format = MBF_UINT32;
						formBuffs.UpdateOffsets(resultBuf);
					}
					else if (maxBoneIndex > 0x7fff && (format == MBF_INT8 || format == MBF_INT16))
					{
						format = MBF_UINT32;
						formBuffs.UpdateOffsets(resultBuf);
					}
					else if (maxBoneIndex > 0xff && format == MBF_UINT8)
					{
						format = MBF_UINT16;
						formBuffs.UpdateOffsets(resultBuf);
					}
					else if (maxBoneIndex > 0x7f && format == MBF_INT8)
					{
						format = MBF_INT16;
						formBuffs.UpdateOffsets(resultBuf);
					}
				}

				semanticIndex++;
			}
		}

		// \todo Make sure that the vertex indices will fit in this format, or extend it.



		if (formatVertices)
		{

			FormatBufferSet(pSource->GetVertexBuffers(), pResult->GetVertexBuffers(),
				keepSystemBuffers, ignoreMissingChannels, true);
		}
		else
		{
			pResult->m_VertexBuffers = pSource->GetVertexBuffers();
		}

		if (formatIndices)
		{
			FormatBufferSet(pSource->GetIndexBuffers(), pResult->GetIndexBuffers(), keepSystemBuffers,
				ignoreMissingChannels, false);
		}
		else
		{
			pResult->m_IndexBuffers = pSource->GetIndexBuffers();
		}

		if (formatFaces)
		{
			FormatBufferSet(pSource->GetFaceBuffers(),
				pResult->GetFaceBuffers(),
				keepSystemBuffers,
				ignoreMissingChannels,
				false);
		}
		else
		{
			pResult->m_FaceBuffers = pSource->GetFaceBuffers();
		}

		// Copy the rest of the data
		pResult->SetSkeleton(pSource->GetSkeleton());
		pResult->SetPhysicsBody(pSource->GetPhysicsBody());

		pResult->m_layouts.Empty();
		for (const auto& Layout : pSource->m_layouts)
		{
			pResult->m_layouts.Add(Layout->Clone());
		}

		pResult->ResetStaticFormatFlags();
		pResult->EnsureSurfaceData();

		pResult->m_tags = pSource->m_tags;

		pResult->m_AdditionalBuffers = pSource->m_AdditionalBuffers;

		pResult->m_bonePoses = pSource->m_bonePoses;

		return pResult;

	}

}

