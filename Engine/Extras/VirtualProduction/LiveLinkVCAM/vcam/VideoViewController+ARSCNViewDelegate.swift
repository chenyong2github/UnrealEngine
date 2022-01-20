//
//  VideoViewController+ARSCNViewDelegate.swift
//  vcam
//
//  Created by Brian Smith on 8/11/20.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import ARKit

extension VideoViewController : ARSCNViewDelegate {
    
    func renderer(_ renderer: SCNSceneRenderer, updateAtTime time: TimeInterval) {
        
        if let pov = renderer.pointOfView {
            self.liveLink?.updateSubject(AppSettings.shared.liveLinkSubjectName, transform: pov.simdTransform, time: Timecode.create().toTimeInterval())
        }
        
    }
    
    func renderer(_ renderer: SCNSceneRenderer, didRenderScene scene: SCNScene, atTime time: TimeInterval) {
        
        guard !self.demoMode else { return }
        
        guard let renderEncoder = renderer.currentRenderCommandEncoder else {
            Log.error("SCNSceneRenderer.currentRenderCommandEncoder returned nil!")
            return
        }

        let offsetY = Double((self.headerViewY + self.headerViewHeight) * UIScreen.main.nativeScale)
        renderEncoder.setViewport(MTLViewport(originX: 0, originY: offsetY, width: Double(renderer.currentViewport.width), height: Double(renderer.currentViewport.height) - offsetY, znear: 0, zfar: 1))
        renderEncoder.setRenderPipelineState(self.mtlPassthroughPipelineState!)
        
        renderEncoder.setVertexBuffer(self.mtlVertexBuffer, offset: 0, index: Int(PassthroughVertexInputIndexVertices.rawValue))

        if let pb = self.takeDecodedPixelBuffer() {
            var texture : CVMetalTexture?
            let status = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, self.mtlTextureCache!, pb, nil, .rgba8Unorm, CVPixelBufferGetWidth(pb), CVPixelBufferGetHeight(pb), 0, &texture)

            if status == kCVReturnSuccess,
               let tex = texture {
                self.mtlDecodedTexture = CVMetalTextureGetTexture(tex)
            }
        }
        
        var isARGB = false
        var flipY = false

        var tex = self.mtlNoSignalTexture!
        if let decodedTex = self.mtlDecodedTexture {
            tex = decodedTex
            isARGB = true
            flipY = true
        }

        let aspectRatio = Float(tex.width) / Float(tex.height) / Float(renderer.currentViewport.width / (renderer.currentViewport.height - CGFloat(offsetY)))

        var vertexUniforms = PassthroughVertexUniforms(aspectRatio: aspectRatio)
        let vertexUniformsBuffer = renderer.device?.makeBuffer(bytes: &vertexUniforms, length: MemoryLayout<PassthroughVertexUniforms>.stride, options: [])!
        renderEncoder.setVertexBuffer(vertexUniformsBuffer, offset: 0, index: Int(PassthroughVertexInputIndexUniforms.rawValue))

        renderEncoder.setFragmentTexture(tex, index: Int(BasicTextureInputIndexBaseColor.rawValue))

        var fragmentUniforms = BasicTextureUniforms(isARGB: isARGB ? 1 : 0, flipY: flipY ? 1 : 0)
        let fragmentUniformsBuffer = renderer.device?.makeBuffer(bytes: &fragmentUniforms, length: MemoryLayout<BasicTextureUniforms>.stride, options: [])!
        renderEncoder.setFragmentBuffer(fragmentUniformsBuffer, offset: 0, index: Int(BasicTextureInputIndexUniforms.rawValue))

        renderEncoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        
    }
    
}
