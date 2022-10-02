// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"

#include <functional>


namespace mu
{
struct PROGRAM;


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	class ASTOpConstantProjector : public ASTOp
	{
	public:

		//!
		PROJECTOR value;

	public:

		OP_TYPE GetOpType() const override { return OP_TYPE::PR_CONSTANT; }
		uint64 Hash() const override;
		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		void Link(PROGRAM& program, const FLinkerOptions*) override;
	};


}

