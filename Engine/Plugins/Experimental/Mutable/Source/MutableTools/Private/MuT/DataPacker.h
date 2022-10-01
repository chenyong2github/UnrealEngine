// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/Compiler.h"
#include "MuT/AST.h"
#include "MuR/MemoryPrivate.h"
#include "MuR/ModelPrivate.h"
#include "MuR/CodeVisitor.h"
#include "MuR/RefCounted.h"


#define MUTABLE_HASH_SEED					((uint32_t)0xcadababa)


namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Convert constant data to different formats, based on their usage
    //---------------------------------------------------------------------------------------------
    extern void DataOptimiseAST( int imageCompressionQuality, ASTOpList& roots,
                                 const MODEL_OPTIMIZATION_OPTIONS& );

    //---------------------------------------------------------------------------------------------
    //! Find the given constant in a subtree
    //---------------------------------------------------------------------------------------------
    class SubtreeSearchConstantVisitor
    {
    public:

        SubtreeSearchConstantVisitor( PROGRAM& program, OP::ADDRESS constant, OP_TYPE optype );

        bool Run( OP::ADDRESS root );


    private:

        PROGRAM& m_program;
        OP::ADDRESS m_constant;
        OP_TYPE m_opType;

        //! 0: not visited
        //! 1: visited and not found
        //! 2: visited and found
        vector<uint8_t> m_visited;
    };

}
