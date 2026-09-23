// Experimental whole-frame 8x8 visibility quadrature. Geometry is evaluated before
// resolution; each triangle/pixel material is shaded once and shared by its samples.
// The bounded record store reports overflow instead of dropping contributors.
struct IntegrationRecord {half4 color;uint next;uint primitive;};
static_assert(sizeof(IntegrationRecord)==16,"Integration record stride");
constant uint integration_capacity=24u*1024u*1024u;
fragment void integration_fragment(ConvOut in [[stage_in]],constant Uniforms& u [[buffer(3)]],
 depth2d<float> shadow [[texture(0)]],texture2d<float> sand [[texture(1)]],
 texture2d<float> sand_normal [[texture(2)]],texture2d<float> rock [[texture(3)]],
 texture2d<float> rock_normal [[texture(4)]],texture2d<float> wood [[texture(5)]],
 texture2d<float> wood_normal [[texture(6)]],texture2d<float> leaf_grain [[texture(7)]],
 device atomic_uint* heads [[buffer(8)]],device IntegrationRecord* records [[buffer(9)]],
 device atomic_uint* counters [[buffer(10)]]) {
    if(in.twiceArea<=0)discard_fragment();
    float2 q=in.position.xy-in.origin;
    float2 edges[3]={in.e1,in.e2-in.e1,-in.e2};
    float3 centres=float3(cross2(edges[0],q),cross2(edges[1],q)+in.twiceArea,cross2(edges[2],q));
    if(any(centres<=-in.radii))discard_fragment();
    Out surface;surface.position=in.position;surface.world=in.world;surface.normal=in.normal;
    surface.local=in.local;surface.color=in.color;surface.behavior=in.behavior;
    surface.light_position=in.light_position;surface.uv=in.uv;surface.surface=in.surface;surface.identity=in.identity;
    float4 color=shade_tank(surface,u,shadow,sand,sand_normal,rock,rock_normal,wood,wood_normal,leaf_grain,in.front!=0);
    uint slot=atomic_fetch_add_explicit(counters,1u,memory_order_relaxed);
    if(slot>=integration_capacity) {atomic_store_explicit(counters+1,1u,memory_order_relaxed);return;}
    uint2 pixel=uint2(in.position.xy);
    uint previous=atomic_exchange_explicit(heads+pixel.y*uint(u.clock.y)+pixel.x,slot,memory_order_relaxed);
    IntegrationRecord record;
    record.color=half4(color);record.next=previous;record.primitive=in.order;
    records[slot]=record;
}
kernel void integration_resolve(const device uint* heads [[buffer(0)]],
 const device IntegrationRecord* records [[buffer(1)]],device atomic_uint* counters [[buffer(2)]],
 const device IntegrationGeometry* geometry [[buffer(3)]],
 texture2d<float,access::write> output [[texture(0)]],uint2 pixel [[threadgroup_position_in_grid]],
 uint lane [[thread_index_in_simdgroup]]) {
    if(pixel.x>=output.get_width() || pixel.y>=output.get_height())return;
    if(atomic_load_explicit(counters+1,memory_order_relaxed)!=0) {
        if(lane==0)output.write(float4(1,0,1,1),pixel);return;
    }
    float3 water=water_color(float2(0.5f,1-(float(pixel.y)+0.5f)/output.get_height()));
    float3 background=pow(max(1-exp(-water*1.6f),0.0f),float3(1.0f/2.2f));
    float depths[2];float3 colors[2];uint orders[2];
    for(uint i=0;i<2;++i) {depths[i]=1;colors[i]=background;orders[i]=0xffffffffu;}
    uint slot=heads[pixel.y*output.get_width()+pixel.x],count=0;
    while(slot!=0xffffffffu) {
        IntegrationRecord record=records[slot];slot=record.next;++count;
        IntegrationGeometry r=geometry[record.primitive];
        float3 color=float3(record.color.rgb);
        float alpha=float(record.color.a);
        for(uint index=0;index<2;++index) {
            uint sample=lane+index*32u;
            uint x=sample%8u,y=sample/8u;
            // Shared coverage mask approximates the existing alpha-to-coverage
            // correlation; independent of triangle partition and scheduling.
            uint rank=0;
            for(uint bit=0;bit<3;++bit) {
                uint a=(x>>bit)&1u,b=(y>>bit)&1u;
                rank|=(a^b)<<(5-2*bit);rank|=b<<(4-2*bit);
            }
            if((float(rank)+0.5f)/64>=alpha)continue;
            float2 q=float2(pixel)+(float2(x,y)+0.5f)/8-r.edge01.xy;
            float b=cross2(q,r.edge2depth.xy)/r.tail.y;
            float c=cross2(r.edge01.zw,q)/r.tail.y;
            float a=1-b-c;
            if(min(a,min(b,c))<0)continue;
            float z=dot(float3(a,b,c),float3(r.edge2depth.zw,r.tail.x));
            if(z<depths[index] || (z==depths[index] && record.primitive<orders[index])) {
                depths[index]=z;orders[index]=record.primitive;colors[index]=color;
            }
        }
    }
    float3 sum=simd_sum(colors[0]+colors[1]);
    if(lane==0) {
        atomic_fetch_max_explicit(counters+2,count,memory_order_relaxed);
        output.write(float4(sum/64,1),pixel);
    }
}
