// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpScalarCurve.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/CodeOptimiser.h"
#include "MuT/StreamsPrivate.h"


using namespace mu;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ASTOpScalarCurve::ASTOpScalarCurve()
    : time(this)
{
}


ASTOpScalarCurve::~ASTOpScalarCurve()
{
    // Explicit call needed to avoid recursive destruction
    ASTOp::RemoveChildren();
}


void ASTOpScalarCurve::ForEachChild(const std::function<void(ASTChild&)>& f)
{
    f( time );
}


uint64 ASTOpScalarCurve::Hash() const
{
	uint64 res = std::hash<uint64>()(size_t(OP_TYPE::SC_CURVE));
    hash_combine( res, curve.keyFrames.size() );
    return res;
}


bool ASTOpScalarCurve::IsEqual(const ASTOp& otherUntyped) const
{
    if ( auto other = dynamic_cast<const ASTOpScalarCurve*>(&otherUntyped) )
    {
        return time==other->time && curve==other->curve;
    }
    return false;
}


mu::Ptr<ASTOp> ASTOpScalarCurve::Clone(MapChildFunc& mapChild) const
{
    Ptr<ASTOpScalarCurve> n = new ASTOpScalarCurve();
    n->curve = curve;
    n->time = mapChild(time.child());
    return n;
}


void ASTOpScalarCurve::Link( PROGRAM& program, const FLinkerOptions* )
{
    if (!linkedAddress)
    {
        OP::ScalarCurveArgs args;
        memset( &args,0, sizeof(args) );
        args.time = time->linkedAddress;
        args.curve = program.AddConstant(curve);

        linkedAddress = (OP::ADDRESS)program.m_opAddress.size();
        //program.m_code.push_back(op);
        program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
        AppendCode(program.m_byteCode,OP_TYPE::SC_CURVE);
        AppendCode(program.m_byteCode,args);
    }

}


