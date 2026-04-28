#version 440

// Vector primitive vertex (lines, polygons) — input in image-pixel space.
//
// Maps pixel coords to NDC using frameSize, applies viewTransform shared with
// the image pipeline so markers track pan/zoom/letterbox.
//
// NOTE: the field is named `frameSize` rather than `imageSize` because
// `imageSize` is a reserved GLSL built-in function name; SPIRV-Cross renames
// it to `_imageSize` when emitting GLSL 100es, which then breaks the QRhi
// uniform-by-name mapping in the OpenGL ES backend.

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

layout(std140, binding = 0) uniform Uniforms {
    mat4 viewTransform;   // shared with image shader
    vec4 frameSize;       // .xy = (width, height) of the underlying image, pixels
};

layout(location = 0) out vec4 v_color;

void main() {
    vec2 ndc;
    ndc.x = (a_pos.x / frameSize.x) * 2.0 - 1.0;
    ndc.y = -((a_pos.y / frameSize.y) * 2.0 - 1.0);

    gl_Position = viewTransform * vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
