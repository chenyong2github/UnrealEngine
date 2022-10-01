// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpMeshTransform : public ASTOp
	{
	public:

		ASTChild source;

		mat4f matrix;

	public:

		ASTOpMeshTransform();
		ASTOpMeshTransform(const ASTOpMeshTransform&) = delete;
		~ASTOpMeshTransform();

		OP_TYPE GetOpType() const override { return OP_TYPE::ME_TRANSFORM; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;

	};


}

