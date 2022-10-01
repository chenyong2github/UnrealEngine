// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Operations.h"
#include "MuR/ModelPrivate.h"
#include "MuR/ImagePrivate.h"
#include "MuR/MeshPrivate.h"
#include "MuR/CodeVisitor.h"

#include "MuT/CompilerPrivate.h"
#include "MuT/ModelReportPrivate.h"


namespace mu
{
    //!
    class GPUSubtreeGenerator;


    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
//    class GPUTranslator : public CodeVisitor
//    {
//    public:

//        GPUTranslator( PROGRAM& program,
//                       int state,
//                       const GPU_PLATFORM_PROPS& platformOptions,
//                       const GPU_STATE_PROPS& stateOptions,
//                       ModelReportPtr pReport );

//        virtual ~GPUTranslator();

//        virtual OP::ADDRESS Visit( OP::ADDRESS at, PROGRAM& program );

//    private:

//        vector<OP::ADDRESS> m_visited;

//        int m_state;
//        //const GPU_PLATFORM_PROPS& m_platformOptions;
//        //const GPU_STATE_PROPS& m_stateOptions;
//        REPORT_STATE::LOD m_currentLOD;
//        REPORT_STATE::COMPONENT m_currentComponent;

//        //!
//        ModelReportPtr m_pReport;

//        //!
//        GPUSubtreeGenerator* m_pGPUSubtreeGen;

//    };



}
