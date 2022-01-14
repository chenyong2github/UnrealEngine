//
//  StartViewController+OSCTCPConnectionDelegate.swift
//  vcam
//
//  Created by Brian Smith on 12/19/20.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import Foundation
import UIKit

extension StartViewController : OSCTCPConnectionDelegate {
    
    func oscConnectionDidConnect(_ connection: OSCTCPConnection) {
        Log.info("Connected to \(connection.host):\(connection.port)")
    }

    func oscConnectionDidDisconnect(_ connection: OSCTCPConnection, withError err: Error?) {

        DispatchQueue.main.async {

            self.hideConnectingAlertView() {
                if let e = err {
                    Log.info("\(e.localizedDescription)")

                    let errorAlert = UIAlertController(title: "Error", message: "Couldn't connect : \(e.localizedDescription)", preferredStyle: .alert)
                    errorAlert.addAction(UIAlertAction(title: "OK", style: .default, handler: { _ in
                        self.hideConnectingView() {}
                    }))
                    self.present(errorAlert, animated:true)
                    
                } else {
                    Log.info("Disconnected")
                }
            }
        }
    }

    func oscConnection(_ connection: OSCTCPConnection, didReceivePacket packet: OSCPacket) {

        if let msg = packet as? OSCPacketMessage {

            switch msg.addressPattern {

            case OSCAddressPattern.rsHello.rawValue:
                if let args = msg.arguments {
                    if args.count == 1,
                        case let OSCArgument.string(version) = args[0] {
                        connection.send(OSCAddressPattern.rsHello, arguments: [ OSCArgument.string(version) ])
                    }
                }

            case OSCAddressPattern.rsChannelList.rawValue:
                connection.send(OSCAddressPattern.rsChangeChannel, arguments: [ OSCArgument.string("FRemoteSessionImageChannel"), OSCArgument.string("Write"), OSCArgument.int32(1)] )
                connection.send(OSCAddressPattern.rsChangeChannel, arguments: [ OSCArgument.string("FRemoteSessionInputChannel"), OSCArgument.string("Read"), OSCArgument.int32(1)] )
                
                DispatchQueue.main.async {
                    self.hideConnectingAlertView {
                        self.performSegue(withIdentifier: "showVideoView", sender: self)
                   }
               }

            default:
                // ignore everything else -- we will handle them in VideoViewController though
                break
            }
        }
    }

}
