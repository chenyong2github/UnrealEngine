// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableTools/Private/Visitor.h"

#include "MutableTools/Private/CodeGenerator_FirstPass.h"
#include "MutableTools/Private/CompilerPrivate.h"
#include "MutableTools/Private/NodeLayoutPrivate.h"
#include "MutableTools/Private/ErrorLogPrivate.h"
#include "MutableTools/Private/NodeObjectNewPrivate.h"
#include "MutableTools/Private/TaskManager.h"

#include "MutableTools/Private/TablePrivate.h"

#include "MutableTools/Public/NodeBool.h"

#include "MutableTools/Public/NodeScalar.h"
#include "MutableTools/Public/NodeScalarConstant.h"
#include "MutableTools/Public/NodeScalarParameter.h"
#include "MutableTools/Public/NodeScalarSwitch.h"
#include "MutableTools/Public/NodeScalarEnumParameter.h"
#include "MutableTools/Public/NodeScalarCurve.h"
#include "MutableTools/Public/NodeScalarArithmeticOperation.h"
#include "MutableTools/Public/NodeScalarVariation.h"
#include "MutableTools/Public/NodeRangeFromScalar.h"
#include "MutableTools/Public/NodeScalarTable.h"

#include "MutableTools/Public/NodeString.h"
#include "MutableTools/Public/NodeStringConstant.h"
#include "MutableTools/Public/NodeStringParameter.h"

#include "MutableTools/Public/NodeColour.h"
#include "MutableTools/Public/NodeColourConstant.h"
#include "MutableTools/Public/NodeColourParameter.h"
#include "MutableTools/Public/NodeColourSwitch.h"
#include "MutableTools/Public/NodeColourSampleImage.h"
#include "MutableTools/Public/NodeColourFromScalars.h"
#include "MutableTools/Public/NodeColourTable.h"
#include "MutableTools/Public/NodeColourArithmeticOperation.h"
#include "MutableTools/Public/NodeColourVariation.h"

#include "MutableTools/Public/NodeProjector.h"

#include "MutableTools/Public/NodeImage.h"
#include "MutableTools/Public/NodeImageConstant.h"
#include "MutableTools/Public/NodeImageParameter.h"
#include "MutableTools/Public/NodeImageDifference.h"
#include "MutableTools/Public/NodeImageInterpolate.h"
#include "MutableTools/Public/NodeImageInterpolate3.h"
#include "MutableTools/Public/NodeImageSaturate.h"
#include "MutableTools/Public/NodeImageLuminance.h"
#include "MutableTools/Public/NodeImageLayer.h"
#include "MutableTools/Public/NodeImageLayerColour.h"
#include "MutableTools/Public/NodeImageMultiLayer.h"
#include "MutableTools/Public/NodeImageTable.h"
#include "MutableTools/Public/NodeImageSwizzle.h"
#include "MutableTools/Public/NodeImageFormat.h"
#include "MutableTools/Public/NodeImageSelectColour.h"
#include "MutableTools/Public/NodeImageColourMap.h"
#include "MutableTools/Public/NodeImageGradient.h"
#include "MutableTools/Public/NodeImageBinarise.h"
#include "MutableTools/Public/NodeImageResize.h"
#include "MutableTools/Public/NodeImagePlainColour.h"
#include "MutableTools/Public/NodeImageSwitch.h"
#include "MutableTools/Public/NodeImageProject.h"
#include "MutableTools/Public/NodeImageMipmap.h"
#include "MutableTools/Public/NodeImageConditional.h"
#include "MutableTools/Public/NodeImageInvert.h"
#include "MutableTools/Public/NodeImageVariation.h"
#include "MutableTools/Public/NodeImageNormalComposite.h"
#include "MutableTools/Public/NodeImageTransform.h"

#include "MutableTools/Public/NodeMesh.h"
#include "MutableTools/Public/NodeMeshConstant.h"
#include "MutableTools/Public/NodeMeshSwitch.h"
#include "MutableTools/Public/NodeMeshTable.h"
#include "MutableTools/Public/NodeMeshInterpolate.h"
#include "MutableTools/Public/NodeMeshMorph.h"
#include "MutableTools/Public/NodeMeshMakeMorph.h"
#include "MutableTools/Public/NodePatchMesh.h"
#include "MutableTools/Public/NodeMeshSubtract.h"
#include "MutableTools/Public/NodeMeshFormat.h"
#include "MutableTools/Public/NodeMeshFragment.h"
#include "MutableTools/Public/NodeMeshTransform.h"
#include "MutableTools/Public/NodeMeshClipMorphPlane.h"
#include "MutableTools/Public/NodeMeshClipWithMesh.h"
#include "MutableTools/Public/NodeMeshApplyPose.h"
#include "MutableTools/Public/NodeMeshVariation.h"
#include "MutableTools/Public/NodeMeshGeometryOperation.h"
#include "MutableTools/Public/NodeMeshReshape.h"
#include "MutableTools/Public/NodeMeshClipDeform.h"

#include "MutableTools/Public/NodeComponent.h"
#include "MutableTools/Public/NodeComponentNew.h"
#include "MutableTools/Public/NodeComponentEdit.h"

#include "MutableTools/Public/NodeModifier.h"
#include "MutableTools/Public/NodeModifierMeshClipMorphPlane.h"
#include "MutableTools/Public/NodeModifierMeshClipWithMesh.h"

#include "MutableTools/Public/NodeSurface.h"
#include "MutableTools/Public/NodeSurfaceNew.h"
#include "MutableTools/Public/NodeSurfaceEdit.h"

#include "MutableTools/Public/NodePatchImage.h"
#include "MutableTools/Public/NodeLOD.h"
#include "MutableTools/Public/NodeObjectNew.h"
#include "MutableTools/Public/NodeObjectState.h"
#include "MutableTools/Public/NodeObjectGroup.h"

#include "MutableTools/Private/AST.h"
#include "MutableTools/Private/ASTOpParameter.h"
#include "MutableTools/Private/ASTOpSwitch.h"

#include "Operations.h"
#include "ModelPrivate.h"
#include "ImagePrivate.h"

#include "Containers/Map.h"

#include <shared_mutex>


namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Code generator
    //---------------------------------------------------------------------------------------------
    class CodeGenerator : public Base,
                          public BaseVisitor,

                          public Visitor<NodeComponentNew::Private, Ptr<ASTOp>, true>,
                          public Visitor<NodeComponentEdit::Private, Ptr<ASTOp>, true>,
                          public Visitor<NodeLOD::Private, Ptr<ASTOp>, true>,
                          public Visitor<NodeObjectNew::Private, Ptr<ASTOp>, true>,
                          public Visitor<NodeObjectState::Private, Ptr<ASTOp>, true>,
                          public Visitor<NodeObjectGroup::Private, Ptr<ASTOp>, true>,
                          public Visitor<NodePatchImage::Private, Ptr<ASTOp>, true>
    {
    public:

        CodeGenerator( CompilerOptions::Private* options );

        //! Data will be stored in m_states
        void GenerateRoot( const NodePtrConst pNode, TaskManager* );

	protected:

        Ptr<ASTOp> Generate(const NodePtrConst pNode);

	public:

        Ptr<ASTOp> Visit( const NodeComponentNew::Private& ) override;
        Ptr<ASTOp> Visit( const NodeComponentEdit::Private& ) override;
        Ptr<ASTOp> Visit( const NodeLOD::Private& ) override;
        Ptr<ASTOp> Visit( const NodeObjectNew::Private& ) override;
        Ptr<ASTOp> Visit( const NodeObjectState::Private& ) override;
        Ptr<ASTOp> Visit( const NodeObjectGroup::Private& ) override;
        Ptr<ASTOp> Visit( const NodePatchImage::Private& ) override;

    public:

        //! Settings
        CompilerOptions::Private* m_compilerOptions = nullptr;

		//!
		FirstPassGenerator m_firstPass;


        struct VISITED_MAP_KEY
        {
            VISITED_MAP_KEY()
            {
            }

			friend FORCEINLINE uint32 GetTypeHash(const VISITED_MAP_KEY& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.pNode.get()));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.imageSize[0]));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.imageSize[1]));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.imageRect.min[0]));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.imageRect.min[1]));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.imageRect.size[0]));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.imageRect.size[1]));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash(InKey.state));
				KeyHash = HashCombine(KeyHash, ::GetTypeHash((uint64)InKey.activeTags.size()));
				return KeyHash;
			}

			bool operator==(const VISITED_MAP_KEY& InKey) const
			{
				if (pNode != InKey.pNode) return false;
				if (state != InKey.state) return false;
				if (imageSize != InKey.imageSize) return false;
				if (imageRect.min != InKey.imageRect.min) return false;
				if (imageRect.size != InKey.imageRect.size) return false;
				if (activeTags != InKey.activeTags) return false;
				if (overrideLayouts != InKey.overrideLayouts) return false;
				return true;
			}

            // This reference has to be the smart pointer to avoid memory aliasing, keeping
            // processed nodes alive.
            NodePtrConst pNode;
            vec2<int> imageSize;
            box< vec2<int> > imageRect;
            int state = -1;
			vector<mu::string> activeTags;
			vector<LayoutPtrConst> overrideLayouts;
        };

        //! This struct contains additional state propagated from bottom to top of the object node graph.
        //! It is stored for every visited node, and restored when the cache is used.
        struct BOTTOM_UP_STATE
        {
            //! Generated root address for the node.
            Ptr<ASTOp> m_address;
        };
        BOTTOM_UP_STATE m_currentBottomUpState;

        typedef TMap<VISITED_MAP_KEY,BOTTOM_UP_STATE> VisitedMap;
        VisitedMap m_compiled;

        //!
        ErrorLogPtr m_pErrorLog;

        //! While generating code, this contains the index of the state being generated. This
        //! can only be used with the state data in m_firstPass.
        int m_currentStateIndex = -1;

        //! After the entire code generation this contains the information about all the states
        typedef vector< std::pair<OBJECT_STATE, Ptr<ASTOp>> > StateList;
        StateList m_states;

    private:

        //! List of meshes generated to be able to reuse them
        vector<MeshPtr> m_constantMeshes;

        //! List of image resources for every image formata that have been generated so far as
        //! palceholders for missing images.
        ImagePtr m_missingImage[size_t(EImageFormat::IF_COUNT)];

        //! If this has something, while generating meshes, the layouts will be ignored, because
        //! they are supposed to match some other set of layouts. If the vector is empty, layouts
        //! are generated normally.
        vector< vector<Ptr<const Layout>> > m_overrideLayoutsStack;

        //! Map of layouts found in the code already generated. The map is from the source layout
        //! pointer to the cloned layout. The cloned layout will have absolute block ids assigned.
        map<Ptr<const Layout>,Ptr<const Layout>> m_addedLayouts;

        //! First free index for a layout block
        int32_t m_absoluteLayoutIndex = 0;

        //! First free index to be used to identify mesh vertices.
        uint32_t m_freeVertexIndex = 0;

        //! First free index to be used to identify mesh masks.
        uint32_t m_freeMaskIndex = 0;

        //! When generating images, here we have the entire source image size and the rect of the
        //! image that we are generating.
        struct IMAGE_STATE
        {
            vec2<int> m_imageSize;
            box< vec2<int> > m_imageRect;
            int32_t m_layoutBlock;
            LayoutPtrConst m_pLayout;
        };
        vector<IMAGE_STATE> m_imageState;

		// (top-down) Tags that are active when generating nodes.
		vector< vector<mu::string> > m_activeTags;

        struct PARENT_KEY
        {
            PARENT_KEY()
            {
                m_pObject = nullptr;
                m_state = -1;
                m_lod = -1;
                m_component = -1;
                m_surface = -1;
                m_texture = -1;
                m_block = -1;
            }

            const NodeObjectNew::Private* m_pObject;
            int m_state;
            int m_lod;
            int m_component;
            int m_surface;
            int m_texture;
            int m_block;
        };

        vector< PARENT_KEY > m_currentParents;

        // List of additional components to add to an object that come from child objects.
        // The index is the object and lod that should receive the components.
        struct ADDITIONAL_COMPONENT_KEY
        {
            ADDITIONAL_COMPONENT_KEY()
            {
                m_pObject = nullptr;
                m_lod = -1;
            }

            const NodeObjectNew::Private* m_pObject;
            int m_lod;

            inline bool operator<(const ADDITIONAL_COMPONENT_KEY& o) const
            {
                if (m_pObject < o.m_pObject) return true;
                if (m_pObject > o.m_pObject) return false;
                return m_lod < o.m_lod;
            }
        };
        std::map< ADDITIONAL_COMPONENT_KEY, vector<Ptr<ASTOp>> > m_additionalComponents;


        struct OBJECT_GENERATION_DATA
        {
            // Condition that enables a specific object
            Ptr<ASTOp> m_condition;
        };
        vector< OBJECT_GENERATION_DATA > m_currentObject;

        map< std::pair<TablePtr,string>, std::pair<TablePtr,Ptr<ASTOp>> > m_generatedTables;

        //! Variables added for every node
        map< Ptr<const Node>, Ptr<ASTOpParameter> > m_nodeVariables;

        //! This is a list of nodes that are set to replace the identity nodes found while
        //! generating code. This is useful when applying object transforms.
        typedef map< NodePtr, Ptr<ASTOp> > IdentityMap;
        vector<IdentityMap> m_identityReplacement;

		//-----------------------------------------------------------------------------------------

		// Get the modifiers that have to be applied to elements with a specific tag.
		void GetModifiersFor(const vector<string>& tags, int LOD,
			bool bModifiersForBeforeOperations, vector<FirstPassGenerator::MODIFIER>& modifiers);

		// Apply the required mesh modifiers to the given operation.
		Ptr<ASTOp> ApplyMeshModifiers( const Ptr<ASTOp>& sourceOp, const vector<string>& tags,
			bool bModifiersForBeforeOperations, const void* errorContext);

		// Get the modifiers that have to be applied to elements with a specific tag.
        //void GetSurfacesWithTag(const string& tag, vector<FirstPassGenerator::SURFACE>& surfaces);

        //-----------------------------------------------------------------------------------------
        void PrepareForLayout( LayoutPtrConst pSourceLayout,
                                  MeshPtr currentLayoutMesh,
                                  size_t currentLayoutChannel,
                                  const void* errorContext );

        //-----------------------------------------------------------------------------------------
        //!
        Ptr<ASTOp> GenerateTableVariable( TablePtr pTable, const string& strName );

        //!
        Ptr<ASTOp> GenerateMissingBoolCode( const char* strWhere, bool value,
                                         const void* errorContext );

        //!
		template<class NODE_TABLE_PRIVATE, TABLE_COLUMN_TYPE TYPE, OP_TYPE OPTYPE, typename F>
		Ptr<ASTOp> GenerateTableSwitch( const NODE_TABLE_PRIVATE& node, F&& GenerateOption );

        //!
        Ptr<ASTOp> GenerateImageBlockPatch( Ptr<ASTOp> blockAd,
                                             const NodePatchImage* pPatch,
                                             Ptr<ASTOp> conditionAd );


    private:

        std::shared_timed_mutex m_codeAccessMutex;

		//! Generate the key with all the relevant state that is used in generation of operations for a node.
		VISITED_MAP_KEY GetCurrentCacheKey(const NodePtrConst& InNode) const
		{
			VISITED_MAP_KEY key;
			key.pNode = InNode;
			key.state = m_currentStateIndex;
			if (!m_imageState.empty())
			{
				key.imageSize = m_imageState.back().m_imageSize;
				key.imageRect = m_imageState.back().m_imageRect;
			}
			if (!m_activeTags.empty())
			{
				key.activeTags = m_activeTags.back();
				if (!m_overrideLayoutsStack.empty())
				{
					key.overrideLayouts = m_overrideLayoutsStack.back();
				}
			}
			return key;
		}


		//-----------------------------------------------------------------------------------------
		// Images
		struct IMAGE_GENERATION_RESULT
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<VISITED_MAP_KEY, IMAGE_GENERATION_RESULT> GeneratedImagesMap;
		GeneratedImagesMap m_generatedImages;

		void GenerateImage(IMAGE_GENERATION_RESULT& result, const NodeImagePtrConst& node);
		void GenerateImage_Constant(IMAGE_GENERATION_RESULT&, const NodeImageConstant*);
		void GenerateImage_Difference(IMAGE_GENERATION_RESULT&, const NodeImageDifference*);
		void GenerateImage_Interpolate(IMAGE_GENERATION_RESULT&, const NodeImageInterpolate*);
		void GenerateImage_Saturate(IMAGE_GENERATION_RESULT&, const NodeImageSaturate*);
		void GenerateImage_Table(IMAGE_GENERATION_RESULT&, const NodeImageTable*);
		void GenerateImage_Swizzle(IMAGE_GENERATION_RESULT&, const NodeImageSwizzle*);
		void GenerateImage_SelectColour(IMAGE_GENERATION_RESULT&, const NodeImageSelectColour*);
		void GenerateImage_ColourMap(IMAGE_GENERATION_RESULT&, const NodeImageColourMap*);
		void GenerateImage_Gradient(IMAGE_GENERATION_RESULT&, const NodeImageGradient*);
		void GenerateImage_Binarise(IMAGE_GENERATION_RESULT&, const NodeImageBinarise*);
		void GenerateImage_Luminance(IMAGE_GENERATION_RESULT&, const NodeImageLuminance*);
		void GenerateImage_Layer(IMAGE_GENERATION_RESULT&, const NodeImageLayer*);
		void GenerateImage_LayerColour(IMAGE_GENERATION_RESULT&, const NodeImageLayerColour*);
		void GenerateImage_Resize(IMAGE_GENERATION_RESULT&, const NodeImageResize*);
		void GenerateImage_PlainColour(IMAGE_GENERATION_RESULT&, const NodeImagePlainColour*);
		void GenerateImage_Interpolate3(IMAGE_GENERATION_RESULT&, const NodeImageInterpolate3*);
		void GenerateImage_Project(IMAGE_GENERATION_RESULT&, const NodeImageProject*);
		void GenerateImage_Mipmap(IMAGE_GENERATION_RESULT&, const NodeImageMipmap*);
		void GenerateImage_Switch(IMAGE_GENERATION_RESULT&, const NodeImageSwitch*);
		void GenerateImage_Conditional(IMAGE_GENERATION_RESULT&, const NodeImageConditional*);
		void GenerateImage_Format(IMAGE_GENERATION_RESULT&, const NodeImageFormat*);
		void GenerateImage_Parameter(IMAGE_GENERATION_RESULT&, const NodeImageParameter*);
		void GenerateImage_MultiLayer(IMAGE_GENERATION_RESULT&, const NodeImageMultiLayer*);
		void GenerateImage_Invert(IMAGE_GENERATION_RESULT&, const NodeImageInvert*);
		void GenerateImage_Variation(IMAGE_GENERATION_RESULT&, const NodeImageVariation*);
		void GenerateImage_NormalComposite(IMAGE_GENERATION_RESULT&, const NodeImageNormalComposite*);
		void GenerateImage_Transform(IMAGE_GENERATION_RESULT&, const NodeImageTransform*);

		//!
		ImagePtr GenerateMissingImage(EImageFormat);

		//!
		Ptr<ASTOp> GenerateMissingImageCode(const char* strWhere, EImageFormat, const void* errorContext);

		//!
		Ptr<ASTOp> GeneratePlainImageCode(const vec3<float>& colour);

		//!
		Ptr<ASTOp> GenerateImageFormat(Ptr<ASTOp>, EImageFormat);

		//!
		Ptr<ASTOp> GenerateImageUncompressed(Ptr<ASTOp>);

		//!
		Ptr<ASTOp> GenerateImageSize(Ptr<ASTOp>, FImageSize);

		//!
		FImageDesc CalculateImageDesc(const Node::Private&);


        //-----------------------------------------------------------------------------------------
        // Meshes
        typedef TMap<VISITED_MAP_KEY,MESH_GENERATION_RESULT> GeneratedMeshMap;
        GeneratedMeshMap m_generatedMeshes;

        void GenerateMesh( MESH_GENERATION_RESULT& result, const NodeMeshPtrConst& node);
        void GenerateMesh_Constant( MESH_GENERATION_RESULT&, const NodeMeshConstant* );
        void GenerateMesh_Format( MESH_GENERATION_RESULT&, const NodeMeshFormat* );
        void GenerateMesh_Morph( MESH_GENERATION_RESULT&, const NodeMeshMorph* );
        void GenerateMesh_MakeMorph( MESH_GENERATION_RESULT&, const NodeMeshMakeMorph* );
        void GenerateMesh_Fragment( MESH_GENERATION_RESULT&, const NodeMeshFragment* );
        void GenerateMesh_Interpolate( MESH_GENERATION_RESULT&, const NodeMeshInterpolate* );
        void GenerateMesh_Switch( MESH_GENERATION_RESULT&, const NodeMeshSwitch* );
        void GenerateMesh_Subtract( MESH_GENERATION_RESULT&, const NodeMeshSubtract* );
        void GenerateMesh_Transform( MESH_GENERATION_RESULT&, const NodeMeshTransform* );
        void GenerateMesh_ClipMorphPlane( MESH_GENERATION_RESULT&, const NodeMeshClipMorphPlane* );
        void GenerateMesh_ClipWithMesh( MESH_GENERATION_RESULT&, const NodeMeshClipWithMesh* );
        void GenerateMesh_ApplyPose( MESH_GENERATION_RESULT&, const NodeMeshApplyPose* );
        void GenerateMesh_Variation( MESH_GENERATION_RESULT&, const NodeMeshVariation* );
		void GenerateMesh_Table(MESH_GENERATION_RESULT&, const NodeMeshTable*);
		void GenerateMesh_GeometryOperation(MESH_GENERATION_RESULT&, const NodeMeshGeometryOperation*);
		void GenerateMesh_Reshape(MESH_GENERATION_RESULT&, const NodeMeshReshape*);
		void GenerateMesh_ClipDeform(MESH_GENERATION_RESULT&, const NodeMeshClipDeform*);

		void GenerateLayout(MESH_GENERATION_RESULT& result, const NodeLayoutBlocksPtrConst& node, uint32 currentLayoutChannel, const MeshPtr currentLayoutMesh);

		//!
		Ptr<const Layout> AddLayout(Ptr<const Layout> pLayout);

        //-----------------------------------------------------------------------------------------
        // Projectors
        struct PROJECTOR_GENERATION_RESULT
        {
            Ptr<ASTOp> op;
            PROJECTOR_TYPE type;
        };

        typedef TMap<VISITED_MAP_KEY,PROJECTOR_GENERATION_RESULT> GeneratedProjectorsMap;
        GeneratedProjectorsMap m_generatedProjectors;

        void GenerateProjector( PROJECTOR_GENERATION_RESULT&, const NodeProjectorPtrConst& );
        void GenerateProjector_Constant( PROJECTOR_GENERATION_RESULT&, const Ptr<const NodeProjectorConstant>& );
        void GenerateProjector_Parameter( PROJECTOR_GENERATION_RESULT&, const Ptr<const NodeProjectorParameter>& );
        void GenerateMissingProjectorCode( PROJECTOR_GENERATION_RESULT&, const void* errorContext );

		//-----------------------------------------------------------------------------------------
		// Bools
		struct BOOL_GENERATION_RESULT
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<VISITED_MAP_KEY, BOOL_GENERATION_RESULT> GeneratedBoolsMap;
		GeneratedBoolsMap m_generatedBools;

		void GenerateBool(BOOL_GENERATION_RESULT&, const NodeBoolPtrConst&);
		void GenerateBool_Constant(BOOL_GENERATION_RESULT&, const Ptr<const NodeBoolConstant>&);
		void GenerateBool_Parameter(BOOL_GENERATION_RESULT&, const Ptr<const NodeBoolParameter>&);
		void GenerateBool_IsNull(BOOL_GENERATION_RESULT&, const Ptr<const NodeBoolIsNull>&);
		void GenerateBool_Not(BOOL_GENERATION_RESULT&, const Ptr<const NodeBoolNot>&);
		void GenerateBool_And(BOOL_GENERATION_RESULT&, const Ptr<const NodeBoolAnd>&);

		//-----------------------------------------------------------------------------------------
		// Scalars
		struct SCALAR_GENERATION_RESULT
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<VISITED_MAP_KEY, SCALAR_GENERATION_RESULT> GeneratedScalarsMap;
		GeneratedScalarsMap m_generatedScalars;

		void GenerateScalar(SCALAR_GENERATION_RESULT&, const NodeScalarPtrConst&);
		void GenerateScalar_Constant(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarConstant>&);
		void GenerateScalar_Parameter(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarParameter>&);
		void GenerateScalar_Switch(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarSwitch>&);
		void GenerateScalar_EnumParameter(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarEnumParameter>&);
		void GenerateScalar_Curve(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarCurve>&);
		void GenerateScalar_Arithmetic(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarArithmeticOperation>&);
		void GenerateScalar_Variation(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarVariation>&);
		void GenerateScalar_Table(SCALAR_GENERATION_RESULT&, const Ptr<const NodeScalarTable>&);
		Ptr<ASTOp> GenerateMissingScalarCode(const char* strWhere, float value, const void* errorContext);

		//-----------------------------------------------------------------------------------------
		// Colors
		struct COLOR_GENERATION_RESULT
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<VISITED_MAP_KEY, COLOR_GENERATION_RESULT> GeneratedColorsMap;
		GeneratedColorsMap m_generatedColors;

		void GenerateColor(COLOR_GENERATION_RESULT&, const NodeColourPtrConst&);
		void GenerateColor_Constant(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourConstant>&);
		void GenerateColor_Parameter(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourParameter>&);
		void GenerateColor_Switch(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourSwitch>&);
		void GenerateColor_SampleImage(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourSampleImage>&);
		void GenerateColor_FromScalars(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourFromScalars>&);
		void GenerateColor_Arithmetic(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourArithmeticOperation>&);
		void GenerateColor_Variation(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourVariation>&);
		void GenerateColor_Table(COLOR_GENERATION_RESULT&, const Ptr<const NodeColourTable>&);
		Ptr<ASTOp> GenerateMissingColourCode(const char* strWhere, const void* errorContext);

		//-----------------------------------------------------------------------------------------
		// Strings
		struct STRING_GENERATION_RESULT
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<VISITED_MAP_KEY, STRING_GENERATION_RESULT> GeneratedStringsMap;
		GeneratedStringsMap m_generatedStrings;

		void GenerateString(STRING_GENERATION_RESULT&, const NodeStringPtrConst&);
		void GenerateString_Constant(STRING_GENERATION_RESULT&, const Ptr<const NodeStringConstant>&);
		void GenerateString_Parameter(STRING_GENERATION_RESULT&, const Ptr<const NodeStringParameter>&);

        //-----------------------------------------------------------------------------------------
        // Ranges
        struct RANGE_GENERATION_RESULT
        {
            //
            Ptr<ASTOp> sizeOp;

            //
            string rangeName;

            //
            string rangeUID;
        };

        typedef TMap<VISITED_MAP_KEY,RANGE_GENERATION_RESULT> GeneratedRangeMap;
        GeneratedRangeMap m_generatedRanges;

        void GenerateRange( RANGE_GENERATION_RESULT& result, Ptr<const NodeRange> node);


        //-----------------------------------------------------------------------------------------
        struct SURFACE_GENERATION_RESULT
        {
            Ptr<ASTOp> surfaceOp;
        };

        void GenerateSurface( SURFACE_GENERATION_RESULT& result,
                              NodeSurfaceNewPtrConst node,
                              const vector<FirstPassGenerator::SURFACE::EDIT>& edits );

        TaskManager* m_pTaskManager = nullptr;
    };


    //---------------------------------------------------------------------------------------------
    //! Analyse the code trying to guess the descriptor of the image genereated by the instruction
    //! address.
    //! \param returnBestOption If true, try to resolve ambiguities returning some value.
    //---------------------------------------------------------------------------------------------
    extern FImageDesc GetImageDesc( const PROGRAM& program, OP::ADDRESS at,
                                    bool returnBestOption = false,
                                    class GetImageDescContext* context=nullptr );

    //!
    extern void PartialOptimise( Ptr<ASTOp>& op, const CompilerOptions* options );
    

	
    //---------------------------------------------------------------------------------------------
    template<class NODE_TABLE_PRIVATE, TABLE_COLUMN_TYPE TYPE, OP_TYPE OPTYPE, typename F>
    Ptr<ASTOp> CodeGenerator::GenerateTableSwitch
        (
            const NODE_TABLE_PRIVATE& node, F&& GenerateOption
        )
    {
        TablePtr pTable;
        Ptr<ASTOp> variable;

        map< std::pair<TablePtr,string>, std::pair<TablePtr,Ptr<ASTOp>> >::iterator it
                = m_generatedTables.find
                ( std::pair<TablePtr,string>(node.m_pTable,node.m_parameterName) );
        if ( it!=m_generatedTables.end() )
        {
            pTable = it->second.first;
            variable = it->second.second;
        }

        if ( !pTable )
        {
            // Create the table variable expression
            pTable = node.m_pTable;
            variable = GenerateTableVariable( pTable, node.m_parameterName );

            m_generatedTables[ std::pair<TablePtr,string>(node.m_pTable,node.m_parameterName) ] =
                    std::pair<TablePtr,Ptr<ASTOp>>( pTable, variable );
        }

        // Verify that the table column is the right type
        int colIndex = pTable->FindColumn( node.m_columnName.c_str() );
        if ( colIndex<0 )
        {
            m_pErrorLog->GetPrivate()->Add("Table column not found.", ELMT_ERROR, node.m_errorContext);
            return nullptr;
        }

        if ( pTable->GetPrivate()->m_columns[ colIndex ].m_type != TYPE )
        {
            m_pErrorLog->GetPrivate()->Add("Table column type is not the right type.",
                                           ELMT_ERROR, node.m_errorContext);
            return nullptr;
        }

        // Create the switch to cover all the options
        Ptr<ASTOp> lastSwitch;
        std::size_t rows = pTable->GetPrivate()->m_rows.size();

        Ptr<ASTOpSwitch> SwitchOp = new ASTOpSwitch();
		SwitchOp->type = OPTYPE;
		SwitchOp->variable = variable;
		SwitchOp->def = nullptr;

		for (size_t i = 0; i < rows; ++i)
        {
            check( pTable->GetPrivate()->m_rows[i].m_id <= 0xFFFF);
            auto condition = (uint16_t)pTable->GetPrivate()->m_rows[i].m_id;
            Ptr<ASTOp> Branch = GenerateOption( node, colIndex, (int)i, m_pErrorLog.get() );
			SwitchOp->cases.push_back(ASTOpSwitch::CASE(condition, SwitchOp, Branch ));
        }

        return SwitchOp;
    }
}
