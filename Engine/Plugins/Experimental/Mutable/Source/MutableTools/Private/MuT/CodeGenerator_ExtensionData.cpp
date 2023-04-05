// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Misc/AssertionMacros.h"
#include "MuR/Layout.h"
#include "MuR/MutableMath.h"
#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/System.h"
#include "MuT/AST.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpConstantExtensionData.h"
#include "MuT/CodeGenerator.h"
#include "MuT/CompilerPrivate.h"
#include "MuT/ErrorLog.h"
#include "MuT/ErrorLogPrivate.h"
#include "MuT/NodeExtensionData.h"
#include "MuT/NodeExtensionDataConstant.h"
#include "MuT/NodeExtensionDataConstantPrivate.h"


namespace mu
{
class Node;

	//---------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateExtensionData(FExtensionDataGenerationResult& OutResult, const NodeExtensionDataPtrConst& InUntypedNode)
	{
		if (!InUntypedNode)
		{
			OutResult = FExtensionDataGenerationResult();
			return;
		}

		// Clear bottom-up state
		m_currentBottomUpState.m_address = nullptr;

		// See if it was already generated
		const FGeneratedExtensionDataCacheKey Key = InUntypedNode.get();
		GeneratedExtensionDataMap::ValueType* CachedResult = m_generatedExtensionData.Find(Key);
		if (CachedResult)
		{
			OutResult = *CachedResult;
			return;
		}

		const NodeExtensionData* Node = InUntypedNode.get();

		// Generate for each different type of node
		switch (Node->GetExtensionDataNodeType())
		{
			case NodeExtensionData::EType::Constant: GenerateExtensionData_Constant(OutResult, static_cast<const NodeExtensionDataConstant*>(Node)); break;
			case NodeExtensionData::EType::None: check(false);
		}

		// Cache the result
		m_generatedExtensionData.Add(Key, OutResult);
	}

	//---------------------------------------------------------------------------------------------
	void CodeGenerator::GenerateExtensionData_Constant(FExtensionDataGenerationResult& OutResult, const NodeExtensionDataConstant* Constant)
	{
		NodeExtensionDataConstant::Private& Node = *Constant->GetPrivate();

		Ptr<ASTOpConstantExtensionData> Op = new ASTOpConstantExtensionData();
		OutResult.Op = Op;

		ExtensionDataPtrConst Data = Node.Value;
		if (!Data)
		{
			// Data can't be null, so make an empty one
			Data = new ExtensionData();
			
			// Log an error message
			m_pErrorLog->GetPrivate()->Add("Constant extension data not set", ELMT_WARNING, Node.m_errorContext);
		}

		Op->Value = Data;
	}

}

