// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dna/BaseImpl.h"
#include "dna/types/Aliases.h"

namespace dna {

template<class TWriterBase>
class WriterImpl : public TWriterBase, public virtual BaseImpl {
    public:
        explicit WriterImpl(MemoryResource* memRes_);

        // DescriptorWriter methods
        void setName(const char* name) override;
        void setArchetype(Archetype archetype) override;
        void setGender(Gender gender) override;
        void setAge(std::uint16_t age) override;
        void setMetaData(const char* key, const char* value) override;
        void setTranslationUnit(TranslationUnit unit) override;
        void setRotationUnit(RotationUnit unit) override;
        void setCoordinateSystem(CoordinateSystem system) override;
        void setLODCount(std::uint16_t lodCount) override;
        void setDBMaxLOD(std::uint16_t lod) override;
        void setDBComplexity(const char* name) override;
        void setDBName(const char* name) override;

        // DefinitionWriter methods
        void setGUIControlName(std::uint16_t index, const char* name) override;
        void setRawControlName(std::uint16_t index, const char* name) override;
        void setJointName(std::uint16_t index, const char* name) override;
        void setJointIndices(std::uint16_t index, const std::uint16_t* jointIndices, std::uint16_t count) override;
        void setLODJointMapping(std::uint16_t lod, std::uint16_t index) override;
        void setJointHierarchy(const std::uint16_t* jointIndices, std::uint16_t count) override;
        void setBlendShapeChannelName(std::uint16_t index, const char* name) override;
        void setBlendShapeChannelIndices(std::uint16_t index, const std::uint16_t* blendShapeChannelIndices,
                                         std::uint16_t count) override;
        void setLODBlendShapeChannelMapping(std::uint16_t lod, std::uint16_t index) override;
        void setAnimatedMapName(std::uint16_t index, const char* name) override;
        void setAnimatedMapIndices(std::uint16_t index, const std::uint16_t* animatedMapIndices, std::uint16_t count) override;
        void setLODAnimatedMapMapping(std::uint16_t lod, std::uint16_t index) override;
        void setMeshName(std::uint16_t index, const char* name) override;
        void setMeshIndices(std::uint16_t index, const std::uint16_t* meshIndices, std::uint16_t count) override;
        void setLODMeshMapping(std::uint16_t lod, std::uint16_t index) override;
        void addMeshBlendShapeChannelMapping(std::uint16_t meshIndex, std::uint16_t blendShapeChannelIndex) override;
        void setNeutralJointTranslations(const Vector3* translations, std::uint16_t count) override;
        void setNeutralJointRotations(const Vector3* rotations, std::uint16_t count) override;

        // BehaviorWriter methods
        void setGUIToRawInputIndices(const std::uint16_t* inputIndices, std::uint16_t count) override;
        void setGUIToRawOutputIndices(const std::uint16_t* outputIndices, std::uint16_t count) override;
        void setGUIToRawFromValues(const float* fromValues, std::uint16_t count) override;
        void setGUIToRawToValues(const float* toValues, std::uint16_t count) override;
        void setGUIToRawSlopeValues(const float* slopeValues, std::uint16_t count) override;
        void setGUIToRawCutValues(const float* cutValues, std::uint16_t count) override;
        void setPSDCount(std::uint16_t count) override;
        void setPSDRowIndices(const std::uint16_t* rowIndices, std::uint16_t count) override;
        void setPSDColumnIndices(const std::uint16_t* columnIndices, std::uint16_t count) override;
        void setPSDValues(const float* weights, std::uint16_t count) override;
        void setJointRowCount(std::uint16_t rowCount) override;
        void setJointColumnCount(std::uint16_t columnCount) override;
        void setJointGroupLODs(std::uint16_t jointGroupIndex, const std::uint16_t* lods, std::uint16_t count) override;
        void setJointGroupInputIndices(std::uint16_t jointGroupIndex, const std::uint16_t* inputIndices,
                                       std::uint16_t count) override;
        void setJointGroupOutputIndices(std::uint16_t jointGroupIndex, const std::uint16_t* outputIndices,
                                        std::uint16_t count) override;
        void setJointGroupValues(std::uint16_t jointGroupIndex, const float* values, std::uint32_t count) override;
        void setJointGroupJointIndices(std::uint16_t jointGroupIndex, const std::uint16_t* jointIndices,
                                       std::uint16_t count) override;
        void setBlendShapeChannelLODs(const std::uint16_t* lods, std::uint16_t count) override;
        void setBlendShapeChannelInputIndices(const std::uint16_t* inputIndices, std::uint16_t count) override;
        void setBlendShapeChannelOutputIndices(const std::uint16_t* outputIndices, std::uint16_t count) override;
        void setAnimatedMapLODs(const std::uint16_t* lods, std::uint16_t count) override;
        void setAnimatedMapInputIndices(const std::uint16_t* inputIndices, std::uint16_t count) override;
        void setAnimatedMapOutputIndices(const std::uint16_t* outputIndices, std::uint16_t count) override;
        void setAnimatedMapFromValues(const float* fromValues, std::uint16_t count) override;
        void setAnimatedMapToValues(const float* toValues, std::uint16_t count) override;
        void setAnimatedMapSlopeValues(const float* slopeValues, std::uint16_t count) override;
        void setAnimatedMapCutValues(const float* cutValues, std::uint16_t count) override;

        // GeometryWriter methods
        void setVertexPositions(std::uint16_t meshIndex, const Position* positions, std::uint32_t count) override;
        void setVertexTextureCoordinates(std::uint16_t meshIndex, const TextureCoordinate* textureCoordinates,
                                         std::uint32_t count) override;
        void setVertexNormals(std::uint16_t meshIndex, const Normal* normals, std::uint32_t count) override;
        void setVertexLayouts(std::uint16_t meshIndex, const VertexLayout* layouts, std::uint32_t count) override;
        void setFaceVertexLayoutIndices(std::uint16_t meshIndex,
                                        std::uint32_t faceIndex,
                                        const std::uint32_t* layoutIndices,
                                        std::uint32_t count) override;
        void setMaximumInfluencePerVertex(std::uint16_t meshIndex, std::uint16_t maxInfluenceCount) override;
        void setSkinWeightsValues(std::uint16_t meshIndex, std::uint32_t vertexIndex, const float* weights,
                                  std::uint16_t count) override;
        void setSkinWeightsJointIndices(std::uint16_t meshIndex,
                                        std::uint32_t vertexIndex,
                                        const std::uint16_t* jointIndices,
                                        std::uint16_t count) override;
        void setBlendShapeChannelIndex(std::uint16_t meshIndex,
                                       std::uint16_t blendShapeTargetIndex,
                                       std::uint16_t blendShapeChannelIndex) override;
        void setBlendShapeTargetDeltas(std::uint16_t meshIndex,
                                       std::uint16_t blendShapeTargetIndex,
                                       const Delta* deltas,
                                       std::uint32_t count) override;
        void setBlendShapeTargetVertexIndices(std::uint16_t meshIndex,
                                              std::uint16_t blendShapeTargetIndex,
                                              const std::uint32_t* vertexIndices,
                                              std::uint32_t count) override;

};

}  // namespace dna

#include "dna/WriterImpl.hpp"
