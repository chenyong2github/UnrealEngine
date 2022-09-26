// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshPrivate.h"
#include "ConvertData.h"
#include "Platform.h"

#include "SparseIndexMap.h"

namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Reference Linear factor version
    //---------------------------------------------------------------------------------------------
    inline MeshPtr MeshMorphReference( const Mesh* pBase, const Mesh* pMorph, float factor )
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshMorph);

        if (!pBase) return nullptr;

        MeshPtr pDest = pBase->Clone();

        if (!pMorph) return pDest;

        // Number of vertices to modify
        uint32_t vcountMorph = pMorph->GetVertexBuffers().GetElementCount();
        uint32_t vcountBase = pBase->GetVertexBuffers().GetElementCount();
        if ( !vcountMorph || !vcountBase )
        {
            return pDest;
        }

        uint32_t ccount = 0;
        if ( pMorph->GetVertexBuffers().GetBufferCount()>0 )
        {
            ccount = pMorph->GetVertexBuffers().GetBufferChannelCount(0);
        }

        // Iterator of the vertex ids of the base vertices
        UntypedMeshBufferIteratorConst itBaseId( pBase->GetVertexBuffers(), MBS_VERTEXINDEX );

        vector< UntypedMeshBufferIterator > itBaseChannels( ccount );
        vector< UntypedMeshBufferIteratorConst > itMorphChannels( ccount );
        for ( size_t c=1; c<ccount; ++c )
        {
            const FMeshBufferSet& MBSPriv = pMorph->GetVertexBuffers();
            MESH_BUFFER_SEMANTIC sem = MBSPriv.m_buffers[0].m_channels[c].m_semantic;
            int semIndex = MBSPriv.m_buffers[0].m_channels[c].m_semanticIndex;

            itBaseChannels[c] = UntypedMeshBufferIterator
                    ( pDest->GetVertexBuffers(), sem, semIndex );
            itMorphChannels[c] = UntypedMeshBufferIteratorConst
                    ( pMorph->GetVertexBuffers(), sem, semIndex );
        }

        // Morph

        // Number of vertices consumed from the morph mesh
        uint32_t processedMorphVertices = 0;

        // Morph mesh channels are always this friendly types
        MeshBufferIteratorConst<MBF_UINT32,uint32_t,1>
                itFirstMorphVertex( pMorph->GetVertexBuffers(), MBS_VERTEXINDEX );

        // Number of vertices at the beginning of the morph already consumed (optimisation)
        int morphVerticesFromStartAlreadyConsumed = 0;

        // Number of vertices we need to advance the base mesh iterators to get to the current base
        // mesh vertex
        int step = 0;

        // Iterate all the base mesh vertices
        for ( uint32_t vi=0; vi<vcountBase && processedMorphVertices<vcountMorph; ++vi )
        {
            uint32_t baseMeshVertexIndex = itBaseId.GetAsUINT32();
            int32_t morphVertexIndex = -1;

            // Look for a morph vertex to apply to this base mesh vertex
            auto itMorphVer = itFirstMorphVertex+morphVerticesFromStartAlreadyConsumed;
            for ( int32_t morphVertexCandidate=morphVerticesFromStartAlreadyConsumed;
                  morphVertexCandidate<int32_t(vcountMorph);
                  ++morphVertexCandidate )
            {
                if ((*itMorphVer)[0]==baseMeshVertexIndex)
                {
                    morphVertexIndex = morphVertexCandidate;
                    if (morphVertexCandidate==morphVerticesFromStartAlreadyConsumed)
                    {
                        ++morphVerticesFromStartAlreadyConsumed;
                    }
                    break;
                }
                ++itMorphVer;
            }

            if( morphVertexIndex>=0 )
            {
                // Morph one vertex
                for ( size_t c=1; c<ccount; ++c )
                {
                    if ( itBaseChannels[c].ptr() )
                    {
                        itBaseChannels[c] += step;

                        vec4<float> value = itBaseChannels[c].GetAsVec4f();
                        vec4<float> delta = (itMorphChannels[c]+morphVertexIndex).GetAsVec4f();

                        value += delta * factor;

                        for( int i=0; i<itBaseChannels[c].GetComponents(); ++i )
                        {
                            ConvertData( i,
                                         itBaseChannels[c].ptr(),
                                         itBaseChannels[c].GetFormat(),
                                         &value, MBF_FLOAT32 );
                        }
                    }
                }

                // Reset the step to the next vertex
                step = 1;
            }
            else
            {
                // Vertex was not morphed, the step to the next vertex will be bigger
                ++step;
            }

            ++itBaseId;
        }

        return pDest;
    }



    //---------------------------------------------------------------------------------------------
    //! Reference Factor-less version
    //---------------------------------------------------------------------------------------------
    inline MeshPtr MeshMorphReference( const Mesh* pBase, const Mesh* pMorph )
    {
        // Trust the compiler to remove the factor
        return MeshMorphReference( pBase, pMorph, 1.0f );
    }


    //---------------------------------------------------------------------------------------------
    //! Reference Linear factor version for morphing 2 targets
    //---------------------------------------------------------------------------------------------
    inline MeshPtr MeshMorph2Reference( const Mesh* pBase, const Mesh* pMin, const Mesh* pMax, float factor )
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshMorph2Reference);

        MeshPtr pDest = pBase->Clone();

        // Number of vertices to modify
        uint32_t vmincount = pMin->GetVertexBuffers().GetElementCount();
        uint32_t vmaxcount = pMax->GetVertexBuffers().GetElementCount();

        uint32_t vcountBase = pBase->GetVertexBuffers().GetElementCount();

        if ( (vmincount+vmaxcount)==0 || !vcountBase )
        {
            return pDest;
        }


        auto refTarget = vmincount > 0 ? pMin : pMax;

        uint32_t ccount = refTarget->GetVertexBuffers().GetBufferChannelCount(0);

        // Iterator of the vertex ids of the base vertices
        UntypedMeshBufferIteratorConst itBaseId( pBase->GetVertexBuffers(), MBS_VERTEXINDEX );

        vector< UntypedMeshBufferIterator > itBaseChannels( ccount );
        vector< UntypedMeshBufferIteratorConst > itMinChannels( ccount );
        vector< UntypedMeshBufferIteratorConst > itMaxChannels( ccount );
        for ( size_t c=1; c<ccount; ++c )
        {
            const FMeshBufferSet& MBSPriv = refTarget->GetVertexBuffers();
            MESH_BUFFER_SEMANTIC sem = MBSPriv.m_buffers[0].m_channels[c].m_semantic;
            int semIndex = MBSPriv.m_buffers[0].m_channels[c].m_semanticIndex;

            itBaseChannels[c] = UntypedMeshBufferIterator
                    ( pDest->GetVertexBuffers(), sem, semIndex );
            itMinChannels[c] = UntypedMeshBufferIteratorConst
                    ( pMin->GetVertexBuffers(), sem, semIndex );
            itMaxChannels[c] = UntypedMeshBufferIteratorConst
                    ( pMax->GetVertexBuffers(), sem, semIndex );
        }

        // Morph
        uint32_t minProcessed=0;
        uint32_t maxProcessed=0;
        float maxFactor = factor;
        float minFactor = 1.0f-factor;

        // Morph mesh channels are always this friendly types
        MeshBufferIteratorConst<MBF_UINT32,uint32_t,1>
                itFirstMinVer( pMin->GetVertexBuffers(), MBS_VERTEXINDEX );
        MeshBufferIteratorConst<MBF_UINT32,uint32_t,1>
                itFirstMaxVer( pMax->GetVertexBuffers(), MBS_VERTEXINDEX );

        // Number of vertices at the beginning of the morph already consumed (optimisation)
        int minMorphVerticesFromStartAlreadyConsumed = 0;
        int maxMorphVerticesFromStartAlreadyConsumed = 0;

        // Number of vertices we need to advance the base mesh iterators to get to the current base
        // mesh vertex
        int step = 0;

        // Iterate all the base mesh vertices
        for ( uint32_t i=0; i<vcountBase && (minProcessed<vmincount || maxProcessed<vmaxcount); ++i)
        {
            uint32_t baseMeshVertexIndex = itBaseId.GetAsUINT32();

            int32_t minMorphVertexIndex = -1;
            int32_t maxMorphVertexIndex = -1;

            // Look for a min morph vertex to apply to this base mesh vertex
            auto itMorphVer = itFirstMinVer+minMorphVerticesFromStartAlreadyConsumed;
            for ( int32_t morphVertexCandidate=minMorphVerticesFromStartAlreadyConsumed;
                  morphVertexCandidate<int32_t(vmincount);
                  ++morphVertexCandidate )
            {
                if ((*itMorphVer)[0]==baseMeshVertexIndex)
                {
                    minMorphVertexIndex = morphVertexCandidate;
                    if (morphVertexCandidate==minMorphVerticesFromStartAlreadyConsumed)
                    {
                        ++minMorphVerticesFromStartAlreadyConsumed;
                    }
                    break;
                }
                ++itMorphVer;
            }

            // Look for a max morph vertex to apply to this base mesh vertex
            itMorphVer = itFirstMaxVer+maxMorphVerticesFromStartAlreadyConsumed;
            for ( int32_t morphVertexCandidate=maxMorphVerticesFromStartAlreadyConsumed;
                  morphVertexCandidate<int32_t(vmaxcount);
                  ++morphVertexCandidate )
            {
                if ((*itMorphVer)[0]==baseMeshVertexIndex)
                {
                    maxMorphVertexIndex = morphVertexCandidate;
                    if (morphVertexCandidate==maxMorphVerticesFromStartAlreadyConsumed)
                    {
                        ++maxMorphVerticesFromStartAlreadyConsumed;
                    }
                    break;
                }
                ++itMorphVer;
            }
            
            // Morph one vertex
            if ( minMorphVertexIndex>=0 || maxMorphVertexIndex>=0 )
            {
                for ( size_t c=1; c<ccount; ++c )
                {
                    if ( itBaseChannels[c].ptr() )
                    {
                        itBaseChannels[c] += step;

                        vec4<float> value = itBaseChannels[c].GetAsVec4f();

                        if ( minMorphVertexIndex>=0 )
                        {
                            vec4<float> minvalue = (itMinChannels[c]+minMorphVertexIndex).GetAsVec4f();
                            value += minvalue * minFactor;
                        }

                        if ( maxMorphVertexIndex>=0 )
                        {
                            vec4<float> maxvalue = (itMaxChannels[c]+maxMorphVertexIndex).GetAsVec4f();
                            value += maxvalue * maxFactor;
                        }

						const int32_t componentCount = itBaseChannels[c].GetComponents();
                        for( int component=0; component<componentCount; ++component )
                        {
                            ConvertData( component,
                                         itBaseChannels[c].ptr(),
                                         itBaseChannels[c].GetFormat(),
                                         &value, MBF_FLOAT32 );
                        }
                    }
                }

                step = 1;
            }
            else
            {
                // Vertex was not morphed, the step to the next vertex will be bigger
                ++step;
            }

            ++itBaseId;
        }

        return pDest;
    }

	
	//---------------------------------------------------------------------------------------------
	//! Optimized linear factor version for morphing 2 targets
	//---------------------------------------------------------------------------------------------
    inline MeshPtr MeshMorph2( const Mesh* pBase, const Mesh* pMin, const Mesh* pMax, const float factor )
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshMorph2);

		if (!pBase) return nullptr;

        const auto ApplyMorph = []
            ( auto BaseIdIter, const std::vector< UntypedMeshBufferIterator >& baseChannelsIters, const int32_t baseSize,
              auto MorphIdIter, const std::vector< UntypedMeshBufferIteratorConst >& morphChannelsIters, const int32_t morphSize,
              const float factor )
        -> void
        {
            uint32_t minBaseId = std::numeric_limits<uint32_t>::max();
            uint32_t maxBaseId = 0;

			{
                auto limitsBaseIdIter = BaseIdIter;
				for ( int32_t i = 0; i < baseSize; ++i, ++limitsBaseIdIter )
				{
					const uint32_t id = limitsBaseIdIter.GetAsUINT32();
					minBaseId = std::min( id, minBaseId );
					maxBaseId = std::max( id, maxBaseId );
				}
			}

            SparseIndexMap IndexMap( minBaseId, maxBaseId );
            
            auto usedMorphIdIter = MorphIdIter;
            for ( uint32_t i = 0; i < static_cast<uint32_t>(morphSize); ++i, ++usedMorphIdIter)
            {
                const uint32_t morphId = (*usedMorphIdIter)[0];
                
                IndexMap.Insert( morphId, i );
            }

            for ( int32_t v = 0; v < baseSize; ++v )
            {
                const uint32_t baseId = (BaseIdIter + v).GetAsUINT32();
                const uint32_t morphIdx = IndexMap.Find( baseId );

                if ( morphIdx == SparseIndexMap::NotFoundValue )
                {
                    continue;
                }

                const int32_t m = static_cast<int32_t>( morphIdx );

                // Find consecutive run.
                auto RunBaseIter = BaseIdIter + v;
                auto RunMorphIter = MorphIdIter + m;

                int32_t runSize = 0;
                for (  ; v + runSize < baseSize && m + runSize < morphSize && RunBaseIter.GetAsUINT32() == (*RunMorphIter)[0];
                        ++runSize, ++RunBaseIter, ++RunMorphIter );

                const size_t channelCount = morphChannelsIters.size();
                for ( size_t c = 1; c < channelCount; ++c )
                {
                
                	if ( !(baseChannelsIters[c].ptr() && morphChannelsIters[c].ptr()) )
                	{
                		continue;
                	}
                
                    auto channelBaseIter = baseChannelsIters[c] + v;
                    auto channelMorphIter = morphChannelsIters[c] + m;
                   
                    const auto dstChannelFormat = baseChannelsIters[c].GetFormat();
                    const auto dstChannelComps = baseChannelsIters[c].GetComponents();

                    // Apply Morph to range found above.
                    for ( int32_t r = 0; r < runSize; ++r, ++channelBaseIter, ++channelMorphIter )
                    {
                        const vec4<float> value = channelBaseIter.GetAsVec4f() + channelMorphIter.GetAsVec4f()*factor;

                        // TODO: Optimize this for the specific components.
                        // Max 4 components
                        for ( int32_t comp = 0; comp < dstChannelComps && comp < 4; ++comp )
                        {
                            ConvertData( comp, channelBaseIter.ptr(), dstChannelFormat, &value, MBF_FLOAT32 );
                        }
                    }
                }

				v += std::max(runSize - 1, 0);
            }
        };

		MeshPtr pDest = pBase->Clone();

		// Number of vertices to modify
		const uint32_t minCount = pMin ? pMin->GetVertexBuffers().GetElementCount() : 0;
		const uint32_t maxCount = pMax ? pMax->GetVertexBuffers().GetElementCount() : 0;
		const uint32_t baseCount = pBase ? pBase->GetVertexBuffers().GetElementCount() : 0;
		const Mesh* refTarget = minCount > 0 ? pMin : pMax;

		if ( baseCount == 0 || (minCount + maxCount) == 0)
		{
			return pDest;
		}

		if (refTarget)
		{
			uint32_t ccount = refTarget->GetVertexBuffers().GetBufferChannelCount(0);

			// Iterator of the vertex ids of the base vertices
			UntypedMeshBufferIteratorConst itBaseId(pBase->GetVertexBuffers(), MBS_VERTEXINDEX);

			std::vector< UntypedMeshBufferIterator > itBaseChannels(ccount);
			std::vector< UntypedMeshBufferIteratorConst > itMinChannels(ccount);
			std::vector< UntypedMeshBufferIteratorConst > itMaxChannels(ccount);

			for (size_t c = 1; c < ccount; ++c)
			{
				const FMeshBufferSet& MBSPriv = refTarget->GetVertexBuffers();
				MESH_BUFFER_SEMANTIC sem = MBSPriv.m_buffers[0].m_channels[c].m_semantic;
				int semIndex = MBSPriv.m_buffers[0].m_channels[c].m_semanticIndex;
				
				itBaseChannels[c] = UntypedMeshBufferIterator(pDest->GetVertexBuffers(), sem, semIndex);
				if (minCount > 0)
				{
					itMinChannels[c] = UntypedMeshBufferIteratorConst(pMin->GetVertexBuffers(), sem, semIndex);
				}

				if (maxCount > 0)
				{
					itMaxChannels[c] = UntypedMeshBufferIteratorConst(pMax->GetVertexBuffers(), sem, semIndex);
				}
			}
			
			if (minCount > 0)
			{
				MeshBufferIteratorConst<MBF_UINT32, uint32_t, 1> itMinId(pMin->GetVertexBuffers(), MBS_VERTEXINDEX);
				ApplyMorph(itBaseId, itBaseChannels, baseCount, itMinId, itMinChannels, minCount, 1.0f - factor);
			}

			if (maxCount > 0)
			{
				MeshBufferIteratorConst<MBF_UINT32, uint32_t, 1> itMaxId(pMax->GetVertexBuffers(), MBS_VERTEXINDEX);
				ApplyMorph(itBaseId, itBaseChannels, baseCount, itMaxId, itMaxChannels, maxCount, factor);
			}
		}

        return pDest;
    }

	//---------------------------------------------------------------------------------------------
	//! \TODO Optimized linear factor version
	//---------------------------------------------------------------------------------------------
	inline MeshPtr MeshMorph(const Mesh* pBase, const Mesh* pMorph, float factor)
	{
		return MeshMorph2( pBase, nullptr, pMorph, factor );
	}

    //---------------------------------------------------------------------------------------------
    //! \TODO Optimized Factor-less version
    //---------------------------------------------------------------------------------------------
    inline MeshPtr MeshMorph( const Mesh* pBase, const Mesh* pMorph )
    {
        // Trust the compiler to remove the factor
        return MeshMorph( pBase, pMorph, 1.0f );
    }
}
