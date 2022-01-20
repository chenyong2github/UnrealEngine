//
//  ViewController.swift
//  vcam
//
//  Created by Brian Smith on 8/8/20.
//  Copyright Epic Games, Inc. All Rights Reserved.
//

import UIKit
import MetalKit
import ARKit
import Easing

class VideoViewController : BaseViewController {

    enum VideoDecoderType {
        
        case jpeg
        case h264
    }
    
    var demoMode : Bool {
        liveLink == nil
    }

    private var displayLink: CADisplayLink?

    @IBOutlet weak var headerView : HeaderView!
    @IBOutlet weak var headerViewTopConstraint : NSLayoutConstraint!
    var headerViewY : CGFloat = 0
    var headerViewHeight : CGFloat = 0
    var headerViewTopConstraintStartValue : CGFloat = 0
    var headerPanGestureRecognizer : UIPanGestureRecognizer!
    var headerPullDownGestureRecognizer : UIScreenEdgePanGestureRecognizer!

    var oscConnection : OSCTCPConnection?

    weak var liveLink : LiveLink?
    private var decoder : VideoDecoder?
    var dismissOnDisconnect = false
    
    var stats = Array<(Date,Int)>()
    var statsTimer : Timer?
    
    private var currentTouches = Array<UITouch?>()
    
    var relayTouchEvents = true {
        didSet {

            // if we are stopping touch events sent to OSC, then we need to send an "ended" event
            // for all of the current touches so the UI doesn't think it's still occurring
            if relayTouchEvents == false {

                for i in 0..<self.currentTouches.count {
                    if let t = self.currentTouches[i] {
                        self.sendOSCAddressPattern(OSCAddressPattern.touchEnded, point: t.location(in: self.arView), finger: i, force: 0);
                    }
                }
                self.currentTouches.removeAll()

            }
            
        }
    }

    @IBOutlet weak var arView : ARSCNView!

    @IBOutlet weak var reconnectingBlurView : UIVisualEffectView!

    @IBOutlet weak var demoModeBlurView : UIVisualEffectView!
    
    var mtlTextureCache : CVMetalTextureCache?
    private var mtlCommandQueue : MTLCommandQueue?
    var mtlPassthroughPipelineState : MTLRenderPipelineState?
    var mtlVertexBuffer : MTLBuffer?
    var mtlNoSignalTexture : MTLTexture?
    var mtlDecodedTexture : MTLTexture?

    var pixelBufferSize : CGSize? {
        didSet {
            if let sz = pixelBufferSize {
                pixelBufferAspectRatio = Float(sz.width / sz.height)
            }
        }
    }
    var pixelBufferAspectRatio : Float?
    var _decodedPixelBufferMutex : pthread_mutex_t!
    var _decodedPixelBuffer : CVMetalTexture?
    var decodedPixelBuffer : CVPixelBuffer? {
        get {
            pthread_mutex_lock(&_decodedPixelBufferMutex)
            defer { pthread_mutex_unlock(&_decodedPixelBufferMutex) }
            return _decodedPixelBuffer
        }
        set {
            pthread_mutex_lock(&_decodedPixelBufferMutex)
            defer { pthread_mutex_unlock(&_decodedPixelBufferMutex) }
            _decodedPixelBuffer = newValue
        }
    }
    func takeDecodedPixelBuffer() -> CVPixelBuffer? {
        pthread_mutex_lock(&_decodedPixelBufferMutex)
        defer { pthread_mutex_unlock(&_decodedPixelBufferMutex) }
        let pb = _decodedPixelBuffer
        _decodedPixelBuffer = nil
        return pb
    }


    var pingTimer : Timer?
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        
        var attr: pthread_mutexattr_t = pthread_mutexattr_t()
        pthread_mutexattr_init(&attr)
        _decodedPixelBufferMutex = pthread_mutex_t()
        pthread_mutex_init(&_decodedPixelBufferMutex, &attr);
    }
    
    deinit {
        pthread_mutex_destroy(&_decodedPixelBufferMutex)
    }

    override var prefersHomeIndicatorAutoHidden: Bool {
        return true
    }
    
    override var prefersStatusBarHidden: Bool {
        return true
    }
    
    override var preferredScreenEdgesDeferringSystemGestures: UIRectEdge {
        return [.top]
    }
    
    @objc func applicationDidBecomeActive(notification: NSNotification) {
        reconnect()
    }
    @objc func applicationDidEnterBackground(notification: NSNotification) {
        disconnect()
    }
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        NotificationCenter.default.addObserver(self, selector: #selector(applicationDidBecomeActive), name: UIApplication.willEnterForegroundNotification, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(applicationDidEnterBackground), name: UIApplication.didEnterBackgroundNotification, object: nil)
        
        self.headerView.start()
        headerViewHeight = self.headerView.frame.height

        headerPanGestureRecognizer = UIPanGestureRecognizer(target: self, action: #selector(handleHeaderPanGesture))
        self.headerView.addGestureRecognizer(headerPanGestureRecognizer)

        headerPullDownGestureRecognizer = UIScreenEdgePanGestureRecognizer(target: self, action: #selector(handleHeaderPanGesture))
        headerPullDownGestureRecognizer.edges = [ .top ]
        self.view.addGestureRecognizer(headerPullDownGestureRecognizer)
        
        headerPanGestureRecognizer.require(toFail: headerPullDownGestureRecognizer)
        
        self.demoModeBlurView.isHidden = !self.demoMode
        showReconnecting(false, animated: false)
        
        let config = ARWorldTrackingConfiguration()

        config.worldAlignment = .gravity
        
        arView.session.run(config, options: [ .resetTracking ] )
        arView.session.delegate = self
        arView.delegate = self
        
        if self.demoMode {
            arView.automaticallyUpdatesLighting = true
            arView.debugOptions = [ .showWorldOrigin ]

            let floor = SCNNode(geometry: SCNPlane(width: 14, height: 14))
            floor.rotation = SCNVector4(1, 0, 0, GLKMathDegreesToRadians(-90))
            floor.position = SCNVector3(0, -1, 0)
            floor.geometry?.firstMaterial?.diffuse.contents = UIImage(named: "checkerboard")
            floor.geometry?.firstMaterial?.diffuse.contentsTransform = SCNMatrix4MakeScale(50, 50, 0)
            floor.geometry?.firstMaterial?.diffuse.wrapS = .repeat
            floor.geometry?.firstMaterial?.diffuse.wrapT = .repeat
            arView.scene.rootNode.addChildNode(floor)

            for x in 0...1 {
                for z in 0...1 {
                    let box = SCNNode(geometry: SCNBox(width: 1, height: 1, length: 1, chamferRadius: 0))
                    box.position = SCNVector3(-5.0 + Double(x) * 10.0, -0.5, -5.0 + Double(z) * 10.0)
                    box.rotation = SCNVector4(0,1,0, Float.random(in: 0...3.14))
                    for i in 0...5 {
                        let mat = SCNMaterial()
                        mat.diffuse.contents = UIImage(named: "UnrealLogo")
                        if i == 0 {
                            box.geometry?.firstMaterial = mat
                        } else {
                            box.geometry?.materials.append(mat)
                        }
                    }
                    arView.scene.rootNode.addChildNode(box)
                }
            }

        } else {
            arView.scene.background.contents = UIColor.black
        }
        
        self.initMetal(arView)
        
        let coachingOverlayView = ARCoachingOverlayView()
        
        self.view.insertSubview(coachingOverlayView, belowSubview: self.reconnectingBlurView)
        coachingOverlayView.layout(.left, .right, .top, .bottom, to: self.arView)
        coachingOverlayView.goal = .tracking
        coachingOverlayView.session = self.arView.session
        coachingOverlayView.activatesAutomatically = true
        coachingOverlayView.delegate = self

        reconnect()

    }
    
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        UIApplication.shared.isIdleTimerDisabled = true
    }
    
    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        UIApplication.shared.isIdleTimerDisabled = false
    }
    
    func showReconnecting(_ visible : Bool, animated: Bool) {

        if (self.reconnectingBlurView.effect != nil && visible) ||
            (self.reconnectingBlurView.effect == nil && !visible) {
            return
        }

        UIView.animate(withDuration: 0.0, animations: {
            self.reconnectingBlurView.effect = visible ? UIBlurEffect(style: UIBlurEffect.Style.dark) : nil
        })

        if visible {
            showConnectingAlertView(mode : .reconnecting) {
                self.exit()
            }
        } else {
            hideConnectingAlertView() {}
        }
    }

    func exit() {
        
        // if still connected, we need to send a disconnect/goodbye, then dismiss the view controller when
        // the goodbye msg is sent.
        // otherwise, we can call disconnect (which tears down some objects) and dismiss this VC immediately.
        if self.oscConnection?.isConnected ?? false {
            dismissOnDisconnect = true
            disconnect()
        } else {
            disconnect()
            self.presentingViewController?.dismiss(animated: true, completion: nil)
        }
    }
    
    func reconnect() {
        
        decoder = JPEGVideoDecoder()

        pingTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true, block: { (t) in
            
            if let conn = self.oscConnection {
                if conn.isConnected {
                    conn.send(OSCAddressPattern.ping)
                } else {
                    conn.reconnect()
                }
            }
        })
        
        statsTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true, block: { t in
            self.updateStreamingStats()
        })
        
        oscConnection?.reconnect()
    }
    
    func disconnect() {
        
        pingTimer?.invalidate()
        pingTimer = nil
        
        statsTimer?.invalidate()
        statsTimer = nil
        
        oscConnection?.disconnect()
        
        self.decoder = nil
    }

    func makeMTLCommandBuffer() -> MTLCommandBuffer? {
        return mtlCommandQueue?.makeCommandBuffer()
    }
    
    func decode(_ width : Int32, _ height : Int32, _ blob : Data) {

        let sz = blob.count
        
        self.decoder?.decode(width: width, height: height, data: blob) { (pixelBuffer) in
            
            if let pb = pixelBuffer {
                
                
                DispatchQueue.main.async {

                    self.pixelBufferSize = CGSize(width: CVPixelBufferGetWidth(pb), height: CVPixelBufferGetHeight(pb))
                    self.stats.append((Date(), sz))
                    
                    self.headerViewY = self.headerView.layer.presentation()?.frame.origin.y ?? 0
                }
                
                self.decodedPixelBuffer = pb
            }
            
        }
    }
    
    func updateStreamingStats() {
        
        let now = Date()
        var countToRemove = 0
        var totalBytesLastSecond = 0
        for item in stats {
            if now.timeIntervalSince(item.0) > 1 {
                countToRemove += 1
            } else {
                totalBytesLastSecond += item.1
            }
        }

        self.stats.removeFirst(countToRemove)

        var fps : Float?
        if stats.count > 1,
           let first = stats.first,
           let last = stats.last {
            
            fps = Float(stats.count - 1) / Float(last.0.timeIntervalSince(first.0))
        }
        
        var str = ""
        
        if let sz = self.pixelBufferSize {
            str += "\(Int(sz.width))x\(Int(sz.height))"
        }
        
        if fps != nil {
            str += String(format: " %0.2f FPS", fps!)
        }

        
        if !str.isEmpty {
            str += " • "
        }
        
        ByteCountFormatter().string(fromByteCount: Int64(totalBytesLastSecond))
        str += ByteCountFormatter().string(fromByteCount: Int64(totalBytesLastSecond)) + "/sec"

        self.headerView.stats = str
    }
    
    
    func initMetal(_ arView : ARSCNView) {
        
        guard let mtlDevice = arView.device else {
            Log.error("MTLCreateSystemDefaultDevice failed!")
            return
        }

        // Load all the shader files with a .metal file extension in the project.
        guard let defaultLibrary = mtlDevice.makeDefaultLibrary() else {
            Log.error("makeDefaultLibrary failed!")
            return
        }
        
        let vertexFunction = defaultLibrary.makeFunction(name: "passthrough_vertex")
        let fragmentFunction = defaultLibrary.makeFunction(name: "basic_texture")

        // Configure a pipeline descriptor that is used to create a pipeline state.
        let pipelineDescriptor = MTLRenderPipelineDescriptor()
        pipelineDescriptor.label = "Passthrough Pipeline";
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = arView.colorPixelFormat;
        pipelineDescriptor.depthAttachmentPixelFormat = arView.depthPixelFormat

        do {
            mtlPassthroughPipelineState = try mtlDevice.makeRenderPipelineState(descriptor: pipelineDescriptor)
        } catch {
            Log.error("makeRenderPipelineState failed : \(error.localizedDescription)")

        }
        
        mtlCommandQueue = mtlDevice.makeCommandQueue()
        
        let vertices : [Float] = [ -1.0,  1.0, 0.0, 1.0,
                                   -1.0, -1.0, 0.0, 0.0,
                                    1.0,  1.0, 1.0, 1.0,
                                    1.0, -1.0, 1.0, 0.0 ]

        mtlVertexBuffer = mtlDevice.makeBuffer(bytes: vertices, length: MemoryLayout<Float>.size * vertices.count, options: .storageModeShared)
        

        let textureLoader = MTKTextureLoader(device: mtlDevice)
        
        do {
            mtlNoSignalTexture = try textureLoader.newTexture(name: "no_signal", scaleFactor: 1.0, bundle: nil, options: nil)
        } catch {
            Log.error("couldn't load the no_signal texture: \(error.localizedDescription)")
        }
        
        let status = CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, mtlDevice, nil, &mtlTextureCache)
        Log.info("CVMetalTextureCacheCreate : \(status)")
        
    }
    
    func sendOSCAddressPattern(_ pattern : OSCAddressPattern, point : CGPoint, finger : Int, force : CGFloat) {

        guard let textureAspectRatio = self.pixelBufferAspectRatio else { return }

        // we need to adjust for the size of the top toolbar in the view : this toolbar is *over* the
        // rendering/arView : if we resize the arView (which is a SCNView) with animated constraints,
        // there are all kinds of visual side effects.
        let toolbarHeightInView = (self.headerViewY + self.headerViewHeight)
        
        let viewportSize = CGSize(width: self.arView.frame.width, height: self.arView.frame.height - toolbarHeightInView)
        let viewportAspectRatio = Float(viewportSize.width) / Float(viewportSize.height)

        // offset the touch point with respect to the top toolbar
        let pointInViewport = CGPoint(x: point.x, y: point.y - toolbarHeightInView)
        var normalizedPoint = CGPoint(x: pointInViewport.x / viewportSize.width, y: pointInViewport.y / viewportSize.height)

        
        if textureAspectRatio > viewportAspectRatio {
            
            normalizedPoint.y = (((normalizedPoint.y * 2.0 - 1.0) * CGFloat(textureAspectRatio / viewportAspectRatio)) + 1.0) / 2.0;

        } else if textureAspectRatio < viewportAspectRatio {

            normalizedPoint.x = (((normalizedPoint.x * 2.0 - 1.0) * CGFloat(viewportAspectRatio / textureAspectRatio)) + 1.0) / 2.0;
        }
        
        // reject any touches that aren't in the 0.0 -> 1.0 of the remote image
        if (normalizedPoint.x < 0.0) ||
            (normalizedPoint.x > 1.0) ||
            (normalizedPoint.y < 0.0) ||
            (normalizedPoint.y > 1.0) {
            return
        }

        //Log.info("\(pattern.rawValue) : \(normalizedPoint.x), \(normalizedPoint.y)")
        let data = OSCUtility.ueTouchData(point: normalizedPoint, finger: finger, force: force)
        oscConnection?.send(pattern, arguments: [ OSCArgument.blob(data) ])
    }
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {

        guard relayTouchEvents else { return }
        
        // for each touch, we need to maintain an index to send via OSC, so that UE
        // can correctly map between began/moved. The currentTouches array will maintain
        // the indices for each touch (keeping some nil if needed) and clean up / compress
        // the array when a touch ends.
        
        for touch in touches {

            // insert this new touch into the array, using the first available slot (a nil entry)
            // or append to the end.
            var fingerIndex = 0
            
            for i in 0...self.currentTouches.count {
                fingerIndex = i
                if i == self.currentTouches.count {
                    self.currentTouches.append(touch)
                    break
                } else if self.currentTouches[i] == nil {
                    self.currentTouches[i] = touch
                    break
                }
            }
            
            self.sendOSCAddressPattern(OSCAddressPattern.touchStarted, point: touch.location(in: self.arView), finger: fingerIndex, force: touch.force)
        }
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {

        guard relayTouchEvents else { return }

        for touch in touches {
            if let fingerIndex = self.currentTouches.firstIndex(of: touch) {
                self.sendOSCAddressPattern(OSCAddressPattern.touchMoved, point: touch.location(in: self.arView), finger: fingerIndex, force: touch.force)
            }
        }

    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {

        guard relayTouchEvents else { return }
        
        for touch in touches {
            
            if let fingerIndex = self.currentTouches.firstIndex(of: touch) {
                self.sendOSCAddressPattern(OSCAddressPattern.touchEnded, point: touch.location(in: self.arView), finger: fingerIndex, force: touch.force)
                
                // this touch is ended, set to nil
                self.currentTouches[fingerIndex] = nil
            }
        }
        
        // compress the array : remove all nils from the end
        if let lastNonNilIndex = self.currentTouches.lastIndex(where: { $0 != nil }) {
            self.currentTouches.removeSubrange(lastNonNilIndex..<self.currentTouches.count)
        } else {
            self.currentTouches.removeAll()
        }
    }

    @objc func handleHeaderPanGesture(_ gesture : UIGestureRecognizer) {

        guard let panGesture = gesture as? UIPanGestureRecognizer else { return }
        
        switch gesture.state {
        case .began, .changed, .ended:
            updateHeaderConstraint(gesture : panGesture)
        default:
            break
        }
    }
    
    func updateHeaderConstraint(gesture pan : UIPanGestureRecognizer) {

        // if gesture is starting, keep track of the start constraint value
        if pan.state == .began {
            headerViewTopConstraintStartValue = headerViewTopConstraint.constant
        }

        if pan.state == .began || pan.state == .changed {

            // use the correct view for the given gesture's translate
            let translation = pan.translation(in: pan is UIScreenEdgePanGestureRecognizer ? view : headerView)

            // add the translation to the start value
            headerViewTopConstraint.constant = headerViewTopConstraintStartValue + translation.y
            
            // if the constant is now over 1, we will stretch downward. We use a sine easeOut to give
            // a rubber-band effect
            if headerViewTopConstraint.constant > 0 {
                let t = Float(headerViewTopConstraint.constant) / Float(UIScreen.main.bounds.height)
                let t2 = CGFloat(Curve.sine.easeOut(t))
                
                //og.info("\(headerViewTopConstraint.constant) -> \(t) -> \(t2)")
                
                headerViewTopConstraint.constant = t2 * headerView.frame.height * 2.0
            }
        } else if pan.state == .ended {
            
            // gesture is ending, we will animate to either visible or hidden
            let newTopY : CGFloat = headerViewTopConstraint.constant > -(headerView.frame.height * 0.25) ? 0.0 : (-self.headerView.frame.height - 1.0)
            
            // we also need a display link here to properly communicate the new headerY from the presentation layer to
            // the metal renderer.
            self.displayLink = CADisplayLink(
              target: self, selector: #selector(displayLinkDidFire)
            )
            self.displayLink!.add(to: .main, forMode: .common)

            UIView.animate(withDuration: 0.2) {
                // animate to the new position
                self.headerViewTopConstraint.constant = newTopY
                self.view.layoutIfNeeded()
            } completion: { _ in
                // all done, we kill the display link
                self.displayLink?.invalidate()
                self.displayLink = nil
            }
        }
    }
    
    @objc func displayLinkDidFire(_ displayLink: CADisplayLink) {

        // save the actual animating value of the header view's Y position
        self.headerViewY = self.headerView.layer.presentation()?.frame.minY ?? 0
    }
}

extension VideoViewController : HeaderViewDelegate {
    
    func headerViewExitButtonTapped(_ headerView : HeaderView) {
     
        let disconnectAlert = UIAlertController(title: nil, message: NSLocalizedString("Disconnect from the remote session?", comment: "Prompt disconnect from a remote session."), preferredStyle: .alert)
        disconnectAlert.addAction(UIAlertAction(title: NSLocalizedString("Disconnect", comment: "Button to disconnect from a UE instance"), style: .destructive, handler: { _ in
            self.exit()
        }))
        disconnectAlert.addAction(UIAlertAction(title: Localized.buttonCancel(), style: .cancel))
        self.present(disconnectAlert, animated:true)
    }

    func headerViewLogButtonTapped(_ headerView : HeaderView) {
     
        performSegue(withIdentifier: "showLog", sender: headerView)
    }
}


