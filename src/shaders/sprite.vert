#version 450
layout(push_constant) uniform Sprite {
    vec4 destination;
    vec4 source;
    vec4 transform;
    vec4 tint;
    vec4 effects;
    vec4 dimensions;
} sprite;
layout(location = 0) out vec2 uv;
void main() {
    const vec2 vertices[6] = vec2[6](vec2(0,0), vec2(1,0), vec2(1,1),
                                   vec2(0,0), vec2(1,1), vec2(0,1));
    vec2 corner = vertices[gl_VertexIndex];
    vec2 local = corner * sprite.destination.zw - sprite.transform.xy;
    vec2 rotated = vec2(local.x * sprite.transform.z - local.y * sprite.transform.w,
                        local.x * sprite.transform.w + local.y * sprite.transform.z);
    vec2 position = sprite.destination.xy + rotated;
    gl_Position = vec4(position / sprite.dimensions.xy * 2.0 - 1.0, 0, 1);
    uv = (sprite.source.xy + corner * sprite.source.zw) / sprite.dimensions.zw;
}
