// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpScalarCurve : public ASTOp
	{
	public:

		ASTChild time;

		//!
		Curve curve;

	public:

		ASTOpScalarCurve();
		ASTOpScalarCurve(const ASTOpScalarCurve&) = delete;
		~ASTOpScalarCurve() override;

		OP_TYPE GetOpType() const override { return OP_TYPE::PR_CONSTANT; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
	};


}

