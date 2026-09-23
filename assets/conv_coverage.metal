// Coverage algebra copied from BFFT experiments/conv_fast_aa/geometry.metal, 2026-09-23.
float cross2(float2 a,float2 b) {return a.x*b.y-a.y*b.x;}
bool clipPlane(float f,float slope,thread float& lo,thread float& hi) {
    if(slope>0)lo=max(lo,-f/slope);
    else if(slope<0)hi=min(hi,-f/slope);
    else if(f<0)return false;
    return hi>lo;
}
float segmentBox(float2 a,float2 b) {
    // The square owns coincident edges, preventing a doubled boundary term.
    if((a.x==b.x && abs(a.x)==.5f)||(a.y==b.y && abs(a.y)==.5f))return 0;
    float2 d=b-a;float lo=0,hi=1;
    if(!clipPlane(a.x+.5f,d.x,lo,hi)||!clipPlane(.5f-a.x,-d.x,lo,hi)||
       !clipPlane(a.y+.5f,d.y,lo,hi)||!clipPlane(.5f-a.y,-d.y,lo,hi))return 0;
    return cross2(a+lo*d,a+hi*d)*.5f;
}
float segmentTriangle(float2 a,float2 b,thread const float2* v) {
    float2 d=b-a;float lo=0,hi=1;
    for(int i=0;i<3;i++) {
        float2 edge=v[(i+1)%3]-v[i];
        if(!clipPlane(cross2(edge,a-v[i]),cross2(edge,d),lo,hi))return 0;
    }
    return cross2(a+lo*d,a+hi*d)*.5f;
}
float edgeCoverage(float d,float2 n) {
    float a=max(abs(n.x),abs(n.y)),b=min(abs(n.x),abs(n.y));
    if(b==0)return saturate(.5f+d/a);
    float z=abs(d),tail;
    if(z<=(a-b)*.5f)tail=.5f-z/a;
    else {float h=max((a+b)*.5f-z,0.0f);tail=h*h/(2*a*b);}
    return d>=0?1-tail:tail;
}
