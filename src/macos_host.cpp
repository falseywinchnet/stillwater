#include "macos_host.hpp"
#include "macos_audio.hpp"
#include "metal_renderer.hpp"
#include "renderer_verification.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <dispatch/dispatch.h>
#include <fstream>
#include <iostream>
#include <sys/resource.h>

namespace stillwater {
using apple::release;
using apple::send;
using apple::string;
using apple::type;
namespace {
using Clock = std::chrono::steady_clock;
struct Host {
    Options options{};
    Scene scene{};
    Renderer renderer{};
    Audio audio{};
    id app{nil}, window{nil}, view{nil}, layer{nil}, delegate{nil}, status{nil};
    id pause_item{nil}, sound_item{nil}, desktop_item{nil}, pointer_item{nil};
    dispatch_source_t frames{nullptr}, quit_timer{nullptr};
    Clock::time_point began{Clock::now()};
    double paused_time{}, pause_started{};
    bool paused{}, occluded{}, sleeping{}, pointer_mode{true}, audio_ready{}, quitting{}, captured{},
        smoke_tapped{}, renderer_failed{};
    double width{1100}, height{700};
    std::uint64_t taps{}, pointer_reactions{};
    CGPoint last_pointer{};
    double pointer_time{};
    bool pointer_valid{};
    double wall_time() const {
        return std::chrono::duration<double>(Clock::now() - began).count();
    }
    double scene_time() const {
        return (paused ? pause_started : wall_time()) - paused_time;
    }
};
Host* host = nullptr;
void start_frames();
void frame(void*);
void stop_source(dispatch_source_t& source) {
    if (source == nullptr)
        return;
    dispatch_source_cancel(source);
    dispatch_release(source);
    source = nullptr;
}
CGRect object_rect(id object, const char* selector) {
#if defined(__x86_64__)
    CGRect result{};
    using Function = void (*)(CGRect*, id, SEL);
    const Function invoke = reinterpret_cast<Function>(objc_msgSend_stret);
    invoke(&result, object, sel_registerName(selector));
    return result;
#else
    return send<CGRect>(object, selector);
#endif
}
void resize_scene() {
    Host& state = *host;
    const CGRect bounds = object_rect(state.view, "bounds");
    state.width = std::max(1.0, bounds.size.width);
    state.height = std::max(1.0, bounds.size.height);
    send<void>(state.layer, "setFrame:", bounds);
    const double scale = std::min(1.0, 1600.0 / state.width) * state.options.render_scale;
    state.renderer.resize(static_cast<unsigned int>(state.width * scale),
                          static_cast<unsigned int>(state.height * scale));
    if (state.paused)
        state.renderer.draw(state.scene, state.scene_time());
}
void update_menu() {
    Host& state = *host;
    const char* title = state.options.desktop
                            ? (state.pointer_mode ? "Stillwater — Interactive desktop"
                                              : "Stillwater — Desktop background")
                            : "Stillwater";
    const std::string window_title = std::string(title) + (state.paused ? " — Paused" : "");
    send<void>(state.window, "setTitle:", string(window_title.c_str()));
    send<void>(state.pause_item,
               "setTitle:", string(state.paused ? "Resume the aquarium" : "Pause the aquarium"));
    send<void>(state.sound_item, "setState:", state.audio.enabled() ? 1L : 0L);
    send<void>(state.desktop_item, "setState:", state.options.desktop ? 1L : 0L);
    send<void>(state.pointer_item, "setState:", state.pointer_mode ? 1L : 0L);

}
void apply_placement() {
    Host& state = *host;
    if (state.options.desktop) {
        const CGRect screen = object_rect(send<id>(type("NSScreen"), "mainScreen"), "frame");
        send<void>(state.window, "setStyleMask:", 128UL);
        send<void>(
            state.window, "setLevel:",
            static_cast<long>(CGWindowLevelForKey(kCGDesktopWindowLevelKey) + 1));
        send<void>(state.window, "setCollectionBehavior:", 1UL | 16UL | 64UL);
        send<void>(state.window, "setFrame:display:", screen, static_cast<BOOL>(YES));
        send<void>(state.window,
                   "setIgnoresMouseEvents:", static_cast<BOOL>(YES));
        send<void>(state.window, "orderFrontRegardless");
    } else {
        send<void>(state.window, "setStyleMask:", 15UL | 128UL);
        send<void>(state.window, "setLevel:", 0L);
        send<void>(state.window, "setCollectionBehavior:", 0UL);
        send<void>(state.window, "setIgnoresMouseEvents:", static_cast<BOOL>(NO));
        const CGRect screen = object_rect(send<id>(type("NSScreen"), "mainScreen"), "visibleFrame");
        const double width = std::min(1180.0, screen.size.width - 80),
                     height = std::min(760.0, screen.size.height - 90);
        send<void>(state.window, "setFrame:display:",
                   CGRectMake(screen.origin.x + (screen.size.width - width) / 2,
                              screen.origin.y + (screen.size.height - height) / 2, width, height),
                   static_cast<BOOL>(YES));
        send<void>(state.window, "orderFrontRegardless");
    }
    state.pointer_valid = false;
    // The aquarium remains nonactivating; controls live in the menu bar.
    resize_scene();
    update_menu();
}
void print_metrics() {
    Host& state = *host;
    const RenderStatistics& render = state.renderer.statistics();
    const Statistics& scene = state.scene.statistics();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const double cpu =
        static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
        static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
    std::ofstream file{};
    if (!state.options.metrics.empty())
        file.open(state.options.metrics);
    std::ostream& output = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;
    output << "{\n  \"objects\": " << state.scene.objects().size()
           << ",\n  \"actors\": " << state.scene.actors().size()
           << ",\n  \"frames\": " << render.frames << ",\n  \"wall_seconds\": " << state.wall_time()
           << ",\n  \"process_cpu_seconds_including_startup\": " << cpu
           << ",\n  \"peak_rss_bytes\": " << usage.ru_maxrss
           << ",\n  \"submit_wall_seconds\": " << render.cpu_submit_seconds
           << ",\n  \"static_upload_bytes\": " << render.static_upload_bytes
           << ",\n  \"material_storage_bytes\": " << render.material_storage_bytes
           << ",\n  \"shadow_storage_bytes\": " << render.shadow_storage_bytes
           << ",\n  \"geometry_storage_bytes\": " << render.geometry_storage_bytes
           << ",\n  \"gpu_executed_seconds\": " << render.gpu_seconds
           << ",\n  \"gpu_timed_frames\": " << render.gpu_timed_frames
           << ",\n  \"gpu_shadow_frame_seconds\": " << render.gpu_shadow_frame_seconds
           << ",\n  \"gpu_shadow_frames\": " << render.gpu_shadow_frames
           << ",\n  \"gpu_reuse_frame_seconds\": " << render.gpu_reuse_frame_seconds
           << ",\n  \"gpu_reuse_frames\": " << render.gpu_reuse_frames
           << ",\n  \"conv_storage_bytes\": " << render.conv_storage_bytes
           << ",\n  \"conv_source_triangles\": " << render.conv_source_triangles
           << ",\n  \"conv_boundary_triangles\": " << render.conv_boundary_triangles
           << ",\n  \"conv_static_updates\": " << render.conv_static_updates
           << ",\n  \"conv_moving_updates\": " << render.conv_moving_updates
           << ",\n  \"compact_camera\": " << (state.options.compact_camera ? "true" : "false")
           << ",\n  \"record_shading\": " << (state.options.record_shading ? "true" : "false")
           << ",\n  \"specialize_foliage\": " << (state.options.specialize_foliage ? "true" : "false")
           << ",\n  \"camera_records\": " << render.camera_records
           << ",\n  \"camera_registration_builds\": " << render.camera_registration_builds
           << ",\n  \"camera_acquisition_command_gpu_seconds\": " << render.camera_acquisition_command_gpu_seconds
           << ",\n  \"msaa_samples\": " << state.options.samples
           << ",\n  \"conv_fast\": " << (state.options.conv_fast ? "true" : "false")
           << ",\n  \"render_scale\": " << state.options.render_scale
           << ",\n  \"requested_fps\": " << state.options.fps
           << ",\n  \"actor_upload_bytes\": " << render.actor_upload_bytes
           << ",\n  \"geometry_builds\": " << scene.geometry_builds
           << ",\n  \"instance_patch_bytes\": " << render.instance_patch_bytes
           << ",\n  \"static_shadow_builds\": " << render.static_shadow_builds
           << ",\n  \"dynamic_shadow_frames\": " << render.dynamic_shadow_frames
           << ",\n  \"visibility_builds\": " << render.visibility_builds
           << ",\n  \"visibility_reuses\": " << render.visibility_reuses
           << ",\n  \"light_visibility_builds\": " << render.light_visibility_builds
           << ",\n  \"light_visibility_reuses\": " << render.light_visibility_reuses
           << ",\n  \"retained_bytes\": " << render.retained_bytes
           << ",\n  \"transient_backing_bytes\": " << render.transient_backing_bytes
           << ",\n  \"memoryless_targets\": " << (render.memoryless_targets ? "true" : "false")
           << ",\n  \"gpu_allocated_bytes\": " << state.renderer.allocated_gpu_bytes()
           << ",\n  \"render_width\": " << render.render_width
           << ",\n  \"render_height\": " << render.render_height
           << ",\n  \"fixed_camera_draws\": " << render.fixed_camera_draws
           << ",\n  \"moving_camera_draws\": " << render.moving_camera_draws
           << ",\n  \"actor_edits\": " << scene.actor_edits << ",\n  \"taps\": " << state.taps
           << ",\n  \"pointer_reactions\": " << state.pointer_reactions
           << ",\n  \"paused\": " << (state.paused ? "true" : "false")
           << ",\n  \"desktop\": " << (state.options.desktop ? "true" : "false")
           << ",\n  \"menu_bar_visible\": " << (send<BOOL>(state.status, "isVisible") ? "true" : "false")
           << ",\n  \"aquarium_accepts_keyboard\": "
           << (send<BOOL>(state.window, "canBecomeKeyWindow") ? "true" : "false")

           << ",\n  \"aquarium_is_key\": "
           << (send<BOOL>(state.window, "isKeyWindow") ? "true" : "false")

           << ",\n  \"window_level\": " << send<long>(state.window, "level")
           << ",\n  \"ignores_mouse\": "
           << (send<BOOL>(state.window, "ignoresMouseEvents") ? "true" : "false")
           << ",\n  \"raster_mode\": \""
           << (state.renderer.retained() ? "retained fixed visibility and lighting"
                                         : "full-frame fallback")
           << "\"\n}\n";
}
void request_quit() {
    Host& state = *host;
    if (state.quitting)
        return;
    state.quitting = true;
    stop_source(state.frames);
    stop_source(state.quit_timer);
    state.audio.set_ambience(false);
    if (!state.renderer.finish_pending(true)) {
        state.renderer_failed = true;
        std::cerr << "Renderer: " << state.renderer.error() << '\n';
    }
    print_metrics();
    send<void>(state.app, "stop:", static_cast<id>(nil));
    id event = send<id>(type("NSEvent"),
                        "otherEventWithType:location:modifierFlags:timestamp:windowNumber:context:"
                        "subtype:data1:data2:",
                        15UL, CGPointZero, 0UL, 0.0, 0L, static_cast<id>(nil),
                        static_cast<short>(0), 0L, 0L);
    send<void>(state.app, "postEvent:atStart:", event, static_cast<BOOL>(YES));
}
void quit_timer(void*) {
    request_quit();
}
void toggle_pause(id, SEL, id) {
    Host& state = *host;
    if (!state.paused) {
        state.pause_started = state.wall_time();
        state.paused = true;
        stop_source(state.frames);
        state.audio.set_ambience(false);
    } else {
        state.paused_time += state.wall_time() - state.pause_started;
        state.paused = false;
        start_frames();
        if (!state.options.muted)
            state.audio.set_ambience(true);
    }
    update_menu();
}
void toggle_sound(id, SEL, id) {
    Host& state = *host;
    state.options.muted = !state.options.muted;
    state.audio.set_ambience(!state.options.muted && !state.paused);
    update_menu();
}
void toggle_desktop(id, SEL, id) {
    (*host).options.desktop = !(*host).options.desktop;
    apply_placement();
}
void toggle_pointer(id, SEL, id) {
    (*host).pointer_mode = !(*host).pointer_mode;
    (*host).pointer_valid = false;
    update_menu();
}
void show_preview(id, SEL, id) {
    (*host).options.desktop = false;
    apply_placement();
}
void quit_action(id, SEL, id) {
    // Leave native button/menu tracking before stopping the application run loop.
    dispatch_async_f(dispatch_get_main_queue(), nullptr, &quit_timer);
}
void resized(id, SEL, id) {
    if (host != nullptr && (*host).layer != nil)
        resize_scene();
}
BOOL reject_keyboard(id, SEL) {
    return NO;
}
BOOL first_mouse(id, SEL, id) {
    return YES;
}
BOOL close_window(id, SEL, id) {
    (*host).options.desktop = true;
    apply_placement();
    return NO;
}
void tap_at(double x, double y) {
    Host& state = *host;
    const TapResult result = state.scene.tap(x, y, state.width / state.height, state.scene_time());
    ++state.taps;
    if (!state.options.muted)
        state.audio.tap(x * 2 - 1);
    std::cout << "tap " << x << ' ' << y << ": " << result.count << " nearby creatures; revision "
              << state.scene.statistics().revision << '\n';
    if (state.paused)
        state.renderer.draw(state.scene, state.scene_time());
}
void mouse_down(id, SEL, id event) {
    const CGPoint point = send<CGPoint>(event, "locationInWindow");
    tap_at(point.x / (*host).width, point.y / (*host).height);
}
// Query position only while the visible aquarium animates. Never intercept,
// consume, or replay Finder events; no global event monitor or event tap.
bool exposed_pointer(CGPoint point) {
    const CGWindowID own = static_cast<CGWindowID>(send<long>((*host).window, "windowNumber"));
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenAboveWindow, own);
    if (windows == nullptr)
        return false;
    const CGRect primary = CGDisplayBounds(CGMainDisplayID());
    const CGPoint quartz = CGPointMake(point.x, primary.size.height - point.y);
    bool exposed = true;
    for (CFIndex index = 0; index < CFArrayGetCount(windows); ++index) {
        CFDictionaryRef entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
        CFNumberRef layer_number = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCGWindowLayer));
        CFNumberRef alpha_number = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCGWindowAlpha));
        int layer = 0;
        double alpha = 1;
        if (layer_number != nullptr)
            CFNumberGetValue(layer_number, kCFNumberIntType, &layer);
        if (alpha_number != nullptr)
            CFNumberGetValue(alpha_number, kCFNumberDoubleType, &alpha);
        // Finder's desktop icon layer must remain usable; normal app windows,
        // Dock and menus suppress reactions under their rectangular bounds.
        if (layer < 0 || alpha < 0.01)
            continue;
        CFDictionaryRef bounds = static_cast<CFDictionaryRef>(CFDictionaryGetValue(entry, kCGWindowBounds));
        CGRect rectangle{};
        if (bounds != nullptr && CGRectMakeWithDictionaryRepresentation(bounds, &rectangle) &&
            CGRectContainsPoint(rectangle, quartz)) {
            // Dock owns a full-display, layer-20 desktop helper even when only
            // Finder is visible. It is not the actual (small) Dock panel.
            // Keep blocking the Dock's active fullscreen UI and all ordinary apps.
            if (layer == 20 && CGRectContainsRect(rectangle, primary)) {
                int pid = 0;
                CFNumberRef owner = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCGWindowOwnerPID));
                if (owner != nullptr) CFNumberGetValue(owner, kCFNumberIntType, &pid);
                id application = send<id>(type("NSRunningApplication"), "runningApplicationWithProcessIdentifier:", pid);
                id bundle = send<id>(application, "bundleIdentifier");
                if (send<BOOL>(bundle, "isEqualToString:", string("com.apple.dock")) &&
                    !send<BOOL>(application, "isActive")) {
                    if ((*host).options.trace_pointer)
                        std::cout << "pointer ignored Dock desktop helper" << std::endl;
                    continue;
                }
            }
            if ((*host).options.trace_pointer) {
                int pid = 0;
                CFNumberRef owner = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCGWindowOwnerPID));
                if (owner != nullptr) CFNumberGetValue(owner, kCFNumberIntType, &pid);
                std::cout << "pointer blocked: layer=" << layer << " pid=" << pid
                          << " bounds=" << rectangle.origin.x << ',' << rectangle.origin.y << ','
                          << rectangle.size.width << ',' << rectangle.size.height << std::endl;
            }
            exposed = false;
            break;
        }
    }
    CFRelease(windows);
    return exposed;
}
void sample_pointer() {
    Host& state = *host;
    if (!state.pointer_mode || !state.options.verify_retained.empty())
        return;
    const double time = state.scene_time();
    const double elapsed = time - state.pointer_time;
    if (elapsed < 0.12)
        return;
    const CGPoint screen = send<CGPoint>(type("NSEvent"), "mouseLocation");
    const CGPoint window = send<CGPoint>(state.window, "convertPointFromScreen:", screen);
    const CGPoint point = send<CGPoint>(state.view, "convertPoint:fromView:", window, nil);
    const double speed = state.pointer_valid ?
        std::hypot(point.x - state.last_pointer.x, point.y - state.last_pointer.y) /
        (state.height * elapsed) : 0;
    state.last_pointer = point;
    state.pointer_time = time;
    state.pointer_valid = point.x >= 0 && point.y >= 0 &&
                          point.x < state.width && point.y < state.height;
    const bool exposed = (speed >= 0.03 || state.options.trace_pointer) && exposed_pointer(screen);
    if (state.options.trace_pointer)
        std::cout << "pointer screen=" << screen.x << ',' << screen.y << " view=" << point.x << ',' << point.y
                  << " speed=" << speed << " valid=" << state.pointer_valid << " exposed=" << exposed << std::endl;
    if (!state.pointer_valid || speed < 0.03 || elapsed > 0.5 || !exposed)
        return;
    const TapResult result = state.scene.pointer_motion(point.x / state.width,
        point.y / state.height, state.width / state.height, time, speed);
    state.pointer_reactions += result.count;
}
void sleeping(id, SEL, id) {
    (*host).sleeping = true;
    stop_source((*host).frames);
    (*host).audio.set_ambience(false);
}
void waking(id, SEL, id) {
    (*host).sleeping = false;
    start_frames();
    if (!(*host).options.muted && !(*host).paused)
        (*host).audio.set_ambience(true);
}
void occlusion_changed(id, SEL, id) {
    Host& state = *host;
    state.occluded = (send<unsigned long>(state.window, "occlusionState") & 2UL) == 0;
    if (state.occluded)
        stop_source(state.frames);
    else
        start_frames();
}
void frame(void*) {
    if (host == nullptr)
        return;
    Host& state = *host;
    if (state.quitting || state.paused || state.sleeping || state.occluded)
        return;
    sample_pointer();
    if (state.options.smoke_tap && !state.smoke_tapped && state.scene_time() > 2) {
        const Vec3 position = actor_position(state.scene.actors()[7], state.scene_time());
        const Vec3 screen = project(position, state.width / state.height);
        tap_at(screen.x, screen.y);
        state.smoke_tapped = true;
    }
    const char* capture = nullptr;
    if (!state.options.capture.empty() && !state.captured && state.scene_time() > 3)
        capture = state.options.capture.c_str();
    const bool success = state.renderer.draw(state.scene, state.scene_time(), capture);
    if (!success && !state.renderer.error().empty()) {
        state.renderer_failed = true;
        std::cerr << "Renderer: " << state.renderer.error() << '\n';
        request_quit();
        return;
    }
    if (capture != nullptr && success) {
        state.captured = true;
        std::cout << "Captured " << capture << '\n';
    }
}
void start_frames() {
    Host& state = *host;
    if (state.frames != nullptr || state.paused || state.sleeping || state.occluded ||
        state.quitting)
        return;
    state.frames =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler_f(state.frames, &frame);
    const std::uint64_t interval = 1000000000ULL / state.options.fps;
    dispatch_source_set_timer(state.frames, dispatch_time(DISPATCH_TIME_NOW, 0), interval,
                              4000000ULL);
    dispatch_resume(state.frames);
}
template <typename Function>
void method(Class cls, const char* name, Function function, const char* encoding) {
    class_addMethod(cls, sel_registerName(name), reinterpret_cast<IMP>(function), encoding);
}
id menu_item(id menu, id delegate, const char* title, const char* action) {
    id item = send<id>(send<id>(type("NSMenuItem"), "alloc"),
                       "initWithTitle:action:keyEquivalent:", string(title),
                       sel_registerName(action), string(""));
    send<void>(item, "setTarget:", delegate);
    send<void>(menu, "addItem:", item);
    release(item);
    return item;
}
void install_menu(Host& state) {
    id menu = apple::make("NSMenu");
    send<void>(menu, "setAutoenablesItems:", static_cast<BOOL>(NO));
    menu_item(menu, state.delegate, "Stillwater — living aquarium", "showPreview:");
    send<void>(menu, "addItem:", send<id>(type("NSMenuItem"), "separatorItem"));
    state.desktop_item = menu_item(menu, state.delegate, "On the desktop", "toggleDesktop:");
    menu_item(menu, state.delegate, "Open aquarium window", "showPreview:");
    state.pointer_item = menu_item(menu, state.delegate, "Fish react to pointer", "togglePointer:");
    state.pause_item = menu_item(menu, state.delegate, "Pause the aquarium", "togglePause:");
    state.sound_item =
        menu_item(menu, state.delegate, "Aquarium sound", "toggleSound:");
    send<void>(menu, "addItem:", send<id>(type("NSMenuItem"), "separatorItem"));
    menu_item(menu, state.delegate, "Quit Stillwater", "quit:");
    id bar = send<id>(type("NSStatusBar"), "systemStatusBar");
    state.status = apple::retain(send<id>(bar, "statusItemWithLength:", -1.0));
    id button = send<id>(state.status, "button");
    send<void>(button, "setTitle:", string("◉"));
    send<void>(button, "setToolTip:", string("Stillwater aquarium"));
    send<void>(button, "setAccessibilityLabel:", string("Stillwater"));
    send<void>(state.status, "setMenu:", menu);
    send<void>(state.view, "setMenu:", menu);
    release(menu);
}
} // namespace
int run_macos(const Options& options) {
    apple::Pool pool{};
    Host state{};
    host = &state;
    state.options = options;
    state.renderer.set_retained(options.retained);
    state.paused = options.paused;
    Class delegate_class =
        objc_allocateClassPair(objc_getClass("NSObject"), "StillwaterDelegate", 0);
    method(delegate_class, "togglePause:", &toggle_pause, "v@:@");
    method(delegate_class, "toggleSound:", &toggle_sound, "v@:@");
    method(delegate_class, "toggleDesktop:", &toggle_desktop, "v@:@");
    method(delegate_class, "togglePointer:", &toggle_pointer, "v@:@");
    method(delegate_class, "showPreview:", &show_preview, "v@:@");
    method(delegate_class, "quit:", &quit_action, "v@:@");
    method(delegate_class, "windowDidResize:", &resized, "v@:@");
    method(delegate_class, "windowShouldClose:", &close_window, "B@:@");
    method(delegate_class, "windowDidChangeOcclusionState:", &occlusion_changed, "v@:@");
    method(delegate_class, "sleeping:", &sleeping, "v@:@");
    method(delegate_class, "waking:", &waking, "v@:@");
    objc_registerClassPair(delegate_class);
    Class view_class = objc_allocateClassPair(objc_getClass("NSView"), "StillwaterView", 0);
    method(view_class, "acceptsFirstResponder", &reject_keyboard, "B@:");
    method(view_class, "acceptsFirstMouse:", &first_mouse, "B@:@");
    method(view_class, "mouseDown:", &mouse_down, "v@:@");
    objc_registerClassPair(view_class);
    Class window_class = objc_allocateClassPair(objc_getClass("NSPanel"), "StillwaterWindow", 0);
    method(window_class, "canBecomeKeyWindow", &reject_keyboard, "B@:");
    method(window_class, "canBecomeMainWindow", &reject_keyboard, "B@:");
    objc_registerClassPair(window_class);
    state.app = send<id>(type("NSApplication"), "sharedApplication");
    send<BOOL>(state.app, "setActivationPolicy:", 1L);
    state.delegate = apple::make("StillwaterDelegate");
    send<void>(state.app, "setDelegate:", state.delegate);
    state.window =
        send<id>(send<id>(type("StillwaterWindow"), "alloc"),
                 "initWithContentRect:styleMask:backing:defer:", CGRectMake(100, 100, 1100, 700),
                 15UL | 128UL, 2UL, static_cast<BOOL>(NO));
    send<void>(state.window, "setReleasedWhenClosed:", static_cast<BOOL>(NO));
    send<void>(state.window, "setHidesOnDeactivate:", static_cast<BOOL>(NO));
    send<void>(state.window, "setFloatingPanel:", static_cast<BOOL>(NO));
    send<void>(state.window, "setDelegate:", state.delegate);
    send<void>(state.window, "setTitle:", string("Stillwater"));
    send<void>(state.window, "setHasShadow:", static_cast<BOOL>(NO));
    state.view = send<id>(send<id>(type("StillwaterView"), "alloc"),
                          "initWithFrame:", CGRectMake(0, 0, 1100, 700));
    send<void>(state.view, "setAutoresizingMask:", 18UL);
    send<void>(state.view, "setWantsLayer:", static_cast<BOOL>(YES));
    send<void>(state.view, "setAccessibilityElement:", static_cast<BOOL>(YES));
    send<void>(state.view, "setAccessibilityRole:", string("AXImage"));
    send<void>(state.view, "setAccessibilityLabel:",
               string("Aquarium. Move the pointer near fish to interact. Desktop clicks pass through. Click the preview to tap the glass. Controls are in the Stillwater menu bar menu."));
    state.layer = apple::make("CAMetalLayer");
    send<void>(state.layer, "setOpaque:", static_cast<BOOL>(YES));
    send<void>(state.view, "setLayer:", state.layer);
    send<void>(state.window, "setContentView:", state.view);
    id bundle = send<id>(type("NSBundle"), "mainBundle");
    id resource = send<id>(bundle, "pathForResource:ofType:", string("aquarium"), string("metal"));
    const char* path = resource == nil ? nullptr : send<const char*>(resource, "UTF8String");
    const std::string shader_path = path == nullptr ? "assets/aquarium.metal" : path;
    const std::string asset_root = shader_path.substr(0, shader_path.find_last_of('/'));
    std::string habitat_error{};
    if (!state.scene.load_habitat(asset_root + "/riverscape.swscene.gz", habitat_error)) {
        std::cerr << "Habitat: " << habitat_error << '\n';
        return 1;
    }
    const RendererConfiguration configuration{
        .samples = options.samples,
        .conv_fast = options.conv_fast,
        .compact_camera = options.compact_camera,
        .record_shading = options.record_shading,
        .specialize_foliage = options.specialize_foliage};
    if (!state.renderer.initialize(state.layer, state.scene, shader_path, configuration)) {
        std::cerr << state.renderer.error() << '\n';
        host = nullptr;
        return 1;
    }
    state.audio_ready = state.audio.initialize();
    if (!state.audio_ready)
        std::cerr << "Audio unavailable; aquarium remains usable.\n";
    install_menu(state);
    send<void>(state.app, "finishLaunching");
    apply_placement();
    id workspace = send<id>(type("NSWorkspace"), "sharedWorkspace");
    id center = send<id>(workspace, "notificationCenter");
    send<void>(center, "addObserver:selector:name:object:", state.delegate,
               sel_registerName("sleeping:"), string("NSWorkspaceWillSleepNotification"),
               static_cast<id>(nil));
    send<void>(center, "addObserver:selector:name:object:", state.delegate,
               sel_registerName("waking:"), string("NSWorkspaceDidWakeNotification"),
               static_cast<id>(nil));
    bool verification_ok = true;
    if (!options.verify_retained.empty())
        verification_ok =
            verify_retained_renderer(state.renderer, state.scene, options.verify_retained);
    state.began = Clock::now();
    state.pause_started = 0;
    if (options.verify_retained.empty()) {
        state.renderer.draw(state.scene, options.capture_time,
                            options.paused && !options.capture.empty() ? options.capture.c_str()
                                                                       : nullptr);
        if (!options.muted && !state.paused)
            state.audio.set_ambience(true);
        start_frames();
        if (options.quit_after > 0 && std::isfinite(options.quit_after)) {
            state.quit_timer =
                dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
            dispatch_source_set_event_handler_f(state.quit_timer, &quit_timer);
            dispatch_source_set_timer(
                state.quit_timer,
                dispatch_time(DISPATCH_TIME_NOW,
                              static_cast<std::int64_t>(options.quit_after * 1e9)),
                DISPATCH_TIME_FOREVER, 10000000ULL);
            dispatch_resume(state.quit_timer);
        }
        std::cout << "Stillwater: " << state.scene.objects().size() << " retained objects; "
                  << state.scene.actors().size() << " creatures; " << options.fps
                  << (options.retained ? " fps retained fixed visibility.\n"
                                       : " fps full redraw.\n");
        send<void>(state.app, "run");
    }
    stop_source(state.frames);
    stop_source(state.quit_timer);
    send<void>(center, "removeObserver:", state.delegate);
    send<void>(state.window, "setDelegate:", static_cast<id>(nil));
    send<void>(state.window, "close");
    send<void>(send<id>(type("NSStatusBar"), "systemStatusBar"), "removeStatusItem:", state.status);
    send<void>(state.app, "setDelegate:", static_cast<id>(nil));
    release(state.status);
    release(state.window);
    release(state.view);
    release(state.layer);
    release(state.delegate);
    host = nullptr;
    return verification_ok && !state.renderer_failed ? 0 : 1;
}
} // namespace stillwater
