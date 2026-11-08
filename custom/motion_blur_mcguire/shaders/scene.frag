#version 450

layout(push_constant) uniform SceneParams {
  mat4 reprojection;
} p;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outVelocity;

const float NEAR_PLANE = 0.1;
const float FAR_PLANE = 100.0;
const float VIEW_Z = 2.0;
const float LINE_HALF_WIDTH = 0.008;
const float MIN_CLIP_W = 1e-6;
const vec3 BACKGROUND = vec3(0.0, 0.03, 0.05);
const vec3 LINE_COLOR = vec3(1.0, 0.8, 0.2);

float depthFromViewZ(float z) {
  return FAR_PLANE * (z - NEAR_PLANE) / (z * (FAR_PLANE - NEAR_PLANE));
}

void main() {
  float line = step(abs(vUv.x - 0.5), LINE_HALF_WIDTH);
  outColor = vec4(mix(BACKGROUND, LINE_COLOR, line), 1.0);
  float depth = depthFromViewZ(VIEW_Z);
  gl_FragDepth = depth;
  vec4 prevClip = p.reprojection * vec4(vUv * 2.0 - 1.0, depth, 1.0);
  vec2 prevUv = abs(prevClip.w) < MIN_CLIP_W
    ? vUv : prevClip.xy / prevClip.w * 0.5 + 0.5;
  outVelocity = vec4(vUv - prevUv, 0.0, 1.0);
}
