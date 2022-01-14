//
//  VideoViewController+ARCoachingOverlayViewDelegate.swift
//  vcam
//
//  Created by Brian Smith on 12/6/21.
//  Copyright © 2021 Brian Smith. All rights reserved.
//

import Foundation
import ARKit

extension VideoViewController : ARCoachingOverlayViewDelegate
{
    func coachingOverlayViewWillActivate(_ coachingOverlayView: ARCoachingOverlayView) {
        
        self.relayTouchEvents = false

    }

    
    /**
     This is called when the view has been deactivated, either manually or automatically
     
     @param coachingOverlayView The view that was deactivated
     */
    func coachingOverlayViewDidDeactivate(_ coachingOverlayView: ARCoachingOverlayView) {
        
        self.relayTouchEvents = true
        
    }

}

