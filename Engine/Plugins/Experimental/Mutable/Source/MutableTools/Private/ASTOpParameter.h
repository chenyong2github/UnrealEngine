// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//! Parameter operation.
	//---------------------------------------------------------------------------------------------
	class ASTOpParameter : public ASTOp
	{
	public:

		//! Type of parameter
		OP_TYPE type;

		//!
		PARAMETER_DESC parameter;

		//! Ranges adding dimensions to this parameter
		vector<RANGE_DATA> ranges;

		//! Additional images attached to the parameter
		vector<ASTChild> additionalImages;

	public:

		~ASTOpParameter() override;

		OP_TYPE GetOpType() const override { return type; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Assert() override;
		void Link(PROGRAM& program, const FLinkerOptions*) override;
		int EvaluateInt(ASTOpList& facts, bool& unknown) const override;
		BOOL_EVAL_RESULT EvaluateBool(ASTOpList& /*facts*/, EVALUATE_BOOL_CACHE* = nullptr) const override;
		FImageDesc GetImageDesc(bool, GetImageDescContext*) override;

	};

}

