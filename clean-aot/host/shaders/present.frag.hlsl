Texture2D<float4> frame_texture : register(t0, space2);
SamplerState frame_sampler : register(s0, space2);

cbuffer Presentation : register(b0, space3) {
    uint2 source_size;
    uint2 output_size;
    uint effects;
    uint2 output_origin;
    uint reserved;
};

uint3 load_rgb(uint2 p) {
    return uint3(round(frame_texture.Load(int3(p, 0)).rgb * 255.0));
}

uint3 edge_pixel(uint2 output_pixel) {
    uint2 mapped = uint2(
        (output_pixel.x * source_size.x) / output_size.x,
        (output_pixel.y * source_size.y) / output_size.y);
    uint2 remainder = uint2(
        (output_pixel.x * source_size.x) % output_size.x,
        (output_pixel.y * source_size.y) % output_size.y);
    bool right_half = remainder.x * 2 >= output_size.x;
    bool lower_half = remainder.y * 2 >= output_size.y;
    uint2 left_p = uint2(mapped.x == 0 ? 0 : mapped.x - 1, mapped.y);
    uint2 right_p = uint2(min(mapped.x + 1, source_size.x - 1), mapped.y);
    uint2 up_p = uint2(mapped.x, mapped.y == 0 ? 0 : mapped.y - 1);
    uint2 down_p = uint2(mapped.x, min(mapped.y + 1, source_size.y - 1));
    uint3 center = load_rgb(mapped);
    uint3 left = load_rgb(left_p), right = load_rgb(right_p);
    uint3 up = load_rgb(up_p), down = load_rgb(down_p);
    if (any(up != down) && any(left != right)) {
        if (!lower_half && !right_half && all(left == up)) return left;
        if (!lower_half && right_half && all(up == right)) return up;
        if (lower_half && !right_half && all(left == down)) return left;
        if (lower_half && right_half && all(down == right)) return down;
    }
    return center;
}

int luma(uint3 p) { return (77 * int(p.r) + 150 * int(p.g) + 29 * int(p.b) + 128) >> 8; }

uint3 resolved(uint2 p) {
    return (effects & 1U) != 0U ? edge_pixel(p) : load_rgb(
        uint2((p.x * source_size.x) / output_size.x,
              (p.y * source_size.y) / output_size.y));
}

float4 PSMain(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    if (effects == 0U) return frame_texture.Sample(frame_sampler, uv);
    uint2 p = min(uint2(position.xy) - output_origin, output_size - 1U);
    uint3 center = resolved(p);
    if ((effects & 2U) != 0U) {
        uint3 left = resolved(uint2(p.x == 0 ? 0 : p.x - 1, p.y));
        uint3 right = resolved(uint2(min(p.x + 1, output_size.x - 1), p.y));
        int cy = luma(center), ly = luma(left), ry = luma(right);
        if (abs(cy - ly) <= 12 && abs(cy - ry) <= 12) {
            int cr = ((int(left.r) - ly) + 2 * (int(center.r) - cy) +
                      (int(right.r) - ry) + 2) / 4;
            int cb = ((int(left.b) - ly) + 2 * (int(center.b) - cy) +
                      (int(right.b) - ry) + 2) / 4;
            int red = clamp(cy + cr, 0, 255);
            int blue = clamp(cy + cb, 0, 255);
            int green = clamp((256 * cy - 77 * red - 29 * blue + 75) / 150, 0, 255);
            center = uint3(red, green, blue);
        }
    }
    return float4(float3(center) / 255.0, 1.0);
}
