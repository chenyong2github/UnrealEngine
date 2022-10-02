// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Math/IntPoint.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/AssertionMacros.h"
#include "MuR/Layout.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/Mesh.h"
#include "MuR/MeshBufferSet.h"
#include "MuR/MutableMath.h"
#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/System.h"
#include "MuT/AST.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpConstantResource.h"
#include "MuT/ASTOpMeshApplyPose.h"
#include "MuT/ASTOpMeshApplyShape.h"
#include "MuT/ASTOpMeshBindShape.h"
#include "MuT/ASTOpMeshClipDeform.h"
#include "MuT/ASTOpMeshClipMorphPlane.h"
#include "MuT/ASTOpMeshExtractLayoutBlocks.h"
#include "MuT/ASTOpMeshFormat.h"
#include "MuT/ASTOpMeshGeometryOperation.h"
#include "MuT/ASTOpMeshMorphReshape.h"
#include "MuT/ASTOpMeshRemapIndices.h"
#include "MuT/ASTOpMeshTransform.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/CodeGenerator.h"
#include "MuT/CodeGenerator_FirstPass.h"
#include "MuT/CompilerPrivate.h"
#include "MuT/ErrorLog.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/NodeImageProject.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeLayoutPrivate.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeMeshApplyPose.h"
#include "MuT/NodeMeshApplyPosePrivate.h"
#include "MuT/NodeMeshClipDeform.h"
#include "MuT/NodeMeshClipDeformPrivate.h"
#include "MuT/NodeMeshClipMorphPlane.h"
#include "MuT/NodeMeshClipMorphPlanePrivate.h"
#include "MuT/NodeMeshClipWithMesh.h"
#include "MuT/NodeMeshClipWithMeshPrivate.h"
#include "MuT/NodeMeshConstant.h"
#include "MuT/NodeMeshConstantPrivate.h"
#include "MuT/NodeMeshFormat.h"
#include "MuT/NodeMeshFormatPrivate.h"
#include "MuT/NodeMeshFragment.h"
#include "MuT/NodeMeshFragmentPrivate.h"
#include "MuT/NodeMeshGeometryOperation.h"
#include "MuT/NodeMeshGeometryOperationPrivate.h"
#include "MuT/NodeMeshInterpolate.h"
#include "MuT/NodeMeshInterpolatePrivate.h"
#include "MuT/NodeMeshMakeMorph.h"
#include "MuT/NodeMeshMakeMorphPrivate.h"
#include "MuT/NodeMeshMorph.h"
#include "MuT/NodeMeshMorphPrivate.h"
#include "MuT/NodeMeshReshape.h"
#include "MuT/NodeMeshReshapePrivate.h"
#include "MuT/NodeMeshSubtract.h"
#include "MuT/NodeMeshSwitch.h"
#include "MuT/NodeMeshSwitchPrivate.h"
#include "MuT/NodeMeshTable.h"
#include "MuT/NodeMeshTablePrivate.h"
#include "MuT/NodeMeshTransform.h"
#include "MuT/NodeMeshTransformPrivate.h"
#include "MuT/NodeMeshVariation.h"
#include "MuT/NodeMeshVariationPrivate.h"
#include "MuT/NodeScalar.h"
#include "MuT/Table.h"
#include "MuT/TablePrivate.h"

#include <memory>
#include <utility>


namespace mu
{
class Node;

    //---------------------------------------------------------------------------------------------
    void CodeGenerator::PrepareForLayout( LayoutPtrConst pSourceLayout,
                                             MeshPtr currentLayoutMesh,
                                             size_t currentLayoutChannel,
                                             const void* errorContext )
    {
        if (currentLayoutMesh->GetVertexCount()==0)
        {
            return;
        }

        // TODO: Done twice? Check new component code.
		Ptr<const Layout> pLayout = AddLayout( pSourceLayout );
        currentLayoutMesh->AddLayout( pLayout );

        int buffer = -1;
        int channel = -1;
        currentLayoutMesh->GetVertexBuffers().FindChannel( MBS_TEXCOORDS,
                                                              (int)currentLayoutChannel,
                                                              &buffer,
                                                              &channel );
        check( buffer>=0 );
        check( channel>=0 );

        // Create the layout block vertex buffer
        uint16_t* pLayoutData = 0;
        {
            int layoutBuf = currentLayoutMesh->GetVertexBuffers().GetBufferCount();
            currentLayoutMesh->GetVertexBuffers().SetBufferCount( layoutBuf+1 );

            // TODO
            check( pLayout->GetBlockCount()<65535 );
            MESH_BUFFER_SEMANTIC semantic = MBS_LAYOUTBLOCK;
            int semanticIndex = int(currentLayoutChannel);
            MESH_BUFFER_FORMAT format = MBF_UINT16;
            int components = 1;
            int offset = 0;
            currentLayoutMesh->GetVertexBuffers().SetBuffer
                    (
                        layoutBuf,
                        sizeof(uint16_t),
                        1,
                        &semantic, &semanticIndex,
                        &format, &components,
                        &offset
                    );
            pLayoutData = (uint16_t*)currentLayoutMesh->GetVertexBuffers().GetBufferData( layoutBuf );
        }

        // Get the information about the texture coordinates channel
        MESH_BUFFER_SEMANTIC semantic;
        int semanticIndex;
        MESH_BUFFER_FORMAT format;
        int components;
        int offset;
        currentLayoutMesh->GetVertexBuffers().GetChannel
            ( buffer, channel, &semantic, &semanticIndex, &format, &components, &offset );
        check( semantic == MBS_TEXCOORDS );

        const uint8_t* pData = currentLayoutMesh->GetVertexBuffers().GetBufferData( buffer );
        int elemSize = currentLayoutMesh->GetVertexBuffers().GetElementSize( buffer );
        int channelOffset = currentLayoutMesh->GetVertexBuffers().GetChannelOffset( buffer, channel );
        pData += channelOffset;

        // Clear block data
        for (int i=0; i< currentLayoutMesh->GetVertexBuffers().GetElementCount(); ++i)
        {
            pLayoutData[i] = 65535;
        }

        // TODO: Check overlapping layout blocks
        // TODO: Check triangles crossing blocks
        int inside = 0;
        int temp = 0;
        for ( int b=0; b<pLayout->GetBlockCount(); ++b )
        {
            FIntPoint grid = pLayout->GetGridSize();

            box< vec2<int> > block;
            pLayout->GetBlock
                ( b, &block.min[0], &block.min[1], &block.size[0], &block.size[1] );

            box< vec2<float> > rect;
            rect.min[0] = ( (float)block.min[0] ) / (float) grid[0];
            rect.min[1] = ( (float)block.min[1] ) / (float) grid[1];
            rect.size[0] = ( (float)block.size[0] ) / (float) grid[0];
            rect.size[1] = ( (float)block.size[1] ) / (float) grid[1];

            const uint8_t* pVertices = pData;

            if ( format == MBF_FLOAT32 )
            {
                for ( int v=0; v<currentLayoutMesh->GetVertexBuffers().GetElementCount(); ++v )
                {
                    vec2<float>* pUV = (vec2<float>*)pVertices;
                    pVertices += elemSize;

                    if ( pLayoutData[v]==65535 && rect.ContainsInclusive(*pUV) )
                    {
                        *pUV = rect.Homogenize( *pUV );

                        // Set the value to the unique block id
                        check( pLayout->m_blocks[b].m_id < 65535 );
                        pLayoutData[v] =(uint16_t) pLayout->m_blocks[b].m_id;

                        inside++;
                    }
                    else
                    {
                        temp++;
                    }
                }
            }
            else if ( format == MBF_FLOAT16 )
            {
                // TODO: Very slow?
                for ( int v=0; v<currentLayoutMesh->GetVertexBuffers().GetElementCount(); ++v )
                {
                    float16* pUV = (float16*)pVertices;
                    pVertices += elemSize;

                    vec2<float> UV( halfToFloat(pUV[0]), halfToFloat(pUV[1]) );

                    if ( pLayoutData[v]==65535 && rect.ContainsInclusive(UV) )
                    {
                        UV = rect.Homogenize( UV );

                        // Set the value to the unique block id
                        check( pLayout->m_blocks[b].m_id < 65535 );
                        pLayoutData[v] = (uint16_t)pLayout->m_blocks[b].m_id;

                        inside++;

                        pUV[0] = floatToHalf( UV[0] );
                        pUV[1] = floatToHalf( UV[1] );
                    }
                }
            }
        }

        int outside = currentLayoutMesh->GetVertexBuffers().GetElementCount() - inside;
        if (outside>0)
        {
            char buf[256];
            mutable_snprintf
                (
                    buf, 256,
                    "Source mesh has %d vertices not assigned to any layout block in LOD %d",
					outside, m_currentParents.back().m_lod
                );
			int blockCount = pLayout->GetBlockCount();
        
            //m_pErrorLog->GetPrivate()->Add( buf, blockCount==1?ELMT_INFO:ELMT_WARNING, errorContext );
            vector< float > unassignedUVs;
            unassignedUVs.reserve(64);           
            
            const uint8_t* pVertices = pData;
            for (int i=0; i<currentLayoutMesh->GetVertexBuffers().GetElementCount(); ++i)
            {
                vec2<float> UV;
                if ( format == MBF_FLOAT32 )
                {
                    UV = *((vec2<float>*)pVertices);
                }
                else if ( format == MBF_FLOAT16 )
                {
                    float16* pUV = (float16*)pVertices;
                    UV = vec2<float>( halfToFloat(pUV[0]), halfToFloat(pUV[1]) );
                }

                pVertices += elemSize;

                if (pLayoutData[i]==65535)
                {
                    unassignedUVs.push_back(UV[0]);
                    unassignedUVs.push_back(UV[1]);
                }
            }

            ErrorLogMessageAttachedDataView attachedDataView;
            attachedDataView.m_unassignedUVs = unassignedUVs.data();
            attachedDataView.m_unassignedUVsSize = unassignedUVs.size();

            m_pErrorLog->GetPrivate()->Add( buf, attachedDataView, blockCount==1?ELMT_INFO:ELMT_WARNING, errorContext );
        }
        
        // Assign broken vertices to the first block
        for (int i=0; i< currentLayoutMesh->GetVertexBuffers().GetElementCount(); ++i)
        {
            if ( pLayoutData[i]==65535 )
            {
                pLayoutData[i] = 0;
            }
        }
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateLayout( 
		MESH_GENERATION_RESULT& result, 
		const NodeLayoutBlocksPtrConst& node,
		uint32 currentLayoutChannel, const MeshPtr currentLayoutMesh )
    {
        if (!currentLayoutMesh)
        {
            m_pErrorLog->GetPrivate()->Add( "Generating a layout node without a parent mesh.",
                                            ELMT_ERROR, node->GetPrivate()->m_errorContext );
            return;
        }

        LayoutPtr pSourceLayout = node->GetPrivate()->m_pLayout;
        result.layouts.push_back( pSourceLayout );

        PrepareForLayout( pSourceLayout,
                          currentLayoutMesh, currentLayoutChannel,
                          node->GetPrivate()->m_errorContext  );
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh( MESH_GENERATION_RESULT& result, const NodeMeshPtrConst& Untyped )
    {
        if (!Untyped)
        {
            result = MESH_GENERATION_RESULT();
            return;
        }

		// Clear bottom-up state
		m_currentBottomUpState.m_address = nullptr;

        // See if it was already generated
		VISITED_MAP_KEY key = GetCurrentCacheKey(Untyped);
        GeneratedMeshMap::ValueType* it = m_generatedMeshes.Find( key );
        if ( it )
        {
            result = *it;
            return;
        }

		const NodeMesh* Node = Untyped.get();

        // Generate for each different type of node
		switch (Untyped->GetMeshNodeType())
		{
		case NodeMesh::EType::Constant: GenerateMesh_Constant(result, static_cast<const NodeMeshConstant*>(Node)); break;
		case NodeMesh::EType::Format: GenerateMesh_Format(result, static_cast<const NodeMeshFormat*>(Node)); break;
		case NodeMesh::EType::Morph: GenerateMesh_Morph(result, static_cast<const NodeMeshMorph*>(Node)); break;
		case NodeMesh::EType::MakeMorph: GenerateMesh_MakeMorph(result, static_cast<const NodeMeshMakeMorph*>(Node)); break;
		case NodeMesh::EType::Fragment: GenerateMesh_Fragment(result, static_cast<const NodeMeshFragment*>(Node)); break;
		case NodeMesh::EType::Interpolate: GenerateMesh_Interpolate(result, static_cast<const NodeMeshInterpolate*>(Node)); break;
		case NodeMesh::EType::Switch: GenerateMesh_Switch(result, static_cast<const NodeMeshSwitch*>(Node)); break;
		case NodeMesh::EType::Subtract: GenerateMesh_Subtract(result, static_cast<const NodeMeshSubtract*>(Node)); break;
		case NodeMesh::EType::Transform: GenerateMesh_Transform(result, static_cast<const NodeMeshTransform*>(Node)); break;
		case NodeMesh::EType::ClipMorphPlane: GenerateMesh_ClipMorphPlane(result, static_cast<const NodeMeshClipMorphPlane*>(Node)); break;
		case NodeMesh::EType::ClipWithMesh: GenerateMesh_ClipWithMesh(result, static_cast<const NodeMeshClipWithMesh*>(Node)); break;
		case NodeMesh::EType::ApplyPose: GenerateMesh_ApplyPose(result, static_cast<const NodeMeshApplyPose*>(Node)); break;
		case NodeMesh::EType::Variation: GenerateMesh_Variation(result, static_cast<const NodeMeshVariation*>(Node)); break;
		case NodeMesh::EType::Table: GenerateMesh_Table(result, static_cast<const NodeMeshTable*>(Node)); break;
		case NodeMesh::EType::GeometryOperation: GenerateMesh_GeometryOperation(result, static_cast<const NodeMeshGeometryOperation*>(Node)); break;
		case NodeMesh::EType::Reshape: GenerateMesh_Reshape(result, static_cast<const NodeMeshReshape*>(Node)); break;
		case NodeMesh::EType::ClipDeform: GenerateMesh_ClipDeform(result, static_cast<const NodeMeshClipDeform*>(Node)); break;
		case NodeMesh::EType::None: check(false);
		}

        // Cache the result
        m_generatedMeshes.Add( key, result );
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Morph( MESH_GENERATION_RESULT& result, const NodeMeshMorph* morph )
    {
        NodeMeshMorph::Private& node = *morph->GetPrivate();

        Ptr<ASTOpFixed> OpMorph = new ASTOpFixed();
        OpMorph->op.type = OP_TYPE::ME_MORPH2;

        // Factor
        if ( node.m_pFactor )
        {
            OpMorph->SetChild( OpMorph->op.args.MeshMorph2.factor, Generate( node.m_pFactor.get() ) );
        }
        else
        {
            // This argument is required
            OpMorph->SetChild( OpMorph->op.args.MeshMorph2.factor, GenerateMissingScalarCode( "Morph factor", 0.5f,
                                                                   node.m_errorContext ) );
        }

        // Base
        MESH_GENERATION_RESULT baseResult;
        if ( node.m_pBase )
        {
            GenerateMesh( baseResult, node.m_pBase );
            OpMorph->SetChild( OpMorph->op.args.MeshMorph2.base, baseResult.meshOp );
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add( "Mesh morph base node is not set.",
                                            ELMT_ERROR, node.m_errorContext );
        }

        // TODO: Support more than MUTABLE_OP_MAX_MORPH2_TARGETS targets
        if ( node.m_morphs.size()>MUTABLE_OP_MAX_MORPH2_TARGETS )
        {
            char temp[256];
            mutable_snprintf( temp, 256,
                              "A morph node has more targets [%d] than currently supported [%d].",
                              node.m_morphs.size(),
                              MUTABLE_OP_MAX_MORPH2_TARGETS );
            m_pErrorLog->GetPrivate()->Add( temp, ELMT_WARNING, node.m_errorContext );
        }

        m_overrideLayoutsStack.push_back( baseResult.layouts );
		m_activeTags.push_back({});

        int count = 0;
        for ( std::size_t t=0
            ; t<node.m_morphs.size() && t<MUTABLE_OP_MAX_MORPH2_TARGETS
            ; ++t )
        {
            if ( auto pA = node.m_morphs[t].get() )
            {
                MESH_GENERATION_RESULT targetResult;
                GenerateMesh( targetResult, pA );

                // TODO: Make sure that the target is a mesh with the morph format
                Ptr<ASTOp> target = targetResult.meshOp;

                // If the vertex indices are supposed to be relative in the targets, adjust them
                if (node.m_vertexIndicesAreRelative)
                {
                    Ptr<ASTOpMeshRemapIndices> remapIndices = new ASTOpMeshRemapIndices;
                    remapIndices->source = target;
                    remapIndices->reference = baseResult.baseMeshOp;
                    target = remapIndices;
                }

                OpMorph->SetChild( OpMorph->op.args.MeshMorph2.targets[count], target);
                count++;
            }
        }
 
        const bool bReshapeEnabled = node.m_reshapeSkeleton || node.m_reshapePhysicsVolumes;
        
        Ptr<ASTOpMeshMorphReshape> OpMorphReshape;
        if ( bReshapeEnabled )
        {
		    Ptr<ASTOpMeshBindShape> OpBind = new ASTOpMeshBindShape();
		    Ptr<ASTOpMeshApplyShape> OpApply = new ASTOpMeshApplyShape();

			// Setting reshapeVertices to false the bind op will remove all mesh members except 
			// PhysicsBodies and the Skeleton.
            OpBind->m_reshapeVertices = false;
		    OpBind->m_reshapeSkeleton = node.m_reshapeSkeleton;
		    OpBind->m_deformAllBones = node.m_deformAllBones;
		    OpBind->m_bonesToDeform = node.m_bonesToDeform;
    	    OpBind->m_reshapePhysicsVolumes = node.m_reshapePhysicsVolumes; 
			OpBind->m_physicsToDeform = node.m_physicsToDeform;
			OpBind->m_deformAllPhysics = node.m_deformAllPhysics;
			OpBind->m_bindingMethod = static_cast<uint32>(EShapeBindingMethod::ReshapeClosestProject);
            
			OpBind->Mesh = baseResult.meshOp;
            OpBind->Shape = baseResult.meshOp;
           
			OpApply->m_reshapeVertices = OpBind->m_reshapeVertices;
		    OpApply->m_reshapeSkeleton = OpBind->m_reshapeSkeleton;
		    OpApply->m_reshapePhysicsVolumes = OpBind->m_reshapePhysicsVolumes;

			OpApply->Mesh = OpBind;
            OpApply->Shape = OpMorph;

            OpMorphReshape = new ASTOpMeshMorphReshape();
            OpMorphReshape->Morph = OpMorph;
            OpMorphReshape->Reshape = OpApply;
        }

        m_overrideLayoutsStack.pop_back();
		m_activeTags.pop_back();

		if (OpMorphReshape)
		{
			result.meshOp = OpMorphReshape;
		}
		else
		{
			result.meshOp = OpMorph;
		}
        result.baseMeshOp = baseResult.baseMeshOp;

        result.layouts = baseResult.layouts;
    } 


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_MakeMorph( MESH_GENERATION_RESULT& result,
		const NodeMeshMakeMorph* morph )
    {
        NodeMeshMakeMorph::Private& node = *morph->GetPrivate();

        Ptr<ASTOpFixed> op = new ASTOpFixed();
        op->op.type = OP_TYPE::ME_DIFFERENCE;

        // \todo Texcoords are broken?
        op->op.args.MeshDifference.ignoreTextureCoords = 1;

        // Base
        MESH_GENERATION_RESULT baseResult;
        if ( node.m_pBase )
        {
            GenerateMesh( baseResult, node.m_pBase );

            op->SetChild( op->op.args.MeshDifference.base, baseResult.meshOp );
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add( "Mesh make morph base node is not set.",
                                            ELMT_ERROR, node.m_errorContext );
        }

        // Target
        m_overrideLayoutsStack.push_back( baseResult.layouts );
		m_activeTags.push_back({});
		if ( node.m_pTarget )
        {
            MESH_GENERATION_RESULT targetResult;
            GenerateMesh( targetResult, node.m_pTarget );

            op->SetChild( op->op.args.MeshDifference.target, targetResult.meshOp );
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add( "Mesh make morph target node is not set.",
                                            ELMT_ERROR, node.m_errorContext );
        }
        m_overrideLayoutsStack.pop_back();
		m_activeTags.pop_back();

        result.meshOp = op;
        result.baseMeshOp = baseResult.baseMeshOp;
        result.layouts = baseResult.layouts;
    }

    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Fragment( MESH_GENERATION_RESULT& result,
		const NodeMeshFragment* fragment )
    {
        NodeMeshFragment::Private& node = *fragment->GetPrivate();

        MESH_GENERATION_RESULT baseResult;
        if ( node.m_pMesh )
        {
            GenerateMesh( baseResult, node.m_pMesh );

            if ( node.m_fragmentType==NodeMeshFragment::FT_LAYOUT_BLOCKS )
            {
                Ptr<ASTOpMeshExtractLayoutBlocks> op = new ASTOpMeshExtractLayoutBlocks();
                result.meshOp = op;

                op->source = baseResult.meshOp;

                if ( baseResult.layouts.size()>(size_t)node.m_layoutOrGroup )
                {
                    LayoutPtrConst pSourceLayout = baseResult.layouts[ node.m_layoutOrGroup ];
                    const Layout* pLayout = m_addedLayouts[ pSourceLayout.get() ].get();
                    op->layout = (uint16_t)node.m_layoutOrGroup;

                    for ( size_t i=0; i<node.m_blocks.size(); ++i )
                    {
                        if (node.m_blocks[i]>=0 && node.m_blocks[i]<pLayout->m_blocks.Num() )
                        {
                            int bid = pLayout->m_blocks[ node.m_blocks[i] ].m_id;
                            op->blocks.push_back(bid);
                        }
                        else
                        {
                            m_pErrorLog->GetPrivate()->Add( "Internal layout block index error.",
                                                            ELMT_ERROR, node.m_errorContext );
                        }
                    }
                }
                else
                {
                    // This argument is required
                    m_pErrorLog->GetPrivate()->Add( "Missing layout in mesh fragment source.",
                                                    ELMT_ERROR, node.m_errorContext );
                }
            }

            else if ( node.m_fragmentType==NodeMeshFragment::FT_FACE_GROUP )
            {
                Ptr<ASTOpFixed> op = new ASTOpFixed();
                result.meshOp = op;

                op->op.type = OP_TYPE::ME_EXTRACTFACEGROUP;

                op->SetChild( op->op.args.MeshExtractFaceGroup.source, baseResult.meshOp );
                op->op.args.MeshExtractFaceGroup.group = node.m_layoutOrGroup;
            }

        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add( "Mesh fragment source is not set.",
                                            ELMT_ERROR, node.m_errorContext );
        }

        result.baseMeshOp = baseResult.baseMeshOp;
        result.layouts = baseResult.layouts;
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Interpolate( MESH_GENERATION_RESULT& result,
		const NodeMeshInterpolate* interpolate )
    {
        NodeMeshInterpolate::Private& node = *interpolate->GetPrivate();

        // Generate the code
        Ptr<ASTOpFixed> op = new ASTOpFixed();
        op->op.type = OP_TYPE::ME_INTERPOLATE;
        result.meshOp = op;

        // Factor
        if ( Node* pFactor = node.m_pFactor.get() )
        {
            op->SetChild( op->op.args.MeshInterpolate.factor, Generate( pFactor ) );
        }
        else
        {
            // This argument is required
            op->SetChild( op->op.args.MeshInterpolate.factor,
                          GenerateMissingScalarCode( "Interpolation factor", 0.5f, node.m_errorContext ) );
        }

        //
        Ptr<ASTOp> base = 0;
        int count = 0;
        for ( std::size_t t=0
            ; t<node.m_targets.size() && t<MUTABLE_OP_MAX_INTERPOLATE_COUNT-1
            ; ++t )
        {
            if ( NodeMesh* pA = node.m_targets[t].get() )
            {
                if ( count>0 )
                {
                    m_overrideLayoutsStack.push_back( result.layouts );
                }

                MESH_GENERATION_RESULT targetResult;
                GenerateMesh( targetResult, pA );

                if ( count>0 )
                {
                    m_overrideLayoutsStack.pop_back();
                }

                // The first target is the base
                if (count==0)
                {
                    base = targetResult.meshOp;
                    op->SetChild( op->op.args.MeshInterpolate.base, targetResult.meshOp );

                    result.baseMeshOp = targetResult.baseMeshOp;
                    result.layouts = targetResult.layouts;
                }
                else
                {
                    Ptr<ASTOpFixed> dop = new ASTOpFixed();
                    dop->op.type = OP_TYPE::ME_DIFFERENCE;
                    dop->SetChild( dop->op.args.MeshDifference.base, base );
                    dop->SetChild( dop->op.args.MeshDifference.target, targetResult.meshOp );

                    // \todo Texcoords are broken?
                    dop->op.args.MeshDifference.ignoreTextureCoords = 1;

                    if ( node.m_channels.size()>MUTABLE_OP_MAX_MORPH_CHANNELS )
                    {
                        char temp[256];
                        mutable_snprintf( temp, 256,
                                          "Morph uses too many channels [%d]. The maximum is [%d].",
                                          node.m_channels.size(),
                                          MUTABLE_OP_MAX_MORPH_CHANNELS );
                        m_pErrorLog->GetPrivate()->Add( temp, ELMT_ERROR, node.m_errorContext );
                    }

                    for ( size_t c=0;
                          c<node.m_channels.size() && c<MUTABLE_OP_MAX_MORPH_CHANNELS;
                          ++c)
                    {
                        check( node.m_channels[c].semantic<256 );
                        dop->op.args.MeshDifference.channelSemantic[c] =
                                uint8_t( node.m_channels[c].semantic );

                        check( node.m_channels[c].semanticIndex<256 );
                        dop->op.args.MeshDifference.channelSemanticIndex[c] =
                                uint8_t( node.m_channels[c].semanticIndex );
                    }

                    op->SetChild( op->op.args.MeshInterpolate.targets[count-1], dop );
                }
                count++;
            }
        }

        // At least one mesh is required
        if (!count)
        {
            // TODO
            //op.args.MeshInterpolate.target[0] = GenerateMissingImageCode( "First mesh", IF_RGB_UBYTE );
            m_pErrorLog->GetPrivate()->Add
                ( "Mesh interpolation: at least the first mesh is required.",
                  ELMT_ERROR, node.m_errorContext );
        }
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Switch( MESH_GENERATION_RESULT& result, const NodeMeshSwitch* sw )
    {
        NodeMeshSwitch::Private& node = *sw->GetPrivate();

        if (node.m_options.size() == 0)
        {
            // No options in the switch!
            // TODO
            result = MESH_GENERATION_RESULT();
			return;
        }

        Ptr<ASTOpSwitch> op = new ASTOpSwitch();
        op->type = OP_TYPE::ME_SWITCH;

        // Factor
        if ( node.m_pParameter )
        {
            op->variable = Generate( node.m_pParameter.get() );
        }
        else
        {
            // This argument is required
            op->variable = GenerateMissingScalarCode( "Switch variable", 0.0f, node.m_errorContext );
        }

        // Options
        for ( std::size_t t=0; t< node.m_options.size(); ++t )
        {
            if (t!=0)
            {
                m_overrideLayoutsStack.push_back( result.layouts );
            }

            if ( node.m_options[t] )
            {
                MESH_GENERATION_RESULT branchResults;
                GenerateMesh( branchResults, node.m_options[t] );

                auto branch = branchResults.meshOp;
                op->cases.emplace_back((int16_t)t,op,branch);

                if (t==0)
                {
                    result = branchResults;
                }
            }

            if (t!=0)
            {
                m_overrideLayoutsStack.pop_back();
            }
        }

        result.meshOp = op;
    }


	//---------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateMesh_Table(MESH_GENERATION_RESULT& result, const NodeMeshTable* TableNode)
	{
		//
		MESH_GENERATION_RESULT NewResult = result;
		int t = 0;

		Ptr<ASTOp> Op = GenerateTableSwitch<NodeMeshTable::Private, TCT_MESH, OP_TYPE::ME_SWITCH>(*TableNode->GetPrivate(), 
			[this, &NewResult, &t] (const NodeMeshTable::Private& node, int colIndex, int row, ErrorLog* pErrorLog)
			{
				NodeMeshConstantPtr pCell = new NodeMeshConstant();
				MeshPtr pMesh = node.m_pTable->GetPrivate()->m_rows[row].m_values[colIndex].m_pMesh;

				if (!pMesh)
				{
					char temp[256];
					mutable_snprintf(temp, 256,
						"Table has a missing mesh in column %d, row %d.", colIndex, row);
					pErrorLog->GetPrivate()->Add(temp, ELMT_ERROR, node.m_errorContext);
				}

				pCell->SetValue(pMesh);

				// TODO Take into account layout strategy
				int numLayouts = (int)node.m_layouts.size();
				pCell->SetLayoutCount(numLayouts);
				for (int i = 0; i < numLayouts; ++i)
				{
					pCell->SetLayout(i, node.m_layouts[i]);
				}

				if (t != 0)
				{
					m_overrideLayoutsStack.push_back(NewResult.layouts);
				}

				MESH_GENERATION_RESULT branchResults;
				GenerateMesh(branchResults, pCell);

				if (t == 0)
				{
					NewResult = branchResults;
				}
				else
				{
					m_overrideLayoutsStack.pop_back();
				}

				++t;
				return branchResults.meshOp;
			});

		NewResult.meshOp = Op;

		result = NewResult;
	}


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Variation( MESH_GENERATION_RESULT& result,
		const NodeMeshVariation* va )
    {
        NodeMeshVariation::Private& node = *va->GetPrivate();

        MESH_GENERATION_RESULT currentResult;
        Ptr<ASTOp> currentMeshOp;

        bool firstOptionProcessed = false;

        // Default case
        if ( node.m_defaultMesh )
        {
            MESH_GENERATION_RESULT branchResults;
			GenerateMesh( branchResults, node.m_defaultMesh );
            currentMeshOp = branchResults.meshOp;
            currentResult = branchResults;
            firstOptionProcessed = true;
        }

        // Process variations in reverse order, since conditionals are built bottom-up.
        for ( int t = int(node.m_variations.size())-1; t >= 0; --t )
        {
            int tagIndex = -1;
            const string& tag = node.m_variations[t].m_tag;
            for ( int i = 0; i < int( m_firstPass.m_tags.size() ); ++i )
            {
                if ( m_firstPass.m_tags[i].tag==tag)
                {
                    tagIndex = i;
                }
            }

            if ( tagIndex < 0 )
            {
                char buf[256];
                mutable_snprintf( buf, 256, "Unknown tag found in mesh variation [%s].",
                                  tag.c_str() );

                m_pErrorLog->GetPrivate()->Add( buf, ELMT_WARNING, node.m_errorContext );
                continue;
            }

            Ptr<ASTOp> variationMeshOp;
            if ( node.m_variations[t].m_mesh )
            {
                if (firstOptionProcessed)
                {
                    m_overrideLayoutsStack.push_back( currentResult.layouts );
                }
         
                MESH_GENERATION_RESULT branchResults;
				GenerateMesh( branchResults, node.m_variations[t].m_mesh );

                variationMeshOp = branchResults.meshOp;

				if (firstOptionProcessed )
				{
					m_overrideLayoutsStack.pop_back();
				}

                if ( !firstOptionProcessed )
                {
                    firstOptionProcessed = true;                   
                    currentResult = branchResults;
                }
            }


            Ptr<ASTOpConditional> conditional = new ASTOpConditional;
            conditional->type = OP_TYPE::ME_CONDITIONAL;
            conditional->no = currentMeshOp;
            conditional->yes = variationMeshOp;            
            conditional->condition = m_firstPass.m_tags[tagIndex].genericCondition;

            currentMeshOp = conditional;
        }

        result = currentResult;
        result.meshOp = currentMeshOp;
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Constant( MESH_GENERATION_RESULT& result,
		const NodeMeshConstant* constant )
    {
        NodeMeshConstant::Private& node = *constant->GetPrivate();

        Ptr<ASTOpConstantResource> op = new ASTOpConstantResource();
        op->type = OP_TYPE::ME_CONSTANT;
		result.baseMeshOp = op;
		result.meshOp = op;

        MeshPtr pMesh = node.m_pValue.get();
        if (pMesh)
        {
            // Clone the mesh
            MeshPtr pCloned = pMesh->Clone();
            pCloned->EnsureSurfaceData();
            if ( m_overrideLayoutsStack.empty() )
            {
                // This means that we are processing a base mesh

                // We will redefine the layouts
				result.layouts.clear();

                // Apply whatever transform is necessary for every layout
                for ( std::size_t l=0; l<node.m_layouts.size();++l )
                {
					NodeLayoutPtr pLayoutNode = node.m_layouts[l];
					// TODO: In a cleanup of the design of the layouts, we should remove this cast.
					const NodeLayoutBlocks* TypedNode = dynamic_cast<NodeLayoutBlocks*>(pLayoutNode.get());
					if (TypedNode)
					{
						GenerateLayout(result, TypedNode, l, pCloned);
					}
                }
            }
            else
            {
                // We need to apply the transform of the source layouts
                for ( std::size_t l=0; l<m_overrideLayoutsStack.back().size(); ++l )
                {
                    PrepareForLayout( m_overrideLayoutsStack.back()[l],
                                      pCloned, l,
                                      node.m_errorContext  );

                }

				result.layouts = m_overrideLayoutsStack.back();

                // Disabled: lots of misleading messages (when not using layouts?).
                // Warn about unnecessary layouts
//                if ( node.m_layouts.size() )
//                {
//                    m_pErrorLog->GetPrivate()->Add( "The layouts in this mesh are not required.",
//                                                    ELMT_WARNING, node.m_errorContext );
//                }
            }

            // See if we already have a mesh identical to this one, except for the internal data
            // like vertex indices.
            bool isDuplicated = false;
            MeshPtrConst pCandidate;
            for ( size_t i=0; !isDuplicated && i<m_constantMeshes.size(); ++i)
            {
                pCandidate = m_constantMeshes[i];

                if ( pCandidate->IsSimilar(*pCloned) )
                {
                    isDuplicated = true;

                    // Remap layouts from the source mesh to the the ones created for the mesh we will use instead.
                    for (int l=0; l<pCandidate->GetLayoutCount(); ++l)
                    {
                        check(pCandidate->GetLayoutCount()==pCloned->GetLayoutCount());

                        const Layout* pSourceLayoutValue = pCandidate->GetLayout(l);
                        const Layout* pDestLayoutValue = pCloned->GetLayout(l);

                        Ptr<const Layout> pDestLayoutKey = nullptr;
                        for( const auto& e : m_addedLayouts )
                        {
                            if (e.second==pDestLayoutValue)
                            {
                                pDestLayoutKey = e.first;
                            }
                        }
                        check(pDestLayoutKey);
                        m_addedLayouts[pDestLayoutKey] = pSourceLayoutValue;
                    }
                }
            }

            if (isDuplicated)
            {
                op->SetValue( pCandidate, m_compilerOptions->m_optimisationOptions.m_useDiskCache );
            }
            else
            {
                // Enumerate the vertices uniquely unless they already have indices
                int buf = -1;
                int chan = -1;
                pCloned->GetVertexBuffers().FindChannel( MBS_VERTEXINDEX,0,&buf,&chan );
                bool hasVertexIndices = (buf>=0 && chan>=0);
                if ( !hasVertexIndices )
                {
                    int newBuffer = pCloned->GetVertexBuffers().GetBufferCount();
                    pCloned->GetVertexBuffers().SetBufferCount( newBuffer+1 );
                    MESH_BUFFER_SEMANTIC semantic = MBS_VERTEXINDEX;
                    int semanticIndex = 0;
                    MESH_BUFFER_FORMAT format = MBF_UINT32;
                    int components = 1;
                    int offset = 0;
                    pCloned->GetVertexBuffers().SetBuffer
                            (
                                newBuffer,
                                sizeof(uint32_t),
                                1,
                                &semantic, &semanticIndex,
                                &format, &components,
                                &offset
                            );
                    uint32_t* pIdData = (uint32_t*)pCloned->GetVertexBuffers().GetBufferData( newBuffer );
                    for ( int i=0; i<pMesh->GetVertexCount(); ++i )
                    {
                        (*pIdData++) = m_freeVertexIndex++;
						check(m_freeVertexIndex < std::numeric_limits<uint32_t>::max());
                    }
                }

                // Add the constant data
                m_constantMeshes.push_back(pCloned);
                op->SetValue( pCloned.get(), m_compilerOptions->m_optimisationOptions.m_useDiskCache );
            }
        }
        else
        {
			result.layouts.clear();

            // This data is required
            MeshPtr pTempMesh = new Mesh();
            op->SetValue( pTempMesh, m_compilerOptions->m_optimisationOptions.m_useDiskCache );
            m_constantMeshes.push_back(pTempMesh);

            // Log an error message
            m_pErrorLog->GetPrivate()->Add( "Constant mesh not set.",
                                            ELMT_WARNING, node.m_errorContext );
        }

		// Apply the modifier for the pre-normal operations stage.
		BOTTOM_UP_STATE temp = m_currentBottomUpState;
		if (!m_activeTags.empty())
		{
			// Clearing layout stack to avoid unwanted information
			vector< vector<Ptr<const Layout>> > m_overrideLayoutsStackCopy = m_overrideLayoutsStack;
			m_overrideLayoutsStack.clear();

			bool bModifiersForBeforeOperations = true;
			result.meshOp = ApplyMeshModifiers(op, m_activeTags.back(),
				bModifiersForBeforeOperations, node.m_errorContext);

			// Retrieving stack information
			m_overrideLayoutsStack = m_overrideLayoutsStackCopy;
		}
		m_currentBottomUpState = temp;

    }




    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Format( MESH_GENERATION_RESULT& result,
		const NodeMeshFormat* format )
    {
        NodeMeshFormat::Private& node = *format->GetPrivate();

        if ( node.m_pSource )
        {
            MESH_GENERATION_RESULT baseResult;
			GenerateMesh(baseResult, node.m_pSource);

            Ptr<ASTOpMeshFormat> op = new ASTOpMeshFormat();
            op->Source = baseResult.meshOp;
            op->Buffers = 0;

            if ( node.m_rebuildTangents )
            {
                op->Buffers |= OP::MeshFormatArgs::BT_REBUILD_TANGENTS;
            }

            MeshPtr pFormatMesh = new Mesh();

            if (node.m_VertexBuffers.GetBufferCount())
            {
                op->Buffers |= OP::MeshFormatArgs::BT_VERTEX;
                pFormatMesh->m_VertexBuffers = node.m_VertexBuffers;
            }

            if (node.m_IndexBuffers.GetBufferCount())
            {
				op->Buffers |= OP::MeshFormatArgs::BT_INDEX;
                pFormatMesh->m_IndexBuffers = node.m_IndexBuffers;
            }

            if (node.m_FaceBuffers.GetBufferCount())
            {
                op->Buffers |= OP::MeshFormatArgs::BT_FACE;
                pFormatMesh->m_FaceBuffers = node.m_FaceBuffers;
            }

            Ptr<ASTOpConstantResource> cop = new ASTOpConstantResource();
            cop->type = OP_TYPE::ME_CONSTANT;
            cop->SetValue( pFormatMesh, m_compilerOptions->m_optimisationOptions.m_useDiskCache );
            op->Format = cop;

            m_constantMeshes.push_back(pFormatMesh);

            result.meshOp = op;
            result.baseMeshOp = baseResult.baseMeshOp;
            result.layouts = baseResult.layouts;
        }
        else
        {
            // Put something there
            GenerateMesh( result, new NodeMeshConstant() );
        }
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Subtract( MESH_GENERATION_RESULT& result,
		const NodeMeshSubtract* subs )
    {
		// This node is deprecated.
		check(false);        
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_Transform( MESH_GENERATION_RESULT& result,
                                                const NodeMeshTransform* trans )
    {
        const auto& node = *trans->GetPrivate();

        Ptr<ASTOpMeshTransform> op = new ASTOpMeshTransform();

        // Base
        if (node.m_pSource)
        {
            GenerateMesh( result, node.m_pSource);
            op->source = result.meshOp;
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add("Mesh transform base node is not set.",
                ELMT_ERROR, node.m_errorContext);
        }

        op->matrix = node.m_transform;

        result.meshOp = op;
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_ClipMorphPlane( MESH_GENERATION_RESULT& result,
                                                     const NodeMeshClipMorphPlane* clip )
    {
        const auto& node = *clip->GetPrivate();

        Ptr<ASTOpMeshClipMorphPlane> op = new ASTOpMeshClipMorphPlane();

        // Base
        if (node.m_pSource)
        {
            GenerateMesh( result, node.m_pSource);
            op->source = result.meshOp;
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add("Mesh clip-morph-plane source node is not set.",
                ELMT_ERROR, node.m_errorContext);
        }

        // Morph to an ellipse
        {
            op->morphShape.type = (uint8_t)SHAPE::Type::Ellipse;
            op->morphShape.position = node.m_origin;
            op->morphShape.up = node.m_normal;
            op->morphShape.size = vec3f(node.m_radius1, node.m_radius2, node.m_rotation); // TODO: Move rotation to ellipse rotation reference base instead of passing it directly

                                                                                      // Generate a "side" vector.
                                                                                      // \todo: make generic and move to the vector class
            {
                // Generate vector perpendicular to normal for ellipse rotation reference base
                vec3f aux_base(0.f, 1.f, 0.f);

                if (fabs(dot(node.m_normal, aux_base)) > 0.95f)
                {
                    aux_base = vec3f(0.f, 0.f, 1.f);
                }

                op->morphShape.side = cross(node.m_normal, aux_base);
            }
        }

        // Selection by shape
        if (node.m_vertexSelectionType== NodeMeshClipMorphPlane::Private::VS_SHAPE)
        {
            op->vertexSelectionType = OP::MeshClipMorphPlaneArgs::VS_SHAPE;
            op->selectionShape.type = (uint8_t)SHAPE::Type::AABox;
            op->selectionShape.position = node.m_selectionBoxOrigin;
            op->selectionShape.size = node.m_selectionBoxRadius;
        }
        else if (node.m_vertexSelectionType == NodeMeshClipMorphPlane::Private::VS_BONE_HIERARCHY)
        {
            // Selection by bone hierarchy?
            op->vertexSelectionType = OP::MeshClipMorphPlaneArgs::VS_BONE_HIERARCHY;
            op->vertexSelectionBone = node.m_vertexSelectionBone;
			op->vertexSelectionBoneMaxRadius = node.m_maxEffectRadius;
        }
        else
        {
            op->vertexSelectionType = OP::MeshClipMorphPlaneArgs::VS_NONE;
        }

        // Parameters
        op->dist = node.m_dist;
        op->factor = node.m_factor;

        result.meshOp = op;
    }


    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_ClipWithMesh( MESH_GENERATION_RESULT& result,
                                                   const NodeMeshClipWithMesh* clip )
    {
        const auto& node = *clip->GetPrivate();

        Ptr<ASTOpFixed> op = new ASTOpFixed();
        op->op.type = OP_TYPE::ME_CLIPWITHMESH;

        // Base
        if (node.m_pSource)
        {
            GenerateMesh( result, node.m_pSource );
            op->SetChild( op->op.args.MeshClipWithMesh.source, result.meshOp );
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add("Mesh clip-with-mesh source node is not set.",
                ELMT_ERROR, node.m_errorContext);
        }

        // Clipping mesh
        if (node.m_pClipMesh)
        {
			m_activeTags.push_back({});

            MESH_GENERATION_RESULT clipResult;
            GenerateMesh( clipResult, node.m_pClipMesh);
            op->SetChild( op->op.args.MeshClipWithMesh.clipMesh, clipResult.meshOp );

			m_activeTags.pop_back();
		}
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add("Mesh clip-with-mesh clipping mesh node is not set.",
                ELMT_ERROR, node.m_errorContext);
        }

        result.meshOp = op;
    }

	//---------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateMesh_ClipDeform(MESH_GENERATION_RESULT& Result, const NodeMeshClipDeform* ClipDeform)
	{
		const auto& Node = *ClipDeform->GetPrivate();

		const Ptr<ASTOpMeshBindShape> OpBind = new ASTOpMeshBindShape();
		const Ptr<ASTOpMeshClipDeform> OpClipDeform = new ASTOpMeshClipDeform();

		// Base Mesh
		if (Node.m_pBaseMesh)
		{
			GenerateMesh(Result, Node.m_pBaseMesh);
			OpBind->Mesh = Result.meshOp;
		}
		else
		{
			// This argument is required
			m_pErrorLog->GetPrivate()->Add("Mesh Clip Deform base mesh node is not set.",
				ELMT_ERROR, Node.m_errorContext);
		}

		// Base Shape
		if (Node.m_pClipShape)
		{
			MESH_GENERATION_RESULT baseResult;
			GenerateMesh(baseResult, Node.m_pClipShape);
			OpBind->Shape = baseResult.meshOp;
			OpClipDeform->ClipShape = baseResult.meshOp;
		}

    	OpBind->m_discardInvalidBindings = false;
		OpClipDeform->Mesh = OpBind;

		Result.meshOp = OpClipDeform;
	}

    //---------------------------------------------------------------------------------------------
    void CodeGenerator::GenerateMesh_ApplyPose( MESH_GENERATION_RESULT& result,
                                                const NodeMeshApplyPose* pose )
    {
        const auto& node = *pose->GetPrivate();

        Ptr<ASTOpMeshApplyPose> op = new ASTOpMeshApplyPose();

        // Base
        if (node.m_pBase)
        {
            GenerateMesh( result, node.m_pBase );
            op->base = result.meshOp;
        }
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add("Mesh apply-pose base node is not set.",
                ELMT_ERROR, node.m_errorContext);
        }

        // Pose mesh
        if (node.m_pPose)
        {
            // We don't need layouts for the pose mesh
            m_overrideLayoutsStack.push_back( vector<LayoutPtrConst>() );
			m_activeTags.push_back({});

            MESH_GENERATION_RESULT poseResult;
            GenerateMesh( poseResult, node.m_pPose );
            op->pose = poseResult.meshOp;

            m_overrideLayoutsStack.pop_back();
			m_activeTags.pop_back();
		}
        else
        {
            // This argument is required
            m_pErrorLog->GetPrivate()->Add("Mesh apply-pose pose node is not set.",
                ELMT_ERROR, node.m_errorContext);
        }

        result.meshOp = op;
    }


	//---------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateMesh_GeometryOperation(MESH_GENERATION_RESULT& result, const NodeMeshGeometryOperation* geom)
	{
		const auto& node = *geom->GetPrivate();

		Ptr<ASTOpMeshGeometryOperation> op = new ASTOpMeshGeometryOperation();

		// Mesh A
		if (node.m_pMeshA)
		{
			GenerateMesh(result, node.m_pMeshA);
			op->meshA = result.meshOp;
		}
		else
		{
			// This argument is required
			m_pErrorLog->GetPrivate()->Add("Mesh geometric op mesh-a node is not set.",
				ELMT_ERROR, node.m_errorContext);
		}

		// Mesh B
		if (node.m_pMeshB)
		{
			MESH_GENERATION_RESULT bResult;
			GenerateMesh(bResult, node.m_pMeshB);
			op->meshB = bResult.meshOp;
		}

		op->scalarA = Generate(node.m_pScalarA);
		op->scalarB = Generate(node.m_pScalarB);

		result.meshOp = op;
	}


	//---------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateMesh_Reshape(MESH_GENERATION_RESULT& result, const NodeMeshReshape* reshape)
	{
		const NodeMeshReshape::Private& node = *reshape->GetPrivate();

		Ptr<ASTOpMeshBindShape> opBind = new ASTOpMeshBindShape();
		Ptr<ASTOpMeshApplyShape> opApply = new ASTOpMeshApplyShape();

		opBind->m_reshapeSkeleton = node.m_reshapeSkeleton;
    	opBind->m_enableRigidParts = node.m_enableRigidParts;
		opBind->m_deformAllBones = node.m_deformAllBones;
		opBind->m_bonesToDeform = node.m_bonesToDeform;
    	opBind->m_reshapePhysicsVolumes = node.m_reshapePhysicsVolumes;
		opBind->m_deformAllPhysics = node.m_deformAllPhysics;
		opBind->m_physicsToDeform = node.m_physicsToDeform;
		opBind->m_reshapeVertices = true;
		opBind->m_bindingMethod = static_cast<uint32>(EShapeBindingMethod::ReshapeClosestProject);

		opApply->m_reshapeVertices = true;	
		opApply->m_reshapeSkeleton = node.m_reshapeSkeleton;
		opApply->m_reshapePhysicsVolumes = node.m_reshapePhysicsVolumes;

		// Base Mesh
		if (node.m_pBaseMesh)
		{
			GenerateMesh(result, node.m_pBaseMesh);
			opBind->Mesh = result.meshOp;
		}
		else
		{
			// This argument is required
			m_pErrorLog->GetPrivate()->Add("Mesh reshape base node is not set.", ELMT_ERROR, node.m_errorContext);
		}

		// Base and target shapes shouldn't have layouts or modifiers.
		m_overrideLayoutsStack.push_back({});
		m_activeTags.push_back({});

		// Base Shape
		if (node.m_pBaseShape)
		{
			MESH_GENERATION_RESULT baseResult;
			GenerateMesh(baseResult, node.m_pBaseShape);
			opBind->Shape = baseResult.meshOp;
		}

		opApply->Mesh = opBind;

		// Target Shape
		if (node.m_pTargetShape)
		{
			MESH_GENERATION_RESULT targetResult;
			GenerateMesh(targetResult, node.m_pTargetShape);
			opApply->Shape = targetResult.meshOp;
		}

		m_overrideLayoutsStack.pop_back();
		m_activeTags.pop_back();

		result.meshOp = opApply;
	}

}

