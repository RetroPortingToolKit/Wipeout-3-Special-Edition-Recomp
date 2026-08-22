Texture2D<float4> frame_texture : register(t0, space2);
SamplerState frame_sampler : register(s0, space2);

float4 PSMain(float2 uv : TEXCOORD0) : SV_Target0 {
    return frame_texture.Sample(frame_sampler, uv);
}
