//
//  LiveLink.swift
//  vcam
//
//  Created by Brian Smith on 1/13/22.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import Foundation

enum EngineVersion {
    case unknown
    case ue4
    case ue5
}

enum LiveLinkError : Error {
    
    case unknownEngineVersion
    case alreadyInitialized
}


class LiveLink {
    
    private static var currentEngineVersion = EngineVersion.unknown
    private var provider : AnyObject?

    static var engineVersion : EngineVersion {
        get {
            switch AppSettings.shared.engineVersion {
            case "UE4": return .ue4
            case "UE5": return .ue5
            default: return .unknown
            }
        }
        set {
            switch newValue {
            case .ue4:
                AppSettings.shared.engineVersion = "UE4"
            case .ue5:
                AppSettings.shared.engineVersion = "UE5"
            case .unknown:
                AppSettings.shared.engineVersion = ""
            }
        }
    }
    
    static var initialized : Bool {
        get {
            currentEngineVersion != .unknown
        }
    }
    
    static var requiresRestart : Bool {
        get {
            currentEngineVersion != engineVersion
        }
    }
    
    class func initialize(_ delegate : AnyObject & UE5LiveLinkLogDelegate & UE4LiveLinkLogDelegate ) throws {
        
        guard currentEngineVersion == .unknown else { throw LiveLinkError.alreadyInitialized }
        
        switch engineVersion {
        case .ue4:
            UE4LiveLink.initialize(delegate)
        case .ue5:
            UE5LiveLink.initialize(delegate)
        default:
            throw LiveLinkError.unknownEngineVersion
        }
        
        LiveLink.currentEngineVersion = self.engineVersion
    }
    
    class func restart() throws {
        switch currentEngineVersion {
        case .ue4:
            UE4LiveLink.restart()
        case .ue5:
            UE5LiveLink.restart()
        default:
            throw LiveLinkError.unknownEngineVersion
        }
    }

    class func shutdown() throws {
        switch currentEngineVersion {
        case .ue4:
            UE4LiveLink.shutdown()
        case .ue5:
            UE5LiveLink.shutdown()
        default:
            throw LiveLinkError.unknownEngineVersion
        }
    }
    
    init(_ providerName : String) throws {
        switch LiveLink.currentEngineVersion {
        case .ue4:
            provider = UE4LiveLink.createProvider(providerName)
        case .ue5:
            provider = UE5LiveLink.createProvider(providerName)
        default:
            throw LiveLinkError.unknownEngineVersion
        }
    }
    
    deinit {
        provider = nil
    }
    
    func addCameraSubject(_ subjectName : String){
        if let p = provider as? UE4LiveLinkProvider {
            p.addCameraSubject(subjectName)
        }

        if let p = provider as? UE5LiveLinkProvider {
            p.addCameraSubject(subjectName)
        }
    }
    
    func removeCameraSubject(_ subjectName : String){
        if let p = provider as? UE4LiveLinkProvider {
            p.removeCameraSubject(subjectName)
        }

        if let p = provider as? UE5LiveLinkProvider {
            p.removeCameraSubject(subjectName)
        }
    }

    func updateSubject( _ subjectName : String, transform xform : simd_float4x4, time : CFTimeInterval) {
        if let p = provider as? UE4LiveLinkProvider {
            p.updateSubject(AppSettings.shared.liveLinkSubjectName, withTransform: xform, atTime: time)
        }
        if let p = provider as? UE5LiveLinkProvider {
            p.updateSubject(AppSettings.shared.liveLinkSubjectName, withTransform: xform, atTime: time)
        }
    }


}
