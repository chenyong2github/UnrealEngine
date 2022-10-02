// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Image.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"

#include <functional>


namespace mu
{
struct PROGRAM;


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpImageMultiLayer : public ASTOp
	{
	public:

		ASTChild base;
		ASTChild blend;
		ASTChild mask;
		RANGE_DATA range;
		EBlendType blendType;

	public:

		ASTOpImageMultiLayer();
		ASTOpImageMultiLayer(const ASTOpImageMultiLayer&) = delete;
		~ASTOpImageMultiLayer();

		OP_TYPE GetOpType() const override { return OP_TYPE::IM_MULTILAYER; }
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

