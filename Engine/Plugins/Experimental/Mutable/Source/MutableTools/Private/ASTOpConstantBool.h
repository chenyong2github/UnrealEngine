// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpConstantBool : public ASTOp
	{
	public:

		//!
		bool value;

	public:

		ASTOpConstantBool(bool value = true);

		OP_TYPE GetOpType() const override { return OP_TYPE::BO_CONSTANT; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
		BOOL_EVAL_RESULT EvaluateBool(ASTOpList& facts, EVALUATE_BOOL_CACHE* cache) const override;
	};


}

