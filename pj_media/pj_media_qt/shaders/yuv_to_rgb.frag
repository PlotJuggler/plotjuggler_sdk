#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

// Y plane (or RGBA texture in passthrough mode)
layout(binding = 1) uniform sampler2D y_tex;
// U plane (or packed UV for NV12)
layout(binding = 2) uniform sampler2D u_tex;
// V plane (unused for NV12/RGBA)
layout(binding = 3) uniform sampler2D v_tex;

layout(std140, binding = 0) uniform Uniforms {
    mat4 viewTransform;
    mat4 colorMatrix;
    int pixelFormat;  // 0 = YUV420P, 1 = NV12, 2 = RGBA
};

void main()
{
    // Bounds check — black outside texture
    if (v_uv.x < 0.0 || v_uv.x > 1.0 || v_uv.y < 0.0 || v_uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // RGBA passthrough
    if (pixelFormat == 2) {
        fragColor = texture(y_tex, v_uv);
        return;
    }

    // Sample Y (full resolution)
    float y = texture(y_tex, v_uv).r;
    float u, v;

    if (pixelFormat == 1) {
        // NV12: UV interleaved in a single RG texture
        vec2 uv_val = texture(u_tex, v_uv).rg;
        u = uv_val.r;
        v = uv_val.g;
    } else {
        // YUV420P: separate U and V planes
        u = texture(u_tex, v_uv).r;
        v = texture(v_tex, v_uv).r;
    }

    // YUV → RGB via color matrix (BT.709 or BT.601)
    vec3 yuv = vec3(y, u - 0.5, v - 0.5);
    vec3 rgb = (colorMatrix * vec4(yuv, 1.0)).rgb;
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
