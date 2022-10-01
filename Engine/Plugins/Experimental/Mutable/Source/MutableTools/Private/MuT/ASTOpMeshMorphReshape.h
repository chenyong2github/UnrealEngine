// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/AST.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpMeshMorphReshape : public ASTOp
	{
	public:

		ASTChild Morph;
		ASTChild Reshape;

	public:

		ASTOpMeshMorphReshape();
		ASTOpMeshMorphReshape(const ASTOpMeshMorphReshape&) = delete;
		~ASTOpMeshMorphReshape();

		OP_TYPE GetOpType() const override { return OP_TYPE::ME_MORPHRESHAPE; }
		uint64 Hash() const override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
	};


}

