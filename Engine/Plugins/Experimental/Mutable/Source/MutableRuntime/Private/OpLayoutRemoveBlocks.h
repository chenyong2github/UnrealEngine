// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableRuntime/Public/Layout.h"
#include "MutableRuntime/Private/MeshPrivate.h"
#include "MutableRuntime/Private/Platform.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
    inline LayoutPtr LayoutRemoveBlocks( const Layout* pSource, const Mesh* pMesh, int layoutIndex )
    {
        // Create the list of blocks in the mesh
        vector<bool> blocksFound(1024, false);

        UntypedMeshBufferIteratorConst itBlocks( pMesh->GetVertexBuffers(), MBS_LAYOUTBLOCK,
                                                 layoutIndex );
        if ( itBlocks.GetFormat()==MBF_UINT16 )
        {
            const uint16_t* pBlocks = reinterpret_cast<const uint16_t*>( itBlocks.ptr() );
            for ( int i=0; i<pMesh->GetVertexCount(); ++i )
            {
                if (pBlocks[i]>=blocksFound.size())
                {
                    blocksFound.resize(pBlocks[i]+1024,false);
                }

                blocksFound[ pBlocks[i] ] = true;
            }
        }
        else if ( itBlocks.GetFormat()==MBF_NONE )
        {
            // This seems to happen.
            // May this happen when entire meshes are removed?
            return pSource->Clone();
        }
        else
        {
            // Format not supported yet
            check( false );
        }

        // Remove blocks that are not in the mesh
		LayoutPtr pResult = pSource->Clone();
		int dest = 0;
		for ( int32 b=0; b<pResult->m_blocks.Num(); ++b )
		{
            int blockIndex = pResult->m_blocks[b].m_id;
            if ( blockIndex<(int)blocksFound.size() && blocksFound[blockIndex] )
			{
				// keep
				pResult->m_blocks[dest] = pResult->m_blocks[b];
				++dest;
			}
		}
		pResult->SetBlockCount( dest );

        return pResult;
	}


	//---------------------------------------------------------------------------------------------
	inline LayoutPtr LayoutMerge(const Layout* pA, const Layout* pB )
	{
		LayoutPtr pResult = pA->Clone();

		// This is faster but fails in the rare case of a block being in both layouts, which may 
		// happen if we merge a mesh with itself.
		//pResult->GetPrivate()->m_blocks.insert
		//(
		//	pResult->GetPrivate()->m_blocks.end(),
		//	pB->GetPrivate()->m_blocks.begin(),
		//	pB->GetPrivate()->m_blocks.end()
		//);

		for ( const Layout::BLOCK& block: pB->m_blocks )
		{
			if ( pResult->FindBlock(block.m_id)<0 )
			{
				pResult->m_blocks.Add(block);
			}
		}

		return pResult;
	}


}
