// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpImageTransform : public ASTOp
	{
	public:

		ASTChild base;
		ASTChild offsetX;
		ASTChild offsetY;
		ASTChild scaleX;
		ASTChild scaleY;
		ASTChild rotation;

	public:

		ASTOpImageTransform();
		ASTOpImageTransform(const ASTOpImageTransform&) = delete;
		~ASTOpImageTransform();

		OP_TYPE GetOpType() const override { return OP_TYPE::IM_TRANSFORM; }
		uint64 Hash() const override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
		FImageDesc GetImageDesc(bool returnBestOption, GetImageDescContext* context) override;
		void GetLayoutBlockSize(int* pBlockX, int* pBlockY) override;
		Ptr<ImageSizeExpression> GetImageSizeExpression() const override;
	};

}

