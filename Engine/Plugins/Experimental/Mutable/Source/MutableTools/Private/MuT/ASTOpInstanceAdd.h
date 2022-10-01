// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/AST.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	//! Operations to add elements to an instance
	//---------------------------------------------------------------------------------------------
	class ASTOpInstanceAdd : public ASTOp
	{
	public:

		//! Type of switch
		OP_TYPE type;

		ASTChild instance;
		ASTChild value;
		uint32_t id = 0;
		uint32_t externalId = 0;
		string name;

	public:

		ASTOpInstanceAdd();
		ASTOpInstanceAdd(const ASTOpInstanceAdd&) = delete;
		~ASTOpInstanceAdd();

		OP_TYPE GetOpType() const override { return type; }

		void ForEachChild(const std::function<void(ASTChild&)>&) override;
		bool IsEqual(const ASTOp& otherUntyped) const override;
		Ptr<ASTOp> Clone(MapChildFunc& mapChild) const override;
		uint64 Hash() const override;
		void Assert() override;
		void Link(PROGRAM& program, const FLinkerOptions* Options) override;
	};


}

