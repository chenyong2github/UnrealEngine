// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//! Variable sized mesh block extract operation.
	//---------------------------------------------------------------------------------------------
	class ASTOpMeshExtractLayoutBlocks : public ASTOp
	{
	public:

		//! Source mesh to extract block from.
		ASTChild source;

		//! Layout to use to select the blocks.
		uint16_t layout = 0;

		//! Blocks
		vector<uint32_t> blocks;

	public:

		ASTOpMeshExtractLayoutBlocks();
		ASTOpMeshExtractLayoutBlocks(const ASTOpMeshExtractLayoutBlocks&) = delete;
		~ASTOpMeshExtractLayoutBlocks() override;

		// ASTOp interface
		OP_TYPE GetOpType() const override { return OP_TYPE::ME_EXTRACTLAYOUTBLOCK; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Assert() override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;

	};


}

