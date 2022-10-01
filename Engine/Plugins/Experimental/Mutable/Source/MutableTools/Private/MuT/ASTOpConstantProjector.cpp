// Copyright Epic Games, Inc. All Rights Reserved.

#include "ASTOpConstantProjector.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/CodeOptimiser.h"
#include "MuT/StreamsPrivate.h"


namespace mu
{


	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantProjector::ForEachChild(const std::function<void(ASTChild&)>&)
	{
	}


	uint64 ASTOpConstantProjector::Hash() const
	{
		uint64 res = std::hash<float>()(value.position[0]);
		hash_combine(res, value.direction[0]);
		return res;
	}


	bool ASTOpConstantProjector::IsEqual(const ASTOp& otherUntyped) const
	{
		if (auto other = dynamic_cast<const ASTOpConstantProjector*>(&otherUntyped))
		{
			return value == other->value;
		}
		return false;
	}


	mu::Ptr<ASTOp> ASTOpConstantProjector::Clone(MapChildFunc&) const
	{
		Ptr<ASTOpConstantProjector> n = new ASTOpConstantProjector();
		n->value = value;
		return n;
	}


	void ASTOpConstantProjector::Link(PROGRAM& program, const FLinkerOptions*)
	{
		if (!linkedAddress)
		{
			OP::ResourceConstantArgs args;
			memset(&args, 0, sizeof(args));
			args.value = program.AddConstant(value);

			linkedAddress = (OP::ADDRESS)program.m_opAddress.size();
			//program.m_code.push_back(op);
			program.m_opAddress.push_back((uint32_t)program.m_byteCode.size());
			AppendCode(program.m_byteCode, OP_TYPE::PR_CONSTANT);
			AppendCode(program.m_byteCode, args);
		}
	}

}
