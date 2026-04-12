#version 440

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

void main()
{
    if (v_uv.x < 0.0 || v_uv.x > 1.0 || v_uv.y < 0.0 || v_uv.y > 1.0)
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    else
        fragColor = texture(tex, v_uv);
}
