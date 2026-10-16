#version 450

layout(location = 0) out vec2 frag_uv;

const vec2 CORNERS[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 corner = CORNERS[gl_VertexIndex];

    frag_uv = corner * 0.5 + 0.5;

    gl_Position = vec4(corner, 0.0, 1.0);
}
