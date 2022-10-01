// Copyright Epic Games, Inc. All Rights Reserved.

#include "ASTOpMeshApplyPose.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/CodeOptimiser.h"
#include "MuT/StreamsPrivate.h"



using namespace mu;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ASTOpMeshApplyPose::ASTOpMeshApplyPose()
    : base(this)
    , pose(this)
{
}


ASTOpMeshApplyPose::~ASTOpMeshApplyPose()
{
    // Explicit call needed to avoid recursive destruction
    ASTOp::RemoveChildren();
}


bool ASTOpMeshApplyPose::IsEqual(const ASTOp& otherUntyped) const
{
    if ( auto other = dynamic_cast<const ASTOpMeshApplyPose*>(&otherUntyped) )
    {
        return base==other->base &&
                pose==other->pose;
    }
    return false;
}


uint64 ASTOpMeshApplyPose::Hash() const
{
	uint64 res = std::hash<void*>()(base.child().get() );
    hash_combine( res, pose.child().get() );
    return res;
}


mu::Ptr<ASTOp> ASTOpMeshApplyPose::Clone(MapChildFunc& mapChild) const
{
    Ptr<ASTOpMeshApplyPose> n = new ASTOpMeshApplyPose();
    n->base = mapChild(base.child());
    n->pose = mapChild(pose.child());
    return n;
}


void ASTOpMeshApplyPose::ForEachChild(const std::function<void(ASTChild&)>& f )
{
    f( base );
    f( pose );
}


void ASTOpMeshApplyPose::Link( PROGRAM& program, const FLinkerOptions* )
{
    // Already linked?
    if (!linkedAddress)
    {
        OP::MeshApplyPoseArgs args;
        memset( &args,0, sizeof(args) );

        if (base) args.base = base->linkedAddress;
        if (pose) args.pose = pose->linkedAddress;

        linkedAddress = (OP::ADDRESS)program.m_opAddress.size();
        //program.m_code.push_back(op);
        program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
        AppendCode(program.m_byteCode,OP_TYPE::ME_APPLYPOSE);
        AppendCode(program.m_byteCode,args);
    }

}

