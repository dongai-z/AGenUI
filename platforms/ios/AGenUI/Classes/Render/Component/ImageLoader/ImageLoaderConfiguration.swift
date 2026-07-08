//
//  ImageLoaderConfiguration.swift
//  AGenUI
//
// Created on 2026/4/9.
//

import UIKit

/// Image loader global configuration
@objc public class ImageLoaderConfiguration: NSObject {
    
    /// Singleton
    @objc public static let shared = ImageLoaderConfiguration()
    
    private let lock = NSLock()
    private var _loader: ImageLoader

    /// Current image loader
    /// - Replace this property to take effect globally
    /// - Default uses DefaultImageLoader (based on URLSession + memory cache)
    @objc public var loader: ImageLoader {
        get {
            lock.lock()
            let result = _loader
            lock.unlock()
            return result
        }
        set {
            lock.lock()
            _loader = newValue
            lock.unlock()
        }
    }

    /// Private initialization
    private override init() {
        self._loader = DefaultImageLoader()
        super.init()
    }
}
