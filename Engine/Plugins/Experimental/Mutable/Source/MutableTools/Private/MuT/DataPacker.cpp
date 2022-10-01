// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataPacker.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/Streams.h"

#include "MuR/CodeVisitor.h"
#include "MuR/ModelPrivate.h"
#include "MuR/SystemPrivate.h"
#include "MuR/Operations.h"
#include "MuR/OpImagePixelFormat.h"

#include "MuR/MutableTrace.h"
#include "MuT/ASTOpImageCompose.h"
#include "MuT/ASTOpConstantResource.h"
#include "MuT/ASTOpImageMultiLayer.h"
#include "MuT/ASTOpMeshRemoveMask.h"
#include "MuT/ASTOpInstanceAdd.h"
#include "MuT/ASTOpMeshExtractLayoutBlocks.h"

#include <algorithm>
#include <cstdint>
#include <random>


namespace mu
{

    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    SubtreeSearchConstantVisitor::SubtreeSearchConstantVisitor( PROGRAM& program,
                                                                OP::ADDRESS constant,
                                                                OP_TYPE optype )
        : m_program( program ),
          m_constant( constant ),
          m_opType( optype )
    {
        m_visited.resize( program.m_opAddress.size() );
        if ( m_visited.size() )
        {
            memset( &m_visited[0], 0, m_visited.size() );
        }
    }


    //---------------------------------------------------------------------------------------------
    bool SubtreeSearchConstantVisitor::Run( OP::ADDRESS root )
    {
        // too spammy
        // \todo: called too many times!
        // MUTABLE_CPUPROFILER_SCOPE(SubtreeSearchConstantVisitor);

        bool found = false;

        vector< std::pair<bool,int> > pending;
        pending.reserve(m_program.m_opAddress.size()/4);

        pending.push_back( std::make_pair(false,root) );

        while( pending.size() )
        {
            std::pair<bool,int> item = pending.back();
            pending.pop_back();

            if (item.first)
            {
                // Item indicating we finished with all the children of a parent.

                // Propagate the "found" state upwards
                m_visited[item.second]=1;

                //OP cop = m_program.m_code[item.second];
                ForEachReference( m_program, (OP::ADDRESS)item.second, [&](OP::ADDRESS ref)
                {
                    if (ref && m_visited[ref]==2)
                    {
                        m_visited[item.second]=2;
                    }
                });
            }
            else
            {
                if ( !m_visited[item.second] )
                {
                    OP_TYPE thisOpType = m_program.GetOpType(item.second);
                    if ( m_opType == thisOpType )
                    {
                        auto args = m_program.GetOpArgs<OP::ResourceConstantArgs>(item.second);
                        if ( m_constant == args.value )
                        {
                            found = true;
                            m_visited[item.second] = 2;
                        }
                    }

                    if (!found)
                    {
                        pending.push_back( std::make_pair(true,item.second) );

                        //OP cop = m_program.m_code[item.second];
                        ForEachReference( m_program, item.second, [&](OP::ADDRESS ref)
                        {
                            if (ref && !m_visited[ref])
                            {
                                pending.push_back( std::make_pair(false,ref) );
                            }
                        });
                    }
                }
                else
                {
                    found = (m_visited[item.second]==2);
                }
            }
        }

        return found;
    }



    //---------------------------------------------------------------------------------------------
    //! Get all the parameters that affect the constant.
    //! "Affect" means that the constant may be used or not depending on the parameter.
    //---------------------------------------------------------------------------------------------
    class GatherParametersVisitor : public UniqueConstCodeVisitorIterative< vector<int> >
    {
    public:

        GatherParametersVisitor( PROGRAM& program, OP::ADDRESS constant, OP_TYPE opType )
            : m_constSearch( program, constant, opType )
        {
            MUTABLE_CPUPROFILER_SCOPE(GatherParametersVisitor);

            vector<int> currentParams;
            currentParams.resize( program.m_parameters.size(), 0 );
            SetDefaultState(currentParams);

            // TODO: we can optimize by precalculating what ops have the required optype below
            m_opType = opType;
            m_constant = constant;

            m_allParams.resize( program.m_parameters.size(), 0 );
            FullTraverse( program );

            for (size_t i=0; i<m_allParams.size(); ++i )
            {
                if ( m_allParams[i] )
                {
                    m_sortedParams.push_back( int(i) );
                }
            }
        }


        virtual bool Visit( OP::ADDRESS at, PROGRAM& program )
        {
            bool recurse = true;

            if ( m_constSearch.Run( at ) )
            {
                switch ( program.GetOpType(at) )
                {
                case OP_TYPE::NU_CONDITIONAL:
                case OP_TYPE::SC_CONDITIONAL:
                case OP_TYPE::CO_CONDITIONAL:
                case OP_TYPE::IM_CONDITIONAL:
                case OP_TYPE::ME_CONDITIONAL:
                case OP_TYPE::LA_CONDITIONAL:
                case OP_TYPE::IN_CONDITIONAL:
                {
                    auto args = program.GetOpArgs<OP::ConditionalArgs>(at);

                    // If the constant is present in only one of the 2 branches
                    bool foundYes = m_constSearch.Run( args.yes  );
                    bool foundNo = m_constSearch.Run( args.no );

                    if ( foundYes!=foundNo )
                    {
                        m_conditionVisitor.Run( args.condition, program );

                        vector<int> currentParams = GetCurrentState();
                        for (size_t p=0; p<m_conditionVisitor.m_params.size(); ++p )
                        {
                            currentParams[ m_conditionVisitor.m_params[p] ]++;
                        }

                        // Set state for child recursion
                        SetCurrentState( currentParams );
                    }

                    break;
                }

                case OP_TYPE::NU_SWITCH:
                case OP_TYPE::SC_SWITCH:
                case OP_TYPE::CO_SWITCH:
                case OP_TYPE::IM_SWITCH:
                case OP_TYPE::ME_SWITCH:
                case OP_TYPE::LA_SWITCH:
                {
					const uint8_t* data = program.GetOpArgsPointer(at);
					
					OP::ADDRESS VarAddress;
					FMemory::Memcpy( &VarAddress, data, sizeof(OP::ADDRESS) );

                    m_conditionVisitor.Run( VarAddress, program );

                    vector<int> currentParams = GetCurrentState();
                    for (size_t p=0; p<m_conditionVisitor.m_params.size(); ++p )
                    {
                        currentParams[ m_conditionVisitor.m_params[p] ]++;
                    }

                    // Set state for child recursion
                    SetCurrentState( currentParams );

                    break;
                }

                default:
                {
                    // If we hit the constant we are analysing
                    if ( m_opType == OP_TYPE::ME_CONSTANT
                         &&
                         program.GetOpType(at) == m_opType )
                    {
                        auto args = program.GetOpArgs<OP::MeshConstantArgs>(at);
                        if ( args.value == m_constant )
                        {
                            // Accumulate the currently relevant parameters
                            const vector<int>& currentParams = GetCurrentState();
                            for (size_t i=0; i<currentParams.size(); ++i )
                            {
                                m_allParams[i] += currentParams[i];
                            }
                        }
                    }
                    else if ( program.GetOpType(at) == m_opType )
                    {
                        auto args = program.GetOpArgs<OP::ResourceConstantArgs>(at);
                        if ( args.value == m_constant )
                        {
                            // Accumulate the currently relevant parameters
                            const vector<int>& currentParams = GetCurrentState();
                            for (size_t i=0; i<currentParams.size(); ++i )
                            {
                                m_allParams[i] += currentParams[i];
                            }
                        }
                    }

                    break;
                }

                }

            }

            return recurse;
        }

    public:

        //! Output result
        vector<int> m_sortedParams;

    private:

        OP::ADDRESS m_constant;
        OP_TYPE m_opType;

        vector<int> m_allParams;

        SubtreeParametersVisitor m_conditionVisitor;
        SubtreeSearchConstantVisitor m_constSearch;

    };


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    class AccumulateImageFormatsAST : public Visitor_TopDown_Unique_Const< vector<bool> >
    {
    public:

        void Run( const ASTOpList& roots )
        {
            MUTABLE_CPUPROFILER_SCOPE(AccumulateImageFormatsAST);

            // TODO: Use statically sized vectors (std::array)
            //vector<bool> initial( IF_COUNT, true );
            //std::fill( m_supportedFormats.begin(), m_supportedFormats.end(), initial );

            vector<bool> defaultState(size_t(EImageFormat::IF_COUNT), false);

            Traverse( roots, defaultState );
        }


        bool Visit( const Ptr<ASTOp>& node ) override
        {
            bool recurse = true;

            const vector<bool>& currentFormats = GetCurrentState();
            bool allFalse = currentFormats == vector<bool>(size_t(EImageFormat::IF_COUNT), false );

            // Can we use the cache?
            if (allFalse)
            {
                if (m_visited.count(node))
                {
                    return false;
                }

                m_visited.insert(node);
            }

            switch ( node->GetOpType() )
            {
            case OP_TYPE::IM_CONSTANT:
            {
                // Remove unsupported formats
                auto op = dynamic_cast<const ASTOpConstantResource*>(node.get());
                if (!m_supportedFormats.count(op))
                {
                    vector<bool> initial(size_t(EImageFormat::IF_COUNT), true );
                    m_supportedFormats.insert( std::make_pair(op, std::move(initial)) );
                }

                for ( unsigned f=0; f< unsigned(EImageFormat::IF_COUNT); ++f )
                {
                    if ( !currentFormats[f] )
                    {
                        m_supportedFormats[op][f] = false;
                    }
                }
                recurse = false;
                break;
            }

            case OP_TYPE::IM_SWITCH:
            case OP_TYPE::IM_CONDITIONAL:
                // Switches and conditionals don't change the supported formats
                break;

            case OP_TYPE::IM_COMPOSE:
            {
                recurse = false;
				const ASTOpImageCompose* op = dynamic_cast<const ASTOpImageCompose*>(node.get());

                vector<bool> newState = vector<bool>(size_t(EImageFormat::IF_COUNT), false);
                RecurseWithState( op->Layout.child(), newState );
                RecurseWithState( op->Base.child(), newState );
                RecurseWithState( op->BlockImage.child(), newState );

                if ( op->Mask )
                {
                    newState[(size_t)EImageFormat::IF_L_UBIT_RLE] = true;
                    RecurseWithState( op->Mask.child(), newState );
                }
                break;
            }

            case OP_TYPE::IM_LAYERCOLOUR:
            {
                recurse = false;
				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());
                vector<bool> newState = vector<bool>(size_t(EImageFormat::IF_COUNT), false);
                RecurseWithState( op->children[op->op.args.ImageLayerColour.base].child(), newState );
                RecurseWithState( op->children[op->op.args.ImageLayerColour.colour].child(), newState );

                if ( op->children[op->op.args.ImageLayerColour.mask] )
                {
                    newState[(size_t)EImageFormat::IF_L_UBYTE] = true;
                    newState[(size_t)EImageFormat::IF_L_UBYTE_RLE] = true;

                    RecurseWithState( op->children[op->op.args.ImageLayerColour.mask].child(), newState );
                }
                break;
            }

            case OP_TYPE::IM_LAYER:
            {
                recurse = false;
				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());
                vector<bool> newState = vector<bool>(size_t(EImageFormat::IF_COUNT), false);
                RecurseWithState( op->children[op->op.args.ImageLayer.base].child(), newState );
                RecurseWithState( op->children[op->op.args.ImageLayer.blended].child(), newState );

                if (op->children[op->op.args.ImageLayer.mask])
                {
                    newState[(size_t)EImageFormat::IF_L_UBYTE] = true;
                    newState[(size_t)EImageFormat::IF_L_UBYTE_RLE] = true;

                    RecurseWithState( op->children[op->op.args.ImageLayer.mask].child(), newState );
                }
                break;
            }

            case OP_TYPE::IM_MULTILAYER:
            {
                recurse = false;
				const ASTOpImageMultiLayer* op = dynamic_cast<const ASTOpImageMultiLayer*>(node.get());
                vector<bool> newState = vector<bool>(size_t(EImageFormat::IF_COUNT), false);
                RecurseWithState( op->base.child(), newState );
                RecurseWithState( op->blend.child(), newState );

                if (op->mask)
                {
                    newState[(size_t)EImageFormat::IF_L_UBYTE] = true;
                    newState[(size_t)EImageFormat::IF_L_UBYTE_RLE] = true;

                    RecurseWithState( op->mask.child(), newState );
                }
                break;
            }

            case OP_TYPE::IM_DISPLACE:
            {
                recurse = false;
				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());
                vector<bool> newState = vector<bool>(size_t(EImageFormat::IF_COUNT), false);
                RecurseWithState( op->children[op->op.args.ImageDisplace.source].child(), newState );

                newState[(size_t)EImageFormat::IF_L_UBYTE ] = true;
                newState[(size_t)EImageFormat::IF_L_UBYTE_RLE ] = true;

                RecurseWithState( op->children[op->op.args.ImageDisplace.displacementMap].child(), newState );
                break;
            }

            default:
            {
                //m_currentFormats.push_back(vector<bool>(IF_COUNT, false));
                //Recurse(at, program);
                //m_currentFormats.pop_back();

                vector<bool> newState = vector<bool>(size_t(EImageFormat::IF_COUNT), false);
                if (currentFormats != newState)
                {
                    RecurseWithState(node, newState);
                    recurse = false;
                }
                else
                {
                    recurse = true;
                }
                break;
            }

            }

            return recurse;
        }

    public:

        //! Result of this visitor:
        //! Formats known to be supported by every constant image.
        std::unordered_map< Ptr<const ASTOpConstantResource>, vector<bool> > m_supportedFormats;

    private:

        //! Cache. Only valid is current formats are all false.
        std::unordered_set<Ptr<ASTOp>> m_visited;

    };




    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    class AccumulateMeshChannelUsageAST : public Visitor_TopDown_Unique_Const< uint64_t >
    {
    public:

        void Run( const ASTOpList& roots )
        {
            MUTABLE_CPUPROFILER_SCOPE(AccumulateMeshChannelUsageAST);

            // Sanity check in case we add more semantics
            static_assert(MBS_COUNT<sizeof(uint64_t)*8, "Too many mesh buffer semantics." );

            // Default state: we need everything except internal semantics
            uint64_t defaultState = 0xffffffffffffffff;
            defaultState ^= (UINT64_C(1)<<MBS_LAYOUTBLOCK);
            defaultState ^= (UINT64_C(1)<<MBS_CHART);
            defaultState ^= (UINT64_C(1)<<MBS_VERTEXINDEX);

            Traverse(roots,defaultState);
        }


        bool Visit( const Ptr<ASTOp>& node ) override
        {
            bool recurse = true;

            uint64_t currentSemantics = GetCurrentState();

            switch ( node->GetOpType() )
            {

            case OP_TYPE::ME_CONSTANT:
            {
                // Accumulate necessary semantics
				const ASTOpConstantResource* op = dynamic_cast<const ASTOpConstantResource*>(node.get());
                uint64_t currentFlags = 0;
                if (m_requiredSemantics.count(op))
                {
                    currentFlags = m_requiredSemantics[op];
                }
                else
                {
                    m_requiredSemantics.insert(std::make_pair(op,currentFlags));
                }

                currentFlags |= currentSemantics;
                m_requiredSemantics[op] = currentFlags;
                recurse = false;
                break;
            }

            // TODO: These could probably optimise something
            //case OP_TYPE::IM_RASTERMESH: break;

            case OP_TYPE::ME_DIFFERENCE:
            {
                recurse = false;

				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());

                uint64_t newState = currentSemantics;
                newState |= (UINT64_C(1)<<MBS_VERTEXINDEX);
                RecurseWithState( op->children[op->op.args.MeshDifference.base].child(), newState );

                RecurseWithState( op->children[op->op.args.MeshDifference.target].child(), currentSemantics );
                break;
             }

            case OP_TYPE::ME_REMOVEMASK:
            {
                recurse = false;

				const ASTOpMeshRemoveMask* op = dynamic_cast<const ASTOpMeshRemoveMask*>(node.get());

                uint64_t newState = currentSemantics;
                newState |= (UINT64_C(1)<<MBS_VERTEXINDEX);
                RecurseWithState( op->source.child(), newState );
                for( auto& r: op->removes )
                {
                    RecurseWithState( r.second.child(), newState );
                }
                break;
             }

            case OP_TYPE::ME_MORPH2:
            {
                recurse = false;

				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());

                uint64_t newState = currentSemantics;
                newState |= (UINT64_C(1)<<MBS_VERTEXINDEX);
                RecurseWithState( op->children[op->op.args.MeshMorph2.base].child(), newState );
                for ( int o=0; o<MUTABLE_OP_MAX_MORPH2_TARGETS; ++o )
                {
                    if (op->children[op->op.args.MeshMorph2.targets[o]])
                    {
                        RecurseWithState( op->children[op->op.args.MeshMorph2.targets[o]].child(), newState );
                    }
                }
                break;
             }


            case OP_TYPE::ME_APPLYLAYOUT:
            {
                recurse = false;

				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());

                uint64_t newState = currentSemantics;
                newState |= (UINT64_C(1)<<MBS_LAYOUTBLOCK);
                RecurseWithState( op->children[op->op.args.MeshApplyLayout.mesh].child(), newState );

                RecurseWithState( op->children[op->op.args.MeshApplyLayout.layout].child(), currentSemantics );
                break;
            }

            case OP_TYPE::ME_EXTRACTLAYOUTBLOCK:
            {
                recurse = false;

				const ASTOpMeshExtractLayoutBlocks* op = dynamic_cast<const ASTOpMeshExtractLayoutBlocks*>(node.get());

                // todo: check if we really need all of them
                uint64_t newState = currentSemantics;
                newState |= (UINT64_C(1)<<MBS_LAYOUTBLOCK);
                newState |= (UINT64_C(1)<<MBS_CHART);
                newState |= (UINT64_C(1)<<MBS_VERTEXINDEX);

                RecurseWithState( op->source.child(), newState );
                break;
            }

            case OP_TYPE::ME_EXTRACTFACEGROUP:
            {
                recurse = false;

				const ASTOpFixed* op = dynamic_cast<const ASTOpFixed*>(node.get());

                // todo: check if we really need all of them
                uint64_t newState = currentSemantics;
                newState |= (UINT64_C(1)<<MBS_LAYOUTBLOCK);
                newState |= (UINT64_C(1)<<MBS_CHART);
                newState |= (UINT64_C(1)<<MBS_VERTEXINDEX);

                RecurseWithState( op->children[op->op.args.MeshExtractFaceGroup.source].child(), newState );
                break;
            }

            case OP_TYPE::IN_ADDMESH:
            {
                recurse = false;

				const ASTOpInstanceAdd* op = dynamic_cast<const ASTOpInstanceAdd*>(node.get());

                RecurseWithState( op->instance.child(), currentSemantics );

                uint64_t newState = GetDefaultState();
                RecurseWithState( op->value.child(), newState );
                break;
            }

            default:
                // Unhandled op, we may need everything? Recurse with current state?
                //uint64_t newState = 0xffffffffffffffff;
                break;

            }

            return recurse;
        }

    public:

        //! Result of this visitor:
        //! Used mesh channel semantics for each constant mesh
        std::unordered_map< Ptr<const ASTOpConstantResource>, uint64_t > m_requiredSemantics;

    };


    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------
    //---------------------------------------------------------------------------------------------

    // Todo: move to its own file
    inline void MeshRemoveUnusedBufferSemantics( Mesh* pMesh, uint64_t usedSemantics )
    {
        // right now we only remove entire buffers if no channel is used
        // TODO: remove from inside the buffer?
        for (int v=0; v<pMesh->GetVertexBuffers().GetBufferCount(); )
        {
            bool used = false;
            for (int c=0; !used && c<pMesh->GetVertexBuffers().GetBufferChannelCount(v); ++c)
            {
                MESH_BUFFER_SEMANTIC semantic;
                pMesh->GetVertexBuffers().GetChannel(v,c,&semantic,
                                                      nullptr, nullptr, nullptr, nullptr);
                used = (( (UINT64_C(1)<<semantic) ) & usedSemantics) != 0;
            }

            if (!used)
            {
                TArray<MESH_BUFFER>& buffers = pMesh->GetVertexBuffers().m_buffers;
                buffers.RemoveAt(v);
            }
            else
            {
                ++v;
            }
        }

        // TODO: hack, if we don't need layouts, remove them.
        {
            uint64_t layoutSemantics = 0;
            layoutSemantics |= (UINT64_C(1)<<MBS_LAYOUTBLOCK);
            layoutSemantics |= (UINT64_C(1)<<MBS_CHART);

            if ( (usedSemantics&layoutSemantics) == 0)
            {
                pMesh->m_layouts.Empty();
            }
        }
    }


    //---------------------------------------------------------------------------------------------
    void DataOptimiseAST( int imageCompressionQuality, ASTOpList& roots,
                          const MODEL_OPTIMIZATION_OPTIONS& options )
    {
        // Images
        AccumulateImageFormatsAST accFormat;
        accFormat.Run( roots );

		// TEMP: Disabled for RLE bug
        // See if we can convert some constants to more efficient formats
        ASTOp::Traverse_BottomUp_Unique_NonReentrant( roots, [&](Ptr<ASTOp>& n)
        {
            if (n->GetOpType()==OP_TYPE::IM_CONSTANT)
            {
				ASTOpConstantResource* typed = dynamic_cast<ASTOpConstantResource*>(n.get());
                Ptr<const Image> pOld = static_cast<const Image*>(typed->GetValue().get());
                if ( accFormat.m_supportedFormats[typed][(size_t)EImageFormat::IF_L_UBIT_RLE] )
                {
                    ImagePtr pNew =
                        ImagePixelFormat( imageCompressionQuality, pOld.get(), EImageFormat::IF_L_UBIT_RLE );

                    // Only replace if the compression was worth!
                    size_t oldSize = pOld->GetDataSize();
                    size_t newSize = pNew->GetDataSize();
                    if (float(oldSize) > float(newSize) * options.m_minRLECompressionGain)
                    {
                        typed->SetValue(pNew, options.m_useDiskCache);
                    }
                }
                else if ( accFormat.m_supportedFormats[typed][(size_t)EImageFormat::IF_L_UBYTE_RLE] )
                {
                    ImagePtr pNew =
                        ImagePixelFormat( imageCompressionQuality, pOld.get(), EImageFormat::IF_L_UBYTE_RLE );

                    // Only replace if the compression was worth!
                    size_t oldSize = pOld->GetDataSize();
                    size_t newSize = pNew->GetDataSize();
                    if (float(oldSize) > float(newSize) * options.m_minRLECompressionGain)
                    {
                        typed->SetValue(pNew, options.m_useDiskCache);
                    }
                }
            }
        });


        // Meshes
        AccumulateMeshChannelUsageAST meshSemanticsVisitor;
        meshSemanticsVisitor.Run( roots );

        // See if we can remove some buffers from the constants
        ASTOp::Traverse_BottomUp_Unique_NonReentrant( roots, [&](Ptr<ASTOp>& n)
        {
            if (n->GetOpType()==OP_TYPE::ME_CONSTANT)
            {
				ASTOpConstantResource* typed = dynamic_cast<ASTOpConstantResource*>(n.get());
                Ptr<Mesh> pMesh = static_cast<const Mesh*>(typed->GetValue().get())->Clone();
                MeshRemoveUnusedBufferSemantics( pMesh.get(), meshSemanticsVisitor.m_requiredSemantics[typed]);
                typed->SetValue(pMesh, options.m_useDiskCache);
            }
        });

    }


}
