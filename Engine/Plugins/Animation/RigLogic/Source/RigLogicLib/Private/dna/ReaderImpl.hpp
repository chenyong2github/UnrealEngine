// Copyright Epic Games, Inc. All Rights Reserved.

#include "dna/TypeDefs.h"

#include <cstddef>
#include <limits>
#include <tuple>

namespace dna {

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4589)
#endif
template<class TReaderBase>
ReaderImpl<TReaderBase>::ReaderImpl(MemoryResource* memRes_) : BaseImpl{memRes_}, cache{memRes_} {
}

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4505)
#endif
template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getName() const {
    return {dna.descriptor.name.data(), dna.descriptor.name.size()};
}

template<class TReaderBase>
Archetype ReaderImpl<TReaderBase>::getArchetype() const {
    return static_cast<Archetype>(dna.descriptor.archetype);
}

template<class TReaderBase>
Gender ReaderImpl<TReaderBase>::getGender() const {
    return static_cast<Gender>(dna.descriptor.gender);
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getAge() const {
    return dna.descriptor.age;
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getMetaDataCount() const {
    return static_cast<std::uint32_t>(dna.descriptor.metadata.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getMetaDataKey(std::uint32_t index) const {
    if (index < dna.descriptor.metadata.size()) {
        const auto& key = std::get<0>(dna.descriptor.metadata[index]);
        return {key.data(), key.size()};
    }
    return {};
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getMetaDataValue(const char* key) const {
    for (const auto& data: dna.descriptor.metadata) {
        if (std::get<0>(data) == key) {
            const auto& value = std::get<1>(data);
            return {value.data(), value.size()};
        }
    }
    return {};
}

template<class TReaderBase>
TranslationUnit ReaderImpl<TReaderBase>::getTranslationUnit() const {
    return static_cast<TranslationUnit>(dna.descriptor.translationUnit);
}

template<class TReaderBase>
RotationUnit ReaderImpl<TReaderBase>::getRotationUnit() const {
    return static_cast<RotationUnit>(dna.descriptor.rotationUnit);
}

template<class TReaderBase>
CoordinateSystem ReaderImpl<TReaderBase>::getCoordinateSystem() const {
    return {
        static_cast<Direction>(dna.descriptor.coordinateSystem.xAxis),
        static_cast<Direction>(dna.descriptor.coordinateSystem.yAxis),
        static_cast<Direction>(dna.descriptor.coordinateSystem.zAxis)
    };
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getLODCount() const {
    return dna.descriptor.lodCount;
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getDBMaxLOD() const {
    return dna.descriptor.maxLOD;
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getDBComplexity() const {
    return {dna.descriptor.complexity.data(), dna.descriptor.complexity.size()};
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getDBName() const {
    return {dna.descriptor.dbName.data(), dna.descriptor.dbName.size()};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getGUIControlCount() const {
    return static_cast<std::uint16_t>(dna.definition.guiControlNames.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getGUIControlName(std::uint16_t index) const {
    if (index < dna.definition.guiControlNames.size()) {
        const auto& guiControlName = dna.definition.guiControlNames[index];
        return {guiControlName.data(), guiControlName.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getRawControlCount() const {
    return static_cast<std::uint16_t>(dna.definition.rawControlNames.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getRawControlName(std::uint16_t index) const {
    if (index < dna.definition.rawControlNames.size()) {
        const auto& rawControlName = dna.definition.rawControlNames[index];
        return {rawControlName.data(), rawControlName.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getJointCount() const {
    return static_cast<std::uint16_t>(dna.definition.jointNames.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getJointName(std::uint16_t index) const {
    if (index < dna.definition.jointNames.size()) {
        const auto& jointName = dna.definition.jointNames[index];
        return {jointName.data(), jointName.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getJointIndicesForLOD(std::uint16_t lod) const {
    return dna.definition.lodJointMapping.getIndices(lod);
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getJointParentIndex(std::uint16_t index) const {
    if (index < dna.definition.jointHierarchy.size()) {
        return dna.definition.jointHierarchy[index];
    }
    return std::numeric_limits<std::uint16_t>::max();
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getBlendShapeChannelCount() const {
    return static_cast<std::uint16_t>(dna.definition.blendShapeChannelNames.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getBlendShapeChannelName(std::uint16_t index) const {
    if (index < dna.definition.blendShapeChannelNames.size()) {
        const auto& blendShapeName = dna.definition.blendShapeChannelNames[index];
        return {blendShapeName.data(), blendShapeName.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getBlendShapeChannelIndicesForLOD(std::uint16_t lod) const {
    return dna.definition.lodBlendShapeMapping.getIndices(lod);
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getAnimatedMapCount() const {
    return static_cast<std::uint16_t>(dna.definition.animatedMapNames.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getAnimatedMapName(std::uint16_t index) const {
    if (index < dna.definition.animatedMapNames.size()) {
        const auto& animatedMapName = dna.definition.animatedMapNames[index];
        return {animatedMapName.data(), animatedMapName.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getAnimatedMapIndicesForLOD(std::uint16_t lod) const {
    return dna.definition.lodAnimatedMapMapping.getIndices(lod);
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getMeshCount() const {
    return static_cast<std::uint16_t>(dna.definition.meshNames.size());
}

template<class TReaderBase>
StringView ReaderImpl<TReaderBase>::getMeshName(std::uint16_t index) const {
    if (index < dna.definition.meshNames.size()) {
        const auto& meshName = dna.definition.meshNames[index];
        return {meshName.data(), meshName.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getMeshIndicesForLOD(std::uint16_t lod) const {
    return dna.definition.lodMeshMapping.getIndices(lod);
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getMeshBlendShapeChannelMappingCount() const {
    return static_cast<std::uint16_t>(dna.definition.meshBlendShapeChannelMapping.size());
}

template<class TReaderBase>
MeshBlendShapeChannelMapping ReaderImpl<TReaderBase>::getMeshBlendShapeChannelMapping(std::uint16_t index) const {
    const auto mapping = dna.definition.meshBlendShapeChannelMapping.get(index);
    return {mapping.from, mapping.to};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getMeshBlendShapeChannelMappingIndicesForLOD(std::uint16_t lod) const {
    if (cache.meshBlendShapeMappingIndices.getLODCount() == static_cast<std::uint16_t>(0)) {
        cache.populate(this);
    }
    return cache.meshBlendShapeMappingIndices.getIndices(lod);
}

template<class TReaderBase>
Vector3 ReaderImpl<TReaderBase>::getNeutralJointTranslation(std::uint16_t index) const {
    const auto& translations = dna.definition.neutralJointTranslations;
    if (index < translations.xs.size()) {
        return {translations.xs[index], translations.ys[index], translations.zs[index]};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getNeutralJointTranslationXs() const {
    const auto& xs = dna.definition.neutralJointTranslations.xs;
    return {xs.data(), xs.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getNeutralJointTranslationYs() const {
    const auto& ys = dna.definition.neutralJointTranslations.ys;
    return {ys.data(), ys.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getNeutralJointTranslationZs() const {
    const auto& zs = dna.definition.neutralJointTranslations.zs;
    return {zs.data(), zs.size()};
}

template<class TReaderBase>
Vector3 ReaderImpl<TReaderBase>::getNeutralJointRotation(std::uint16_t index) const {
    const auto& rotations = dna.definition.neutralJointRotations;
    if (index < rotations.size()) {
        return {rotations.xs[index], rotations.ys[index], rotations.zs[index]};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getNeutralJointRotationXs() const {
    const auto& xs = dna.definition.neutralJointRotations.xs;
    return {xs.data(), xs.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getNeutralJointRotationYs() const {
    const auto& ys = dna.definition.neutralJointRotations.ys;
    return {ys.data(), ys.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getNeutralJointRotationZs() const {
    const auto& zs = dna.definition.neutralJointRotations.zs;
    return {zs.data(), zs.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getGUIToRawInputIndices() const {
    const auto& inputIndices = dna.behavior.controls.conditionals.inputIndices;
    return {inputIndices.data(), inputIndices.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getGUIToRawOutputIndices() const {
    const auto& outputIndices = dna.behavior.controls.conditionals.outputIndices;
    return {outputIndices.data(), outputIndices.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getGUIToRawFromValues() const {
    const auto& fromValues = dna.behavior.controls.conditionals.fromValues;
    return {fromValues.data(), fromValues.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getGUIToRawToValues() const {
    const auto& toValues = dna.behavior.controls.conditionals.toValues;
    return {toValues.data(), toValues.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getGUIToRawSlopeValues() const {
    const auto& slopeValues = dna.behavior.controls.conditionals.slopeValues;
    return {slopeValues.data(), slopeValues.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getGUIToRawCutValues() const {
    const auto& cutValues = dna.behavior.controls.conditionals.cutValues;
    return {cutValues.data(), cutValues.size()};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getPSDCount() const {
    return dna.behavior.controls.psdCount;
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getPSDRowIndices() const {
    const auto& rows = dna.behavior.controls.psds.rows;
    return {rows.data(), rows.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getPSDColumnIndices() const {
    const auto& columns = dna.behavior.controls.psds.columns;
    return {columns.data(), columns.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getPSDValues() const {
    const auto& values = dna.behavior.controls.psds.values;
    return {values.data(), values.size()};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getJointRowCount() const {
    return dna.behavior.joints.rowCount;
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getJointColumnCount() const {
    return dna.behavior.joints.colCount;
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getJointVariableAttributeIndices(std::uint16_t lod) const {
    if (cache.jointVariableAttributeIndices.getLODCount() == static_cast<std::uint16_t>(0)) {
        cache.populate(this);
    }
    return cache.jointVariableAttributeIndices.getIndices(lod);
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getJointGroupCount() const {
    return static_cast<std::uint16_t>(dna.behavior.joints.jointGroups.size());
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getJointGroupLODs(std::uint16_t jointGroupIndex) const {
    if (jointGroupIndex < dna.behavior.joints.jointGroups.size()) {
        const auto& lods = dna.behavior.joints.jointGroups[jointGroupIndex].lods;
        return {lods.data(), lods.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getJointGroupInputIndices(std::uint16_t jointGroupIndex) const {
    if (jointGroupIndex < dna.behavior.joints.jointGroups.size()) {
        const auto& inputIndices = dna.behavior.joints.jointGroups[jointGroupIndex].inputIndices;
        return {inputIndices.data(), inputIndices.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getJointGroupOutputIndices(std::uint16_t jointGroupIndex) const {
    if (jointGroupIndex < dna.behavior.joints.jointGroups.size()) {
        const auto& outputIndices = dna.behavior.joints.jointGroups[jointGroupIndex].outputIndices;
        return {outputIndices.data(), outputIndices.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getJointGroupValues(std::uint16_t jointGroupIndex) const {
    if (jointGroupIndex < dna.behavior.joints.jointGroups.size()) {
        const auto& values = dna.behavior.joints.jointGroups[jointGroupIndex].values;
        return {values.data(), values.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getJointGroupJointIndices(std::uint16_t jointGroupIndex) const {
    if (jointGroupIndex < dna.behavior.joints.jointGroups.size()) {
        const auto& jointIndices = dna.behavior.joints.jointGroups[jointGroupIndex].jointIndices;
        return {jointIndices.data(), jointIndices.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getBlendShapeChannelLODs() const {
    const auto& lods = dna.behavior.blendShapeChannels.lods;
    return {lods.data(), lods.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getBlendShapeChannelInputIndices() const {
    const auto& inputIndices = dna.behavior.blendShapeChannels.inputIndices;
    return {inputIndices.data(), inputIndices.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getBlendShapeChannelOutputIndices() const {
    const auto& outputIndices = dna.behavior.blendShapeChannels.outputIndices;
    return {outputIndices.data(), outputIndices.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getAnimatedMapLODs() const {
    const auto& lods = dna.behavior.animatedMaps.lods;
    return {lods.data(), lods.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getAnimatedMapInputIndices() const {
    const auto& inputIndices = dna.behavior.animatedMaps.conditionals.inputIndices;
    return {inputIndices.data(), inputIndices.size()};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getAnimatedMapOutputIndices() const {
    const auto& outputIndices = dna.behavior.animatedMaps.conditionals.outputIndices;
    return {outputIndices.data(), outputIndices.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getAnimatedMapFromValues() const {
    const auto& fromValues = dna.behavior.animatedMaps.conditionals.fromValues;
    return {fromValues.data(), fromValues.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getAnimatedMapToValues() const {
    const auto& toValues = dna.behavior.animatedMaps.conditionals.toValues;
    return {toValues.data(), toValues.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getAnimatedMapSlopeValues() const {
    const auto& slopeValues = dna.behavior.animatedMaps.conditionals.slopeValues;
    return {slopeValues.data(), slopeValues.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getAnimatedMapCutValues() const {
    const auto& cutValues = dna.behavior.animatedMaps.conditionals.cutValues;
    return {cutValues.data(), cutValues.size()};
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getVertexPositionCount(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return static_cast<std::uint32_t>(dna.geometry.meshes[meshIndex].positions.xs.size());
    }
    return 0u;
}

template<class TReaderBase>
Position ReaderImpl<TReaderBase>::getVertexPosition(std::uint16_t meshIndex, std::uint32_t vertexIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& positions = dna.geometry.meshes[meshIndex].positions;
        if (vertexIndex < positions.size()) {
            return {positions.xs[vertexIndex], positions.ys[vertexIndex], positions.zs[vertexIndex]};
        }
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexPositionXs(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& xPositions = dna.geometry.meshes[meshIndex].positions.xs;
        return {xPositions.data(), xPositions.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexPositionYs(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& yPositions = dna.geometry.meshes[meshIndex].positions.ys;
        return {yPositions.data(), yPositions.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexPositionZs(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& zPositions = dna.geometry.meshes[meshIndex].positions.zs;
        return {zPositions.data(), zPositions.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getVertexTextureCoordinateCount(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return static_cast<std::uint32_t>(dna.geometry.meshes[meshIndex].textureCoordinates.us.size());
    }
    return 0u;
}

template<class TReaderBase>
TextureCoordinate ReaderImpl<TReaderBase>::getVertexTextureCoordinate(std::uint16_t meshIndex,
                                                                      std::uint32_t textureCoordinateIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& textureCoordinates = dna.geometry.meshes[meshIndex].textureCoordinates;
        if (textureCoordinateIndex < textureCoordinates.size()) {
            return {textureCoordinates.us[textureCoordinateIndex], textureCoordinates.vs[textureCoordinateIndex]};
        }
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexTextureCoordinateUs(std::uint16_t meshIndex) const {
    const auto& uTextureCoordinates = dna.geometry.meshes[meshIndex].textureCoordinates.us;
    return {uTextureCoordinates.data(), uTextureCoordinates.size()};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexTextureCoordinateVs(std::uint16_t meshIndex) const {
    const auto& vTextureCoordinates = dna.geometry.meshes[meshIndex].textureCoordinates.vs;
    return {vTextureCoordinates.data(), vTextureCoordinates.size()};
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getVertexNormalCount(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return static_cast<std::uint32_t>(dna.geometry.meshes[meshIndex].normals.xs.size());
    }
    return 0u;
}

template<class TReaderBase>
Normal ReaderImpl<TReaderBase>::getVertexNormal(std::uint16_t meshIndex, std::uint32_t normalIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& normals = dna.geometry.meshes[meshIndex].normals;
        if (normalIndex < normals.size()) {
            return {normals.xs[normalIndex], normals.ys[normalIndex], normals.zs[normalIndex]};
        }
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexNormalXs(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& xNormals = dna.geometry.meshes[meshIndex].normals.xs;
        return {xNormals.data(), xNormals.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexNormalYs(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& yNormals = dna.geometry.meshes[meshIndex].normals.ys;
        return {yNormals.data(), yNormals.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getVertexNormalZs(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& zNormals = dna.geometry.meshes[meshIndex].normals.zs;
        return {zNormals.data(), zNormals.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getFaceCount(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return static_cast<std::uint32_t>(dna.geometry.meshes[meshIndex].faces.size());
    }
    return 0u;
}

template<class TReaderBase>
ConstArrayView<std::uint32_t> ReaderImpl<TReaderBase>::getFaceVertexLayoutIndices(std::uint16_t meshIndex,
                                                                                  std::uint32_t faceIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (faceIndex < meshes[meshIndex].faces.size())) {
        const auto& layoutIndices = meshes[meshIndex].faces[faceIndex].layoutIndices;
        return {layoutIndices.data(), layoutIndices.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getVertexLayoutCount(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return static_cast<std::uint32_t>(dna.geometry.meshes[meshIndex].layouts.positions.size());
    }
    return 0u;
}

template<class TReaderBase>
VertexLayout ReaderImpl<TReaderBase>::getVertexLayout(std::uint16_t meshIndex, std::uint32_t layoutIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& layouts = dna.geometry.meshes[meshIndex].layouts;
        if (layoutIndex < layouts.size()) {
            return {layouts.positions[layoutIndex], layouts.textureCoordinates[layoutIndex], layouts.normals[layoutIndex]};
        }
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint32_t> ReaderImpl<TReaderBase>::getVertexLayoutPositionIndices(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& positions = dna.geometry.meshes[meshIndex].layouts.positions;
        return {positions.data(), positions.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint32_t> ReaderImpl<TReaderBase>::getVertexLayoutTextureCoordinateIndices(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& textureCoordinated = dna.geometry.meshes[meshIndex].layouts.textureCoordinates;
        return {textureCoordinated.data(), textureCoordinated.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint32_t> ReaderImpl<TReaderBase>::getVertexLayoutNormalIndices(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        const auto& normals = dna.geometry.meshes[meshIndex].layouts.normals;
        return {normals.data(), normals.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getMaximumInfluencePerVertex(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return dna.geometry.meshes[meshIndex].maximumInfluencePerVertex;
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getSkinWeightsValues(std::uint16_t meshIndex, std::uint32_t vertexIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (vertexIndex < meshes[meshIndex].skinWeights.size())) {
        const auto& weights = meshes[meshIndex].skinWeights[vertexIndex].weights;
        return {weights.data(), weights.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint16_t> ReaderImpl<TReaderBase>::getSkinWeightsJointIndices(std::uint16_t meshIndex,
                                                                                  std::uint32_t vertexIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (vertexIndex < meshes[meshIndex].skinWeights.size())) {
        const auto& jointIndices = meshes[meshIndex].skinWeights[vertexIndex].jointIndices;
        return {jointIndices.data(), jointIndices.size()};
    }
    return {};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getBlendShapeTargetCount(std::uint16_t meshIndex) const {
    if (meshIndex < dna.geometry.meshes.size()) {
        return static_cast<std::uint16_t>(dna.geometry.meshes[meshIndex].blendShapeTargets.size());
    }
    return {};
}

template<class TReaderBase>
std::uint16_t ReaderImpl<TReaderBase>::getBlendShapeChannelIndex(std::uint16_t meshIndex,
                                                                 std::uint16_t blendShapeTargetIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size())) {
        return meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].blendShapeChannelIndex;
    }
    return {};
}

template<class TReaderBase>
std::uint32_t ReaderImpl<TReaderBase>::getBlendShapeTargetDeltaCount(std::uint16_t meshIndex,
                                                                     std::uint16_t blendShapeTargetIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size())) {
        return static_cast<std::uint32_t>(meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].deltas.xs.size());
    }
    return {};
}

template<class TReaderBase>
Delta ReaderImpl<TReaderBase>::getBlendShapeTargetDelta(std::uint16_t meshIndex,
                                                        std::uint16_t blendShapeTargetIndex,
                                                        std::uint32_t deltaIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size()) &&
        (deltaIndex < meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].deltas.size())) {
        const auto& deltas = meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].deltas;
        return {deltas.xs[deltaIndex], deltas.ys[deltaIndex], deltas.zs[deltaIndex]};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getBlendShapeTargetDeltaXs(std::uint16_t meshIndex,
                                                                          std::uint16_t blendShapeTargetIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size())) {
        const auto& xDeltas = meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].deltas.xs;
        return {xDeltas.data(), xDeltas.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getBlendShapeTargetDeltaYs(std::uint16_t meshIndex,
                                                                          std::uint16_t blendShapeTargetIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size())) {
        const auto& yDeltas = meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].deltas.ys;
        return {yDeltas.data(), yDeltas.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<float> ReaderImpl<TReaderBase>::getBlendShapeTargetDeltaZs(std::uint16_t meshIndex,
                                                                          std::uint16_t blendShapeTargetIndex) const {
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size())) {
        const auto& zDeltas = meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].deltas.zs;
        return {zDeltas.data(), zDeltas.size()};
    }
    return {};
}

template<class TReaderBase>
ConstArrayView<std::uint32_t> ReaderImpl<TReaderBase>::getBlendShapeTargetVertexIndices(std::uint16_t meshIndex,
                                                                                        std::uint16_t blendShapeTargetIndex) const
{
    const auto& meshes = dna.geometry.meshes;
    if ((meshIndex < meshes.size()) && (blendShapeTargetIndex < meshes[meshIndex].blendShapeTargets.size())) {
        const auto& vertexIndices = meshes[meshIndex].blendShapeTargets[blendShapeTargetIndex].vertexIndices;
        return {vertexIndices.data(), vertexIndices.size()};
    }
    return {};
}

#ifdef _MSC_VER
    #pragma warning(pop)
#endif

}  // namespace dna
