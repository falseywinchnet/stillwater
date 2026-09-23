// Riverscape strand motion and selected material formulas adapted from Desktop Habitats.
// Copyright (c) 2026 Chase Lean, MIT; see licenses/desktop-habitats-MIT.txt.
#include <metal_stdlib>
using namespace metal;
#if SW_SINGLE_SAMPLE
#define SW_TEXTURE texture2d
#define SW_DEPTH depth2d
#define SW_READ(texture, pixel, sample) texture.read(pixel)
#define SW_SAMPLES(texture) 1u
#else
#define SW_TEXTURE texture2d_ms
#define SW_DEPTH depth2d_ms
#define SW_READ(texture, pixel, sample) texture.read(pixel, sample)
#define SW_SAMPLES(texture) texture.get_num_samples()
#endif
struct Vertex { float4 position;float4 normal;float4 color;float4 uv;float4 anchor;float4 bend;float4 along;float4 binding; };
struct Instance { float4x4 transform;float4 color;float4 behavior;float4 anatomy; };
struct Actor { float4 center;float4 cruise;float4 startled_from;float4 escape;float4 traits; };
struct Uniforms { float4x4 camera;float4x4 light;float4x4 reconstruction;float4 clock;float4 eye;float4 illumination; };
struct Out {
    float4 position [[position]];
    float3 world;float3 normal;float3 local;
    float4 color;float4 behavior;float4 light_position;float4 uv;float4 surface;
    uint identity [[flat]];
};
float3 water_color(float2 uv) {
    return float3(0.0055f,0.023f,0.015f) * (0.82f+0.18f*uv.y);
}
float3 prototype_water_color(float2 uv) {
    float light=exp(-pow((uv.x-0.29f)*2.2f,2.0f)-pow((uv.y-0.93f)*2.4f,2.0f));
    float3 water=mix(float3(0.001f,0.008f,0.012f),float3(0.004f,0.026f,0.030f),uv.y);
    water+=float3(0.007f,0.024f,0.019f)*light;
    float shaft=pow(max(0.0f,sin((uv.x+uv.y*0.16f)*45)),18.0f)*0.035f*light;
    return water+float3(0.14f,0.25f,0.16f)*shaft;
}
struct WaterOut { float4 position [[position]];float2 uv; };
vertex WaterOut water_vertex(uint index [[vertex_id]]) {
    float2 p=index==0 ? float2(-1,-1) : (index==1 ? float2(3,-1) : float2(-1,3));
    WaterOut out;out.position=float4(p,0.9999f,1);out.uv=p*0.5f+0.5f;return out;
}
fragment float4 water_fragment(WaterOut in [[stage_in]]) {
    float3 color=1-exp(-water_color(in.uv)*1.6f);color=pow(max(color,0.0f),float3(1.0f/2.2f));return float4(color,1);
}
float3 rotate_y(float3 p,float angle) {
    float c=cos(angle),s=sin(angle);return float3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);
}
float3 actor_position(Actor actor,float time) {
    float since=time-actor.startled_from.w;
    if(actor.escape.w>0 && since<actor.escape.w) {
        float f=clamp(since/actor.escape.w,0.0f,1.0f);
        return mix(actor.startled_from.xyz,actor.escape.xyz,1-pow(1-f,3.0f));
    }
    float elapsed=actor.escape.w>0 ? since-actor.escape.w : time;
    float angle=elapsed*actor.cruise.z+actor.cruise.w;
    return actor.center.xyz+float3(actor.cruise.x*sin(angle),actor.cruise.y*sin(2*angle),actor.cruise.x*0.25f*(cos(angle)-1));
}
float2 strand_motion(float3 root, float3 direction, float distance, float compliance, float time) {
    float strength=0.34f*sin(time*0.031f)+0.15f*sin(time*0.055f-root.x*0.34f-root.z*0.19f)
        +0.03f*sin(time*0.235f+root.x*1.7f+root.z*1.1f)+0.03f*sin(time*0.155f+root.x*0.6f-root.z*2.3f);
    float seed=fract(sin(root.x*12.9898f+root.z*78.233f)*43758.5453f);
    float phase=seed*6.2832f+root.x*0.9f;
    float drag=compliance*dot(normalize(float3(1,0,0.22f)),direction)*strength;
    float saturation=1+0.06f*distance*distance;
    float amount=drag*0.09f*distance*distance/saturation;
    float slope=drag*0.18f*distance/(saturation*saturation);
    float gain=compliance*(0.012f+0.02f*strength);
    float safe_distance=max(distance,0.0001f), power=pow(safe_distance,0.3f);
    float envelope=gain*safe_distance*power, envelope_slope=gain*1.3f*power;
    float theta=time*0.95f-1.05f*distance+phase;
    float ripple=time*1.55f-1.7f*distance+phase*2.3f;
    float shape=sin(theta)+0.3f*sin(ripple);
    return float2(amount+envelope*shape,slope+envelope_slope*shape-envelope*(1.05f*cos(theta)+0.51f*cos(ripple)));
}
Out prepare(Vertex sample,Instance instance,constant Actor* actors,constant Uniforms& u) {
    float3 p=sample.position.xyz,n=sample.normal.xyz;float time=u.clock.x;
    const int material=int(instance.behavior.x+0.5f);
    if(material==10) {
        float2 motion=strand_motion(sample.anchor.xyz,sample.bend.xyz,sample.along.w,sample.bend.w,time);
        p+=sample.bend.xyz*motion.x;
        n=normalize(n-sample.along.xyz*(motion.y*dot(sample.bend.xyz,n)));
    }
    if(material==11 || material==12) {
        float aft=max(0.0f,0.20f-p.x), phase=time*7.0f+instance.behavior.w*1.7f-p.x*6;
        float bend=0.19f*aft*aft*sin(phase);
        float slope=-0.38f*aft*sin(phase)-1.14f*aft*aft*cos(phase);
        p.z+=bend;
        n=normalize(float3(n.x-slope*n.z,n.y,n.z));
        if(material==12) p.z+=sample.uv.w*0.006f*sin(time*12+sample.uv.z*1.4f);
    }
    if(material==1) {
        float seed=instance.behavior.y;
        float bulge=1+0.11f*sin(p.x*4+seed)*sin(p.y*5+seed*0.71f)*sin(p.z*4+seed*0.31f);
        p*=bulge;
    }
    if(material==2) {
        float height=p.y;
        p.x+=height*0.25f*sin(height*5+instance.behavior.y)+height*height*instance.behavior.z*(sin(time*0.6f+instance.behavior.y)+0.3f*sin(time*1.1f+height*4));
        p.z+=height*height*(0.4f+0.25f*sin(time*0.7f+instance.behavior.y+height*2));
        n=normalize(float3(-height*instance.behavior.z*0.3f,0.12f,1));
    }
    if(instance.anatomy.x==1) {
        // Tail fan grows backward from its attachment, with a true oscillating joint.
        p=float3(-p.y,p.x,p.z*0.5f);
        p=rotate_y(p,sin(time*6+instance.behavior.w)*0.45f);
        n=rotate_y(n,sin(time*6+instance.behavior.w)*0.45f);
    }
    float3 world=(instance.transform*float4(p,1)).xyz;
    float3x3 basis=float3x3(instance.transform[0].xyz,instance.transform[1].xyz,instance.transform[2].xyz);
    float3 scale2=float3(dot(basis[0],basis[0]),dot(basis[1],basis[1]),dot(basis[2],basis[2]));
    n=normalize(basis*(n/max(scale2,float3(0.00001f))));
    if(material==3) {
        float height=fmod(time*instance.behavior.z+instance.behavior.y,10.0f);
        world.y+=height;world.x+=0.10f*sin(height*2+instance.behavior.y);
    }
    if(instance.behavior.w>=0) {
        Actor actor=actors[int(instance.behavior.w)];
        float elapsed=actor.escape.w>0 ? time-actor.startled_from.w-actor.escape.w : time;
        float angle=elapsed*actor.cruise.z+actor.cruise.w;
        float heading=atan2(0.25f*sin(angle),cos(angle));
        if(actor.escape.w>0 && elapsed<0) {
            float3 flight=actor.escape.xyz-actor.startled_from.xyz;
            heading=atan2(-flight.z,flight.x);
        }
        if(actor.traits.x>0.5f) heading=0;
        if(instance.anatomy.x==3) world.y+=0.04f*sin(time*4+instance.behavior.y*1.7f+instance.anatomy.y);
        world=rotate_y(world*actor.center.w,heading)+actor_position(actor,time);
        n=rotate_y(n,heading);
    }
    Out out;out.world=world;out.normal=n;out.local=p;out.color=instance.color;out.behavior=instance.behavior;
    out.uv=sample.uv;out.surface=float4(sample.normal.w,sample.anchor.w,sample.binding.z,0);
    if(sample.binding.z>0.5f) out.color*=sample.color;
    out.position=u.camera*float4(world,1);out.light_position=u.light*float4(world,1);return out;
}
vertex Out tank_vertex(uint vertex_id [[vertex_id]],uint instance_id [[instance_id]],
                       constant Vertex* vertices [[buffer(0)]],constant Instance* instances [[buffer(1)]],
                       constant Actor* actors [[buffer(2)]],constant Uniforms& u [[buffer(3)]]) {
    Vertex sample=vertices[vertex_id];
    uint index=sample.binding.y>0.5f ? uint(sample.binding.x) : instance_id;
    Out out=prepare(sample,instances[index],actors,u);out.identity=index+1;return out;
}
vertex float4 shadow_vertex(uint vertex_id [[vertex_id]],uint instance_id [[instance_id]],
                            constant Vertex* vertices [[buffer(0)]],constant Instance* instances [[buffer(1)]],
                            constant Actor* actors [[buffer(2)]],constant Uniforms& u [[buffer(3)]]) {
    Vertex sample=vertices[vertex_id];
    uint index=sample.binding.y>0.5f ? uint(sample.binding.x) : instance_id;
    Out out=prepare(sample,instances[index],actors,u);return out.light_position;
}
float hash(float3 p) { return fract(sin(dot(p,float3(127.1f,311.7f,74.7f)))*43758.5453f); }
float noise(float3 p) {
    float3 cell=floor(p),f=fract(p);f=f*f*(3-2*f);
    float a=mix(hash(cell),hash(cell+float3(1,0,0)),f.x);
    float b=mix(hash(cell+float3(0,1,0)),hash(cell+float3(1,1,0)),f.x);
    float c=mix(hash(cell+float3(0,0,1)),hash(cell+float3(1,0,1)),f.x);
    float d=mix(hash(cell+float3(0,1,1)),hash(cell+float3(1,1,1)),f.x);
    return mix(mix(a,b,f.y),mix(c,d,f.y),f.z);
}
float visibility(float4 clip,depth2d<float> map) {
    constexpr sampler sample_state(coord::normalized,address::clamp_to_edge,filter::linear,compare_func::less_equal);
    float3 p=clip.xyz/clip.w;float2 uv=float2(p.x*0.5f+0.5f,0.5f-p.y*0.5f);
    if(any(uv<0)||any(uv>1)||p.z<0||p.z>1) return 1;
    float result=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) result+=map.sample_compare(sample_state,uv+float2(x,y)*1.5f/2048.0f,p.z-0.0018f);
    return result/9;
}
// ACES fit used by Three.js, MIT; licenses/THREE-MIT.txt.
float3 aces(float3 value) {
    const float3x3 input=float3x3(float3(0.59719f,0.076f,0.0284f),float3(0.35458f,0.90834f,0.13383f),float3(0.04823f,0.01566f,0.83777f));
    const float3x3 output=float3x3(float3(1.60475f,-0.10208f,-0.00327f),float3(-0.53108f,1.10813f,-0.07276f),float3(-0.07367f,-0.00605f,1.07602f));
    float3 x=input*(value*(1.17f/0.6f));
    float3 numerator=x*(x+0.0245786f)-0.000090537f;
    float3 denominator=x*(0.983729f*x+0.432951f)+0.238081f;
    return clamp(output*(numerator/denominator),0.0f,1.0f);
}
float3 mapped_normal(float3 normal,float3 point,float2 uv,float3 sample,float strength) {
    float3 dp1=dfdx(point),dp2=dfdy(point);
    float2 duv1=dfdx(uv),duv2=dfdy(uv);
    float3 dp2perp=cross(dp2,normal),dp1perp=cross(normal,dp1);
    float3 tangent=dp2perp*duv1.x+dp1perp*duv2.x;
    float3 bitangent=dp2perp*duv1.y+dp1perp*duv2.y;
    float divisor=max(dot(tangent,tangent),dot(bitangent,bitangent));
    if(divisor<1e-12f) return normal;
    float scale=rsqrt(divisor);
    sample=sample*2-1;sample.xy*=strength;
    return normalize(tangent*scale*sample.x+bitangent*scale*sample.y+normal*sample.z);
}
float3 fish_skin(Out in) {
    float part=in.uv.z,band=clamp(in.uv.y,0.0f,1.0f),x=in.local.x;
    if(part<0.5f) {
        float3 skin=mix(float3(0.0105f,0.015f,0.0125f),float3(0.034f,0.049f,0.043f),smoothstep(0.02f,0.135f,band));
        skin=mix(skin,float3(0.47f,0.51f,0.5f),smoothstep(0.185f,0.42f,band));
        float belly=smoothstep(-0.26f,-0.12f,x);
        skin=mix(skin,float3(0.655f,0.66f,0.63f),smoothstep(0.52f,0.84f,band)*belly);
        float sheen=exp(-pow((band-0.25f)/0.07f,2.0f))*smoothstep(-0.285f,-0.225f,x)*(1-smoothstep(0.188f,0.245f,x));
        skin=mix(skin,float3(0.08f,0.41f,0.62f),sheen*0.82f);
        float warm=(1-smoothstep(-0.27f,0.0f,x))*smoothstep(0.32f,0.60f,band)*(1-smoothstep(0.88f,1.0f,band));
        skin=mix(skin,float3(0.42f,0.105f,0.03f),warm*0.45f);
        float head=smoothstep(0.175f,0.22f,x);
        float3 cheek=mix(float3(0.04f,0.052f,0.046f),float3(0.42f,0.44f,0.42f),smoothstep(0.13f,0.4f,band));
        skin=mix(skin,cheek,head*0.92f);
        skin=mix(skin,float3(0.05f,0.056f,0.046f),smoothstep(0.25f,0.33f,x)*0.82f);
        float opercle=0.196f-0.03f*(1-pow(clamp((in.local.y+0.004f)/0.078f,-1.0f,1.0f),2.0f));
        skin*=1-0.5f*exp(-pow((x-opercle)/0.0028f,2.0f));
        return skin;
    }
    if(part<6.5f || part>11.5f) {
        float ribs=pow(0.5f+0.5f*cos(in.uv.x*6.283185f*(part<1.5f ? 18 : 11)),12.0f);
        float3 membrane=mix(float3(0.32f,0.20f,0.14f),float3(0.45f,0.035f,0.012f),smoothstep(0.2f,0.8f,in.uv.y));
        return membrane*(0.65f+0.6f*ribs);
    }
    if(part<7.5f) return mix(float3(0.62f,0.6f,0.415f),float3(0.33f,0.30f,0.15f),in.uv.y);
    if(part<8.5f) return float3(0.0055f,0.0075f,0.0085f);
    if(part<9.5f) return float3(0.036f,0.020f,0.018f);
    return float3(0.175f,0.168f,0.132f);
}
struct Surface { float3 normal; float3 albedo; float alpha; };
Surface riverscape_surface(Out in,texture2d<float> sand,texture2d<float> sand_normal,
 texture2d<float> rock,texture2d<float> rock_normal,texture2d<float> wood,
 texture2d<float> wood_normal,bool front) {
    constexpr sampler surface_sampler(coord::normalized,address::repeat,filter::linear,mip_filter::linear,max_anisotropy(8));
    int material=int(in.behavior.x+0.5f);
    float3 normal=normalize(front ? in.normal : -in.normal),albedo=in.color.rgb;
    float2 uv=in.uv.xy*in.behavior.yz;
    float3 detail=float3(0.5f,0.5f,1);
    float alpha=1;
    if(material==7) { albedo*=sand.sample(surface_sampler,uv).rgb;detail=sand_normal.sample(surface_sampler,uv).rgb; }
    if(material==8) { albedo*=rock.sample(surface_sampler,uv).rgb;detail=rock_normal.sample(surface_sampler,uv).rgb; }
    if(material==9) { albedo*=wood.sample(surface_sampler,uv).rgb;detail=wood_normal.sample(surface_sampler,uv).rgb; }
    if(material>=7 && material<=9) {
        normal=mapped_normal(normal,in.world,uv,detail,material==7 ? 0.32f : 0.8f);
        float fine=noise(in.world*9)*0.6f+noise(in.world*27)*0.4f;
        float moss=smoothstep(0.07f,0.5f,in.surface.x+(fine-0.5f)*0.45f);
        float3 film=material==7 ? float3(0.10f,0.10f,0.02f) : float3(0.03f,0.055f,0.007f);
        float3 turf=float3(0.0035f,0.013f,0.0025f);
        float3 moss_color=mix(film,turf,smoothstep(0.15f,0.85f,in.surface.x))*(0.6f+0.8f*fine);
        albedo=mix(albedo,moss_color*in.color.rgb,moss);
    }
    if(material==10) {
        float2 leaf=in.uv.xy;
        float edge=pow(abs(leaf.x-0.5f)*2,5.0f);
        float midrib=(1-smoothstep(0.008f,0.035f,abs(leaf.x-0.5f)))*(1-smoothstep(0.02f,0.06f,fwidth(leaf.x)));
        float veins=pow(0.5f+0.5f*cos((leaf.y-abs(leaf.x-0.5f)*0.32f)*155),22.0f);
        albedo*=(0.965f+0.035f*sin(leaf.y*64+sin(leaf.x*25)))*(1-0.09f*edge+0.12f*veins);
        albedo=mix(albedo,albedo*1.22f+float3(0.008f,0.012f,0),midrib*0.6f);
        if(!front) albedo*=float3(0.82f,0.76f,0.66f);
        if(in.surface.y>=0.7f) alpha=edge>0.45f ? 0.5f : 0.75f;
    }
    if(material==11 || material==12) albedo=fish_skin(in);
    if(material==12) alpha=0.55f+0.35f*(1-in.uv.w);
    return {normal,albedo,alpha};
}
struct LightingTerms { float3 base; float3 surface; float direct; float caustic; };
LightingTerms fixed_lighting(Out in,Surface surface,constant Uniforms& u) {
    float3 n=surface.normal,albedo=surface.albedo;
    float3 light=normalize(float3(0,0.9138f,0.4061f));
    float3 hemisphere=mix(float3(0.035f,0.028f,0.016f),float3(0.10f,0.13f,0.085f),n.y*0.5f+0.5f);
    float3 base=albedo*hemisphere+albedo*float3(0.06f,0.085f,0.10f)*max(0.0f,dot(n,normalize(float3(1,5,10))));
    float direct=max(0.0f,dot(n,light));
    int material=int(in.behavior.x+0.5f);
    float caustic=material>=7 && material<=9 ? max(n.y,0.0f) : 0.0f;
    float depth=max(0.0f,length(u.eye.xyz-in.world)-16);
    float fog=1-exp(-depth*depth*0.001156f);
    float3 transmission=exp(-float3(0.030f,0.006f,0.016f)*depth)*(1-fog);
    return {base*transmission+water_color(float2(0.5f))*fog,albedo*transmission,direct,caustic};
}
float3 illuminate_fixed(LightingTerms terms,float3 world,constant Uniforms& u,float visible) {
    float2 q=world.xz*2;
    float wave=sin(q.x+sin(q.y*1.4f+u.clock.x*0.2f))+sin(q.y+sin(q.x*1.2f-u.clock.x*0.17f));
    float caustic=pow(max(0.0f,1-abs(wave)*1.8f),8.0f);
    float3 direct=terms.surface*float3(1.43f,1.39f,1.28f)*terms.direct;
    float3 focused=terms.surface*float3(0.18f,0.25f,0.13f)*terms.caustic;
    float3 linear=terms.base+(direct+focused*caustic)*(visible*u.illumination.x);
    return pow(aces(linear),float3(1.0f/2.2f));
}
struct CachedSurface {
    half4 base [[color(0)]];
    half4 surface [[color(1)]];
    float2 distance_identity [[color(2)]];
    half2 center_correction [[color(3)]];
};
fragment CachedSurface retain_surface(Out in [[stage_in]],constant Uniforms& u [[buffer(3)]],
 texture2d<float> sand [[texture(1)]],texture2d<float> sand_normal [[texture(2)]],
 texture2d<float> rock [[texture(3)]],texture2d<float> rock_normal [[texture(4)]],
 texture2d<float> wood [[texture(5)]],texture2d<float> wood_normal [[texture(6)]],bool front [[front_facing]]) {
    Surface surface=riverscape_surface(in,sand,sand_normal,rock,rock_normal,wood,wood_normal,front);
    LightingTerms terms=fixed_lighting(in,surface,u);
    float4 clip=u.camera*float4(in.world,1);
    float2 center=in.position.xy/u.clock.yz*float2(2,-2)+float2(-1,1);
    // Preserve the raster interpolator's small displacement from the ideal ray.
    // Without this correction, rare hard-shadow edges change by several codes.
    half2 correction=half2(clip.xy/clip.w-center);
    return {half4(half3(terms.base),half(terms.direct)),half4(half3(terms.surface),half(terms.caustic)),
            float2(clip.w,float(in.identity)),correction};
}
// Camera acquisition shades at pixel centers, whereas raster depth varies per
// MSAA sample. Retain the center's linear camera distance: unprojecting the
// per-sample depth would move caustics and shadow receivers at polygon edges.
float3 retained_world(uint2 pixel,float distance,float2 correction,
                      constant float4x4& reconstruction,float2 dimensions) {
    float2 ndc=(float2(pixel)+0.5f)/dimensions*float2(2,-2)+float2(-1,1);
    float4 world=reconstruction*float4((ndc+correction)*distance,-distance,1);
    return world.xyz/world.w;
}
// Each record stores the original seven 32-bit words, with no quantization.
// Per-pixel selectors preserve the raster sample's visible surface identity.
#ifdef SW_COMPACT_CAMERA
struct CameraRecord { uint words[7]; };
#define SW_CAMERA_INPUT ,const device uint2* camera_map [[buffer(8)]],const device CameraRecord* camera_records [[buffer(9)]]
CameraRecord camera_record(uint2 pixel,uint sample,uint width,
 const device uint2* map,const device CameraRecord* records) {
    uint2 entry=map[pixel.y*width+pixel.x];
    return records[entry.x+((entry.y>>(sample*2))&3)];
}
#else
#define SW_CAMERA_INPUT
#endif
struct RestoredSurface { float4 color [[color(0)]]; float depth [[depth(any)]]; };
fragment RestoredSurface restore_surface(WaterOut in [[stage_in]],uint sample [[sample_id]],
 constant Uniforms& u [[buffer(3)]],depth2d<float> shadow [[texture(0)]],
 SW_TEXTURE<half> base [[texture(1)]],SW_TEXTURE<half> surface [[texture(2)]],
 SW_TEXTURE<float> distance_identity [[texture(4)]],SW_TEXTURE<half> center_correction [[texture(7)]],
 SW_DEPTH<float> depth [[texture(5)]],SW_TEXTURE<half> light_visibility [[texture(6)]] SW_CAMERA_INPUT) {
    uint2 pixel=uint2(in.position.xy);
    float z=SW_READ(depth,pixel,sample);
    if(z>=1) {
        float3 color=pow(max(1-exp(-water_color(in.uv)*1.6f),0.0f),float3(1.0f/2.2f));
        return {float4(color,1),1};
    }
#ifdef SW_COMPACT_CAMERA
    CameraRecord record=camera_record(pixel,sample,uint(u.clock.y),camera_map,camera_records);
    float4 cached_base=float4(half4(as_type<half2>(record.words[0]),as_type<half2>(record.words[1])));
    float4 cached_surface=float4(half4(as_type<half2>(record.words[2]),as_type<half2>(record.words[3])));
#else
    float4 cached_base=float4(SW_READ(base,pixel,sample)),cached_surface=float4(SW_READ(surface,pixel,sample));
#endif
    LightingTerms terms={cached_base.rgb,cached_surface.rgb,cached_base.a,cached_surface.a};
#ifdef SW_COMPACT_CAMERA
    float distance=as_type<float>(record.words[4]);
    float2 correction=float2(as_type<half2>(record.words[6]));
#else
    float distance=SW_READ(distance_identity,pixel,sample).r;
    float2 correction=float2(SW_READ(center_correction,pixel,sample).rg);
#endif
    float3 world=retained_world(pixel,distance,correction,u.reconstruction,u.clock.yz);
    float visible=float(SW_READ(light_visibility,pixel,sample).r);
    float3 color=illuminate_fixed(terms,world,u,visible);
    return {float4(color,1),z};
}
// This source-to-receiver visibility is recomputed only when its shadow map or
// retained camera surfaces change; animated caustics remain evaluated every frame.
fragment half retain_light_visibility(WaterOut in [[stage_in]],uint sample [[sample_id]],
 constant Uniforms& u [[buffer(3)]],depth2d<float> shadow [[texture(0)]],
 SW_TEXTURE<float> distance_identity [[texture(4)]],SW_TEXTURE<half> center_correction [[texture(7)]],SW_DEPTH<float> depth [[texture(5)]] SW_CAMERA_INPUT) {
    uint2 pixel=uint2(in.position.xy);
    if(SW_READ(depth,pixel,sample)>=1) return half(1);
#ifdef SW_COMPACT_CAMERA
    CameraRecord record=camera_record(pixel,sample,uint(u.clock.y),camera_map,camera_records);
    float distance=as_type<float>(record.words[4]);
    float2 correction=float2(as_type<half2>(record.words[6]));
#else
    float distance=SW_READ(distance_identity,pixel,sample).r;
    float2 correction=float2(SW_READ(center_correction,pixel,sample).rg);
#endif
    float3 world=retained_world(pixel,distance,correction,u.reconstruction,u.clock.yz);
    float visible=visibility(u.light*float4(world,1),shadow);
    return half(visible);
}
struct VisibilityProbe { float4 samples[4]; float4 depths; };
kernel void query_visibility(SW_TEXTURE<float> distance_identity [[texture(0)]],
 SW_DEPTH<float> depth [[texture(1)]],SW_TEXTURE<half> center_correction [[texture(2)]],constant uint2& pixel [[buffer(0)]],
 device VisibilityProbe& result [[buffer(1)]],constant float4x4& reconstruction [[buffer(2)]],
 constant float4& dimensions [[buffer(3)]] SW_CAMERA_INPUT) {
    result.depths=float4(1);
    for(uint index=0;index<4;++index) {
        result.samples[index]=float4(0);
        if(index<SW_SAMPLES(depth)) {
#ifdef SW_COMPACT_CAMERA
            CameraRecord record=camera_record(pixel,index,uint(dimensions.x),camera_map,camera_records);
            float2 cached=float2(as_type<float>(record.words[4]),as_type<float>(record.words[5]));
#else
            float2 cached=SW_READ(distance_identity,pixel,index).rg;
#endif
            if(cached.y!=0) {
#ifdef SW_COMPACT_CAMERA
                float2 correction=float2(as_type<half2>(record.words[6]));
#else
                float2 correction=float2(SW_READ(center_correction,pixel,index).rg);
#endif
                float3 world=retained_world(pixel,cached.x,correction,reconstruction,dimensions.xy);
                result.samples[index]=float4(world,cached.y);
            }
            result.depths[index]=SW_READ(depth,pixel,index);
        }
    }
}
float4 riverscape_fragment(Out in,constant Uniforms& u,depth2d<float> shadow,
 texture2d<float> sand,texture2d<float> sand_normal,texture2d<float> rock,texture2d<float> rock_normal,
 texture2d<float> wood,texture2d<float> wood_normal,bool front) {
    Surface surface=riverscape_surface(in,sand,sand_normal,rock,rock_normal,wood,wood_normal,front);
    float3 normal=surface.normal,albedo=surface.albedo;
    float alpha=surface.alpha;
    int material=int(in.behavior.x+0.5f);
    if(material==7 || material==8 || material==9 || material==13 || material==14)
        return float4(illuminate_fixed(fixed_lighting(in,surface,u),in.world,u,visibility(in.light_position,shadow)),1);
    float3 light=normalize(float3(0,0.9138f,0.4061f)),view=normalize(u.eye.xyz-in.world);
    float direct=max(0.0f,dot(normal,light)),visibility_value=visibility(in.light_position,shadow)*u.illumination.x;
    float3 hemisphere=mix(float3(0.035f,0.028f,0.016f),float3(0.10f,0.13f,0.085f),normal.y*0.5f+0.5f);
    float3 color=albedo*(hemisphere+float3(1.43f,1.39f,1.28f)*direct*visibility_value);
    color+=albedo*float3(0.06f,0.085f,0.10f)*max(0.0f,dot(normal,normalize(float3(1,5,10))));
    if(material==10) {
        float back=max(0.0f,dot(-normal,light));
        color+=albedo*float3(0.55f,0.85f,0.30f)*back*in.surface.y*0.9f*visibility_value;
        color+=albedo*float3(0.13f,0.20f,0.075f)*max(0.0f,dot(normal,normalize(float3(2,10,-4))));
    }
    if(material==11) {
        float3 reflected=reflect(-view,normal);
        float reflector=smoothstep(0.07f,0.24f,in.uv.y)*(1-smoothstep(0.58f,0.92f,in.uv.y));
        float strip=exp(-pow((reflected.y-0.6f)*3,2.0f));
        color+=albedo*(0.25f+0.70f*strip)*reflector;
        float shine=pow(max(dot(normal,normalize(light+view)),0.0f),70.0f);
        color+=float3(0.7f,0.85f,0.85f)*shine*visibility_value;
    }
    if(material==7 || material==8 || material==9) {
        float2 q=in.world.xz*2;
        float wave=sin(q.x+sin(q.y*1.4f+u.clock.x*0.2f))+sin(q.y+sin(q.x*1.2f-u.clock.x*0.17f));
        float caustic=pow(max(0.0f,1-abs(wave)*1.8f),8.0f);
        color+=albedo*float3(0.18f,0.25f,0.13f)*caustic*max(normal.y,0.0f)*visibility_value;
    }
    // Water absorption is shallow in the foreground; there is no rear-wall geometry.
    float range=length(u.eye.xyz-in.world),depth=max(0.0f,range-16);
    color*=exp(-float3(0.030f,0.006f,0.016f)*depth);
    float fog=1-exp(-depth*depth*0.001156f);
    color=mix(color,water_color(float2(0.5f,0.5f)),fog);
    return float4(pow(aces(color),float3(1.0f/2.2f)),alpha);
}

float4 shade_tank(Out in,constant Uniforms& u,depth2d<float> shadow,
 texture2d<float> sand,texture2d<float> sand_normal,
 texture2d<float> rock,texture2d<float> rock_normal,
 texture2d<float> wood,texture2d<float> wood_normal,bool front) {
    if(in.surface.z>0.5f) return riverscape_fragment(in,u,shadow,sand,sand_normal,rock,rock_normal,wood,wood_normal,front);
    float3 n=normalize(front ? in.normal : -in.normal);
    float3 light=normalize(float3(0,0.9138f,0.4061f));float3 view=normalize(u.eye.xyz-in.world);
    int material=int(in.behavior.x+0.5f);float3 albedo=in.color.rgb;
    float grain=hash(floor(in.world*150));
    if(material==0) albedo*=0.82f+0.28f*grain+0.05f*sin(in.world.x*28+sin(in.world.z*8));
    if(material==1) {
        float stone=noise(in.world*18);albedo*=0.6f+0.6f*stone;
        float moss=smoothstep(0.15f,0.7f,n.y)*smoothstep(0.25f,0.70f,noise(in.world*4));
        albedo=mix(albedo,float3(0.055f,0.16f,0.027f),moss*0.8f);
    }
    if(material==2) {
        float vein=0.84f+0.16f*cos(in.local.x*95);
        albedo*=vein*(0.8f+0.2f*in.local.y);
    }
    if(material==4) {
        float scales=0.91f+0.09f*sin(in.local.x*55+sin(in.local.y*35));albedo*=scales;
        if(in.behavior.y<0.5f) albedo*=0.55f+0.45f*smoothstep(-0.2f,0.25f,sin(in.local.x*13));
        if(in.behavior.y>0.5f && in.behavior.y<1.5f) {
            float stripe=exp(-in.local.y*in.local.y*100);
            albedo=mix(albedo,float3(0.1f,0.88f,0.95f),stripe*0.8f);
            albedo=mix(albedo,float3(0.68f,0.035f,0.04f),smoothstep(0.0f,0.6f,-in.local.y));
        }
    }
    if(material==5) albedo*=0.7f+0.3f*cos(in.local.x*55);
    float shadow_factor=visibility(in.light_position,shadow)*u.illumination.x;
    float direct=max(dot(n,light),0.0f);
    float transmission=material==2 ? 0.24f*max(dot(-n,light),0.0f) : 0.0f;
    float3 color=albedo*(0.12f+float3(1.10f,1.13f,0.84f)*(direct*shadow_factor+transmission));
    float specular=pow(max(dot(n,normalize(light+view)),0.0f),material==4 ? 55.0f : 22.0f);
    color+=specular*shadow_factor*(material==4 ? 0.32f : 0.08f);
    // A deliberately labeled wave-light approximation, not a Snell transport solve.
    if(material<=2) {
        float2 q=in.world.xz*1.25f;
        float a=sin(q.x+sin(q.y*1.4f+u.clock.x*0.20f))+sin(q.y+sin(q.x*1.2f-u.clock.x*0.17f));
        float caustic=pow(max(0.0f,1-abs(a)*1.8f),8.0f);
        color+=albedo*float3(0.25f,0.43f,0.27f)*caustic*max(n.y,0.0f)*shadow_factor*exp(-max(0.0f,7-in.world.y)*0.1f);
    }
    if(material==3) {
        float rim=pow(1-abs(dot(n,view)),3.0f);color=float3(0.08f,0.19f,0.18f)+rim*float3(0.4f,0.62f,0.56f)+specular*0.5f;
    }
    float range=length(u.eye.xyz-in.world);
    float3 attenuation=exp(-float3(0.038f,0.010f,0.018f)*range);
    color*=attenuation;
    float fog=1-exp(-max(0.0f,range-8.0f)*0.090f);
    float3 water=water_color(float2(in.position.x/u.clock.y,1-in.position.y/u.clock.z));
    color=mix(color,water,fog);
    color=1-exp(-color*1.6f);
    color=pow(max(color,0.0f),float3(1.0f/2.2f));
    return float4(color,1);
}

fragment float4 tank_fragment(Out in [[stage_in]],constant Uniforms& u [[buffer(3)]],depth2d<float> shadow [[texture(0)]],
 texture2d<float> sand [[texture(1)]],texture2d<float> sand_normal [[texture(2)]],
 texture2d<float> rock [[texture(3)]],texture2d<float> rock_normal [[texture(4)]],
 texture2d<float> wood [[texture(5)]],texture2d<float> wood_normal [[texture(6)]],bool front [[front_facing]]) {
    return shade_tank(in,u,shadow,sand,sand_normal,rock,rock_normal,wood,wood_normal,front);
}
