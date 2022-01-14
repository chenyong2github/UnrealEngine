//
//  StartViewController+UELogDelegate.swift
//  vcam
//
//  Created by Brian Smith on 12/4/20.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import Foundation

extension StartViewController : UELogDelegate {
    
    func ueLogMessage(_ message: String!) {
        Log.info(message)
    }
    
}
