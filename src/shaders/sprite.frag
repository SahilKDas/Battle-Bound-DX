#version 450
layout(set = 0, binding = 0) uniform sampler2D costume;
layout(push_constant) uniform Sprite {
    vec4 destination;
    vec4 source;
    vec4 transform;
    vec4 tint;
    vec4 effects;
    vec4 dimensions;
} sprite;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outputColor;
vec3 toHsv(vec3 c) {
    vec4 k = vec4(0, -1.0/3.0, 2.0/3.0, -1);
    vec4 p = mix(vec4(c.bg, k.wz), vec4(c.gb, k.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y)/(6*d + 1e-10)), d/(q.x + 1e-10), q.x);
}
vec3 toRgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0, 2.0/3.0, 1.0/3.0))*6 - 3);
    return c.z * mix(vec3(1), clamp(p - 1, 0, 1), c.y);
}
void main() {
    vec4 color = texture(costume, uv);
    if (sprite.effects.x != 0) {
        vec3 hsv = toHsv(color.rgb);
        if (hsv.z < 0.055) hsv = vec3(0, 1, 0.055);
        else if (hsv.y < 0.09) hsv.xy = vec2(0, 0.09);
        hsv.x = fract(hsv.x + sprite.effects.x);
        color.rgb = toRgb(hsv);
    }
    color.rgb = clamp(color.rgb + sprite.effects.y, 0, 1);
    outputColor = color * sprite.tint;
}
