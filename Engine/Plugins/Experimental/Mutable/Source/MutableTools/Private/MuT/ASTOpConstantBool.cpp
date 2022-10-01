// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpConstantBool.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/CodeOptimiser.h"
#include "Hash/CityHash.h"
#include "MuT/StreamsPrivate.h"


using namespace mu;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ASTOpConstantBool::ASTOpConstantBool( bool v )
{
    value = v;
}


void ASTOpConstantBool::ForEachChild(const std::function<void(ASTChild&)>& )
{
}


bool ASTOpConstantBool::IsEqual(const ASTOp& otherUntyped) const
{
    if ( auto other = dynamic_cast<const ASTOpConstantBool*>(&otherUntyped) )
    {
        return value==other->value;
    }
    return false;
}


uint64 ASTOpConstantBool::Hash() const
{
	uint64 res = std::hash<uint64>()(uint64(OP_TYPE::BO_CONSTANT));
    hash_combine( res, value );
    return res;
}


mu::Ptr<ASTOp> ASTOpConstantBool::Clone(MapChildFunc&) const
{
    Ptr<ASTOpConstantBool> n = new ASTOpConstantBool();
    n->value = value;
    return n;
}


void ASTOpConstantBool::Link( PROGRAM& program, const FLinkerOptions*)
{
    if (!linkedAddress)
    {
        OP::BoolConstantArgs args;
        memset( &args,0, sizeof(args) );
        args.value = value;

        linkedAddress = (OP::ADDRESS)program.m_opAddress.size();
//        program.m_code.push_back(op);
        program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
        AppendCode(program.m_byteCode,OP_TYPE::BO_CONSTANT);
        AppendCode(program.m_byteCode,args);
    }
}


ASTOp::BOOL_EVAL_RESULT ASTOpConstantBool::EvaluateBool( ASTOpList&, EVALUATE_BOOL_CACHE* ) const
{
    return value? BET_TRUE : BET_FALSE;
}


