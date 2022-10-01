// Copyright Epic Games, Inc. All Rights Reserved.

#include "ASTOpMeshRemoveMask.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/CodeOptimiser.h"
#include "MuT/StreamsPrivate.h"


using namespace mu;


//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
ASTOpMeshRemoveMask::ASTOpMeshRemoveMask()
    : source(this)
{
}


//---------------------------------------------------------------------------------------------
ASTOpMeshRemoveMask::~ASTOpMeshRemoveMask()
{
    // Explicit call needed to avoid recursive destruction
    ASTOp::RemoveChildren();
}


//---------------------------------------------------------------------------------------------
void ASTOpMeshRemoveMask::AddRemove( const Ptr<ASTOp>& condition, const Ptr<ASTOp>& mask  )
{
    removes.push_back( std::make_pair<>( ASTChild(this,condition),
                                         ASTChild(this,mask) ) );
}


//---------------------------------------------------------------------------------------------
bool ASTOpMeshRemoveMask::IsEqual(const ASTOp& otherUntyped) const
{
    if ( auto other = dynamic_cast<const ASTOpMeshRemoveMask*>(&otherUntyped) )
    {
        return source==other->source && removes==other->removes;
    }
    return false;
}


//---------------------------------------------------------------------------------------------
mu::Ptr<ASTOp> ASTOpMeshRemoveMask::Clone(MapChildFunc& mapChild) const
{
    Ptr<ASTOpMeshRemoveMask> n = new ASTOpMeshRemoveMask();
    n->source = mapChild(source.child());
    for(const auto& r:removes)
    {
        n->removes.push_back( std::make_pair<>( ASTChild(n,mapChild(r.first.child())),
                                                ASTChild(n,mapChild(r.second.child()))
                                                ) );
    }
    return n;
}


//---------------------------------------------------------------------------------------------
void ASTOpMeshRemoveMask::Assert()
{
    ASTOp::Assert();
}


//---------------------------------------------------------------------------------------------
void ASTOpMeshRemoveMask::ForEachChild(const std::function<void(ASTChild&)>& f )
{
    f(source);
    for(auto& r:removes)
    {
        f(r.first);
        f(r.second);
    }
}


//---------------------------------------------------------------------------------------------
uint64 ASTOpMeshRemoveMask::Hash() const
{
	uint64 res = std::hash<ASTOp*>()( source.child().get() );
    for(const auto& r:removes)
    {
        hash_combine( res, r.first.child().get() );
        hash_combine( res, r.second.child().get() );
    }
    return res;
}


//---------------------------------------------------------------------------------------------
void ASTOpMeshRemoveMask::Link( PROGRAM& program, const FLinkerOptions*)
{
    // Already linked?
    if (!linkedAddress)
    {
        linkedAddress = (OP::ADDRESS)program.m_opAddress.size();

        program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
        AppendCode(program.m_byteCode, OP_TYPE::ME_REMOVEMASK);
        OP::ADDRESS sourceAt = source ? source->linkedAddress : 0;
        AppendCode(program.m_byteCode, sourceAt );
        AppendCode(program.m_byteCode, (uint16_t)removes.size() );
        for ( const auto& b: removes )
        {
            OP::ADDRESS condition = b.first ? b.first->linkedAddress : 0;
            AppendCode(program.m_byteCode, condition );

            OP::ADDRESS remove = b.second ? b.second->linkedAddress : 0;
            AppendCode(program.m_byteCode, remove );
        }
    }
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
namespace
{
    class Sink_MeshRemoveMaskAST
    {
    public:

        // \TODO This is recursive and may cause stack overflows in big models.

		mu::Ptr<ASTOp> Apply( const ASTOpMeshRemoveMask* root )
        {
            m_root = root;
            m_oldToNew.clear();

            m_initialSource = root->source.child();
			mu::Ptr<ASTOp> newSource = Visit( m_initialSource );

            // If there is any change, it is the new root.
            if (newSource!=m_initialSource)
            {
                return newSource;
            }

            return nullptr;
        }

    protected:

        const ASTOpMeshRemoveMask* m_root;
		mu::Ptr<ASTOp> m_initialSource;
        std::unordered_map<mu::Ptr<ASTOp>, mu::Ptr<ASTOp>> m_oldToNew;
        vector<mu::Ptr<ASTOp>> m_newOps;

		mu::Ptr<ASTOp> Visit( const mu::Ptr<ASTOp>& at )
        {
            if (!at) return nullptr;

            // Newly created?
            if (std::find(m_newOps.begin(), m_newOps.end(), at )!=m_newOps.end())
            {
                return at;
            }

            // Already visited?
            auto cacheIt = m_oldToNew.find(at);
            if (cacheIt!=m_oldToNew.end())
            {
                return cacheIt->second;
            }

			mu::Ptr<ASTOp> newAt = at;
            switch ( at->GetOpType() )
            {

            case OP_TYPE::ME_MORPH2:
            {
				mu::Ptr<ASTOpFixed> newOp = mu::Clone<ASTOpFixed>(at);
                newOp->SetChild( newOp->op.args.MeshMorph2.base, Visit( newOp->children[newOp->op.args.MeshMorph2.base].child() ) );
                newAt = newOp;
                break;
            }

                // disabled to avoid code explosion (or bug?) TODO
//            case OP_TYPE::ME_CONDITIONAL:
//            {
//                Ptr<ASTOpConditional> newOp = mu::Clone<ASTOpConditional>(at);
//                newOp->yes = Visit(newOp->yes.child());
//                newOp->no = Visit(newOp->no.child());
//                newAt = newOp;
//                break;
//            }

//            case OP_TYPE::ME_SWITCH:
//            {
//                auto newOp = mu::Clone<ASTOpSwitch>(at);
//                newOp->def = Visit(newOp->def.child());
//                for( auto& c:newOp->cases )
//                {
//                    c.branch = Visit(c.branch.child());
//                }
//                newAt = newOp;
//                break;
//            }

            default:
            {
                //
                if (at!=m_initialSource)
                {
                    auto newOp = mu::Clone<ASTOpMeshRemoveMask>(m_root);
                    newOp->source = at;
                    newAt = newOp;
                }
                break;
            }

            }

            m_oldToNew[at] = newAt;

            return newAt;
        }
    };
}


//-------------------------------------------------------------------------------------------------
mu::Ptr<ASTOp> ASTOpMeshRemoveMask::OptimiseSink(const MODEL_OPTIMIZATION_OPTIONS&, OPTIMIZE_SINK_CONTEXT&) const
{
    Sink_MeshRemoveMaskAST sinker;
    auto at = sinker.Apply(this);

    return at;
}

