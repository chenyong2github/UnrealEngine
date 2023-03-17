//
//  VideoViewController+StreamingConnectionDelegate.swift
//  vcam
//
//  Created by Brian Smith on 6/28/22.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import Foundation
import UIKit

extension VideoViewController : StreamingConnectionDelegate {

    func streamingConnectionDidConnect(_ connection: StreamingConnection) {
        Log.info("StreamingConnection \(connection.name) did connect to \(connection.destination)")
        DispatchQueue.main.async {
            self.showReconnecting(false, animated: true)
        }
    }
    
    func streamingConnection(_ connection: StreamingConnection, didDisconnectWithError err: Error?) {
        if let e = err {
            Log.info("StreamingConnection \(connection.name) disconnected: \(e.localizedDescription)")
        } else {
            Log.info("StreamingConnection \(connection.name) disconnected.")
        }

        DispatchQueue.main.async {

            if self.dismissOnDisconnect {
                self.forceDisconnectAndDismiss()
            } else {
                self.reconnect()
            }
        }

    }
    
    func streamingConnection(_ connection: StreamingConnection, requestsTextEditWithContents contents: String, handler: @escaping (Bool, String?) -> Void) {
        
        DispatchQueue.main.async {
            let alert = UIAlertController(title: nil, message: contents.count == 0 ? "Enter the value:" : "Update the value:", preferredStyle: .alert)
            alert.addTextField()
            alert.textFields![0].text = contents
            alert.addAction(UIAlertAction(title: Localized.buttonOK(), style: .default) {_ in
                handler(true, alert.textFields![0].text!)
            })
            alert.addAction(UIAlertAction(title: Localized.buttonCancel(), style: .cancel) {_ in
                handler(false, nil)
            })

            self.present(alert, animated: true)
        }
    }
    
    func streamingConnection(_ connection: StreamingConnection, requestStreamerSelectionWithStreamers streamers: Array<String>, handler: @escaping (String) -> Void) {
        self.pickerData = streamers;
        self.selectedStreamer = streamers[0];
        DispatchQueue.main.async {
            self.hideConnectingAlertView() {
                let alert = UIAlertController(title: "Select Streamer", message: "\n\n\n\n\n\n", preferredStyle: .alert)
                
                let picker = UIPickerView(frame: CGRect(x: 5, y: 20, width: 250, height: 140))
                picker.dataSource = self;
                picker.delegate = self;
                alert.view.addSubview(picker)

                alert.addAction(UIAlertAction(title: "OK", style: .default) {_ in
                    handler(self.selectedStreamer)
                })
                
                self.present(alert, animated:true)
            }
        }
    }
    
    func streamingConnection(_ connection: StreamingConnection, receivedGamepadResponse: UInt8) {
        self.controllerResponseReceived(controllerIndex: receivedGamepadResponse)
    }
}

