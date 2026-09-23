// Experimental single-sample adapter for the upstream fast boundary measure.
// The original single-sample raster provides solid interiors. Only fractional
// boundaries blend over it, with depth writes disabled. This remains an approximate
// composition adapter, NOT the upstream exact visibility solver.
// Forty bytes per unique source vertex/instance, not per triangle corner.
// The static part persists until the camera, size or object instances change.
struct ConvPrepared { packed_float4 clip; packed_float3 normal; packed_float3 local; };
static_assert(sizeof(ConvPrepared)==40, "Prepared vertex stride must be forty bytes");
struct ConvBatch { uint first_vertex,vertex_count,first_instance,prepared_offset;
    uint triangle_count,first_index,list_offset,adjacency_offset; uint instance_count,group_offset,cull_back; };
struct ConvCandidate { uint triangle_mask,instance; };
struct ConvArguments { atomic_uint vertex_count; uint instance_count,vertex_start,base_instance; };
float2 conv_screen(float4 clip,float2 dimensions) {
    return (clip.xy/clip.w*float2(.5f,-.5f)+.5f)*dimensions;
}
kernel void conv_classify(uint index [[thread_position_in_grid]],
    const device uint* indices [[buffer(0)]],const device ConvPrepared* prepared [[buffer(1)]],
    const device uint* adjacency [[buffer(2)]],device uint* masks [[buffer(3)]],
    constant ConvBatch& batch [[buffer(4)]],device ConvArguments& arguments [[buffer(5)]],
    constant Uniforms& u [[buffer(6)]],device atomic_uint* counts [[buffer(7)]]) {
    masks[batch.list_offset+index]=0;
    uint triangle=index%batch.triangle_count,instance=index/batch.triangle_count;
    uint offset=batch.prepared_offset+instance*batch.vertex_count;
    float2 p[3];
    for(uint i=0;i<3;++i) {
        float4 clip=float4(prepared[offset+indices[batch.first_index+triangle*3+i]-batch.first_vertex].clip);
        if(clip.w<=0 || clip.z<0)return;
        p[i]=conv_screen(clip,u.clock.yz);
    }
    float area=cross2(p[1]-p[0],p[2]-p[0]);
    if(abs(area)<1e-7f || (batch.cull_back!=0 && area>=0))return;
    float2 lo=min(p[0],min(p[1],p[2]))-.5f,hi=max(p[0],max(p[1],p[2]))+.5f;
    if(any(hi<0) || any(lo>u.clock.yz))return;
    uint mask=0;
    for(uint edge=0;edge<3;++edge) {
        uint other=adjacency[batch.adjacency_offset+triangle*3+edge];
        if(other==0xffffffffu) {mask|=1u<<edge;continue;}
        float4 clip=float4(prepared[offset+other-batch.first_vertex].clip);
        float2 q=conv_screen(clip,u.clock.yz);
        // Adjacent triangles share the opposite directed edge. They are an
        // internal surface partition when their projected orientations agree.
        float adjacent=cross2(p[edge]-p[(edge+1)%3],q-p[(edge+1)%3]);
        if(clip.w<=0 || area*adjacent<=0)mask|=1u<<edge;
    }
    if(mask==0)return;
    masks[batch.list_offset+index]=mask;
    atomic_fetch_add_explicit(&counts[batch.group_offset+index/64],1u,memory_order_relaxed);
}
// A stable prefix keeps alpha composition deterministic across GPU scheduling.
// One bounded group of 64 source triangles is compacted by each GPU thread.
kernel void conv_prefix(uint index [[thread_position_in_grid]],device uint* counts [[buffer(0)]],
    constant ConvBatch& batch [[buffer(1)]],device ConvArguments& arguments [[buffer(2)]]) {
    uint groups=(batch.triangle_count*batch.instance_count+63)/64,total=0;
    for(uint group=0;group<groups;++group) {
        uint count=counts[batch.group_offset+group];counts[batch.group_offset+group]=total;total+=count;
    }
    atomic_store_explicit(&arguments.vertex_count,4u,memory_order_relaxed);
    arguments.instance_count=total;arguments.vertex_start=0;arguments.base_instance=0;
}
kernel void conv_compact(uint group [[thread_position_in_grid]],const device uint* counts [[buffer(0)]],
    constant ConvBatch& batch [[buffer(1)]],const device uint* masks [[buffer(2)]],
    device ConvCandidate* candidates [[buffer(3)]]) {
    uint total=batch.triangle_count*batch.instance_count;
    uint slot=counts[batch.group_offset+group];
    for(uint index=group*64;index<min(group*64+64,total);++index) {
        uint mask=masks[batch.list_offset+index];
        if(mask!=0)candidates[batch.list_offset+slot++]={
            index%batch.triangle_count | (mask<<29),batch.first_instance+index/batch.triangle_count};
    }
}
kernel void conv_prepare(uint index [[thread_position_in_grid]],
    const device Vertex* vertices [[buffer(0)]], const device Instance* instances [[buffer(1)]],
    constant Actor* actors [[buffer(2)]], constant Uniforms& u [[buffer(3)]],
    device ConvPrepared* prepared [[buffer(4)]], constant ConvBatch& batch [[buffer(5)]]) {
    uint vertex_id=batch.first_vertex+index%batch.vertex_count;
    uint instance_id=batch.first_instance+index/batch.vertex_count;
    Vertex sample=vertices[vertex_id];
    uint object=sample.binding.y>.5f ? uint(sample.binding.x) : instance_id;
    Instance instance=instances[object];
    if(int(instance.behavior.x)==15)
        instance=pearl_transform(instance,vertices[uint(instance.anatomy.x)],instances[uint(instance.anatomy.y)],u.clock.x);
    Out out=prepare(sample,instance,actors,u);
    prepared[batch.prepared_offset+index]={packed_float4(out.position),packed_float3(out.normal),packed_float3(out.local)};
}
Out conv_load(Vertex sample,Instance instance,ConvPrepared prepared,constant Uniforms& u) {
    Out out;out.position=float4(prepared.clip);out.normal=float3(prepared.normal);out.local=float3(prepared.local);
    float4 world=u.reconstruction*float4(out.position.xy,-out.position.w,1);
    out.world=world.xyz/world.w;out.color=instance.color;
    if(sample.binding.z>.5f)out.color*=sample.color;
    out.behavior=instance.behavior;out.uv=sample.uv;
    out.surface=float4(sample.normal.w,sample.anchor.w,sample.binding.z,0);
    out.light_position=u.light*float4(out.world,1);out.identity=0;return out;
}
Out conv_source(uint vertex_id,uint instance_id,const device Vertex* vertices,
    const device Instance* instances,const device ConvPrepared* prepared,
    constant ConvBatch& batch,constant Uniforms& u) {
    Vertex sample=vertices[vertex_id];
    uint object=sample.binding.y>.5f ? uint(sample.binding.x) : instance_id;
    uint index=batch.prepared_offset+(instance_id-batch.first_instance)*batch.vertex_count
               +vertex_id-batch.first_vertex;
    Out out=conv_load(sample,instances[object],prepared[index],u);out.identity=object+1;return out;
}
vertex Out conv_regular_vertex(uint vertex_id [[vertex_id]],uint instance_id [[instance_id]],
    const device Vertex* vertices [[buffer(0)]],const device Instance* instances [[buffer(1)]],
    constant Uniforms& u [[buffer(3)]],const device ConvPrepared* prepared [[buffer(5)]],
    constant ConvBatch& batch [[buffer(6)]]) {
    return conv_source(vertex_id,instance_id,vertices,instances,prepared,batch,u);
}
vertex float4 conv_shadow_vertex(uint vertex_id [[vertex_id]],uint instance_id [[instance_id]],
    const device Vertex* vertices [[buffer(0)]],const device Instance* instances [[buffer(1)]],
    constant Uniforms& u [[buffer(3)]],const device ConvPrepared* prepared [[buffer(5)]],
    constant ConvBatch& batch [[buffer(6)]]) {
    Out out=conv_source(vertex_id,instance_id,vertices,instances,prepared,batch,u);return out.light_position;
}
struct ConvOut {
    float4 position [[position]];
    float3 world; float3 normal; float3 local;
    float4 color; float4 behavior; float4 light_position; float4 uv; float4 surface;
    float2 origin [[flat]]; float2 e1 [[flat]]; float2 e2 [[flat]];
    float3 radii [[flat]]; float twiceArea [[flat]]; uint front [[flat]]; uint mask [[flat]];
};
vertex ConvOut conv_vertex(uint vertex_id [[vertex_id]], uint instance_id [[instance_id]],
    constant Vertex* vertices [[buffer(0)]], constant Instance* instances [[buffer(1)]],
    constant Actor* actors [[buffer(2)]], constant Uniforms& u [[buffer(3)]],
    const device uint* indices [[buffer(4)]], const device ConvPrepared* prepared [[buffer(5)]],
    constant ConvBatch& batch [[buffer(6)]],const device ConvCandidate* candidates [[buffer(7)]]) {
    ConvCandidate candidate=candidates[batch.list_offset+instance_id];
    const uint triangle=candidate.triangle_mask&0x1fffffffu,corner=vertex_id;
    instance_id=candidate.instance;
    Out v[3]; float2 p[3];
    for(uint i=0;i<3;++i) {
        uint source_index=indices[triangle*3+i];
        Vertex sample=vertices[source_index];
        uint index=sample.binding.y>0.5f ? uint(sample.binding.x) : instance_id;
        uint prepared_index=batch.prepared_offset+(instance_id-batch.first_instance)*batch.vertex_count
                            +source_index-batch.first_vertex;
        v[i]=conv_load(sample,instances[index],prepared[prepared_index],u);
        p[i]=(v[i].position.xy/v[i].position.w*float2(.5f,-.5f)+.5f)*u.clock.yz;
    }
    ConvOut out={}; out.position=float4(2,2,2,1);
    float area=cross2(p[1]-p[0],p[2]-p[0]);
    int material=int(v[0].behavior.x+.5f);
    bool front=material>=7 ? area<0 : area>0;
    // The trial's fixed aquarium camera keeps the near plane clear. Reject
    // crossing primitives rather than project behind-eye vertices unboundedly.
    if(abs(area)<1e-7f || any(float3(v[0].position.w,v[1].position.w,v[2].position.w)<=0) ||
       any(float3(v[0].position.z,v[1].position.z,v[2].position.z)<0) ||
       ((material==8 || material==11) && !front))return out;
    bool swapped=area<0;
    if(swapped) { float2 tmp=p[1];p[1]=p[2];p[2]=tmp;Out swap=v[1];v[1]=v[2];v[2]=swap;area=-area; }
    out.mask=candidate.triangle_mask>>29;
    if(swapped)out.mask=((out.mask&1u)<<2)|(out.mask&2u)|((out.mask&4u)>>2);
    out.origin=p[0];out.e1=p[1]-p[0];out.e2=p[2]-p[0];out.twiceArea=area;out.front=front;
    float2 e3=out.e2-out.e1;
    out.radii=.5f*float3(abs(out.e1.x)+abs(out.e1.y),abs(e3.x)+abs(e3.y),abs(out.e2.x)+abs(out.e2.y));
    float2 lo=floor(min(p[0],min(p[1],p[2]))-.5f),hi=ceil(max(p[0],max(p[1],p[2]))+.5f);
    constexpr uint2 corners[4]={uint2(0,0),uint2(1,0),uint2(0,1),uint2(1,1)};
    float2 q=mix(lo,hi,float2(corners[corner]));
    float scale=1+3*max(out.radii.x,max(out.radii.y,out.radii.z))/area;
    if(.5f*area*scale*scale<(hi.x-lo.x)*(hi.y-lo.y)) {
        float2 centre=(p[0]+p[1]+p[2])/3;
        q=centre+scale*(p[min(corner,2u)]-centre);
    }
    float2 delta=q-p[0];
    float3 weights=float3(0,cross2(delta,out.e2)/area,cross2(out.e1,delta)/area);
    weights.x=1-weights.y-weights.z;
    float z=dot(weights,float3(v[0].position.z/v[0].position.w,v[1].position.z/v[1].position.w,v[2].position.z/v[2].position.w));
    weights/=float3(v[0].position.w,v[1].position.w,v[2].position.w);
    float reciprocalW=weights.x+weights.y+weights.z;
    if(reciprocalW<=0)return out;
    weights/=reciprocalW;
    out.position=float4(q/u.clock.yz*float2(2,-2)+float2(-1,1),z,1)/reciprocalW;
    out.world=v[0].world*weights.x+v[1].world*weights.y+v[2].world*weights.z;
    out.normal=v[0].normal*weights.x+v[1].normal*weights.y+v[2].normal*weights.z;
    out.local=v[0].local*weights.x+v[1].local*weights.y+v[2].local*weights.z;
    out.color=v[0].color*weights.x+v[1].color*weights.y+v[2].color*weights.z;
    out.behavior=v[0].behavior;out.surface=v[0].surface;
    out.uv=v[0].uv*weights.x+v[1].uv*weights.y+v[2].uv*weights.z;
    out.light_position=v[0].light_position*weights.x+v[1].light_position*weights.y+v[2].light_position*weights.z;
    return out;
}
fragment float4 conv_fragment(ConvOut in [[stage_in]],constant Uniforms& u [[buffer(3)]],depth2d<float> shadow [[texture(0)]],
 texture2d<float> sand [[texture(1)]],texture2d<float> sand_normal [[texture(2)]],
 texture2d<float> rock [[texture(3)]],texture2d<float> rock_normal [[texture(4)]],
 texture2d<float> wood [[texture(5)]],texture2d<float> wood_normal [[texture(6)]]) {
    if(in.twiceArea<=0)discard_fragment();
    float2 q=in.position.xy-in.origin;
    float2 edges[3]={in.e1,in.e2-in.e1,-in.e2};
    float3 centres=float3(cross2(edges[0],q),cross2(edges[1],q)+in.twiceArea,cross2(edges[2],q));
    if(any(centres<=-in.radii))discard_fragment();
    int active=0;bool boundary=false;float d=0;float2 normal=0;
    for(int i=0;i<3;i++) {
        if(centres[i]<in.radii[i]) {boundary=boundary || ((in.mask&(1u<<i))!=0);active++;d=centres[i];normal=float2(-edges[i].y,edges[i].x);}
    }
    if(active==0 || !boundary)discard_fragment();
    float area=1;
    if(active==1)area=edgeCoverage(d,normal);
    if(active>1) {
        float2 v[3]={-q,in.e1-q,in.e2-q};area=0;
        for(int i=0;i<3;i++)area+=segmentBox(v[i],v[(i+1)%3]);
        float2 box[4]={float2(-.5f,-.5f),float2(.5f,-.5f),float2(.5f,.5f),float2(-.5f,.5f)};
        for(int i=0;i<4;i++)area+=segmentTriangle(box[i],box[(i+1)%4],v);
    }
    if(area<=0)discard_fragment();
    Out surface;surface.position=in.position;surface.world=in.world;surface.normal=in.normal;
    surface.local=in.local;surface.color=in.color;surface.behavior=in.behavior;
    surface.light_position=in.light_position;surface.uv=in.uv;surface.surface=in.surface;surface.identity=0;
    float4 color=shade_tank(surface,u,shadow,sand,sand_normal,rock,rock_normal,wood,wood_normal,in.front!=0);
    color.a*=saturate(area);
    return color;
}
