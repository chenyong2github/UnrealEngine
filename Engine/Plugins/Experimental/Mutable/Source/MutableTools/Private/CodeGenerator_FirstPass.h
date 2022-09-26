// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Visitor.h"

#include "AST.h"
#include "CompilerPrivate.h"
#include "NodeLayoutPrivate.h"
#include "ErrorLogPrivate.h"
#include "NodeObjectNewPrivate.h"

#include "Table.h"

#include "NodeBool.h"

#include "NodeComponent.h"
#include "NodeComponentNew.h"
#include "NodeComponentEdit.h"

#include "NodeModifier.h"
#include "NodeModifierMeshClipMorphPlane.h"
#include "NodeModifierMeshClipWithMesh.h"
#include "NodeModifierMeshClipDeform.h"

#include "NodeSurface.h"
#include "NodeSurfaceNew.h"
#include "NodeSurfaceEdit.h"
#include "NodeSurfaceVariation.h"

#include "NodePatchImage.h"
#include "NodeLOD.h"
#include "NodeObjectNew.h"
#include "NodeObjectState.h"
#include "NodeObjectGroup.h"
#include "NodePatchMesh.h"

#include "Operations.h"
#include "ModelPrivate.h"
#include "ImagePrivate.h"

#include <memory>

namespace mu
{


    //! Store the results of the code generation of a mesh.
    struct MESH_GENERATION_RESULT
    {
        //! Mesh after all code tree is applied
        Ptr<ASTOp> meshOp;

        //! Original base mesh before removes, morphs, etc.
        Ptr<ASTOp> baseMeshOp;

        vector<Ptr<const Layout>> layouts;

        vector<Ptr<ASTOp>> layoutOps;


        struct EXTRA_LAYOUTS
        {
            vector<Ptr<const Layout>> layouts;
            Ptr<ASTOp> condition;
            Ptr<ASTOp> meshFragment;
        };
        vector< EXTRA_LAYOUTS > extraMeshLayouts;

    };


	//---------------------------------------------------------------------------------------------
	//! First pass of the code generation process.
	//! It collects data about the object hierarchy, the conditions for each object and the global
	//! modifiers.
	//---------------------------------------------------------------------------------------------
	class FirstPassGenerator :
		public Base,
		public BaseVisitor,

        public Visitor<NodeSurfaceNew::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeSurfaceEdit::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeSurfaceVariation::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeComponentNew::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeComponentEdit::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeLOD::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeObjectNew::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeObjectGroup::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeObjectState::Private, Ptr<ASTOp>, true>,
        public Visitor<NodePatchMesh::Private, Ptr<ASTOp>, true>,

        public Visitor<NodeModifierMeshClipMorphPlane::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeModifierMeshClipWithMesh::Private, Ptr<ASTOp>, true>,
        public Visitor<NodeModifierMeshClipDeform::Private, Ptr<ASTOp>, true>
	{
	public:

		FirstPassGenerator();

        void Generate(ErrorLogPtr pErrorLog,
                      const Node::Private* root,
                      bool ignoreStates);

	public:

		// Results
		//-------------------------

		//! Store the conditions that will enable or disable every object
		struct OBJECT
		{
			const NodeObjectNew::Private* node;
            Ptr<ASTOp> condition;
		};
		vector<OBJECT> objects;

        //! Type used to represent the activation conditions regarding states
        //! This is the state mask for the states in which this surface must be added. If it
        //! is empty it means the surface is valid for all states. Otherwise it is only valid
        //! for the states whose index is true.
        using StateCondition = std::vector<uint8_t>;

		//! Store information about every surface including
		//! - the component it may be added to
		//! - the conditions that will enable or disable it
		//! - all edit operators
        //! A surface may have different versions depending on the different parents and conditions
        //! it is reached with.
		struct SURFACE
		{
            NodeSurfaceNewPtrConst node;

			// Parent component where this surface will be added. It may be different from the 
			// component that defined it (if it was an edit component).
            const NodeComponentNew::Private* component = nullptr;

            // List of tags that are required for the presence of this surface
            vector<string> positiveTags;

            // List of tags that block the presence of this surface
            vector<string> negativeTags;

			// This conditions is the condition of the object defining this surface which may not
			// be the parent object where this surface will be added.
            Ptr<ASTOp> objectCondition;

            // This is filled in the first pass.
            StateCondition stateCondition;

            // Condition for this surface to be enabled when all the object conditions are met.
            // This is filled in CodeGenerator_SecondPass.
            Ptr<ASTOp> surfaceCondition;

			// All surface editing nodes that edit this surface
            struct EDIT
            {
                //! Condition that enables the effects of this edit node on the surface
                Ptr<ASTOp> condition;

                //! Weak reference to the edit node, used during compilation.
                const NodeSurfaceEdit::Private* node = nullptr;
            };
            vector<EDIT> edits;

            // This is filled in the final code generation pass
            Ptr<ASTOp> resultSurfaceOp;
            Ptr<ASTOp> resultMeshOp;
        };
		vector<SURFACE> surfaces;

		//! Store the conditions that enable every modifier.
		struct MODIFIER
		{
            const NodeModifier::Private* node = nullptr;

            // List of tags that are required for the presence of this surface
            vector<string> positiveTags;

            // List of tags that block the presence of this surface
            vector<string> negativeTags;

            // This conditions is the condition of the object defining this modifier which may not
            // be the parent object where this surface will be added.
            Ptr<ASTOp> objectCondition;

            // This conditions is the condition for this modifier to be enabled when all the object
            // conditions are met.
            // This is filled in CodeGenerator_SecondPass.
            Ptr<ASTOp> surfaceCondition;

            // This is filled in CodeGenerator_SecondPass.
            StateCondition stateCondition;

            //
            int lod = 0;
        };
		vector<MODIFIER> modifiers;

		//! Info about all found tags.
		struct TAG
		{
			string tag;

            // Surfaces that activate the tag. These are indices to the FirstPassGenerator::surfaces
            // vector.
            vector<int> surfaces;

            // Edit Surfaces that activate the tag. These first element of the pair are indices to
            // the FirstPassGenerator::surfaces vector. The second element are indices to the
            // "edits" in the specific surface.
            vector<std::pair<int,int>> edits;

            // This conditions is the condition for this tag to be enabled considering no other
            // condition. This is filled in CodeGenerator_SecondPass.
            Ptr<ASTOp> genericCondition;
        };
        vector<TAG> m_tags;

        //! Accumulate the model states found while generating code, with their generated root
        //! nodes.
        typedef vector< std::pair<OBJECT_STATE, const Node::Private*> > StateList;
        StateList m_states;

	public:

        Ptr<ASTOp> Visit(const NodeSurfaceNew::Private&) override;
        Ptr<ASTOp> Visit(const NodeSurfaceEdit::Private&) override;
        Ptr<ASTOp> Visit(const NodeSurfaceVariation::Private&) override;
        Ptr<ASTOp> Visit(const NodeComponentNew::Private&) override;
        Ptr<ASTOp> Visit(const NodeComponentEdit::Private&) override;
        Ptr<ASTOp> Visit(const NodeLOD::Private&) override;
        Ptr<ASTOp> Visit(const NodeObjectNew::Private& node) override;
        Ptr<ASTOp> Visit(const NodeObjectGroup::Private&) override;
        Ptr<ASTOp> Visit(const NodeObjectState::Private&) override;
        Ptr<ASTOp> Visit(const NodeModifierMeshClipMorphPlane::Private&) override;
        Ptr<ASTOp> Visit(const NodeModifierMeshClipWithMesh::Private&) override;
        Ptr<ASTOp> Visit(const NodeModifierMeshClipDeform::Private&) override;
        Ptr<ASTOp> Visit(const NodePatchMesh::Private&) override;

	private:

        struct CONDITION_CONTEXT
        {
            Ptr<ASTOp> objectCondition;
        };
        vector< CONDITION_CONTEXT > m_currentCondition;

        //!
        vector< StateCondition > m_currentStateCondition;

		//! When processing surfaces, this is the parent component the surfaces may be added to
        const NodeComponentNew::Private* m_currentComponent = nullptr;

        //! Current relevant tags so far. Used during traversal.
        vector<string> m_currentPositiveTags;
        vector<string> m_currentNegativeTags;

		//! Index of the LOD we are processing
        int m_currentLOD = -1;

        //!
        ErrorLogPtr m_pErrorLog;

        //!
        bool m_ignoreStates = false;
	};

}
