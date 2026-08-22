struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput VSMain(uint vertex_id : SV_VertexID) {
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };
    VertexOutput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.uv = positions[vertex_id] * float2(0.5, -0.5) + 0.5;
    return output;
}
