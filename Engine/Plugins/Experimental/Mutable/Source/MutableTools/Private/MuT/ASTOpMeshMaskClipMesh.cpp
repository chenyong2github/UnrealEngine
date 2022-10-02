// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshMaskClipMesh.h"

#include "HAL/PlatformMath.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpMeshRemoveMask.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/StreamsPrivate.h"

#include <memory>
#include <utility>


using namespace mu;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ASTOpMeshMaskClipMesh::ASTOpMeshMaskClipMesh()
    : source(this)
    , clip(this)
{
}


//-------------------------------------------------------------------------------------------------
ASTOpMeshMaskClipMesh::~ASTOpMeshMaskClipMesh()
{
    // Explicit call needed to avoid recursive destruction
    ASTOp::RemoveChildren();
}


//-------------------------------------------------------------------------------------------------
bool ASTOpMeshMaskClipMesh::IsEqual(const ASTOp& otherUntyped) const
{
    if ( auto other = dynamic_cast<const ASTOpMeshMaskClipMesh*>(&otherUntyped) )
    {
        return source==other->source && clip==other->clip;
    }
    return false;
}


//-------------------------------------------------------------------------------------------------
uint64 ASTOpMeshMaskClipMesh::Hash() const
{
	uint64 res = std::hash<void*>()(source.child().get());
    hash_combine( res, clip.child() );
    return res;
}


//-------------------------------------------------------------------------------------------------
mu::Ptr<ASTOp> ASTOpMeshMaskClipMesh::Clone(MapChildFunc& mapChild) const
{
    Ptr<ASTOpMeshMaskClipMesh> n = new ASTOpMeshMaskClipMesh();
    n->source = mapChild(source.child());
    n->clip = mapChild(clip.child());
    return n;
}


//-------------------------------------------------------------------------------------------------
void ASTOpMeshMaskClipMesh::ForEachChild(const std::function<void(ASTChild&)>& f )
{
    f( source );
    f( clip );
}


//-------------------------------------------------------------------------------------------------
void ASTOpMeshMaskClipMesh::Link( PROGRAM& program, const FLinkerOptions* )
{
    // Already linked?
    if (!linkedAddress)
    {
        OP::MeshMaskClipMeshArgs args;
        memset( &args,0, sizeof(args) );

        if (source) args.source = source->linkedAddress;
        if (clip) args.clip = clip->linkedAddress;

        linkedAddress = (OP::ADDRESS)program.m_opAddress.size();
        //program.m_code.push_back(op);
        program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
        AppendCode(program.m_byteCode,OP_TYPE::ME_MASKCLIPMESH);
        AppendCode(program.m_byteCode,args);
    }
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
namespace
{
    class Sink_MeshMaskClipMeshAST
    {
    public:

        // \TODO This is recursive and may cause stack overflows in big models.

        mu::Ptr<ASTOp> Apply( const ASTOpMeshMaskClipMesh* root )
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

        const ASTOpMeshMaskClipMesh* m_root;
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

			// This cannot be sunk since the result is different. Since the clipping is now correctly
			// generated at the end of the chain when really necessary, this wrong optimisation is no 
			// longer needed.
			//case OP_TYPE::ME_MORPH2:
   //         {
   //             // Remove this op: Let's clip the unmorphed mesh only
   //             auto typedAt = dynamic_cast<const ASTOpFixed*>(at.get());
   //             newAt = Visit( typedAt->children[typedAt->op.args.MeshMorph2.base].child() );
   //             break;
   //         }

            case OP_TYPE::ME_REMOVEMASK:
            {
                // Remove this op:
                // This may lead to the mask being bigger than needed since it will include
                // faces removed by the ignored removemask, but it is ok

                // TODO: Swap instead of ignore, and implement removemask on a mask?
                auto typedAt = dynamic_cast<const ASTOpMeshRemoveMask*>(at.get());
                newAt = Visit( typedAt->source.child() );
                break;
            }

            case OP_TYPE::ME_CONDITIONAL:
            {
                // We move the mask creation down the two paths
                // It always needs to be a clone because otherwise we could be modifying an
                // instruction that shouldn't if the parent was a ME_REMOVEMASK above and we
                // skipped the cloning for the parent.
                auto newOp = mu::Clone<ASTOpConditional>(at);
                newOp->yes = Visit(newOp->yes.child());
                newOp->no = Visit(newOp->no.child());
                newAt = newOp;
                break;
            }

            case OP_TYPE::ME_SWITCH:
            {
                // We move the mask creation down all the paths
                auto newOp = mu::Clone<ASTOpSwitch>(at);
                newOp->def = Visit(newOp->def.child());
                for( auto& c:newOp->cases )
                {
                    c.branch = Visit(c.branch.child());
                }
                newAt = newOp;
                break;
            }

			// This cannot be sunk since the result is different. Since the clipping is now correctly
			// generated at the end of the chain when really necessary, this wrong optimisation is no 
			// longer needed.
			//case OP_TYPE::ME_CLIPMORPHPLANE:
   //         {
   //             // We move the mask creation down the source
   //             auto typedAt = dynamic_cast<const ASTOpMeshClipMorphPlane*>(at.get());
   //             newAt = Visit(typedAt->source.child());
   //             break;
   //         }

            default:
            {
                //
                if (at!=m_initialSource)
                {
                    auto newOp = mu::Clone<ASTOpMeshMaskClipMesh>(m_root);
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
mu::Ptr<ASTOp> ASTOpMeshMaskClipMesh::OptimiseSink(const MODEL_OPTIMIZATION_OPTIONS&, OPTIMIZE_SINK_CONTEXT&) const
{
    Sink_MeshMaskClipMeshAST sinker;
    auto at = sinker.Apply(this);

    return at;
}
