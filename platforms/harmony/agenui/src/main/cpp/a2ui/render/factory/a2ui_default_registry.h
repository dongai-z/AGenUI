#pragma once

namespace a2ui {

class ComponentRegistry;

/**
 * @brief Returns the process-lifetime shared registry containing all built-in
 *        component factories.
 *
 * Thread-safe: uses C++11 static local initialization guarantee.
 * The returned registry is read-only after construction and must NOT be
 * modified by callers (do not call registerFactory/setOwnsFactories on it).
 *
 * Lifetime: intentionally leaked (never destroyed) to avoid static
 * destruction order issues — consistent with other process-lifetime
 * singletons in this codebase.
 */
ComponentRegistry& getDefaultFactoryRegistry();

} // namespace a2ui
