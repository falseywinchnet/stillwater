// Camera registration runs only after fixed visibility changes. Threadgroups
// reserve deterministic disjoint ranges; no atomics, global append or world mesh
// traversal is needed to read the registered view during animation.
CameraRecord acquire_record(uint2 pixel,uint sample,SW_TEXTURE<half> base,
 SW_TEXTURE<half> surface,SW_TEXTURE<float> distance,SW_TEXTURE<half> correction) {
    half4 b=SW_READ(base,pixel,sample),s=SW_READ(surface,pixel,sample);
    float2 d=SW_READ(distance,pixel,sample).rg;
    half2 c=SW_READ(correction,pixel,sample).rg;
    CameraRecord record={{as_type<uint>(b.xy),as_type<uint>(b.zw),
        as_type<uint>(s.xy),as_type<uint>(s.zw),as_type<uint>(d.x),
        as_type<uint>(d.y),as_type<uint>(c)}};
    return record;
}
bool same_record(thread const CameraRecord& a,thread const CameraRecord& b) {
    for(uint word=0;word<7;++word) if(a.words[word]!=b.words[word]) return false;
    return true;
}
kernel void classify_camera(SW_TEXTURE<half> base [[texture(0)]],
 SW_TEXTURE<half> surface [[texture(1)]],SW_TEXTURE<float> distance [[texture(2)]],
 SW_TEXTURE<half> correction [[texture(3)]],device uint2* map [[buffer(0)]],
 device uint* counts [[buffer(1)]],uint index [[thread_position_in_grid]],
 uint lane [[thread_index_in_threadgroup]],uint group [[threadgroup_position_in_grid]]) {
    uint pixels=base.get_width()*base.get_height();
    uint count=0,selectors=0;
    if(index<pixels) {
        uint2 pixel=uint2(index%base.get_width(),index/base.get_width());
        CameraRecord unique[4];
        for(uint sample=0;sample<SW_SAMPLES(base);++sample) {
            CameraRecord record=acquire_record(pixel,sample,base,surface,distance,correction);
            uint selected=0;
            while(selected<count&&!same_record(record,unique[selected])) ++selected;
            if(selected==count) unique[count++]=record;
            selectors|=selected<<(sample*2);
        }
    }
    threadgroup uint scan[256];
    scan[lane]=count;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for(uint stride=1;stride<256;stride*=2) {
        uint previous=lane>=stride?scan[lane-stride]:0;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        scan[lane]+=previous;
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if(index<pixels) map[index]=uint2(scan[lane]-count,selectors);
    if(lane==255) counts[group]=scan[lane];
}
kernel void pack_camera(SW_TEXTURE<half> base [[texture(0)]],
 SW_TEXTURE<half> surface [[texture(1)]],SW_TEXTURE<float> distance [[texture(2)]],
 SW_TEXTURE<half> correction [[texture(3)]],device uint2* map [[buffer(0)]],
 const device uint* offsets [[buffer(1)]],device CameraRecord* records [[buffer(2)]],
 uint index [[thread_position_in_grid]],uint group [[threadgroup_position_in_grid]]) {
    if(index>=base.get_width()*base.get_height()) return;
    uint2 pixel=uint2(index%base.get_width(),index/base.get_width());
    uint2 entry=map[index];
    entry.x+=offsets[group];
    map[index]=entry;
    uint written=0;
    for(uint sample=0;sample<SW_SAMPLES(base);++sample) {
        uint selected=(entry.y>>(sample*2))&3;
        if(selected==written) {
            records[entry.x+written]=acquire_record(pixel,sample,base,surface,distance,correction);
            ++written;
        }
    }
}
// Diagnostic inverse: compare every reconstructed sample with the acquisition
// attachments before releasing them. Enabled only by --verify-retained.
kernel void validate_camera(SW_TEXTURE<half> base [[texture(0)]],
 SW_TEXTURE<half> surface [[texture(1)]],SW_TEXTURE<float> distance [[texture(2)]],
 SW_TEXTURE<half> correction [[texture(3)]],const device uint2* map [[buffer(0)]],
 const device CameraRecord* records [[buffer(1)]],device atomic_uint& errors [[buffer(2)]],
 uint index [[thread_position_in_grid]]) {
    if(index>=base.get_width()*base.get_height()) return;
    uint2 pixel=uint2(index%base.get_width(),index/base.get_width());
    for(uint sample=0;sample<SW_SAMPLES(base);++sample) {
        CameraRecord expected=acquire_record(pixel,sample,base,surface,distance,correction);
        CameraRecord actual=camera_record(pixel,sample,base.get_width(),map,records);
        if(!same_record(expected,actual)) atomic_fetch_add_explicit(&errors,1,memory_order_relaxed);
    }
}
#ifdef SW_SHADE_RECORDS
// Shade a distinct camera surface once, then let coverage samples share its
// already-quantized render-target color. Shadow visibility stays half precision.
kernel void shade_camera(const device uint2* map [[buffer(0)]],
 const device CameraRecord* records [[buffer(1)]],device uint* colors [[buffer(2)]],
 constant Uniforms& u [[buffer(3)]],device half* lights [[buffer(4)]],
 depth2d<float> shadow [[texture(0)]],uint index [[thread_position_in_grid]]) {
    uint2 pixel=uint2(index%uint(u.clock.y),index/uint(u.clock.y));
    uint2 entry=map[index];
    uint count=1+max(max(entry.y&3,(entry.y>>2)&3),max((entry.y>>4)&3,(entry.y>>6)&3));
    for(uint selected=0;selected<count;++selected) {
        uint address=entry.x+selected;
        CameraRecord record=records[address];
        float3 color;
        if(record.words[5]==0) {
            float2 uv=(float2(pixel)+0.5f)/u.clock.yz;
            uv.y=1-uv.y;
            color=pow(max(1-exp(-water_color(uv)*1.6f),0.0f),float3(1.0f/2.2f));
        } else {
            float4 base=float4(half4(as_type<half2>(record.words[0]),as_type<half2>(record.words[1])));
            float4 surface=float4(half4(as_type<half2>(record.words[2]),as_type<half2>(record.words[3])));
            LightingTerms terms={base.rgb,surface.rgb,base.a,surface.a};
            float distance=as_type<float>(record.words[4]);
            float2 correction=float2(as_type<half2>(record.words[6]));
            float3 world=retained_world(pixel,distance,correction,u.reconstruction,u.clock.yz);
            if(u.clock.w!=0) lights[address]=half(visibility(u.light*float4(world,1),shadow));
            color=illuminate_fixed(terms,world,u,float(lights[address]));
        }
        uint3 codes=uint3(rint(clamp(color,0.0f,1.0f)*255.0f));
        colors[address]=codes.r|(codes.g<<8)|(codes.b<<16);
    }
}
#endif
