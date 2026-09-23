#include "metal_renderer.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
extern "C" id MTLCreateSystemDefaultDevice();
namespace stillwater {
using apple::release;
using apple::send;
using apple::string;
using apple::type;
namespace {
using Clock = std::chrono::steady_clock;
struct ClearColor {
    double red{}, green{}, blue{}, alpha{1};
};
struct Region {
    unsigned long x{}, y{}, z{}, width{}, height{}, depth{1};
};
struct Uniforms {
    Matrix camera;
    Matrix light;
    Float4 clock;
    Float4 eye;
};
Matrix light_matrix() {
    const double c = 0.9138115, s = 0.4061385;
    Matrix view{{1, 0, 0, 0, 0, static_cast<float>(s), static_cast<float>(c), 0, 0,
                 static_cast<float>(-c), static_cast<float>(s), 0, 0, -5, -20, 1}};
    Matrix orthographic{{1.0F / 12, 0, 0, 0, 0, 1.0F / 14, 0, 0, 0, 0, -1.0F / 60, 0, 0, 0, 0, 1}};
    return multiply(orthographic, view);
}
std::string error_text(id error) {
    if (error == nil)
        return "Unknown Metal error";
    const char* message = send<const char*>(send<id>(error, "localizedDescription"), "UTF8String");
    return message == nullptr ? "Metal error" : message;
}
bool save_png(id texture, id queue, unsigned int width, unsigned int height, const char* path) {
    const std::size_t stride = (static_cast<std::size_t>(width) * 4 + 255) & ~std::size_t(255);
    id device = send<id>(queue, "device");
    id buffer = send<id>(
        device, "newBufferWithLength:options:", static_cast<unsigned long>(stride * height), 0UL);
    if (buffer == nil)
        return false;
    id command = send<id>(queue, "commandBuffer");
    id blit = send<id>(command, "blitCommandEncoder");
    const Region region{0, 0, 0, width, height, 1};
    // MTLOrigin and MTLSize are each three NSUInteger values.
    struct Origin {
        unsigned long x, y, z;
    };
    struct Size {
        unsigned long width, height, depth;
    };
    send<void>(blit,
               "copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toBuffer:"
               "destinationOffset:destinationBytesPerRow:destinationBytesPerImage:",
               texture, 0UL, 0UL, Origin{region.x, region.y, region.z},
               Size{region.width, region.height, region.depth}, buffer, 0UL,
               static_cast<unsigned long>(stride), static_cast<unsigned long>(stride * height));
    send<void>(blit, "endEncoding");
    send<void>(command, "commit");
    send<void>(command, "waitUntilCompleted");
    if (send<unsigned long>(command, "status") == 5UL) {
        release(buffer);
        return false;
    }
    void* bytes = send<void*>(buffer, "contents");
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGBitmapInfo bitmap_info = static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) |
                                     static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little);
    CGContextRef context =
        CGBitmapContextCreate(bytes, width, height, 8, stride, space, bitmap_info);
    CGImageRef image = context == nullptr ? nullptr : CGBitmapContextCreateImage(context);
    CFURLRef url =
        CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path),
                                                static_cast<CFIndex>(std::strlen(path)), false);
    CGImageDestinationRef destination =
        image == nullptr ? nullptr
                         : CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    bool success = false;
    if (destination != nullptr) {
        CGImageDestinationAddImage(destination, image, nullptr);
        success = CGImageDestinationFinalize(destination);
        CFRelease(destination);
    }
    if (url != nullptr)
        CFRelease(url);
    if (image != nullptr)
        CGImageRelease(image);
    if (context != nullptr)
        CGContextRelease(context);
    CGColorSpaceRelease(space);
    release(buffer);
    return success;
}
} // namespace
Renderer::~Renderer() {
    for (const id material : materials_)
        release(material);
    release(previous_command_);
    release(color_);
    release(shadow_);
    release(fixed_shadow_);
    release(depth_);
    release(actors_);
    release(instances_);
    release(indices_);
    release(vertices_);
    release(depth_state_);
    release(background_pipeline_);
    release(shadow_pipeline_);
    release(pipeline_);
    release(queue_);
    release(device_);
    release(layer_);
}
id Renderer::make_texture(unsigned long format, unsigned int width, unsigned int height,
                          unsigned long usage, unsigned long samples) {
    id descriptor = send<id>(type("MTLTextureDescriptor"),
                             "texture2DDescriptorWithPixelFormat:width:height:mipmapped:", format,
                             static_cast<unsigned long>(width), static_cast<unsigned long>(height),
                             static_cast<BOOL>(NO));
    send<void>(descriptor, "setUsage:", usage);
    send<void>(descriptor, "setStorageMode:", 2UL);
    if (samples > 1) {
        send<void>(descriptor, "setTextureType:", 4UL);
        send<void>(descriptor, "setSampleCount:", samples);
    }
    id texture = send<id>(device_, "newTextureWithDescriptor:", descriptor);
    return texture;
}
bool Renderer::initialize(id layer, const Scene& scene, const std::string& shader_path,
                          unsigned int samples) {
    if (samples != 2 && samples != 4) {
        error_ = "MSAA must use two or four samples";
        return false;
    }
    samples_ = samples;
    layer_ = apple::retain(layer);
    device_ = MTLCreateSystemDefaultDevice();
    if (device_ == nil) {
        error_ = "No Metal device";
        return false;
    }
    queue_ = send<id>(device_, "newCommandQueue");
    send<void>(layer_, "setDevice:", device_);
    send<void>(layer_, "setPixelFormat:", 80UL);
    send<void>(layer_, "setFramebufferOnly:", static_cast<BOOL>(NO));
    send<void>(layer_, "setMaximumDrawableCount:", 2UL);
    std::ifstream file(shader_path);
    if (!file) {
        error_ = "Cannot open aquarium.metal";
        return false;
    }
    const std::string source{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};
    id error = nil;
    id library = send<id>(device_, "newLibraryWithSource:options:error:", string(source.c_str()),
                          static_cast<id>(nil), &error);
    if (library == nil) {
        error_ = error_text(error);
        return false;
    }
    id vertex = send<id>(library, "newFunctionWithName:", string("tank_vertex"));
    id fragment = send<id>(library, "newFunctionWithName:", string("tank_fragment"));
    id shadow_vertex = send<id>(library, "newFunctionWithName:", string("shadow_vertex"));
    id background_vertex = send<id>(library, "newFunctionWithName:", string("water_vertex"));
    id background_fragment = send<id>(library, "newFunctionWithName:", string("water_fragment"));
    id descriptor = apple::make("MTLRenderPipelineDescriptor");
    send<void>(descriptor, "setVertexFunction:", vertex);
    send<void>(descriptor, "setFragmentFunction:", fragment);
    id attachment =
        send<id>(send<id>(descriptor, "colorAttachments"), "objectAtIndexedSubscript:", 0UL);
    send<void>(attachment, "setPixelFormat:", 80UL);
    send<void>(descriptor, "setDepthAttachmentPixelFormat:", 252UL);
    send<void>(descriptor, "setRasterSampleCount:", static_cast<unsigned long>(samples_));
    send<void>(descriptor, "setAlphaToCoverageEnabled:", static_cast<BOOL>(YES));
    pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (pipeline_ == nil)
        error_ = error_text(error);
    send<void>(descriptor, "setAlphaToCoverageEnabled:", static_cast<BOOL>(NO));
    send<void>(descriptor, "setVertexFunction:", background_vertex);
    send<void>(descriptor, "setFragmentFunction:", background_fragment);
    background_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (background_pipeline_ == nil)
        error_ = error_text(error);
    send<void>(descriptor, "setVertexFunction:", shadow_vertex);
    send<void>(descriptor, "setFragmentFunction:", static_cast<id>(nil));
    send<void>(attachment, "setPixelFormat:", 0UL);
    send<void>(descriptor, "setRasterSampleCount:", 1UL);
    shadow_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (shadow_pipeline_ == nil)
        error_ = error_text(error);
    release(descriptor);
    release(vertex);
    release(fragment);
    release(shadow_vertex);
    release(background_vertex);
    release(background_fragment);
    release(library);
    if (pipeline_ == nil || shadow_pipeline_ == nil || background_pipeline_ == nil)
        return false;
    descriptor = apple::make("MTLDepthStencilDescriptor");
    send<void>(descriptor, "setDepthCompareFunction:", 1UL);
    send<void>(descriptor, "setDepthWriteEnabled:", static_cast<BOOL>(YES));
    depth_state_ = send<id>(device_, "newDepthStencilStateWithDescriptor:", descriptor);
    release(descriptor);
    const unsigned long vertex_bytes = scene.vertices().size() * sizeof(Vertex),
                        index_bytes = scene.indices().size() * sizeof(std::uint32_t),
                        instance_bytes = scene.instances().size() * sizeof(Instance);
    vertices_ = send<id>(device_, "newBufferWithBytes:length:options:",
                         static_cast<const void*>(scene.vertices().data()), vertex_bytes, 0UL);
    indices_ = send<id>(device_, "newBufferWithBytes:length:options:",
                        static_cast<const void*>(scene.indices().data()), index_bytes, 0UL);
    instances_ = send<id>(device_, "newBufferWithBytes:length:options:",
                          static_cast<const void*>(scene.instances().data()), instance_bytes, 0UL);
    statistics_.static_upload_bytes = vertex_bytes + index_bytes + instance_bytes;
    shadow_ = make_texture(252UL, 2048, 2048, 5UL);
    fixed_shadow_ = make_texture(252UL, 2048, 2048, 5UL);
    const std::string root = shader_path.substr(0, shader_path.find_last_of('/')) + "/materials/";
    const std::array<const char*, 6> names{
        "sand_01_diff.jpg",          "sand_01_nor_gl.jpg",
        "rock_boulder_dry_diff.jpg", "rock_boulder_dry_nor_gl.jpg",
        "rough_wood_diff.jpg",       "rough_wood_nor_gl.jpg"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        materials_[index] = load_material(root + names[index], index % 2 == 0);
        if (materials_[index] == nil) {
            error_ = "Cannot load material " + root + names[index];
            return false;
        }
    }
    upload_actors(scene);
    if (vertices_ == nil || indices_ == nil || instances_ == nil || actors_ == nil ||
        shadow_ == nil || fixed_shadow_ == nil) {
        error_ = "GPU resource allocation failed";
        return false;
    }
    return true;
}
id Renderer::load_material(const std::string& path, bool srgb) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()),
        false);
    CGImageSourceRef source = url == nullptr ? nullptr : CGImageSourceCreateWithURL(url, nullptr);
    if (url != nullptr)
        CFRelease(url);
    if (source == nullptr)
        return nil;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (image == nullptr)
        return nil;
    const std::size_t width = CGImageGetWidth(image), height = CGImageGetHeight(image);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        CGImageRelease(image);
        return nil;
    }
    std::vector<unsigned char> pixels(width * height * 4);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context =
        CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, space,
                              static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
                                  static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(space);
    if (context == nullptr) {
        CGImageRelease(image);
        return nil;
    }
    CGContextDrawImage(
        context, CGRectMake(0, 0, static_cast<double>(width), static_cast<double>(height)), image);
    CGContextRelease(context);
    CGImageRelease(image);
    id descriptor =
        send<id>(type("MTLTextureDescriptor"),
                 "texture2DDescriptorWithPixelFormat:width:height:mipmapped:", srgb ? 71UL : 70UL,
                 static_cast<unsigned long>(width), static_cast<unsigned long>(height),
                 static_cast<BOOL>(YES));
    send<void>(descriptor, "setStorageMode:", 0UL);
    send<void>(descriptor, "setUsage:", 1UL);
    id texture = send<id>(device_, "newTextureWithDescriptor:", descriptor);
    if (texture == nil)
        return nil;
    send<void>(texture, "replaceRegion:mipmapLevel:withBytes:bytesPerRow:",
               Region{0, 0, 0, width, height, 1}, 0UL, static_cast<const void*>(pixels.data()),
               static_cast<unsigned long>(width * 4));
    id command = send<id>(queue_, "commandBuffer");
    id blit = send<id>(command, "blitCommandEncoder");
    send<void>(blit, "generateMipmapsForTexture:", texture);
    send<void>(blit, "endEncoding");
    send<void>(command, "commit");
    send<void>(command, "waitUntilCompleted");
    if (send<unsigned long>(command, "status") == 5UL) {
        release(texture);
        return nil;
    }
    return texture;
}
void Renderer::upload_actors(const Scene& scene) {
    const unsigned long bytes = scene.actors().size() * sizeof(Actor);
    id replacement = send<id>(device_, "newBufferWithBytes:length:options:",
                              static_cast<const void*>(scene.actors().data()), bytes, 0UL);
    if (replacement == nil)
        return;
    release(actors_);
    actors_ = replacement;
    revision_ = scene.statistics().revision;
    statistics_.actor_upload_bytes += bytes;
}
void Renderer::upload_edits(id command, const Scene& scene) {
    if (revision_ == scene.statistics().revision)
        return;
    id blit = send<id>(command, "blitCommandEncoder");
    bool complete = true;
    for (std::size_t index = 0; index < scene.actors().size(); ++index) {
        if (scene.actor_revisions()[index] <= revision_)
            continue;
        id staging = send<id>(device_, "newBufferWithBytes:length:options:",
                              static_cast<const void*>(&scene.actors()[index]),
                              static_cast<unsigned long>(sizeof(Actor)), 0UL);
        if (staging == nil) {
            complete = false;
            continue;
        }
        send<void>(blit, "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:", staging,
                   0UL, actors_, static_cast<unsigned long>(index * sizeof(Actor)),
                   static_cast<unsigned long>(sizeof(Actor)));
        statistics_.actor_upload_bytes += sizeof(Actor);
        release(staging);
    }
    for (std::size_t index = 0; index < scene.instances().size(); ++index) {
        if (scene.instance_revisions()[index] <= revision_)
            continue;
        id staging = send<id>(device_, "newBufferWithBytes:length:options:",
                              static_cast<const void*>(&scene.instances()[index]),
                              static_cast<unsigned long>(sizeof(Instance)), 0UL);
        if (staging == nil) {
            complete = false;
            continue;
        }
        send<void>(blit, "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:", staging,
                   0UL, instances_, static_cast<unsigned long>(index * sizeof(Instance)),
                   static_cast<unsigned long>(sizeof(Instance)));
        statistics_.instance_patch_bytes += sizeof(Instance);
        release(staging);
    }
    send<void>(blit, "endEncoding");
    if (complete)
        revision_ = scene.statistics().revision;
}
void Renderer::resize(unsigned int width, unsigned int height) {
    if (width == width_ && height == height_)
        return;
    id replacement = make_texture(252UL, width, height, 4UL, samples_);
    if (replacement == nil)
        return;
    id color = make_texture(80UL, width, height, 4UL, samples_);
    if (color == nil) {
        release(replacement);
        return;
    }
    release(depth_);
    release(color_);
    depth_ = replacement;
    color_ = color;
    width_ = width;
    height_ = height;
    send<void>(layer_, "setDrawableSize:", CGSizeMake(width, height));
}
void Renderer::encode_geometry(id encoder, const Scene& scene, id pipeline, const void* uniforms,
                               unsigned long uniform_size, DrawSet draw_set) {
    send<void>(encoder, "setRenderPipelineState:", pipeline);
    send<void>(encoder, "setDepthStencilState:", depth_state_);
    send<void>(encoder, "setCullMode:", 0UL);
    send<void>(encoder, "setFrontFacingWinding:", 1UL);
    send<void>(encoder, "setVertexBuffer:offset:atIndex:", vertices_, 0UL, 0UL);
    send<void>(encoder, "setVertexBuffer:offset:atIndex:", instances_, 0UL, 1UL);
    send<void>(encoder, "setVertexBuffer:offset:atIndex:", actors_, 0UL, 2UL);
    send<void>(encoder, "setVertexBytes:length:atIndex:", uniforms, uniform_size, 3UL);
    if (pipeline == pipeline_) {
        send<void>(encoder, "setFragmentBytes:length:atIndex:", uniforms, uniform_size, 3UL);
        send<void>(encoder, "setFragmentTexture:atIndex:", shadow_, 0UL);
        for (std::size_t index = 0; index < materials_.size(); ++index)
            send<void>(encoder, "setFragmentTexture:atIndex:", materials_[index],
                       static_cast<unsigned long>(index + 1));
    }
    for (const Batch& batch : scene.batches()) {
        const Instance& instance = scene.instances()[batch.first_instance];
        const int material = static_cast<int>(instance.behavior.x);
        const bool moving =
            instance.behavior.w >= 0 || material == 2 || material == 3 || material == 10;
        if ((draw_set == DrawSet::fixed && moving) || (draw_set == DrawSet::moving && !moving))
            continue;
        send<void>(encoder, "setFrontFacingWinding:", material >= 7 ? 1UL : 0UL);
        // Imported rock shells and fish bodies are closed, outward-wound meshes.
        // Keep foliage, fins and all other materials two-sided.
        send<void>(encoder, "setCullMode:", (material == 8 || material == 11) ? 2UL : 0UL);
        send<void>(encoder,
                   "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"
                   "instanceCount:baseVertex:baseInstance:",
                   3UL, static_cast<unsigned long>(batch.mesh.index_count), 1UL, indices_,
                   static_cast<unsigned long>(batch.mesh.first_index * sizeof(std::uint32_t)),
                   static_cast<unsigned long>(batch.instance_count), 0L,
                   static_cast<unsigned long>(batch.first_instance));
    }
    send<void>(encoder, "endEncoding");
}
bool Renderer::draw(const Scene& scene, double time, const char* capture_path) {
    if (width_ == 0 || height_ == 0)
        return false;
    const Clock::time_point start = Clock::now();
    apple::Pool pool{};
    if (previous_command_ != nil && send<unsigned long>(previous_command_, "status") == 4UL) {
        const double began = send<double>(previous_command_, "GPUStartTime"),
                     ended = send<double>(previous_command_, "GPUEndTime");
        if (ended > began && began > 0) {
            statistics_.gpu_seconds += ended - began;
            ++statistics_.gpu_timed_frames;
            if (previous_updated_shadows_) {
                statistics_.gpu_shadow_frame_seconds += ended - began;
                ++statistics_.gpu_shadow_frames;
            } else {
                statistics_.gpu_reuse_frame_seconds += ended - began;
                ++statistics_.gpu_reuse_frames;
            }
        }
    }
    release(previous_command_);
    previous_command_ = nil;
    previous_updated_shadows_ = false;
    id drawable = send<id>(layer_, "nextDrawable");
    if (drawable == nil)
        return false;
    const Uniforms uniforms{
        multiply(perspective(static_cast<double>(width_) / height_), view_matrix()),
        light_matrix(),
        {static_cast<float>(time), static_cast<float>(width_), static_cast<float>(height_), 0},
        {0, 4.65F, 20.5F, 0}};
    id command = send<id>(queue_, "commandBuffer");
    upload_edits(command, scene);
    id encoder = nil;
    id attachment = nil;
    const bool rebuild_fixed =
        !fixed_shadow_ready_ || shadow_instance_edits_ != scene.statistics().instance_edits;
    if (rebuild_fixed) {
        id fixed_pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
        attachment = send<id>(fixed_pass, "depthAttachment");
        send<void>(attachment, "setTexture:", fixed_shadow_);
        send<void>(attachment, "setLoadAction:", 2UL);
        send<void>(attachment, "setStoreAction:", 1UL);
        send<void>(attachment, "setClearDepth:", 1.0);
        encoder = send<id>(command, "renderCommandEncoderWithDescriptor:", fixed_pass);
        encode_geometry(encoder, scene, shadow_pipeline_, &uniforms, sizeof(uniforms),
                        DrawSet::fixed);
        fixed_shadow_ready_ = true;
        shadow_instance_edits_ = scene.statistics().instance_edits;
        ++statistics_.static_shadow_builds;
    }
    // Direct-light visibility is retained. Moving casters refresh at 8 Hz or on edits;
    // their depth pass starts from the cached static occluders, never yesterday's fish.
    if (rebuild_fixed || time - shadow_time_ >= 0.125 ||
        shadow_revision_ != scene.statistics().revision) {
        id blit = send<id>(command, "blitCommandEncoder");
        send<void>(blit, "copyFromTexture:toTexture:", fixed_shadow_, shadow_);
        send<void>(blit, "endEncoding");
        id shadow_pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
        attachment = send<id>(shadow_pass, "depthAttachment");
        send<void>(attachment, "setTexture:", shadow_);
        send<void>(attachment, "setLoadAction:", 1UL);
        send<void>(attachment, "setStoreAction:", 1UL);
        encoder = send<id>(command, "renderCommandEncoderWithDescriptor:", shadow_pass);
        encode_geometry(encoder, scene, shadow_pipeline_, &uniforms, sizeof(uniforms),
                        DrawSet::moving);
        shadow_time_ = time;
        shadow_revision_ = scene.statistics().revision;
        ++statistics_.dynamic_shadow_frames;
        previous_updated_shadows_ = true;
    }
    id pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
    attachment = send<id>(send<id>(pass, "colorAttachments"), "objectAtIndexedSubscript:", 0UL);
    id texture = send<id>(drawable, "texture");
    send<void>(attachment, "setTexture:", color_);
    send<void>(attachment, "setResolveTexture:", texture);
    send<void>(attachment, "setLoadAction:", 2UL);
    send<void>(attachment, "setStoreAction:", 2UL);
    send<void>(attachment, "setClearColor:", ClearColor{0.0874, 0.209, 0.225, 1});
    attachment = send<id>(pass, "depthAttachment");
    send<void>(attachment, "setTexture:", depth_);
    send<void>(attachment, "setLoadAction:", 2UL);
    send<void>(attachment, "setStoreAction:", 0UL);
    send<void>(attachment, "setClearDepth:", 1.0);
    encoder = send<id>(command, "renderCommandEncoderWithDescriptor:", pass);
    send<void>(encoder, "setRenderPipelineState:", background_pipeline_);
    send<void>(encoder, "setFragmentBytes:length:atIndex:", static_cast<const void*>(&uniforms),
               static_cast<unsigned long>(sizeof(uniforms)), 3UL);
    send<void>(encoder, "drawPrimitives:vertexStart:vertexCount:", 3UL, 0UL, 3UL);
    encode_geometry(encoder, scene, pipeline_, &uniforms, sizeof(uniforms));
    send<void>(command, "presentDrawable:", drawable);
    send<void>(command, "commit");
    previous_command_ = apple::retain(command);
    ++statistics_.frames;
    statistics_.cpu_submit_seconds += std::chrono::duration<double>(Clock::now() - start).count();
    if (capture_path != nullptr) {
        send<void>(command, "waitUntilCompleted");
        return save_png(texture, queue_, width_, height_, capture_path);
    }
    return true;
}
} // namespace stillwater
