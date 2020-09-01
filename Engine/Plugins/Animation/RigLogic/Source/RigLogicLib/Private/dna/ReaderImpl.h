// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "dna/BaseImpl.h"
#include "dna/DenormalizedData.h"
#include "dna/types/Aliases.h"

namespace dna {

template<class TReaderBase>
class ReaderImpl : public TReaderBase, public virtual BaseImpl {
    public:
        explicit ReaderImpl(MemoryResource* memRes_);

        // DescriptorReader methods start
        StringView getName() const override;
        Archetype getArchetype() const override;
        Gender getGender() const override;
        std::uint16_t getAge() const override;
        std::uint32_t getMetaDataCount() const override;
        StringView getMetaDataKey(std::uint32_t index) const override;
        StringView getMetaDataValue(const char* key) const override;
        TranslationUnit getTranslationUnit() const override;
        RotationUnit getRotationUnit() const override;
        CoordinateSystem getCoordinateSystem() const override;
        std::uint16_t getLODCount() const override;
        std::uint16_t getDBMaxLOD() const override;
        StringView getDBComplexity() const override;
        StringView getDBName() const override;

        // DefinitionReader methods start
        std::uint16_t getGUIControlCount() const override;
        StringView getGUIControlName(std::uint16_t index) const override;
        std::uint16_t getRawControlCount() const override;
        StringView getRawControlName(std::uint16_t index) const override;
        std::uint16_t getJointCount() const override;
        StringView getJointName(std::uint16_t index) const override;
        ConstArrayView<std::uint16_t> getJointIndicesForLOD(std::uint16_t lod) const override;
        std::uint16_t getJointParentIndex(std::uint16_t index) const override;
        std::uint16_t getBlendShapeChannelCount() const override;
        StringView getBlendShapeChannelName(std::uint16_t index) const override;
        ConstArrayView<std::uint16_t> getBlendShapeChannelIndicesForLOD(std::uint16_t lod) const override;
        std::uint16_t getAnimatedMapCount() const override;
        StringView getAnimatedMapName(std::uint16_t index) const override;
        ConstArrayView<std::uint16_t> getAnimatedMapIndicesForLOD(std::uint16_t lod) const override;
        std::uint16_t getMeshCount() const override;
        StringView getMeshName(std::uint16_t index) const override;
        ConstArrayView<std::uint16_t> getMeshIndicesForLOD(std::uint16_t lod) const override;
        std::uint16_t getMeshBlendShapeChannelMappingCount() const override;
        MeshBlendShapeChannelMapping getMeshBlendShapeChannelMapping(std::uint16_t index) const override;
        ConstArrayView<std::uint16_t> getMeshBlendShapeChannelMappingIndicesForLOD(std::uint16_t lod) const override;
        Vector3 getNeutralJointTranslation(std::uint16_t index) const override;
        ConstArrayView<float> getNeutralJointTranslationXs() const override;
        ConstArrayView<float> getNeutralJointTranslationYs() const override;
        ConstArrayView<float> getNeutralJointTranslationZs() const override;
        Vector3 getNeutralJointRotation(std::uint16_t index) const override;
        ConstArrayView<float> getNeutralJointRotationXs() const override;
        ConstArrayView<float> getNeutralJointRotationYs() const override;
        ConstArrayView<float> getNeutralJointRotationZs() const override;

        // BehaviorReader methods start
        ConstArrayView<std::uint16_t> getGUIToRawInputIndices() const override;
        ConstArrayView<std::uint16_t> getGUIToRawOutputIndices() const override;
        ConstArrayView<float> getGUIToRawFromValues() const override;
        ConstArrayView<float> getGUIToRawToValues() const override;
        ConstArrayView<float> getGUIToRawSlopeValues() const override;
        ConstArrayView<float> getGUIToRawCutValues() const override;
        std::uint16_t getPSDCount() const override;
        ConstArrayView<std::uint16_t> getPSDRowIndices() const override;
        ConstArrayView<std::uint16_t> getPSDColumnIndices() const override;
        ConstArrayView<float> getPSDValues() const override;
        std::uint16_t getJointRowCount() const override;
        std::uint16_t getJointColumnCount() const override;
        ConstArrayView<std::uint16_t> getJointVariableAttributeIndices(std::uint16_t lod) const override;
        std::uint16_t getJointGroupCount() const override;
        ConstArrayView<std::uint16_t> getJointGroupLODs(std::uint16_t jointGroupIndex) const override;
        ConstArrayView<std::uint16_t> getJointGroupInputIndices(std::uint16_t jointGroupIndex) const override;
        ConstArrayView<std::uint16_t> getJointGroupOutputIndices(std::uint16_t jointGroupIndex) const override;
        ConstArrayView<float> getJointGroupValues(std::uint16_t jointGroupIndex) const override;
        ConstArrayView<std::uint16_t> getJointGroupJointIndices(std::uint16_t jointGroupIndex) const override;
        ConstArrayView<std::uint16_t> getBlendShapeChannelLODs() const override;
        ConstArrayView<std::uint16_t> getBlendShapeChannelOutputIndices() const override;
        ConstArrayView<std::uint16_t> getBlendShapeChannelInputIndices() const override;
        ConstArrayView<std::uint16_t> getAnimatedMapLODs() const override;
        ConstArrayView<std::uint16_t> getAnimatedMapInputIndices() const override;
        ConstArrayView<std::uint16_t> getAnimatedMapOutputIndices() const override;
        ConstArrayView<float> getAnimatedMapFromValues() const override;
        ConstArrayView<float> getAnimatedMapToValues() const override;
        ConstArrayView<float> getAnimatedMapSlopeValues() const override;
        ConstArrayView<float> getAnimatedMapCutValues() const override;

        // GeometryReader methods start
        std::uint32_t getVertexPositionCount(std::uint16_t meshIndex) const override;
        Position getVertexPosition(std::uint16_t meshIndex, std::uint32_t vertexIndex) const override;
        ConstArrayView<float> getVertexPositionXs(std::uint16_t meshIndex) const override;
        ConstArrayView<float> getVertexPositionYs(std::uint16_t meshIndex) const override;
        ConstArrayView<float> getVertexPositionZs(std::uint16_t meshIndex) const override;
        std::uint32_t getVertexTextureCoordinateCount(std::uint16_t meshIndex) const override;
        TextureCoordinate getVertexTextureCoordinate(std::uint16_t meshIndex,
                                                     std::uint32_t textureCoordinateIndex) const override;
        ConstArrayView<float> getVertexTextureCoordinateUs(std::uint16_t meshIndex) const override;
        ConstArrayView<float> getVertexTextureCoordinateVs(std::uint16_t meshIndex) const override;
        std::uint32_t getVertexNormalCount(std::uint16_t meshIndex) const override;
        Normal getVertexNormal(std::uint16_t meshIndex, std::uint32_t normalIndex) const override;
        ConstArrayView<float> getVertexNormalXs(std::uint16_t meshIndex) const override;
        ConstArrayView<float> getVertexNormalYs(std::uint16_t meshIndex) const override;
        ConstArrayView<float> getVertexNormalZs(std::uint16_t meshIndex) const override;
        std::uint32_t getFaceCount(std::uint16_t meshIndex) const override;
        ConstArrayView<std::uint32_t> getFaceVertexLayoutIndices(std::uint16_t meshIndex, std::uint32_t faceIndex) const override;
        std::uint32_t getVertexLayoutCount(std::uint16_t meshIndex) const override;
        VertexLayout getVertexLayout(std::uint16_t meshIndex, std::uint32_t layoutIndex) const override;
        ConstArrayView<std::uint32_t> getVertexLayoutPositionIndices(std::uint16_t meshIndex) const override;
        ConstArrayView<std::uint32_t> getVertexLayoutTextureCoordinateIndices(std::uint16_t meshIndex) const override;
        ConstArrayView<std::uint32_t> getVertexLayoutNormalIndices(std::uint16_t meshIndex) const override;
        std::uint16_t getMaximumInfluencePerVertex(std::uint16_t meshIndex) const override;
        ConstArrayView<float> getSkinWeightsValues(std::uint16_t meshIndex, std::uint32_t vertexIndex) const override;
        ConstArrayView<std::uint16_t> getSkinWeightsJointIndices(std::uint16_t meshIndex,
                                                                 std::uint32_t vertexIndex) const override;
        std::uint16_t getBlendShapeTargetCount(std::uint16_t meshIndex) const override;
        std::uint16_t getBlendShapeChannelIndex(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override;
        std::uint32_t getBlendShapeTargetDeltaCount(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex) const override;
        Delta getBlendShapeTargetDelta(std::uint16_t meshIndex, std::uint16_t blendShapeTargetIndex,
                                       std::uint32_t deltaIndex) const override;
        ConstArrayView<float> getBlendShapeTargetDeltaXs(std::uint16_t meshIndex,
                                                         std::uint16_t blendShapeTargetIndex) const override;
        ConstArrayView<float> getBlendShapeTargetDeltaYs(std::uint16_t meshIndex,
                                                         std::uint16_t blendShapeTargetIndex) const override;
        ConstArrayView<float> getBlendShapeTargetDeltaZs(std::uint16_t meshIndex,
                                                         std::uint16_t blendShapeTargetIndex) const override;
        ConstArrayView<std::uint32_t> getBlendShapeTargetVertexIndices(std::uint16_t meshIndex,
                                                                       std::uint16_t blendShapeTargetIndex) const override;

    protected:
        mutable DenormalizedData<TReaderBase> cache;

};

}  // namespace dna

#include "dna/ReaderImpl.hpp"
