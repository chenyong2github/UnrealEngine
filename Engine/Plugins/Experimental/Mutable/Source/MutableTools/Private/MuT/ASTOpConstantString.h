// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpConstantString : public ASTOp
	{
	public:
		//!
		string value;

	public:
		OP_TYPE GetOpType() const override { return OP_TYPE::ST_CONSTANT; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
	};


}

