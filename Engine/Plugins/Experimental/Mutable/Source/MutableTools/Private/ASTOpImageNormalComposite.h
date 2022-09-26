// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AST.h"


namespace mu
{

	class ASTOpImageNormalComposite : public ASTOp
	{
	public:
		ASTChild Base;
		ASTChild Normal;
		ECompositeImageMode Mode;
		float Power;

	public:

		ASTOpImageNormalComposite();
		ASTOpImageNormalComposite(const ASTOpImageNormalComposite&) = delete;
		~ASTOpImageNormalComposite();

		OP_TYPE GetOpType() const override { return OP_TYPE::IM_NORMALCOMPOSITE; }
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

