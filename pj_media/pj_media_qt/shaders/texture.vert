#version 440

layout(std140, binding = 0) uniform Uniforms {
    mat4 viewTransform;
};

layout(location = 0) out vec2 v_uv;

void main()
{
    vec2 pos = vec2((gl_VertexIndex & 1) * 4.0 - 1.0,
                    (gl_VertexIndex & 2) * 2.0 - 1.0);
    v_uv = pos * 0.5 + 0.5;
    v_uv.y = 1.0 - v_uv.y;
    gl_Position = viewTransform * vec4(pos, 0.0, 1.0);
}
