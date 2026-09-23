#include "metal_renderer.hpp"
#include "stillwater/timing.hpp"
#include "stillwater/leaf_texture.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <simd/simd.h>
extern "C" id MTLCreateSystemDefaultDevice();
namespace stillwater {
using apple::release;
using apple::send;
using apple::string;
using apple::type;
namespace {
using Clock = std::chrono::steady_clock;
// RGBA16F base/direct, RGBA16F surface/caustic, RG32F distance/identity,
// RG16F interpolation correction. Depth32F and R16F visibility add six bytes.
constexpr std::array<unsigned long, 4> visibility_formats{115, 115, 105, 65};
constexpr std::array<unsigned long, 4> visibility_slots{1, 2, 4, 7};
constexpr std::uint64_t retained_bytes_per_sample = 34;
struct ClearColor {
    double red{}, green{}, blue{}, alpha{1};
};
struct Region {
    unsigned long x{}, y{}, z{}, width{}, height{}, depth{1};
};
struct Uniforms {
    Matrix camera;
    Matrix light;
    Matrix reconstruction;
    Float4 clock;
    Float4 eye;
    Float4 illumination;
};
bool moving_instance(const Instance& instance) {
    const int material = static_cast<int>(instance.behavior.x);
    return instance.behavior.w >= 0 || material == 2 || material == 3 || material == 10 || material == 15;
}
bool retained_instance(const Instance& instance) {
    const int material = static_cast<int>(instance.behavior.x);
    return !moving_instance(instance) &&
           (material == 7 || material == 8 || material == 9 || material == 13 || material == 14);
}
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
bool decode_material(const std::string& path, std::vector<unsigned char>& pixels,
                     std::size_t& width, std::size_t& height) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()),
        false);
    CGImageSourceRef source = url == nullptr ? nullptr : CGImageSourceCreateWithURL(url, nullptr);
    if (url != nullptr)
        CFRelease(url);
    if (source == nullptr)
        return false;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (image == nullptr)
        return false;
    width = CGImageGetWidth(image);
    height = CGImageGetHeight(image);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        CGImageRelease(image);
        return false;
    }
    pixels.resize(width * height * 4);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context =
        CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, space,
                              static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
                                  static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(space);
    if (context == nullptr) {
        CGImageRelease(image);
        return false;
    }
    CGContextDrawImage(
        context, CGRectMake(0, 0, static_cast<double>(width), static_cast<double>(height)), image);
    CGContextRelease(context);
    CGImageRelease(image);
    return true;
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
    // CAMetalLayer is opaque. The render target alpha also carries material
    // coverage; interpreting RGB as premultiplied would brighten PNG captures.
    const CGBitmapInfo bitmap_info = static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst) |
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
    finish_pending(true);
    release(shadow_leaf_);release(shadow_other_);
    release(shadow_leaf_pipeline_);release(shadow_other_pipeline_);
    for (const id material : materials_)
        release(material);
    for (const id texture : visibility_)
        release(texture);
    release(visibility_depth_);
    release(camera_map_);
    release(camera_records_);
    release(record_color_);
    release(record_light_);
    record_color_ = nil;
    record_light_ = nil;
    release(camera_classify_pipeline_);
    release(camera_pack_pipeline_);
    release(camera_validate_pipeline_);
    release(record_shade_pipeline_);
    release(foliage_pipeline_);
    release(light_visibility_);
    release(light_visibility_pipeline_);
    release(query_pipeline_);
    release(visibility_pipeline_);
    release(restore_pipeline_);
    release(restore_depth_state_);
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
    release(conv_pipeline_);
    release(conv_depth_state_);
    release(conv_prepare_pipeline_);
    release(conv_prepared_);
    release(conv_classify_pipeline_);
    release(conv_adjacency_);
    release(conv_list_);
    release(conv_arguments_);
    release(conv_prefix_pipeline_);
    release(conv_compact_pipeline_);
    release(conv_masks_);
    release(conv_counts_);
    release(queue_);
    release(device_);
    release(layer_);
}
id Renderer::make_texture(unsigned long format, unsigned int width, unsigned int height,
                          unsigned long usage, unsigned long samples, bool transient) {
    id descriptor = send<id>(type("MTLTextureDescriptor"),
                             "texture2DDescriptorWithPixelFormat:width:height:mipmapped:", format,
                             static_cast<unsigned long>(width), static_cast<unsigned long>(height),
                             static_cast<BOOL>(NO));
    send<void>(descriptor, "setUsage:", usage);
    // Only pass-local color/depth targets may use tile memory. Retained surfaces,
    // shadow maps, and anything sampled by a later pass require backing storage.
    send<void>(descriptor, "setStorageMode:", transient && memoryless_supported_ ? 3UL : 2UL);
    if (samples > 1) {
        send<void>(descriptor, "setTextureType:", 4UL);
        send<void>(descriptor, "setSampleCount:", samples);
    }
    id texture = send<id>(device_, "newTextureWithDescriptor:", descriptor);
    return texture;
}
bool Renderer::initialize(id layer, const Scene& scene, const std::string& shader_path,
                          const RendererConfiguration& configuration) {
    const unsigned int samples = configuration.samples;
    if (samples != 1 && samples != 2 && samples != 4) {
        error_ = "Sample count must be one, two or four";
        return false;
    }
    samples_ = samples;
    conv_fast_ = configuration.conv_fast;
    compact_camera_ = configuration.compact_camera;
    record_shading_ = configuration.record_shading && compact_camera_;
    specialize_foliage_ = configuration.specialize_foliage && !conv_fast_;
    leaf_grain_ = configuration.leaf_grain;
    leaf_self_shadows_ = configuration.leaf_self_shadows;
    if (conv_fast_)
        samples_ = 1;
    source_vertices_ = scene.vertices().data();
    source_indices_ = scene.indices().data();
    source_instances_ = scene.instances().size();
    source_actors_ = scene.actors().size();
    layer_ = apple::retain(layer);
    device_ = MTLCreateSystemDefaultDevice();
    if (device_ == nil) {
        error_ = "No Metal device";
        return false;
    }
    queue_ = send<id>(device_, "newCommandQueue");
    memoryless_supported_ = send<BOOL>(device_, "supportsFamily:", 1001L) != NO;
    send<void>(layer_, "setDevice:", device_);
    send<void>(layer_, "setPixelFormat:", 80UL);
    send<void>(layer_, "setFramebufferOnly:", static_cast<BOOL>(NO));
    send<void>(layer_, "setMaximumDrawableCount:", 2UL);
    std::ifstream file(shader_path);
    if (!file) {
        error_ = "Cannot open aquarium.metal";
        return false;
    }
    std::string source{std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>()};
    if (compact_camera_) {
        std::ifstream camera(shader_path.substr(0, shader_path.find_last_of('/')) + "/camera_registry.metal");
        if (!camera) {
            error_ = "Cannot open camera_registry.metal";
            return false;
        }
        source = "#define SW_COMPACT_CAMERA 1\n" + source +
                 std::string(std::istreambuf_iterator<char>(camera), std::istreambuf_iterator<char>());
    }
    if (record_shading_)
        source = "#define SW_SHADE_RECORDS 1\n" + source;
    if (samples_ == 1)
        source = "#define SW_SINGLE_SAMPLE 1\n" + source;
    if (conv_fast_) {
        const std::string root = shader_path.substr(0, shader_path.find_last_of('/'));
        for (const char* name : {"conv_coverage.metal", "conv_geometry.metal"}) {
            std::ifstream extra(root + "/" + name);
            if (!extra) {
                error_ = std::string("Cannot open ") + name;
                return false;
            }
            source += std::string(std::istreambuf_iterator<char>(extra),
                                  std::istreambuf_iterator<char>());
        }
    }
    id error = nil;
    id compile_options = nil;
    if (conv_fast_) {
        compile_options = apple::make("MTLCompileOptions");
        send<void>(compile_options, "setFastMathEnabled:", static_cast<BOOL>(YES));
    }
    id library = send<id>(device_, "newLibraryWithSource:options:error:", string(source.c_str()),
                          compile_options, &error);
    release(compile_options);
    if (library == nil) {
        error_ = error_text(error);
        return false;
    }
    id vertex = send<id>(library, "newFunctionWithName:", string(conv_fast_ ? "conv_regular_vertex" : "tank_vertex"));
    id fragment = send<id>(library, "newFunctionWithName:", string("tank_fragment"));
    id shadow_vertex = send<id>(library, "newFunctionWithName:", string(conv_fast_ ? "conv_shadow_vertex" : "shadow_vertex"));
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
    send<void>(descriptor, "setAlphaToCoverageEnabled:", static_cast<BOOL>(samples_ > 1));
    pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (pipeline_ == nil)
        error_ = error_text(error);
    if (specialize_foliage_) {
        id foliage_vertex = send<id>(library, "newFunctionWithName:", string("foliage_vertex"));
        id foliage_fragment = send<id>(library, "newFunctionWithName:", string("foliage_fragment"));
        send<void>(descriptor, "setVertexFunction:", foliage_vertex);
        send<void>(descriptor, "setFragmentFunction:", foliage_fragment);
        foliage_pipeline_ = send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
        release(foliage_vertex);
        release(foliage_fragment);
        if (foliage_pipeline_ == nil)
            error_ = error_text(error);
        send<void>(descriptor, "setVertexFunction:", vertex);
        send<void>(descriptor, "setFragmentFunction:", fragment);
    }
    if (conv_fast_) {
        send<void>(attachment, "setBlendingEnabled:", static_cast<BOOL>(YES));
        send<void>(attachment, "setSourceRGBBlendFactor:", 4UL);
        send<void>(attachment, "setDestinationRGBBlendFactor:", 5UL);
        send<void>(attachment, "setSourceAlphaBlendFactor:", 1UL);
        send<void>(attachment, "setDestinationAlphaBlendFactor:", 5UL);
    }
    if (conv_fast_) {
        id conv_vertex = send<id>(library, "newFunctionWithName:", string("conv_vertex"));
        id conv_fragment = send<id>(library, "newFunctionWithName:", string("conv_fragment"));
        send<void>(descriptor, "setVertexFunction:", conv_vertex);
        send<void>(descriptor, "setFragmentFunction:", conv_fragment);
        conv_pipeline_ =
            send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
        release(conv_vertex);
        release(conv_fragment);
        if (conv_pipeline_ == nil) {
            error_ = error_text(error);
            release(descriptor);
            release(vertex);
            release(fragment);
            release(shadow_vertex);
            release(background_vertex);
            release(background_fragment);
            release(library);
            return false;
        }
    }
    send<void>(attachment, "setBlendingEnabled:", static_cast<BOOL>(NO));
    send<void>(descriptor, "setAlphaToCoverageEnabled:", static_cast<BOOL>(NO));
    send<void>(descriptor, "setVertexFunction:", background_vertex);
    send<void>(descriptor, "setFragmentFunction:", background_fragment);
    background_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (background_pipeline_ == nil)
        error_ = error_text(error);
    id restore = send<id>(library, "newFunctionWithName:", string("restore_surface"));
    send<void>(descriptor, "setFragmentFunction:", restore);
    restore_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (restore_pipeline_ == nil)
        error_ = error_text(error);
    release(restore);
    id light_visibility =
        send<id>(library, "newFunctionWithName:", string("retain_light_visibility"));
    send<void>(descriptor, "setFragmentFunction:", light_visibility);
    send<void>(descriptor, "setDepthAttachmentPixelFormat:", 0UL);
    send<void>(attachment, "setPixelFormat:", 25UL);
    light_visibility_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (light_visibility_pipeline_ == nil)
        error_ = error_text(error);
    release(light_visibility);
    send<void>(descriptor, "setDepthAttachmentPixelFormat:", 252UL);
    id query = send<id>(library, "newFunctionWithName:", string("query_visibility"));
    query_pipeline_ =
        send<id>(device_, "newComputePipelineStateWithFunction:error:", query, &error);
    if (query_pipeline_ == nil)
        error_ = error_text(error);
    release(query);
    if (compact_camera_) {
        id classify = send<id>(library, "newFunctionWithName:", string("classify_camera"));
        camera_classify_pipeline_ = send<id>(device_, "newComputePipelineStateWithFunction:error:", classify, &error);
        release(classify);
        id pack = send<id>(library, "newFunctionWithName:", string("pack_camera"));
        camera_pack_pipeline_ = send<id>(device_, "newComputePipelineStateWithFunction:error:", pack, &error);
        release(pack);
        id validate = send<id>(library, "newFunctionWithName:", string("validate_camera"));
        camera_validate_pipeline_ = send<id>(device_, "newComputePipelineStateWithFunction:error:", validate, &error);
        release(validate);
        if (record_shading_) {
            id shade = send<id>(library, "newFunctionWithName:", string("shade_camera"));
            record_shade_pipeline_ = send<id>(device_, "newComputePipelineStateWithFunction:error:", shade, &error);
            release(shade);
        }
        if ((record_shading_ && record_shade_pipeline_ == nil) || camera_validate_pipeline_ == nil || camera_classify_pipeline_ == nil ||
            camera_pack_pipeline_ == nil) {
            error_ = error_text(error);
            release(vertex);
            release(fragment);
            release(shadow_vertex);
            release(background_vertex);
            release(background_fragment);
            release(library);
            release(descriptor);
            return false;
        }
    }
    id retain = send<id>(library, "newFunctionWithName:", string("retain_surface"));
    id retained_vertex = send<id>(library, "newFunctionWithName:", string(conv_fast_ ? "conv_regular_vertex" : "tank_vertex"));
    send<void>(descriptor, "setVertexFunction:", retained_vertex);
    send<void>(descriptor, "setFragmentFunction:", retain);
    for (unsigned long index = 0; index < visibility_formats.size(); ++index) {
        id color_attachment =
            send<id>(send<id>(descriptor, "colorAttachments"), "objectAtIndexedSubscript:", index);
        send<void>(color_attachment, "setPixelFormat:", visibility_formats[index]);
    }
    visibility_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (visibility_pipeline_ == nil)
        error_ = error_text(error);
    release(retain);
    release(retained_vertex);
    for (unsigned long index = 1; index < visibility_formats.size(); ++index)
        send<void>(
            send<id>(send<id>(descriptor, "colorAttachments"), "objectAtIndexedSubscript:", index),
            "setPixelFormat:", 0UL);
    send<void>(descriptor, "setVertexFunction:", shadow_vertex);
    send<void>(descriptor, "setFragmentFunction:", static_cast<id>(nil));
    send<void>(attachment, "setPixelFormat:", 0UL);
    send<void>(descriptor, "setRasterSampleCount:", 1UL);
    shadow_pipeline_ =
        send<id>(device_, "newRenderPipelineStateWithDescriptor:error:", descriptor, &error);
    if (shadow_pipeline_ == nil)
        error_ = error_text(error);
    id leaf_vertex=send<id>(library,"newFunctionWithName:",string("shadow_leaf_vertex"));
    id leaf_fragment=send<id>(library,"newFunctionWithName:",string("shadow_leaf_fragment"));
    id other_fragment=send<id>(library,"newFunctionWithName:",string("shadow_other_fragment"));
    send<void>(descriptor,"setVertexFunction:",leaf_vertex);
    send<void>(descriptor,"setFragmentFunction:",leaf_fragment);
    send<void>(attachment,"setPixelFormat:",23UL);
    shadow_leaf_pipeline_=send<id>(device_,"newRenderPipelineStateWithDescriptor:error:",descriptor,&error);
    send<void>(attachment,"setPixelFormat:",0UL);
    send<void>(descriptor,"setFragmentFunction:",other_fragment);
    shadow_other_pipeline_=send<id>(device_,"newRenderPipelineStateWithDescriptor:error:",descriptor,&error);
    release(leaf_vertex);release(leaf_fragment);release(other_fragment);
    if(shadow_leaf_pipeline_==nil || shadow_other_pipeline_==nil)error_=error_text(error);
    release(descriptor);
    release(vertex);
    release(fragment);
    release(shadow_vertex);
    release(background_vertex);
    release(background_fragment);
    if (conv_fast_) {
        id prepare = send<id>(library, "newFunctionWithName:", string("conv_prepare"));
        conv_prepare_pipeline_ =
            send<id>(device_, "newComputePipelineStateWithFunction:error:", prepare, &error);
        release(prepare);
        id classify = send<id>(library, "newFunctionWithName:", string("conv_classify"));
        conv_classify_pipeline_ =
            send<id>(device_, "newComputePipelineStateWithFunction:error:", classify, &error);
        release(classify);
        id prefix = send<id>(library, "newFunctionWithName:", string("conv_prefix"));
        conv_prefix_pipeline_ = send<id>(device_, "newComputePipelineStateWithFunction:error:", prefix, &error);
        release(prefix);
        id compact = send<id>(library, "newFunctionWithName:", string("conv_compact"));
        conv_compact_pipeline_ = send<id>(device_, "newComputePipelineStateWithFunction:error:", compact, &error);
        release(compact);
        if (conv_prepare_pipeline_ == nil || conv_classify_pipeline_ == nil ||
            conv_prefix_pipeline_ == nil || conv_compact_pipeline_ == nil)
            error_ = error_text(error);
    }
    release(library);
    if ((specialize_foliage_ && foliage_pipeline_ == nil) ||
        pipeline_ == nil || shadow_pipeline_ == nil || background_pipeline_ == nil ||
        visibility_pipeline_ == nil || restore_pipeline_ == nil ||
        light_visibility_pipeline_ == nil || query_pipeline_ == nil ||
        (conv_fast_ && (conv_prepare_pipeline_ == nil || conv_classify_pipeline_ == nil ||
            conv_prefix_pipeline_ == nil || conv_compact_pipeline_ == nil)))
        return false;
    descriptor = apple::make("MTLDepthStencilDescriptor");
    send<void>(descriptor, "setDepthCompareFunction:", 1UL);
    send<void>(descriptor, "setDepthWriteEnabled:", static_cast<BOOL>(YES));
    depth_state_ = send<id>(device_, "newDepthStencilStateWithDescriptor:", descriptor);
    send<void>(descriptor, "setDepthCompareFunction:", 7UL);
    restore_depth_state_ = send<id>(device_, "newDepthStencilStateWithDescriptor:", descriptor);
    send<void>(descriptor, "setDepthCompareFunction:", 3UL);
    send<void>(descriptor, "setDepthWriteEnabled:", static_cast<BOOL>(NO));
    conv_depth_state_ = send<id>(device_, "newDepthStencilStateWithDescriptor:", descriptor);
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
    if (conv_fast_) {
        std::uint64_t prepared_count = 0;
        std::uint32_t list_count = 0, group_count = 0;
        std::vector<std::uint32_t> adjacency{};
        struct EdgeOwner { std::uint32_t slot, opposite, count, start; };
        for (const Batch& batch : scene.batches()) {
            std::uint32_t first = std::numeric_limits<std::uint32_t>::max(), last = 0;
            for (std::uint32_t index = 0; index < batch.mesh.index_count; ++index) {
                const std::uint32_t source_index = scene.indices()[batch.mesh.first_index + index];
                first = std::min(first, source_index);
                last = std::max(last, source_index);
            }
            const std::uint32_t count = last - first + 1;
            const std::uint64_t next = prepared_count +
                                      static_cast<std::uint64_t>(count) * batch.instance_count;
            if (next > 8000000) {
                error_ = "CONV prepared geometry exceeds trial storage budget";
                return false;
            }
            const std::uint32_t triangles = batch.mesh.index_count / 3;
            const std::uint64_t expanded = static_cast<std::uint64_t>(triangles) * batch.instance_count;
            if (expanded + list_count > 8000000 || adjacency.size() + batch.mesh.index_count > 24000000) {
                error_ = "CONV boundary topology exceeds trial storage budget";
                return false;
            }
            const std::uint32_t adjacency_offset = static_cast<std::uint32_t>(adjacency.size());
            adjacency.resize(adjacency.size() + batch.mesh.index_count, UINT32_MAX);
            std::unordered_map<std::uint64_t, EdgeOwner> edges{};
            for (std::uint32_t triangle = 0; triangle < triangles; ++triangle) {
                const std::uint32_t* corners = &scene.indices()[batch.mesh.first_index + triangle * 3];
                for (std::uint32_t edge = 0; edge < 3; ++edge) {
                    const std::uint32_t a = corners[edge], b = corners[(edge + 1) % 3];
                    const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) |
                                               std::max(a, b);
                    const std::uint32_t slot = adjacency_offset + triangle * 3 + edge;
                    const std::uint32_t opposite = corners[(edge + 2) % 3];
                    std::unordered_map<std::uint64_t, EdgeOwner>::iterator found = edges.find(key);
                    if (found == edges.end())
                        edges.emplace(key, EdgeOwner{slot, opposite, 1, a});
                    else {
                        EdgeOwner& owner = (*found).second;
                        if (owner.count == 1) {
                            if (owner.start != a) {
                                adjacency[slot] = owner.opposite;
                                adjacency[owner.slot] = opposite;
                            }
                            owner.opposite = slot;
                        } else {
                            // Nonmanifold topology stays conservative: keep every edge.
                            adjacency[owner.slot] = UINT32_MAX;
                            adjacency[owner.opposite] = UINT32_MAX;
                        }
                        ++owner.count;
                    }
                }
            }
            const int material = static_cast<int>(scene.instances()[batch.first_instance].behavior.x);
            conv_batches_.push_back(ConvBatch{first, count, batch.first_instance,
                                               static_cast<std::uint32_t>(prepared_count),
                                               triangles, batch.mesh.first_index, list_count,
                                               adjacency_offset, batch.instance_count, group_count,
                                               material == 8 || material == 11 ? 1U : 0U});
            group_count += (triangles * batch.instance_count + 63) / 64;
            list_count += static_cast<std::uint32_t>(expanded);
            prepared_count = next;
        }
        conv_prepared_ = send<id>(device_, "newBufferWithLength:options:",
                                  static_cast<unsigned long>(prepared_count * 40), 32UL);
        conv_adjacency_ = send<id>(device_, "newBufferWithBytes:length:options:",
                                   static_cast<const void*>(adjacency.data()),
                                   static_cast<unsigned long>(adjacency.size() * 4), 0UL);
        conv_list_ = send<id>(device_, "newBufferWithLength:options:",
                              static_cast<unsigned long>(list_count) * 8UL, 32UL);
        conv_arguments_ = send<id>(device_, "newBufferWithLength:options:",
                                   static_cast<unsigned long>(conv_batches_.size()) * 16UL, 32UL);
        statistics_.conv_source_triangles = list_count;
        statistics_.conv_storage_bytes = prepared_count * 40 + adjacency.size() * 4 +
                                         static_cast<std::uint64_t>(list_count) * 12 +
                                         group_count * 4 + conv_batches_.size() * 16;
        conv_masks_ = send<id>(device_, "newBufferWithLength:options:",
                               static_cast<unsigned long>(list_count) * 4UL, 32UL);
        conv_counts_ = send<id>(device_, "newBufferWithLength:options:",
                                static_cast<unsigned long>(group_count) * 4UL, 32UL);
        if (conv_masks_ == nil || conv_counts_ == nil || conv_prepared_ == nil || conv_adjacency_ == nil || conv_list_ == nil ||
            conv_arguments_ == nil) {
            error_ = "Cannot allocate CONV retained boundary resources";
            return false;
        }
    }
    shadow_ = make_texture(252UL, 2048, 2048, 5UL);
    fixed_shadow_ = make_texture(252UL, 2048, 2048, 5UL);
    shadow_leaf_ = make_texture(23UL,2048,2048,5UL);
    shadow_other_ = make_texture(252UL,2048,2048,5UL);
    const std::string root = shader_path.substr(0, shader_path.find_last_of('/')) + "/materials/";
    const std::array<const char*, 6> names{
        "sand_01_diff.jpg",          "sand_01_nor_gl.jpg",
        "rock_boulder_dry_diff.jpg", "rock_boulder_dry_nor_gl.jpg",
        "rough_wood_diff.jpg",       "rough_wood_nor_gl.jpg"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        std::string occlusion_path;
        if (index == 3)
            occlusion_path = root + "rock_boulder_dry_ao.jpg";
        if (index == 5)
            occlusion_path = root + "rough_wood_ao.jpg";
        materials_[index] = load_material(root + names[index], index % 2 == 0, occlusion_path);
        if (materials_[index] == nil) {
            error_ = "Cannot load material " + root + names[index];
            if (!occlusion_path.empty())
                error_ += " with ambient-occlusion map " + occlusion_path;
            return false;
        }
    }
    materials_[6] = make_leaf_texture();
    if (materials_[6] == nil) {
        error_ = "Cannot allocate leaf grain texture";
        return false;
    }
    for (const id material : materials_)
        statistics_.material_storage_bytes += send<unsigned long>(material, "allocatedSize");
    statistics_.shadow_storage_bytes = send<unsigned long>(shadow_, "allocatedSize") +
                                       send<unsigned long>(fixed_shadow_, "allocatedSize") +
                                       send<unsigned long>(shadow_leaf_,"allocatedSize") +
                                       send<unsigned long>(shadow_other_,"allocatedSize");
    statistics_.geometry_storage_bytes = vertex_bytes + index_bytes + instance_bytes +
                                         scene.actors().size() * sizeof(Actor);
    upload_actors(scene);
    if (vertices_ == nil || indices_ == nil || instances_ == nil || actors_ == nil ||
        shadow_ == nil || fixed_shadow_ == nil || shadow_leaf_ == nil || shadow_other_ == nil ||
        shadow_leaf_pipeline_ == nil || shadow_other_pipeline_ == nil) {
        error_ = "GPU resource allocation failed";
        return false;
    }
    return true;
}
id Renderer::make_leaf_texture() {
    const std::vector<std::uint8_t> pixels = make_leaf_grain();
    id descriptor = send<id>(type("MTLTextureDescriptor"),
        "texture2DDescriptorWithPixelFormat:width:height:mipmapped:", 10UL,
        static_cast<unsigned long>(leaf_grain_size), static_cast<unsigned long>(leaf_grain_size),
        static_cast<BOOL>(YES));
    send<void>(descriptor, "setStorageMode:", 0UL);
    send<void>(descriptor, "setUsage:", 1UL);
    id texture = send<id>(device_, "newTextureWithDescriptor:", descriptor);
    if (texture == nil)
        return nil;
    send<void>(texture, "replaceRegion:mipmapLevel:withBytes:bytesPerRow:",
        Region{0, 0, 0, leaf_grain_size, leaf_grain_size, 1}, 0UL,
        static_cast<const void*>(pixels.data()), static_cast<unsigned long>(leaf_grain_size));
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
id Renderer::load_material(const std::string& path, bool srgb, const std::string& occlusion_path) {
    std::vector<unsigned char> pixels;
    std::size_t width{}, height{};
    if (!decode_material(path, pixels, width, height))
        return nil;
    if (!occlusion_path.empty()) {
        std::vector<unsigned char> occlusion;
        std::size_t occlusion_width{}, occlusion_height{};
        if (!decode_material(occlusion_path, occlusion, occlusion_width, occlusion_height) ||
            occlusion_width != width || occlusion_height != height)
            return nil;
        // Pack after decoding: alpha is material data, never premultiply the normal.
        // The matching source maps use the same UVs and bitmap orientation.
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
            pixels[offset + 3] = occlusion[offset];
    }
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
bool Renderer::upload_edits(id command, const Scene& scene) {
    if (revision_ == scene.statistics().revision)
        return true;
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
        if (retained_instance(scene.instances()[index]))
            visibility_ready_ = false;
        if (!moving_instance(scene.instances()[index]))
            fixed_shadow_ready_ = false;
        statistics_.instance_patch_bytes += sizeof(Instance);
        release(staging);
    }
    send<void>(blit, "endEncoding");
    if (complete)
        revision_ = scene.statistics().revision;
    else
        error_ = "Cannot stage scene edits";
    return complete;
}
void Renderer::set_retained(bool enabled) {
    retained_ = enabled;
    if (enabled && width_ != 0 && visibility_depth_ == nil)
        resize(width_, height_);
}
std::uint64_t Renderer::allocated_gpu_bytes() const {
    return device_ == nil ? 0 : send<unsigned long>(device_, "currentAllocatedSize");
}
void Renderer::set_camera(const Matrix& view, Float4 eye) {
    if (view.values == view_.values && eye.x == eye_.x && eye.y == eye_.y && eye.z == eye_.z)
        return;
    view_ = view;
    eye_ = eye;
    update_reconstruction();
    visibility_ready_ = false;
}
void Renderer::update_reconstruction() {
    // Invert only when the camera or output dimensions change, never per pixel
    // or per frame. This maps (ndc.x * distance, ndc.y * distance, -distance, 1)
    // from camera space to world space, including projection x/y scaling.
    if (width_ == 0 || height_ == 0)
        return;
    simd_float4x4 native_view{};
    static_assert(sizeof(native_view) == sizeof(view_));
    std::memcpy(&native_view, &view_, sizeof(native_view));
    const simd_float4x4 inverse = simd_inverse(native_view);
    std::memcpy(&reconstruction_, &inverse, sizeof(reconstruction_));
    const Matrix projection = perspective(static_cast<double>(width_) / height_);
    for (std::size_t row = 0; row < 4; ++row) {
        reconstruction_.values[row] /= projection.values[0];
        reconstruction_.values[4 + row] /= projection.values[5];
    }
}
bool Renderer::set_light_intensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0 || intensity > 10)
        return false;
    if (light_intensity_ == intensity)
        return true;
    light_intensity_ = intensity;
    return true;
}
bool Renderer::query_fixed_visibility(unsigned int x, unsigned int y, VisibilityProbe& result) {
    // Explicit, synchronous diagnostic query. No readback occurs during animation.
    if (!visibility_ready_ || x >= width_ || y >= height_)
        return false;
    static_assert(sizeof(VisibilityProbe) == 80);
    apple::Pool pool{};
    id buffer = send<id>(device_, "newBufferWithLength:options:", 80UL, 0UL);
    if (buffer == nil)
        return false;
    const std::array<unsigned int, 2> pixel{x, y};
    id command = send<id>(queue_, "commandBuffer");
    id encoder = send<id>(command, "computeCommandEncoder");
    send<void>(encoder, "setComputePipelineState:", query_pipeline_);
    if (compact_camera_) {
        send<void>(encoder, "setBuffer:offset:atIndex:", camera_map_, 0UL, 8UL);
        send<void>(encoder, "setBuffer:offset:atIndex:", camera_records_, 0UL, 9UL);
    }
    send<void>(encoder, "setTexture:atIndex:", visibility_[2], 0UL);
    send<void>(encoder, "setTexture:atIndex:", visibility_depth_, 1UL);
    send<void>(encoder, "setTexture:atIndex:", visibility_[3], 2UL);
    send<void>(encoder, "setBytes:length:atIndex:", static_cast<const void*>(pixel.data()), 8UL,
               0UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", buffer, 0UL, 1UL);
    send<void>(encoder, "setBytes:length:atIndex:", static_cast<const void*>(&reconstruction_),
               static_cast<unsigned long>(sizeof(reconstruction_)), 2UL);
    const Float4 dimensions{static_cast<float>(width_), static_cast<float>(height_), 0, 0};
    send<void>(encoder, "setBytes:length:atIndex:", static_cast<const void*>(&dimensions),
               static_cast<unsigned long>(sizeof(dimensions)), 3UL);
    struct Size {
        unsigned long width, height, depth;
    };
    send<void>(encoder, "dispatchThreadgroups:threadsPerThreadgroup:", Size{1, 1, 1},
               Size{1, 1, 1});
    send<void>(encoder, "endEncoding");
    send<void>(command, "commit");
    send<void>(command, "waitUntilCompleted");
    const bool success = send<unsigned long>(command, "status") == 4UL;
    if (success)
        std::memcpy(&result, send<void*>(buffer, "contents"), sizeof(result));
    else
        error_ = error_text(send<id>(command, "error"));
    release(buffer);
    return success;
}
void Renderer::resize(unsigned int width, unsigned int height) {
    if (width == width_ && height == height_ && (!retained_ || visibility_depth_ != nil))
        return;
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        error_ = "Invalid render dimensions";
        return;
    }
    id replacement = make_texture(252UL, width, height, 4UL, samples_, !conv_fast_);
    id color = samples_ > 1 ? make_texture(80UL, width, height, 4UL, samples_, true) : nil;
    id visibility_depth = retained_ ? make_texture(252UL, width, height, 5UL, samples_) : nil;
    id light_visibility = retained_ && !record_shading_ ? make_texture(25UL, width, height, 5UL, samples_) : nil;
    std::array<id, 4> visibility{};
    bool complete = replacement != nil && (samples_ == 1 || color != nil) &&
                    (!retained_ || (visibility_depth != nil && (record_shading_ || light_visibility != nil)));
    for (std::size_t index = 0; retained_ && !compact_camera_ && index < visibility.size(); ++index) {
        visibility[index] = make_texture(visibility_formats[index], width, height, 5UL, samples_);
        complete = complete && visibility[index] != nil;
    }
    if (!complete) {
        release(replacement);
        release(color);
        release(visibility_depth);
        release(light_visibility);
        for (const id texture : visibility)
            release(texture);
        error_ = "Cannot allocate retained view buffers";
        return;
    }
    release(depth_);
    release(color_);
    release(visibility_depth_);
    for (const id texture : visibility_)
        release(texture);
    depth_ = replacement;
    color_ = color;
    visibility_depth_ = visibility_depth;
    release(light_visibility_);
    light_visibility_ = light_visibility;
    light_visibility_ready_ = false;
    visibility_ = visibility;
    release(camera_map_);
    release(camera_records_);
    release(record_color_);
    release(record_light_);
    record_color_ = nil;
    record_light_ = nil;
    camera_map_ = nil;
    camera_records_ = nil;
    visibility_ready_ = false;
    width_ = width;
    height_ = height;
    update_reconstruction();
    statistics_.render_width = width;
    statistics_.render_height = height;
    statistics_.retained_bytes =
        retained_ ? static_cast<std::uint64_t>(width) * height * samples_ * retained_bytes_per_sample
                  : 0;
    statistics_.memoryless_targets = memoryless_supported_ && !conv_fast_;
    statistics_.transient_backing_bytes =
        statistics_.memoryless_targets ? 0
                              : static_cast<std::uint64_t>(width) * height * samples_ *
                                    (samples_ > 1 ? 8 : 4);
    send<void>(layer_, "setDrawableSize:", CGSizeMake(width, height));
}
void Renderer::encode_geometry(id encoder, const Scene& scene, id pipeline, const void* uniforms,
                               unsigned long uniform_size, DrawSet draw_set, bool finish) {
    send<void>(encoder, "setRenderPipelineState:", pipeline);
    send<void>(encoder, "setDepthStencilState:",
               pipeline == conv_pipeline_ ? conv_depth_state_ : depth_state_);
    send<void>(encoder, "setCullMode:", 0UL);
    send<void>(encoder, "setFrontFacingWinding:", 1UL);
    send<void>(encoder, "setVertexBuffer:offset:atIndex:", vertices_, 0UL, 0UL);
    send<void>(encoder, "setVertexBuffer:offset:atIndex:", instances_, 0UL, 1UL);
    send<void>(encoder, "setVertexBuffer:offset:atIndex:", actors_, 0UL, 2UL);
    send<void>(encoder, "setVertexBytes:length:atIndex:", uniforms, uniform_size, 3UL);
    if (pipeline == pipeline_ || pipeline == visibility_pipeline_ || pipeline == conv_pipeline_) {
        send<void>(encoder, "setFragmentBytes:length:atIndex:", uniforms, uniform_size, 3UL);
        send<void>(encoder, "setFragmentTexture:atIndex:", shadow_, 0UL);
        send<void>(encoder, "setFragmentTexture:atIndex:", shadow_leaf_, 8UL);
        send<void>(encoder, "setFragmentTexture:atIndex:", shadow_other_, 9UL);
        for (std::size_t index = 0; index < materials_.size(); ++index)
            send<void>(encoder, "setFragmentTexture:atIndex:", materials_[index],
                       static_cast<unsigned long>(index + 1));
    }
    if (pipeline == shadow_other_pipeline_)
        send<void>(encoder,"setFragmentTexture:atIndex:",shadow_leaf_,0UL);
    const bool shadow_pass=pipeline==shadow_pipeline_ || pipeline==shadow_leaf_pipeline_ || pipeline==shadow_other_pipeline_;
    std::size_t batch_index = 0;
    for (const Batch& batch : scene.batches()) {
        const std::size_t conv_index = batch_index++;
        const Instance& instance = scene.instances()[batch.first_instance];
        const int material = static_cast<int>(instance.behavior.x);
        if (shadow_pass && material == 15)
            continue;
        const bool moving = moving_instance(instance);
        if ((draw_set == DrawSet::fixed && moving) || (draw_set == DrawSet::moving && !moving))
            continue;
        const bool cached = retained_instance(instance);
        if ((draw_set == DrawSet::retained_surface && !cached) ||
            (draw_set == DrawSet::uncached_surface && cached))
            continue;
        if (pipeline == pipeline_ || pipeline == visibility_pipeline_ || pipeline == conv_pipeline_) {
            if (moving)
                ++statistics_.moving_camera_draws;
            else
                ++statistics_.fixed_camera_draws;
        }
        if (pipeline == pipeline_ && specialize_foliage_)
            send<void>(encoder, "setRenderPipelineState:", material == 10 ? foliage_pipeline_ : pipeline_);
        send<void>(encoder, "setFrontFacingWinding:", material >= 7 && material != 15 ? 1UL : 0UL);
        // Imported rock shells and fish bodies are closed, outward-wound meshes.
        // Keep foliage, fins and all other materials two-sided.
        send<void>(encoder, "setCullMode:", (material == 8 || material == 11) ? 2UL : 0UL);
        if (conv_fast_) {
            const ConvBatch& prepared = conv_batches_[conv_index];
            send<void>(encoder, "setVertexBuffer:offset:atIndex:", conv_prepared_, 0UL, 5UL);
            send<void>(encoder, "setVertexBytes:length:atIndex:", static_cast<const void*>(&prepared),
                       static_cast<unsigned long>(sizeof(prepared)), 6UL);
        }
        if (pipeline == conv_pipeline_) {
            send<void>(encoder, "setCullMode:", 0UL);
            send<void>(encoder, "setVertexBuffer:offset:atIndex:", indices_,
                       static_cast<unsigned long>(batch.mesh.first_index * sizeof(std::uint32_t)),
                       4UL);
            send<void>(encoder, "setVertexBuffer:offset:atIndex:", conv_list_, 0UL, 7UL);
            send<void>(encoder, "drawPrimitives:indirectBuffer:indirectBufferOffset:", 4UL,
                       conv_arguments_, static_cast<unsigned long>(conv_index) * 16UL);
            continue;
        }
        send<void>(encoder,
                   "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"
                   "instanceCount:baseVertex:baseInstance:",
                   3UL, static_cast<unsigned long>(batch.mesh.index_count), 1UL, indices_,
                   static_cast<unsigned long>(batch.mesh.first_index * sizeof(std::uint32_t)),
                   static_cast<unsigned long>(batch.instance_count), 0L,
                   static_cast<unsigned long>(batch.first_instance));
    }
    if (finish)
        send<void>(encoder, "endEncoding");
}
bool Renderer::prepare_conv_vertices(id command, const Scene& scene, const void* uniforms,
                                     unsigned long uniform_size) {
    if (!visibility_ready_)
        conv_fixed_ready_ = false;
    id encoder = send<id>(command, "computeCommandEncoder");
    if (encoder == nil)
        return false;
    send<void>(encoder, "setComputePipelineState:", conv_prepare_pipeline_);
    send<void>(encoder, "setBuffer:offset:atIndex:", vertices_, 0UL, 0UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", instances_, 0UL, 1UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", actors_, 0UL, 2UL);
    send<void>(encoder, "setBytes:length:atIndex:", uniforms, uniform_size, 3UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", conv_prepared_, 0UL, 4UL);
    struct Size {
        unsigned long width, height, depth;
    };
    for (std::size_t index = 0; index < scene.batches().size(); ++index) {
        const Batch& batch = scene.batches()[index];
        if (conv_fixed_ready_ && !moving_instance(scene.instances()[batch.first_instance]))
            continue;
        const ConvBatch& prepared = conv_batches_[index];
        if (moving_instance(scene.instances()[batch.first_instance]))
            ++statistics_.conv_moving_updates;
        else
            ++statistics_.conv_static_updates;
        send<void>(encoder, "setBytes:length:atIndex:", static_cast<const void*>(&prepared),
                   static_cast<unsigned long>(sizeof(prepared)), 5UL);
        const unsigned long count = static_cast<unsigned long>(prepared.vertex_count) * batch.instance_count;
        send<void>(encoder, "dispatchThreads:threadsPerThreadgroup:", Size{count, 1, 1}, Size{64, 1, 1});
    }
    send<void>(encoder, "endEncoding");
    id clear = send<id>(command, "blitCommandEncoder");
    struct Range { unsigned long location, length; };
    for (std::size_t index = 0; index < scene.batches().size(); ++index) {
        const Batch& batch = scene.batches()[index];
        if (conv_fixed_ready_ && !moving_instance(scene.instances()[batch.first_instance]))
            continue;
        const ConvBatch& prepared = conv_batches_[index];
        const unsigned long groups = (static_cast<unsigned long>(prepared.triangle_count) * batch.instance_count + 63) / 64;
        send<void>(clear, "fillBuffer:range:value:", conv_counts_,
                   Range{static_cast<unsigned long>(prepared.group_offset) * 4UL, groups * 4UL},
                   static_cast<unsigned char>(0));
    }
    send<void>(clear, "endEncoding");
    encoder = send<id>(command, "computeCommandEncoder");
    send<void>(encoder, "setComputePipelineState:", conv_classify_pipeline_);
    send<void>(encoder, "setBuffer:offset:atIndex:", indices_, 0UL, 0UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", conv_prepared_, 0UL, 1UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", conv_adjacency_, 0UL, 2UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", conv_masks_, 0UL, 3UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", conv_counts_, 0UL, 7UL);
    send<void>(encoder, "setBytes:length:atIndex:", uniforms, uniform_size, 6UL);
    for (std::size_t index = 0; index < scene.batches().size(); ++index) {
        const Batch& batch = scene.batches()[index];
        if (conv_fixed_ready_ && !moving_instance(scene.instances()[batch.first_instance]))
            continue;
        const ConvBatch& prepared = conv_batches_[index];
        send<void>(encoder, "setBytes:length:atIndex:", static_cast<const void*>(&prepared),
                   static_cast<unsigned long>(sizeof(prepared)), 4UL);
        send<void>(encoder, "setBuffer:offset:atIndex:", conv_arguments_,
                   static_cast<unsigned long>(index) * 16UL, 5UL);
        const unsigned long count = static_cast<unsigned long>(prepared.triangle_count) * batch.instance_count;
        send<void>(encoder, "dispatchThreads:threadsPerThreadgroup:", Size{count, 1, 1}, Size{64, 1, 1});
    }
    send<void>(encoder, "endEncoding");
    for (unsigned int phase = 0; phase < 2; ++phase) {
        encoder = send<id>(command, "computeCommandEncoder");
        send<void>(encoder, "setComputePipelineState:", phase == 0 ? conv_prefix_pipeline_ : conv_compact_pipeline_);
        send<void>(encoder, "setBuffer:offset:atIndex:", conv_counts_, 0UL, 0UL);
        if (phase == 1) {
            send<void>(encoder, "setBuffer:offset:atIndex:", conv_masks_, 0UL, 2UL);
            send<void>(encoder, "setBuffer:offset:atIndex:", conv_list_, 0UL, 3UL);
        }
        for (std::size_t index = 0; index < scene.batches().size(); ++index) {
            const Batch& batch = scene.batches()[index];
            if (conv_fixed_ready_ && !moving_instance(scene.instances()[batch.first_instance]))
                continue;
            const ConvBatch& prepared = conv_batches_[index];
            send<void>(encoder, "setBytes:length:atIndex:", static_cast<const void*>(&prepared),
                       static_cast<unsigned long>(sizeof(prepared)), 1UL);
            const unsigned long groups = (static_cast<unsigned long>(prepared.triangle_count) * batch.instance_count + 63) / 64;
            if (phase == 0)
                send<void>(encoder, "setBuffer:offset:atIndex:", conv_arguments_,
                           static_cast<unsigned long>(index) * 16UL, 2UL);
            send<void>(encoder, "dispatchThreads:threadsPerThreadgroup:",
                       Size{phase == 0 ? 1 : groups, 1, 1}, Size{phase == 0 ? 1UL : 64UL, 1, 1});
        }
        send<void>(encoder, "endEncoding");
    }
    conv_fixed_ready_ = true;
    return true;
}
// Keep commands until they complete: an overloaded GPU may still be executing
// the previous frame. Dropping that command would bias timings toward fast frames.
bool Renderer::finish_pending(bool wait) {
    bool success = true;
    const bool had_pending = pending_count_ != 0;
    std::size_t completed = 0;
    for (; completed < pending_count_; ++completed) {
        const PendingCommand& pending = pending_[completed];
        if (wait)
            send<void>(pending.command, "waitUntilCompleted");
        const unsigned long status = send<unsigned long>(pending.command, "status");
        if (status != 4UL && status != 5UL)
            break;
        if (status == 5UL) {
            error_ = error_text(send<id>(pending.command, "error"));
            visibility_ready_ = false;
            fixed_shadow_ready_ = false;
            success = false;
        } else {
            const double began = send<double>(pending.command, "GPUStartTime");
            const double ended = send<double>(pending.command, "GPUEndTime");
            if (ended > began && began > 0) {
                statistics_.gpu_seconds += ended - began;
                ++statistics_.gpu_timed_frames;
                if (pending.updated_shadows) {
                    statistics_.gpu_shadow_frame_seconds += ended - began;
                    ++statistics_.gpu_shadow_frames;
                } else {
                    statistics_.gpu_reuse_frame_seconds += ended - began;
                    ++statistics_.gpu_reuse_frames;
                }
            }
        }
        release(pending.command);
    }
    for (std::size_t index = completed; index < pending_count_; ++index)
        pending_[index - completed] = pending_[index];
    const std::size_t remaining = pending_count_ - completed;
    for (std::size_t index = remaining; index < pending_count_; ++index)
        pending_[index] = PendingCommand{};
    pending_count_ = remaining;
    if (wait && had_pending && conv_fast_ && conv_arguments_ != nil && statistics_.frames != 0) {
        // Diagnostic readback only when draining, never during ordinary animation.
        const unsigned long bytes = static_cast<unsigned long>(conv_batches_.size()) * 16UL;
        id buffer = send<id>(device_, "newBufferWithLength:options:", bytes, 0UL);
        if (buffer != nil) {
            id command = send<id>(queue_, "commandBuffer");
            id blit = send<id>(command, "blitCommandEncoder");
            send<void>(blit, "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:",
                       conv_arguments_, 0UL, buffer, 0UL, bytes);
            send<void>(blit, "endEncoding");
            send<void>(command, "commit");
            send<void>(command, "waitUntilCompleted");
            if (send<unsigned long>(command, "status") == 4UL) {
                const std::uint32_t* counts = static_cast<const std::uint32_t*>(send<void*>(buffer, "contents"));
                statistics_.conv_boundary_triangles = 0;
                for (std::size_t index = 0; index < conv_batches_.size(); ++index)
                    statistics_.conv_boundary_triangles += counts[index * 4 + 1];
            }
            release(buffer);
        }
    }
    return success;
}
bool Renderer::register_camera(id& command, bool updated_shadows) {
    // Acquisition is an infrequent synchronization boundary. Read back only one
    // count per 256 pixels, allocate exact storage, then release all four dense
    // coefficient attachments. Ordinary animation has no registration/readback.
    const unsigned long pixels = static_cast<unsigned long>(width_) * height_;
    const unsigned long groups = (pixels + 255UL) / 256UL;
    id map = send<id>(device_, "newBufferWithLength:options:", pixels * 8UL, 32UL);
    id counts = send<id>(device_, "newBufferWithLength:options:", groups * 4UL, 0UL);
    if (map == nil || counts == nil) {
        release(map);
        release(counts);
        error_ = "Cannot allocate camera registration map";
        return false;
    }
    struct Size {
        unsigned long width, height, depth;
    };
    id encoder = send<id>(command, "computeCommandEncoder");
    send<void>(encoder, "setComputePipelineState:", camera_classify_pipeline_);
    for (unsigned long index = 0; index < visibility_.size(); ++index)
        send<void>(encoder, "setTexture:atIndex:", visibility_[index], index);
    send<void>(encoder, "setBuffer:offset:atIndex:", map, 0UL, 0UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", counts, 0UL, 1UL);
    send<void>(encoder, "dispatchThreadgroups:threadsPerThreadgroup:",
                   Size{groups, 1, 1}, Size{256, 1, 1});
    send<void>(encoder, "endEncoding");
    send<void>(command, "commit");
    send<void>(command, "waitUntilCompleted");
    if (send<unsigned long>(command, "status") != 4UL) {
        error_ = error_text(send<id>(command, "error"));
        release(map);
        release(counts);
        return false;
    }
    const double elapsed = send<double>(command, "GPUEndTime") - send<double>(command, "GPUStartTime");
    statistics_.camera_acquisition_command_gpu_seconds += elapsed;
    statistics_.gpu_seconds += elapsed;
    if (updated_shadows)
        statistics_.gpu_shadow_frame_seconds += elapsed;
    else
        statistics_.gpu_reuse_frame_seconds += elapsed;
    std::uint32_t* offsets = static_cast<std::uint32_t*>(send<void*>(counts, "contents"));
    std::uint32_t total = 0;
    for (unsigned long group = 0; group < groups; ++group) {
        const std::uint32_t count = offsets[group];
        offsets[group] = total;
        total += count;
    }
    id records = send<id>(device_, "newBufferWithLength:options:", static_cast<unsigned long>(total) * 28UL, 32UL);
    if (records == nil) {
        release(map);
        release(counts);
        error_ = "Cannot allocate registered camera surfaces";
        return false;
    }
    command = send<id>(queue_, "commandBuffer");
    encoder = send<id>(command, "computeCommandEncoder");
    send<void>(encoder, "setComputePipelineState:", camera_pack_pipeline_);
    for (unsigned long index = 0; index < visibility_.size(); ++index)
        send<void>(encoder, "setTexture:atIndex:", visibility_[index], index);
    send<void>(encoder, "setBuffer:offset:atIndex:", map, 0UL, 0UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", counts, 0UL, 1UL);
    send<void>(encoder, "setBuffer:offset:atIndex:", records, 0UL, 2UL);
    send<void>(encoder, "dispatchThreadgroups:threadsPerThreadgroup:",
                   Size{groups, 1, 1}, Size{256, 1, 1});
    send<void>(encoder, "endEncoding");
    release(counts);
    if (validate_camera_) {
        id errors = send<id>(device_, "newBufferWithLength:options:", 4UL, 0UL);
        if (errors == nil) {
            release(map);
            release(records);
            error_ = "Cannot allocate camera validation counter";
            return false;
        }
        std::uint32_t* error_count = static_cast<std::uint32_t*>(send<void*>(errors, "contents"));
        *error_count = 0;
        encoder = send<id>(command, "computeCommandEncoder");
        send<void>(encoder, "setComputePipelineState:", camera_validate_pipeline_);
        for (unsigned long index = 0; index < visibility_.size(); ++index)
            send<void>(encoder, "setTexture:atIndex:", visibility_[index], index);
        send<void>(encoder, "setBuffer:offset:atIndex:", map, 0UL, 0UL);
        send<void>(encoder, "setBuffer:offset:atIndex:", records, 0UL, 1UL);
        send<void>(encoder, "setBuffer:offset:atIndex:", errors, 0UL, 2UL);
        send<void>(encoder, "dispatchThreadgroups:threadsPerThreadgroup:",
                   Size{groups, 1, 1}, Size{256, 1, 1});
        send<void>(encoder, "endEncoding");
        send<void>(command, "commit");
        send<void>(command, "waitUntilCompleted");
        const bool valid = send<unsigned long>(command, "status") == 4UL && *error_count == 0;
        release(errors);
        if (!valid) {
            release(map);
            release(records);
            error_ = "Camera registry failed exact sample reconstruction";
            return false;
        }
        statistics_.camera_validated_samples += pixels * samples_;
        // Diagnostic GPU work is separate from production measurements.
        command = send<id>(queue_, "commandBuffer");
    }
    release(camera_map_);
    release(camera_records_);
    release(record_color_);
    release(record_light_);
    record_color_ = nil;
    record_light_ = nil;
    camera_map_ = map;
    camera_records_ = records;
    if (record_shading_) {
        record_color_ = send<id>(device_, "newBufferWithLength:options:", static_cast<unsigned long>(total) * 4UL, 32UL);
        record_light_ = send<id>(device_, "newBufferWithLength:options:", static_cast<unsigned long>(total) * 2UL, 32UL);
        if (record_color_ == nil || record_light_ == nil) {
            error_ = "Cannot allocate camera record lighting";
            return false;
        }
    }
    // The command buffer owns acquisition textures through pack completion.
    for (id& texture : visibility_) {
        release(texture);
        texture = nil;
    }
    statistics_.camera_records = total;
    ++statistics_.camera_registration_builds;
    statistics_.retained_bytes =
        pixels * (8UL + samples_ * (record_shading_ ? 4UL : 6UL)) +
        static_cast<std::uint64_t>(total) * (record_shading_ ? 34UL : 28UL);
    return true;
}
bool Renderer::draw(const Scene& scene, double time, const char* capture_path) {
    if (scene.vertices().data() != source_vertices_ || scene.indices().data() != source_indices_ ||
        scene.instances().size() != source_instances_ || scene.actors().size() != source_actors_) {
        error_ = "Scene storage replaced; recreate the renderer before drawing";
        return false;
    }
    if (width_ == 0 || height_ == 0 || (retained_ && visibility_depth_ == nil))
        return false;
    const Clock::time_point start = Clock::now();
    apple::Pool pool{};
    if (!finish_pending(false))
        return false;
    if (pending_count_ == pending_.size() && !finish_pending(true))
        return false;
    bool updated_shadows = false;
    id drawable = send<id>(layer_, "nextDrawable");
    if (drawable == nil)
        return false;
    const Uniforms uniforms{
        multiply(perspective(static_cast<double>(width_) / height_), view_),
        light_matrix(),
        reconstruction_,
        {static_cast<float>(time), static_cast<float>(width_), static_cast<float>(height_), 0},
        {eye_.x,eye_.y,eye_.z,leaf_self_shadows_ ? 1.0F : 0.0F},
        {light_intensity_, leaf_grain_ ? 1.0F : 0.0F, 0, 0}};
    id command = send<id>(queue_, "commandBuffer");
    if (!upload_edits(command, scene))
        return false;
    if (conv_fast_ && !prepare_conv_vertices(command, scene, &uniforms, sizeof(uniforms)))
        return false;
    id encoder = nil;
    id attachment = nil;
    const bool rebuild_fixed = !fixed_shadow_ready_;
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
        ++statistics_.static_shadow_builds;
    }
    // Direct-light visibility is retained. Moving casters refresh at 16 Hz or on edits;
    // their depth pass starts from the cached static occluders, never yesterday's fish.
    if (rebuild_fixed || shadow_refresh_due(time, shadow_time_) ||
        shadow_revision_ != scene.statistics().revision) {
        id blit = send<id>(command, "blitCommandEncoder");
        send<void>(blit, "copyFromTexture:toTexture:", fixed_shadow_, shadow_);
        send<void>(blit, "copyFromTexture:toTexture:", fixed_shadow_, shadow_other_);
        send<void>(blit, "endEncoding");
        id shadow_pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
        attachment = send<id>(shadow_pass, "depthAttachment");
        send<void>(attachment, "setTexture:", shadow_);
        send<void>(attachment, "setLoadAction:", 1UL);
        send<void>(attachment, "setStoreAction:", 1UL);
        id identity_attachment=send<id>(send<id>(shadow_pass,"colorAttachments"),"objectAtIndexedSubscript:",0UL);
        send<void>(identity_attachment,"setTexture:",shadow_leaf_);
        send<void>(identity_attachment,"setLoadAction:",2UL);
        send<void>(identity_attachment,"setStoreAction:",1UL);
        send<void>(identity_attachment,"setClearColor:",ClearColor{0,0,0,0});
        encoder = send<id>(command, "renderCommandEncoderWithDescriptor:", shadow_pass);
        encode_geometry(encoder, scene, shadow_leaf_pipeline_, &uniforms, sizeof(uniforms),DrawSet::moving);
        // Retain the nearest different blade, including the fixed geometry copied above.
        send<void>(identity_attachment,"setTexture:",static_cast<id>(nil));
        send<void>(attachment,"setTexture:",shadow_other_);
        encoder=send<id>(command,"renderCommandEncoderWithDescriptor:",shadow_pass);
        encode_geometry(encoder,scene,shadow_other_pipeline_,&uniforms,sizeof(uniforms),DrawSet::moving);
        shadow_time_ = time;
        shadow_revision_ = scene.statistics().revision;
        ++statistics_.dynamic_shadow_frames;
        updated_shadows = true;
    }
    if (!retained_ && updated_shadows)
        light_visibility_ready_ = false;
    if (retained_) {
        if (!visibility_ready_) {
            for (std::size_t index = 0; index < visibility_.size(); ++index) {
                if (visibility_[index] == nil)
                    visibility_[index] = make_texture(visibility_formats[index], width_, height_, 5UL, samples_);
                if (visibility_[index] == nil) {
                    error_ = "Cannot allocate camera acquisition textures";
                    return false;
                }
            }
            id visibility_pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
            for (unsigned long index = 0; index < visibility_.size(); ++index) {
                id target = send<id>(send<id>(visibility_pass, "colorAttachments"),
                                     "objectAtIndexedSubscript:", index);
                send<void>(target, "setTexture:", visibility_[index]);
                send<void>(target, "setLoadAction:", 2UL);
                send<void>(target, "setStoreAction:", 1UL);
                send<void>(target, "setClearColor:", ClearColor{0, 0, 0, 0});
            }
            id target = send<id>(visibility_pass, "depthAttachment");
            send<void>(target, "setTexture:", visibility_depth_);
            send<void>(target, "setLoadAction:", 2UL);
            send<void>(target, "setStoreAction:", 1UL);
            send<void>(target, "setClearDepth:", 1.0);
            id visibility_encoder =
                send<id>(command, "renderCommandEncoderWithDescriptor:", visibility_pass);
            encode_geometry(visibility_encoder, scene, visibility_pipeline_, &uniforms,
                            sizeof(uniforms), DrawSet::retained_surface);
            if (compact_camera_ && !register_camera(command, updated_shadows))
                return false;
            visibility_ready_ = true;
            ++statistics_.visibility_builds;
            light_visibility_ready_ = false;
        } else
            ++statistics_.visibility_reuses;
    }
    if (retained_ && record_shading_) {
        Uniforms shade_uniforms = uniforms;
        shade_uniforms.clock.w = (!light_visibility_ready_ || updated_shadows) ? 1.0F : 0.0F;
        id shade_encoder = send<id>(command, "computeCommandEncoder");
        send<void>(shade_encoder, "setComputePipelineState:", record_shade_pipeline_);
        send<void>(shade_encoder, "setBuffer:offset:atIndex:", camera_map_, 0UL, 0UL);
        send<void>(shade_encoder, "setBuffer:offset:atIndex:", camera_records_, 0UL, 1UL);
        send<void>(shade_encoder, "setBuffer:offset:atIndex:", record_color_, 0UL, 2UL);
        send<void>(shade_encoder, "setBytes:length:atIndex:", static_cast<const void*>(&shade_uniforms),
                   static_cast<unsigned long>(sizeof(shade_uniforms)), 3UL);
        send<void>(shade_encoder, "setBuffer:offset:atIndex:", record_light_, 0UL, 4UL);
        send<void>(shade_encoder, "setTexture:atIndex:", shadow_, 0UL);
        struct Size { unsigned long width, height, depth; };
        send<void>(shade_encoder, "dispatchThreads:threadsPerThreadgroup:",
                   Size{static_cast<unsigned long>(width_) * height_, 1, 1}, Size{128, 1, 1});
        send<void>(shade_encoder, "endEncoding");
        if (shade_uniforms.clock.w != 0)
            ++statistics_.light_visibility_builds;
        else
            ++statistics_.light_visibility_reuses;
        light_visibility_ready_ = true;
    } else if (retained_ && (!light_visibility_ready_ || updated_shadows)) {
        id light_pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
        id target =
            send<id>(send<id>(light_pass, "colorAttachments"), "objectAtIndexedSubscript:", 0UL);
        send<void>(target, "setTexture:", light_visibility_);
        send<void>(target, "setLoadAction:", 0UL);
        send<void>(target, "setStoreAction:", 1UL);
        id light_encoder = send<id>(command, "renderCommandEncoderWithDescriptor:", light_pass);
        send<void>(light_encoder, "setRenderPipelineState:", light_visibility_pipeline_);
        send<void>(light_encoder,
                   "setFragmentBytes:length:atIndex:", static_cast<const void*>(&uniforms),
                   static_cast<unsigned long>(sizeof(uniforms)), 3UL);
        send<void>(light_encoder, "setFragmentTexture:atIndex:", shadow_, 0UL);
        send<void>(light_encoder, "setFragmentTexture:atIndex:", visibility_[2], 4UL);
        send<void>(light_encoder, "setFragmentTexture:atIndex:", visibility_[3], 7UL);
        send<void>(light_encoder, "setFragmentTexture:atIndex:", visibility_depth_, 5UL);
        if (compact_camera_) {
            send<void>(light_encoder, "setFragmentBuffer:offset:atIndex:", camera_map_, 0UL, 8UL);
            send<void>(light_encoder, "setFragmentBuffer:offset:atIndex:", camera_records_, 0UL, 9UL);
        }
        send<void>(light_encoder, "drawPrimitives:vertexStart:vertexCount:", 3UL, 0UL, 3UL);
        send<void>(light_encoder, "endEncoding");
        light_visibility_ready_ = true;
        ++statistics_.light_visibility_builds;
    } else if (retained_)
        ++statistics_.light_visibility_reuses;
    id pass = send<id>(type("MTLRenderPassDescriptor"), "renderPassDescriptor");
    attachment = send<id>(send<id>(pass, "colorAttachments"), "objectAtIndexedSubscript:", 0UL);
    id texture = send<id>(drawable, "texture");
    send<void>(attachment, "setTexture:", samples_ > 1 ? color_ : texture);
    if (samples_ > 1)
        send<void>(attachment, "setResolveTexture:", texture);
    send<void>(attachment, "setLoadAction:", 2UL);
    send<void>(attachment, "setStoreAction:", samples_ > 1 ? 2UL : 1UL);
    send<void>(attachment, "setClearColor:", ClearColor{0.0874, 0.209, 0.225, 1});
    attachment = send<id>(pass, "depthAttachment");
    send<void>(attachment, "setTexture:", depth_);
    send<void>(attachment, "setLoadAction:", 2UL);
    send<void>(attachment, "setStoreAction:", 0UL);
    send<void>(attachment, "setClearDepth:", 1.0);
    encoder = send<id>(command, "renderCommandEncoderWithDescriptor:", pass);
    send<void>(encoder,
               "setRenderPipelineState:", retained_ ? restore_pipeline_ : background_pipeline_);
    send<void>(encoder, "setFragmentBytes:length:atIndex:", static_cast<const void*>(&uniforms),
               static_cast<unsigned long>(sizeof(uniforms)), 3UL);
    if (retained_) {
        send<void>(encoder, "setDepthStencilState:", restore_depth_state_);
        send<void>(encoder, "setFragmentTexture:atIndex:", shadow_, 0UL);
        for (unsigned long index = 0; index < visibility_.size(); ++index)
            send<void>(encoder, "setFragmentTexture:atIndex:", visibility_[index],
                       visibility_slots[index]);
        send<void>(encoder, "setFragmentTexture:atIndex:", visibility_depth_, 5UL);
        send<void>(encoder, "setFragmentTexture:atIndex:", light_visibility_, 6UL);
        if (record_shading_)
            send<void>(encoder, "setFragmentBuffer:offset:atIndex:", record_color_, 0UL, 10UL);
        if (compact_camera_) {
            send<void>(encoder, "setFragmentBuffer:offset:atIndex:", camera_map_, 0UL, 8UL);
            send<void>(encoder, "setFragmentBuffer:offset:atIndex:", camera_records_, 0UL, 9UL);
        }
    }
    send<void>(encoder, "drawPrimitives:vertexStart:vertexCount:", 3UL, 0UL, 3UL);
    encode_geometry(encoder, scene, pipeline_, &uniforms, sizeof(uniforms),
                    retained_ ? DrawSet::uncached_surface : DrawSet::all, !conv_fast_);
    if (conv_fast_)
        encode_geometry(encoder, scene, conv_pipeline_, &uniforms, sizeof(uniforms));
    send<void>(command, "presentDrawable:", drawable);
    send<void>(command, "commit");
    pending_[pending_count_++] = PendingCommand{apple::retain(command), updated_shadows};
    ++statistics_.frames;
    statistics_.cpu_submit_seconds += std::chrono::duration<double>(Clock::now() - start).count();
    if (capture_path != nullptr) {
        send<void>(command, "waitUntilCompleted");
        if (send<unsigned long>(command, "status") == 5UL) {
            error_ = error_text(send<id>(command, "error"));
            visibility_ready_ = false;
            fixed_shadow_ready_ = false;
            return false;
        }
        return save_png(texture, queue_, width_, height_, capture_path);
    }
    return true;
}
} // namespace stillwater
