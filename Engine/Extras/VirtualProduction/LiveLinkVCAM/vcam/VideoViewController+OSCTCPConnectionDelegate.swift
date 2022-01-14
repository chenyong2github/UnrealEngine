//
//  VideoViewController+OSCTCPConnectionDelegate.swift
//  vcam
//
//  Created by Brian Smith on 8/8/20.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import Foundation
import UIKit

extension VideoViewController : OSCTCPConnectionDelegate {
    
    func oscConnectionDidConnect(_ connection: OSCTCPConnection) {
        
        Log.info("RemoteSession did connect to \(connection.host):\(connection.port)")
        DispatchQueue.main.async {
            self.showReconnecting(false, animated: true)
        }
    }
    
    func oscConnectionDidDisconnect(_ connection: OSCTCPConnection, withError err: Error?) {

        if let e = err {
            Log.error("RemoteSession disconnected: \(e.localizedDescription)")
        } else {
            Log.info("RemoteSession disconnected.")
        }

        DispatchQueue.main.async {

            if self.dismissOnDisconnect {
                self.oscConnection?.delegate = nil
                self.oscConnection = nil
                self.presentingViewController?.dismiss(animated: true, completion: nil)
            } else {
                self.showReconnecting(true, animated: true)
            }
        }
    }
    
    func oscConnection(_ connection: OSCTCPConnection, didReceivePacket packet: OSCPacket) {

        if let msg = packet as? OSCPacketMessage {
            
            //Log.info(msg.debugString())
            
            switch msg.addressPattern {

            case OSCAddressPattern.rsHello.rawValue:
                if let args = msg.arguments {
                    if args.count == 1,
                        case let OSCArgument.string(version) = args[0] {
                        connection.send(OSCAddressPattern.rsHello, arguments: [ OSCArgument.string(version) ])
                    }
                }

            case OSCAddressPattern.rsGoodbye.rawValue:
                connection.disconnect()
                if let args = msg.arguments {
                    if args.count == 1,
                        case let OSCArgument.string(version) = args[0] {
                        Log.info("RemoteSession closed by server : \(version)")
                    }
                }
                
            case OSCAddressPattern.rsChannelList.rawValue:
                connection.send(OSCAddressPattern.rsChangeChannel, arguments: [ OSCArgument.string("FRemoteSessionImageChannel"), OSCArgument.string("Write"), OSCArgument.int32(1)] )
                connection.send(OSCAddressPattern.rsChangeChannel, arguments: [ OSCArgument.string("FRemoteSessionInputChannel"), OSCArgument.string("Read"), OSCArgument.int32(1)] )

            case OSCAddressPattern.screen.rawValue:

                if let args = msg.arguments {
                    if args.count == 4,
                        case let OSCArgument.int32(width) = args[0],
                        case let OSCArgument.int32(height) = args[1],
                       case let OSCArgument.blob(blob) = args[2] {
                        
                        //Log.info("RECV frame \(frame) : \(width)x\(height) \(blob.count) bytes")
                        self.decode(width, height, blob)
                    }
                }
                
            case OSCAddressPattern.ping.rawValue:
                connection.send(OSCAddressPattern.ping)


            default:
                break
            }
        }
    }

}
